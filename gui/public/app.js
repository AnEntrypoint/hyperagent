import { applyDiff, createElement } from "./vendor/webjsx/index.js";
import htm from "./vendor/htm.mjs";

const html = htm.bind(createElement);
const root = document.getElementById("app");

const KINDS = ["instruct", "you", "tool", "result", "log", "raw", "malformed", "text"];
const DEFAULT_KINDS = KINDS.filter((kind) => kind !== "text");
const TRANSCRIPT_KINDS = new Set(["instruct", "text", "tool", "result"]);
const BLOCK_TYPES = { instruct: "prompt", text: "reply", result: "end" };
const VIEWS = ["overview", "transcript", "records"];
const VIEW_LABELS = { overview: "Overview", transcript: "Transcript", records: "Records" };
const KIND_LABELS = {
  instruct: "prompt",
  you: "echo",
  tool: "tool",
  result: "result",
  log: "log",
  raw: "raw",
  malformed: "bad",
  text: "delta",
};
const SHORTCUTS = [
  ["o  t  r", "overview, transcript, records"],
  ["/", "filter text"],
  ["j  k", "next or previous record (records)"],
  ["p", "pause or resume the stream"],
  ["f", "follow the newest record"],
  ["w", "wrap long lines (records)"],
  ["Esc", "deselect, or leave an agent"],
  ["?", "this help"],
];
const SERVER = "airc";
const PAGE_SIZE = 300;
const MAX_RECORDS = 8000;
const ACTIVE_SECONDS = 60;
const HOUR_SECONDS = 3600;
const BIN_SECONDS = 60;
const BINS = 30;
const FEED_SIZE = 40;
const CARD_CHARS = 280;
const SUMMARY_CHARS = 240;
const FLASH_MS = 1500;
const ERROR_PATTERN = /Traceback|Error|error/;

const state = {
  records: [],
  ids: new Set(),
  agents: new Map(),
  pending: [],
  paused: false,
  live: "connecting",
  follow: true,
  unseen: 0,
  limit: PAGE_SIZE,
  selected: null,
  reveal: false,
  agent: "",
  query: "",
  view: "overview",
  kinds: new Set(DEFAULT_KINDS),
  wrap: false,
  help: false,
  flash: "",
  eventsPerSecond: 0,
  arrivals: 0,
  unsorted: false,
};

let frame = 0;

function scheduleRender() {
  if (!frame) {
    frame = requestAnimationFrame(render);
  }
}

function pad(number) {
  return String(number).padStart(2, "0");
}

function stamp(epoch) {
  const date = new Date(epoch * 1000);
  return `${pad(date.getHours())}:${pad(date.getMinutes())}:${pad(date.getSeconds())}`;
}

function fullStamp(epoch) {
  return new Date(epoch * 1000).toLocaleString();
}

function ago(seconds) {
  if (seconds < 60) return `${Math.max(0, Math.floor(seconds))}s`;
  if (seconds < 3600) return `${Math.floor(seconds / 60)}m`;
  return `${Math.floor(seconds / 3600)}h`;
}

function summary(record) {
  if (record.kind === "result") {
    const acked = record.acked ? " · acked" : "";
    return `${record.status ?? "?"} · ${record.duration_s ?? "?"}s${acked}`;
  }
  if (record.kind === "tool") return record.text ?? "";
  const line = (record.text ?? "")
    .split("\n")
    .map((part) => part.trim())
    .find((part) => part && !part.startsWith("<pasted_content"));
  return (line ?? "").slice(0, SUMMARY_CHARS);
}

function highlight(text, query) {
  if (!query) return text;
  const lower = text.toLowerCase();
  const parts = [];
  let at = 0;
  let hit = lower.indexOf(query, at);
  while (hit !== -1) {
    parts.push(text.slice(at, hit), html`<mark>${text.slice(hit, hit + query.length)}</mark>`);
    at = hit + query.length;
    hit = lower.indexOf(query, at);
  }
  parts.push(text.slice(at));
  return parts;
}

function isKindShown(kind) {
  return state.kinds.has(kind) || !KINDS.includes(kind);
}

function passes(record, query) {
  if (state.agent && record.agent !== state.agent) return false;
  if (state.view === "transcript") {
    if (!TRANSCRIPT_KINDS.has(record.kind)) return false;
  } else if (!isKindShown(record.kind)) {
    return false;
  }
  if (!query) return true;
  const haystack = `${record.agent} ${record.kind} ${record.status ?? ""} ${record.text ?? ""}`;
  return haystack.toLowerCase().includes(query);
}

function matching() {
  const query = state.query.trim().toLowerCase();
  return state.records.filter((record) => passes(record, query));
}

function noteAgent(record) {
  const info = state.agents.get(record.agent) ?? { count: 0, last: 0, session: "" };
  info.count += 1;
  info.last = Math.max(info.last, record.epoch);
  info.session = record.session ?? info.session;
  state.agents.set(record.agent, info);
}

function byEpoch(left, right) {
  return left.epoch - right.epoch;
}

function trim(list) {
  const excess = list.length - MAX_RECORDS;
  if (excess <= 0) return;
  for (const record of list.splice(0, excess)) {
    state.ids.delete(record.id);
  }
}

function ingest(batch) {
  let added = 0;
  for (const record of batch) {
    if (state.ids.has(record.id)) continue;
    state.ids.add(record.id);
    noteAgent(record);
    state.arrivals += 1;
    added += 1;
    if (state.paused) {
      state.pending.push(record);
      continue;
    }
    const last = state.records[state.records.length - 1];
    if (last && record.epoch < last.epoch) state.unsorted = true;
    state.records.push(record);
  }
  if (!added) return;
  if (!state.follow) state.unseen += added;
  if (state.paused) {
    trim(state.pending);
  } else {
    flushSorted();
  }
  scheduleRender();
}

function flushSorted() {
  if (state.unsorted) {
    state.records.sort(byEpoch);
    state.unsorted = false;
  }
  trim(state.records);
}

function setPaused(paused) {
  state.paused = paused;
  if (!paused && state.pending.length) {
    state.records.push(...state.pending);
    state.pending = [];
    flushSorted();
  }
  scheduleRender();
}

function setFollow(follow) {
  state.follow = follow;
  if (follow) state.unseen = 0;
  scheduleRender();
}

function toggleWrap() {
  state.wrap = !state.wrap;
  scheduleRender();
}

function toggleHelp() {
  state.help = !state.help;
  scheduleRender();
}

function flash(message) {
  state.flash = message;
  scheduleRender();
  setTimeout(() => {
    if (state.flash === message) {
      state.flash = "";
      scheduleRender();
    }
  }, FLASH_MS);
}

function copy(value, label) {
  if (!navigator.clipboard) {
    flash("copy unavailable");
    return;
  }
  navigator.clipboard.writeText(value).then(
    () => flash(`copied ${label}`),
    () => flash("copy failed"),
  );
}

function exportVisible() {
  const visible = matching().slice(-state.limit);
  const body = visible.map((record) => JSON.stringify(record)).join("\n");
  const blob = new Blob([body ? `${body}\n` : ""], { type: "application/x-ndjson" });
  const link = document.createElement("a");
  link.href = URL.createObjectURL(blob);
  link.download = `hyperagent-logs-${new Date().toISOString().replace(/[:.]/g, "-")}.jsonl`;
  link.click();
  setTimeout(() => URL.revokeObjectURL(link.href), FLASH_MS * 4);
  flash(`exported ${visible.length}`);
}

function writeHash() {
  const params = new URLSearchParams();
  if (state.query) params.set("q", state.query);
  if (state.agent) params.set("agent", state.agent);
  if (state.view !== "overview") params.set("view", state.view);
  const customKinds = state.kinds.size !== DEFAULT_KINDS.length || DEFAULT_KINDS.some((kind) => !state.kinds.has(kind));
  if (customKinds) params.set("kinds", [...state.kinds].join(","));
  const text = params.toString();
  history.replaceState(null, "", text ? `#${text}` : location.pathname + location.search);
}

function readHash() {
  const params = new URLSearchParams(location.hash.slice(1));
  state.query = params.get("q") ?? "";
  state.agent = params.get("agent") ?? "";
  state.view = VIEWS.includes(params.get("view")) ? params.get("view") : "overview";
  if (params.has("kinds")) {
    state.kinds = new Set(params.get("kinds").split(",").filter((kind) => KINDS.includes(kind)));
  }
}

function changeFilter(mutate) {
  mutate();
  state.limit = PAGE_SIZE;
  writeHash();
  scheduleRender();
}

function setView(view) {
  state.view = view;
  if (view === "overview") state.agent = "";
  state.limit = PAGE_SIZE;
  writeHash();
  scheduleRender();
}

function drill(name) {
  state.agent = name;
  state.view = "transcript";
  state.limit = PAGE_SIZE;
  writeHash();
  scheduleRender();
}

function toggleKind(kind) {
  changeFilter(() => {
    if (state.kinds.has(kind)) {
      state.kinds.delete(kind);
    } else {
      state.kinds.add(kind);
    }
  });
}

function onQuery(event) {
  changeFilter(() => {
    state.query = event.target.value;
  });
}

function select(id) {
  state.selected = state.selected === id ? null : id;
  scheduleRender();
}

function step(delta) {
  const visible = matching().slice(-state.limit);
  if (!visible.length) return;
  const index = visible.findIndex((record) => record.id === state.selected);
  let next;
  if (index === -1) {
    next = delta > 0 ? 0 : visible.length - 1;
  } else {
    next = Math.min(visible.length - 1, Math.max(0, index + delta));
  }
  state.selected = visible[next].id;
  state.reveal = true;
  setFollow(false);
}

function onScroll(event) {
  const { scrollTop, scrollHeight, clientHeight } = event.currentTarget;
  const atBottom = scrollHeight - scrollTop - clientHeight < 24;
  if (atBottom !== state.follow) setFollow(atBottom);
}

function detail(record) {
  const { text, pane_tail: paneTail, ...fields } = record;
  return html`
    <div class="detail" onclick=${(event) => event.stopPropagation()}>
      <div class="actions">
        ${text ? html`<button class="btn" onclick=${() => copy(text, "text")}>copy text</button>` : null}
        <button class="btn" onclick=${() => copy(JSON.stringify(record, null, 2), "json")}>copy json</button>
      </div>
      ${text ? html`<pre class="body">${text}</pre>` : null}
      ${paneTail?.length ? html`<pre class="pane">${paneTail.join("\n")}</pre>` : null}
      <pre class="json">${JSON.stringify(fields, null, 2)}</pre>
    </div>`;
}

function row(record) {
  const open = state.selected === record.id;
  const query = state.query.trim().toLowerCase();
  return html`
    <div key=${record.id} class=${`row kind-${record.kind}${open ? " open" : ""}`} onclick=${() => select(record.id)}>
      <time title=${fullStamp(record.epoch)}>${stamp(record.epoch)}</time>
      <span class="agent">${record.agent}</span>
      <span class="kind">${KIND_LABELS[record.kind] ?? record.kind}</span>
      <span class="text">${highlight(summary(record), query)}</span>
      ${open ? detail(record) : null}
    </div>`;
}

function blocks(records) {
  const items = [];
  for (const record of records) {
    const last = items[items.length - 1];
    if (record.kind === "tool") {
      const name = record.text || "tool";
      if (last?.type === "tools" && last.agent === record.agent) {
        last.count += 1;
        last.names.set(name, (last.names.get(name) ?? 0) + 1);
      } else {
        items.push({ type: "tools", key: record.id, agent: record.agent, epoch: record.epoch, count: 1, names: new Map([[name, 1]]) });
      }
      continue;
    }
    items.push({ type: BLOCK_TYPES[record.kind], key: record.id, agent: record.agent, epoch: record.epoch, record });
  }
  return items;
}

function block(item, tagged) {
  const query = state.query.trim().toLowerCase();
  if (item.type === "tools") {
    const names = [...item.names].map(([name, count]) => (count > 1 ? `${name} ×${count}` : name)).join(", ");
    return html`
      <div key=${item.key} class="block tools">
        <time title=${fullStamp(item.epoch)}>${stamp(item.epoch)}</time>
        <span class="gutter">·</span>
        <span class="msg">${tagged ? html`<span class="tag">${item.agent}</span>` : null}Ran ${item.count} ${item.count === 1 ? "tool" : "tools"}: ${names}</span>
      </div>`;
  }
  if (item.type === "end") {
    const ok = item.record.status === "turn_end";
    return html`
      <div key=${item.key} class="block end">
        <time title=${fullStamp(item.epoch)}>${stamp(item.epoch)}</time>
        <span class=${`gutter ${ok ? "ok" : "fail"}`}>${ok ? "✓" : "✗"}</span>
        <span class="msg">${tagged ? html`<span class="tag">${item.agent}</span>` : null}${item.record.status ?? "?"} · ${item.record.duration_s ?? "?"}s</span>
      </div>`;
  }
  const text = item.record.text ?? "";
  return html`
    <div key=${item.key} class=${`block ${item.type}`}>
      <time title=${fullStamp(item.epoch)}>${stamp(item.epoch)}</time>
      <span class="gutter">${item.type === "prompt" ? "❯" : "●"}</span>
      <div class="body">
        ${tagged ? html`<span class="tag">${item.agent}</span>` : null}
        <pre class="msg">${highlight(text, query)}</pre>
        <button class="copy" onclick=${() => copy(text, "message")}>copy</button>
      </div>
    </div>`;
}

function stream(matches, visible) {
  const hidden = matches.length - visible.length;
  const transcript = state.view === "transcript";
  let empty = "No records match the current filters.";
  if (state.records.length === 0) empty = "Waiting for journal records…";
  const classes = ["stream", state.wrap && !transcript ? "wrap" : "", transcript ? "transcript" : ""].filter(Boolean).join(" ");
  const body = transcript ? blocks(visible).map((item) => block(item, !state.agent)) : visible.map(row);
  return html`
    <section class=${classes} onscroll=${onScroll}>
      ${hidden > 0 ? html`<button class="older" onclick=${() => { state.limit += PAGE_SIZE; scheduleRender(); }}>Show ${hidden} older</button>` : null}
      ${body.length ? body : html`<p class="empty">${empty}</p>`}
    </section>`;
}

function digests(now) {
  const map = new Map();
  for (const record of state.records) {
    let info = map.get(record.agent);
    if (!info) {
      info = {
        name: record.agent,
        session: record.session ?? "",
        last: 0,
        prompt: null,
        reply: null,
        result: null,
        turns: 0,
        tools: 0,
        errors: 0,
        hourTurns: 0,
        hourTools: 0,
        hourErrors: 0,
        logErrors: 0,
        bins: new Array(BINS).fill(0),
      };
      map.set(record.agent, info);
    }
    const age = now - record.epoch;
    info.last = Math.max(info.last, record.epoch);
    if (record.session) info.session = record.session;
    if (record.kind === "log") {
      if (age < HOUR_SECONDS && ERROR_PATTERN.test(record.text ?? "")) info.logErrors += 1;
      continue;
    }
    if (!TRANSCRIPT_KINDS.has(record.kind)) continue;
    if (record.kind === "instruct") info.prompt = record;
    if (record.kind === "text") info.reply = record;
    if (record.kind === "tool") info.tools += 1;
    if (record.kind === "result") {
      info.result = record;
      info.turns += 1;
      if (record.status !== "turn_end") info.errors += 1;
    }
    if (age < HOUR_SECONDS) {
      if (record.kind === "tool") info.hourTools += 1;
      if (record.kind === "result") {
        info.hourTurns += 1;
        if (record.status !== "turn_end") info.hourErrors += 1;
      }
    }
    const bin = Math.floor(age / BIN_SECONDS);
    if (bin >= 0 && bin < BINS) info.bins[BINS - 1 - bin] += 1;
  }
  return map;
}

function firstLine(text) {
  return (text ?? "").slice(0, CARD_CHARS);
}

function card(info, now) {
  const active = now - info.last < ACTIVE_SECONDS;
  const peak = Math.max(1, ...info.bins);
  const bars = info.bins.map((count) => html`<i class=${count ? "on" : ""} style=${`height:${Math.max(6, Math.round((count / peak) * 100))}%`}></i>`);
  const result = info.result;
  return html`
    <article key=${info.name} class=${`card${active ? " active" : ""}`} onclick=${() => drill(info.name)}>
      <header class="card-head">
        <span class=${`dot${active ? " active" : ""}`}></span>
        <span class="name">${info.name}</span>
        <span class="seen">${ago(now - info.last)}</span>
      </header>
      ${info.prompt ? html`<div class="line prompt"><span class="gutter">❯</span><span class="clip">${firstLine(info.prompt.text)}</span></div>` : null}
      ${info.reply ? html`<div class="line reply"><span class="gutter">●</span><span class="clip">${firstLine(info.reply.text)}</span></div>` : null}
      <div class="spark">${bars}</div>
      <footer class="meta">
        <span>${info.turns} turns</span>
        <span>${info.tools} tools</span>
        ${info.errors ? html`<span class="bad">${info.errors} failed</span>` : null}
        ${result ? html`<span class=${result.status === "turn_end" ? "ok" : "bad"}>${result.status ?? "?"} · ${result.duration_s ?? "?"}s</span>` : null}
      </footer>
    </article>`;
}

function overview(now) {
  const infos = [...digests(now).values()];
  const agents = infos
    .filter((info) => info.name !== SERVER && (info.turns || info.tools || info.prompt || info.reply))
    .sort((left, right) => right.last - left.last);
  const server = infos.find((info) => info.name === SERVER);
  const active = agents.filter((info) => now - info.last < ACTIVE_SECONDS).length;
  const turns = agents.reduce((sum, info) => sum + info.hourTurns, 0);
  const tools = agents.reduce((sum, info) => sum + info.hourTools, 0);
  const failed = agents.reduce((sum, info) => sum + info.hourErrors, 0);
  const serverErrors = server?.logErrors ?? 0;
  const feed = state.records.filter((record) => TRANSCRIPT_KINDS.has(record.kind) && record.agent !== SERVER).slice(-FEED_SIZE);
  return html`
    <section class="overview">
      <p class="bluf">
        <strong>${active}</strong> of ${agents.length} agents active
        <span class="sep">·</span> last hour <strong>${turns}</strong> turns, <strong>${tools}</strong> tools
        ${failed ? html`<span class="sep">·</span> <strong class="bad">${failed} failed</strong>` : null}
        ${serverErrors ? html`<span class="sep">·</span> <strong class="bad">${serverErrors} server errors</strong>` : null}
      </p>
      <div class="cards">${agents.map((info) => card(info, now))}</div>
      <h3>Latest</h3>
      <div class="feed">${blocks(feed).map((item) => block(item, true))}</div>
    </section>`;
}

function drillBar(now) {
  if (state.view !== "transcript" || !state.agent) return null;
  const info = digests(now).get(state.agent);
  return html`
    <div class="drill">
      <button class="btn" onclick=${() => setView("overview")}>← overview</button>
      <span class="name">${state.agent}</span>
      ${info ? html`<span class="meta">${info.turns} turns · ${info.tools} tools · last ${ago(now - info.last)} ago${info.errors ? html`<span class="bad"> · ${info.errors} failed</span>` : null}</span>` : null}
    </div>`;
}

function jump() {
  if (state.follow) return null;
  const label = state.unseen ? `${state.unseen} new · latest` : "latest";
  return html`<button class="jump" onclick=${() => setFollow(true)}>${label} ↓</button>`;
}

function help() {
  if (!state.help) return null;
  return html`
    <div class="help" onclick=${toggleHelp}>
      <div class="help-card" onclick=${(event) => event.stopPropagation()}>
        <h3>Keyboard</h3>
        <dl>
          ${SHORTCUTS.map(([key, text]) => html`<div key=${key} class="pair"><dt>${key}</dt><dd>${text}</dd></div>`)}
        </dl>
      </div>
    </div>`;
}

function sidebar(now) {
  const agents = [...state.agents.entries()].sort((left, right) => right[1].last - left[1].last);
  const overviewOn = state.view === "overview";
  return html`
    <aside class="side">
      <h2>Agents</h2>
      <button class=${`source all${overviewOn ? " on" : ""}`} onclick=${() => setView("overview")}>
        <span class="name">overview</span>
        <span class="count">${state.records.length}</span>
      </button>
      ${agents.map(([name, info]) => html`
        <button key=${name} class=${`source${state.agent === name ? " on" : ""}`} onclick=${() => drill(name)}>
          <span class=${`dot${now - info.last < ACTIVE_SECONDS ? " active" : ""}`}></span>
          <span class="name">${name}</span>
          <span class="count">${info.count}</span>
          <span class="when">${ago(now - info.last)}</span>
        </button>`)}
    </aside>`;
}

function toolbar() {
  const overviewView = state.view === "overview";
  const records = state.view === "records";
  return html`
    <div class="toolbar">
      <div class="tabs">
        ${VIEWS.map((view) => html`<button key=${view} class=${`tab${state.view === view ? " on" : ""}`} onclick=${() => setView(view)}>${VIEW_LABELS[view]}</button>`)}
      </div>
      ${overviewView ? null : html`<input id="query" type="search" placeholder="Filter  ( / )" value=${state.query} oninput=${onQuery} />`}
      ${records ? html`
        <div class="chips">
          ${KINDS.map((kind) => html`
            <button key=${kind} class=${`chip kind-${kind}${state.kinds.has(kind) ? " on" : ""}`} onclick=${() => toggleKind(kind)}>${KIND_LABELS[kind]}</button>`)}
        </div>` : null}
      <button class=${`btn${state.paused ? " on" : ""}`} onclick=${() => setPaused(!state.paused)}>
        ${state.paused ? `Resume · ${state.pending.length} new` : "Pause"}
      </button>
      ${overviewView ? null : html`<button class=${`btn${state.follow ? " on" : ""}`} onclick=${() => setFollow(!state.follow)}>Follow</button>`}
      ${records ? html`<button class=${`btn${state.wrap ? " on" : ""}`} onclick=${toggleWrap}>Wrap</button>` : null}
      <button class="btn" onclick=${exportVisible}>Export</button>
      <button class=${`btn${state.help ? " on" : ""}`} onclick=${toggleHelp}>Keys</button>
    </div>`;
}

function header(now, matchCount) {
  const labels = { live: "live", connecting: "connecting", down: "reconnecting" };
  const shown = state.view === "overview" ? null : html`<span class="stat">${matchCount} shown</span>`;
  return html`
    <header>
      <div class="brand">hyperagent <span>logs</span></div>
      <span class=${`live ${state.live}`}>${labels[state.live]}</span>
      <span class="stat">${state.eventsPerSecond}/s</span>
      ${shown}
      <span class="flash">${state.flash}</span>
    </header>`;
}

function content(now, matches, visible) {
  if (state.view === "overview") return overview(now);
  return html`
    <div class="pane">
      ${drillBar(now)}
      ${stream(matches, visible)}
      ${jump()}
    </div>`;
}

function view(now, matches, visible) {
  return html`
    <div class="app">
      ${header(now, matches.length)}
      <div class="body">
        ${sidebar(now)}
        <main>
          ${toolbar()}
          ${content(now, matches, visible)}
        </main>
      </div>
      ${help()}
    </div>`;
}

function render() {
  frame = 0;
  const now = Date.now() / 1000;
  const matches = matching();
  const visible = matches.slice(-state.limit);
  applyDiff(root, view(now, matches, visible));
  if (state.view === "overview") return;
  if (state.follow) {
    const stream = root.querySelector(".stream");
    if (stream) stream.scrollTop = stream.scrollHeight;
  } else if (state.reveal) {
    root.querySelector(".row.open")?.scrollIntoView({ block: "nearest" });
  }
  state.reveal = false;
}

function connectEvents() {
  const source = new EventSource("api/events");
  source.onopen = () => {
    state.live = "live";
    scheduleRender();
  };
  source.onerror = () => {
    state.live = "down";
    scheduleRender();
  };
  source.addEventListener("snapshot", (event) => ingest(JSON.parse(event.data)));
  source.addEventListener("records", (event) => ingest(JSON.parse(event.data)));
}

function swapStylesheets() {
  for (const link of document.querySelectorAll('link[rel="stylesheet"]')) {
    const url = new URL(link.href);
    url.searchParams.set("v", String(Date.now()));
    link.href = url.href;
  }
}

function connectReload() {
  let interrupted = false;
  const source = new EventSource("api/reload");
  source.onopen = () => {
    if (interrupted) location.reload();
  };
  source.onerror = () => {
    interrupted = true;
  };
  source.addEventListener("change", (event) => {
    const { files, restart } = JSON.parse(event.data);
    if (restart) return;
    if (files.every((file) => file.endsWith(".css"))) {
      swapStylesheets();
    } else {
      location.reload();
    }
  });
}

function onKey(event) {
  const typing = event.target instanceof HTMLInputElement;
  if (typing) {
    if (event.key === "Escape") event.target.blur();
    return;
  }
  if (state.help) {
    if (event.key === "Escape" || event.key === "?") toggleHelp();
    return;
  }
  if (event.key in { o: 1, t: 1, r: 1 }) {
    setView({ o: "overview", t: "transcript", r: "records" }[event.key]);
  } else if (event.key === "/") {
    event.preventDefault();
    document.getElementById("query")?.focus();
  } else if (event.key === "p") {
    setPaused(!state.paused);
  } else if (event.key === "f") {
    setFollow(!state.follow);
  } else if (event.key === "w") {
    toggleWrap();
  } else if (event.key === "j" && state.view === "records") {
    step(1);
  } else if (event.key === "k" && state.view === "records") {
    step(-1);
  } else if (event.key === "?") {
    toggleHelp();
  } else if (event.key === "Escape") {
    if (state.selected !== null) {
      select(null);
    } else if (state.view === "transcript" && state.agent) {
      setView("overview");
    }
  }
}

function tick() {
  state.eventsPerSecond = state.arrivals;
  state.arrivals = 0;
  scheduleRender();
}

readHash();
connectEvents();
connectReload();
addEventListener("keydown", onKey);
setInterval(tick, 1000);
scheduleRender();
