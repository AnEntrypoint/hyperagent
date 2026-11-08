import { applyDiff, createElement } from "./vendor/webjsx/index.js";
import htm from "./vendor/htm.mjs";

const html = htm.bind(createElement);
const root = document.getElementById("app");

const KINDS = ["instruct", "you", "tool", "result", "log", "raw", "malformed", "text"];
const DEFAULT_KINDS = KINDS.filter((kind) => kind !== "text");
const TRANSCRIPT_KINDS = new Set(["instruct", "text", "tool", "result"]);
const BLOCK_TYPES = { instruct: "prompt", text: "reply", result: "end" };
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
  ["/", "filter text"],
  ["v", "transcript or records view"],
  ["j  k", "next or previous record (records view)"],
  ["p", "pause or resume the stream"],
  ["f", "follow the newest record"],
  ["w", "wrap long lines (records view)"],
  ["Esc", "deselect, or close this help"],
  ["?", "this help"],
];
const PAGE_SIZE = 300;
const MAX_RECORDS = 8000;
const ACTIVE_SECONDS = 60;
const SUMMARY_CHARS = 240;
const FLASH_MS = 1500;

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
  view: "transcript",
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
  const time = `${pad(date.getHours())}:${pad(date.getMinutes())}:${pad(date.getSeconds())}.${String(date.getMilliseconds()).padStart(3, "0")}`;
  if (date.toDateString() === new Date().toDateString()) return time;
  return `${date.toLocaleDateString(undefined, { month: "short", day: "numeric" })} ${time}`;
}

function ago(seconds) {
  if (seconds < 60) return `${Math.floor(seconds)}s`;
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

function toggleView() {
  state.view = state.view === "transcript" ? "records" : "transcript";
  state.limit = PAGE_SIZE;
  writeHash();
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
  if (state.view !== "transcript") params.set("view", state.view);
  const customKinds = state.kinds.size !== DEFAULT_KINDS.length || DEFAULT_KINDS.some((kind) => !state.kinds.has(kind));
  if (customKinds) params.set("kinds", [...state.kinds].join(","));
  const text = params.toString();
  history.replaceState(null, "", text ? `#${text}` : location.pathname + location.search);
}

function readHash() {
  const params = new URLSearchParams(location.hash.slice(1));
  state.query = params.get("q") ?? "";
  state.agent = params.get("agent") ?? "";
  state.view = params.get("view") === "records" ? "records" : "transcript";
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

function setAgent(name) {
  changeFilter(() => {
    state.agent = name;
  });
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
      <time>${stamp(record.epoch)}</time>
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

function block(item) {
  const query = state.query.trim().toLowerCase();
  if (item.type === "tools") {
    const names = [...item.names].map(([name, count]) => (count > 1 ? `${name} ×${count}` : name)).join(", ");
    return html`
      <div key=${item.key} class="block tools">
        <time>${stamp(item.epoch)}</time>
        <span class="gutter">·</span>
        <span class="msg">Ran ${item.count} ${item.count === 1 ? "tool" : "tools"}: ${names}</span>
      </div>`;
  }
  if (item.type === "end") {
    const ok = item.record.status === "turn_end";
    return html`
      <div key=${item.key} class="block end">
        <time>${stamp(item.epoch)}</time>
        <span class=${`gutter ${ok ? "ok" : "fail"}`}>${ok ? "✓" : "✗"}</span>
        <span class="msg">${item.record.status ?? "?"} · ${item.record.duration_s ?? "?"}s</span>
      </div>`;
  }
  const text = item.record.text ?? "";
  return html`
    <div key=${item.key} class=${`block ${item.type}`}>
      <time>${stamp(item.epoch)}</time>
      <span class="gutter">${item.type === "prompt" ? "❯" : "●"}</span>
      <div class="body">
        ${state.agent ? null : html`<span class="tag">${item.agent}</span>`}
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
  const body = transcript ? blocks(visible).map(block) : visible.map(row);
  return html`
    <section class=${classes} onscroll=${onScroll}>
      ${hidden > 0 ? html`<button class="older" onclick=${() => { state.limit += PAGE_SIZE; scheduleRender(); }}>Show ${hidden} older</button>` : null}
      ${body.length ? body : html`<p class="empty">${empty}</p>`}
    </section>`;
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

function sidebar() {
  const now = Date.now() / 1000;
  const agents = [...state.agents.entries()].sort((left, right) => right[1].last - left[1].last);
  return html`
    <aside class="side">
      <h2>Sources</h2>
      <button class=${`source all${state.agent === "" ? " on" : ""}`} onclick=${() => setAgent("")}>
        <span class="name">all agents</span>
        <span class="count">${state.records.length}</span>
      </button>
      ${agents.map(([name, info]) => html`
        <button key=${name} class=${`source${state.agent === name ? " on" : ""}`} onclick=${() => setAgent(name)}>
          <span class=${`dot${now - info.last < ACTIVE_SECONDS ? " active" : ""}`}></span>
          <span class="name">${name}</span>
          <span class="count">${info.count}</span>
          <span class="when">${ago(now - info.last)}</span>
        </button>`)}
    </aside>`;
}

function toolbar() {
  const transcript = state.view === "transcript";
  return html`
    <div class="toolbar">
      <input id="query" type="search" placeholder="Filter text  ( / )" value=${state.query} oninput=${onQuery} />
      <button class=${`btn${transcript ? " on" : ""}`} onclick=${toggleView}>${transcript ? "Transcript" : "Records"} ( v )</button>
      ${transcript ? null : html`
        <div class="chips">
          ${KINDS.map((kind) => html`
            <button key=${kind} class=${`chip kind-${kind}${state.kinds.has(kind) ? " on" : ""}`} onclick=${() => toggleKind(kind)}>${KIND_LABELS[kind]}</button>`)}
        </div>`}
      <button class=${`btn${state.paused ? " on" : ""}`} onclick=${() => setPaused(!state.paused)}>
        ${state.paused ? `Resume · ${state.pending.length} new` : "Pause ( p )"}
      </button>
      <button class=${`btn${state.follow ? " on" : ""}`} onclick=${() => setFollow(!state.follow)}>Follow ( f )</button>
      ${transcript ? null : html`<button class=${`btn${state.wrap ? " on" : ""}`} onclick=${toggleWrap}>Wrap ( w )</button>`}
      <button class="btn" onclick=${exportVisible}>Export</button>
      <button class=${`btn${state.help ? " on" : ""}`} onclick=${toggleHelp}>Keys ( ? )</button>
    </div>`;
}

function header(matchCount) {
  const labels = { live: "live", connecting: "connecting", down: "reconnecting" };
  return html`
    <header>
      <div class="brand">hyperagent <span>logs</span></div>
      <span class=${`live ${state.live}`}>${labels[state.live]}</span>
      <span class="stat">${state.eventsPerSecond}/s</span>
      <span class="stat">${matchCount} of ${state.records.length} shown</span>
      <span class="flash">${state.flash}</span>
    </header>`;
}

function view(matches, visible) {
  return html`
    <div class="app">
      ${header(matches.length)}
      <div class="body">
        ${sidebar()}
        <main>
          ${toolbar()}
          ${stream(matches, visible)}
          ${jump()}
        </main>
      </div>
      ${help()}
    </div>`;
}

function render() {
  frame = 0;
  const matches = matching();
  const visible = matches.slice(-state.limit);
  applyDiff(root, view(matches, visible));
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
  if (event.key === "/") {
    event.preventDefault();
    document.getElementById("query")?.focus();
  } else if (event.key === "p") {
    setPaused(!state.paused);
  } else if (event.key === "f") {
    setFollow(!state.follow);
  } else if (event.key === "v") {
    toggleView();
  } else if (event.key === "w") {
    toggleWrap();
  } else if (event.key === "j") {
    step(1);
  } else if (event.key === "k") {
    step(-1);
  } else if (event.key === "?") {
    toggleHelp();
  } else if (event.key === "Escape") {
    select(null);
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
