import { applyDiff, createElement } from "./vendor/webjsx/index.js";
import htm from "./vendor/htm.mjs";

const html = htm.bind(createElement);
const root = document.getElementById("app");

const KINDS = ["instruct", "you", "tool", "result", "log", "raw", "malformed", "text"];
const DEFAULT_KINDS = KINDS.filter((kind) => kind !== "text");
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
const PAGE_SIZE = 300;
const MAX_RECORDS = 8000;
const ACTIVE_SECONDS = 60;
const SUMMARY_CHARS = 240;

const state = {
  records: [],
  ids: new Set(),
  agents: new Map(),
  pending: [],
  paused: false,
  live: "connecting",
  follow: true,
  limit: PAGE_SIZE,
  selected: null,
  agent: "",
  query: "",
  kinds: new Set(DEFAULT_KINDS),
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

function clock(epoch) {
  const date = new Date(epoch * 1000);
  const millis = String(date.getMilliseconds()).padStart(3, "0");
  return `${pad(date.getHours())}:${pad(date.getMinutes())}:${pad(date.getSeconds())}.${millis}`;
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

function isKindShown(kind) {
  return state.kinds.has(kind) || !KINDS.includes(kind);
}

function passes(record, query) {
  if (state.agent && record.agent !== state.agent) return false;
  if (!isKindShown(record.kind)) return false;
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
  let added = false;
  for (const record of batch) {
    if (state.ids.has(record.id)) continue;
    state.ids.add(record.id);
    noteAgent(record);
    state.arrivals += 1;
    added = true;
    if (state.paused) {
      state.pending.push(record);
      continue;
    }
    const last = state.records[state.records.length - 1];
    if (last && record.epoch < last.epoch) state.unsorted = true;
    state.records.push(record);
  }
  if (!added) return;
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

function writeHash() {
  const params = new URLSearchParams();
  if (state.query) params.set("q", state.query);
  if (state.agent) params.set("agent", state.agent);
  const customKinds = state.kinds.size !== DEFAULT_KINDS.length || DEFAULT_KINDS.some((kind) => !state.kinds.has(kind));
  if (customKinds) params.set("kinds", [...state.kinds].join(","));
  const text = params.toString();
  history.replaceState(null, "", text ? `#${text}` : location.pathname + location.search);
}

function readHash() {
  const params = new URLSearchParams(location.hash.slice(1));
  state.query = params.get("q") ?? "";
  state.agent = params.get("agent") ?? "";
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

function onScroll(event) {
  const { scrollTop, scrollHeight, clientHeight } = event.currentTarget;
  const atBottom = scrollHeight - scrollTop - clientHeight < 24;
  if (atBottom !== state.follow) {
    state.follow = atBottom;
    scheduleRender();
  }
}

function detail(record) {
  const { text, pane_tail: paneTail, ...fields } = record;
  return html`
    <div class="detail" onclick=${(event) => event.stopPropagation()}>
      ${text ? html`<pre class="body">${text}</pre>` : null}
      ${paneTail?.length ? html`<pre class="pane">${paneTail.join("\n")}</pre>` : null}
      <pre class="json">${JSON.stringify(fields, null, 2)}</pre>
    </div>`;
}

function row(record) {
  const open = state.selected === record.id;
  return html`
    <div key=${record.id} class=${`row kind-${record.kind}${open ? " open" : ""}`} onclick=${() => select(record.id)}>
      <time>${clock(record.epoch)}</time>
      <span class="agent">${record.agent}</span>
      <span class="kind">${KIND_LABELS[record.kind] ?? record.kind}</span>
      <span class="text">${summary(record)}</span>
      ${open ? detail(record) : null}
    </div>`;
}

function stream(matches, visible) {
  const hidden = matches.length - visible.length;
  let empty = "No records match the current filters.";
  if (state.records.length === 0) empty = "Waiting for journal records…";
  return html`
    <section class="stream" onscroll=${onScroll}>
      ${hidden > 0 ? html`<button class="older" onclick=${() => { state.limit += PAGE_SIZE; scheduleRender(); }}>Show ${hidden} older</button>` : null}
      ${visible.length ? visible.map(row) : html`<p class="empty">${empty}</p>`}
    </section>`;
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
  return html`
    <div class="toolbar">
      <input id="query" type="search" placeholder="Filter text  ( / )" value=${state.query} oninput=${onQuery} />
      <div class="chips">
        ${KINDS.map((kind) => html`
          <button key=${kind} class=${`chip kind-${kind}${state.kinds.has(kind) ? " on" : ""}`} onclick=${() => toggleKind(kind)}>${KIND_LABELS[kind]}</button>`)}
      </div>
      <button class=${`btn${state.paused ? " on" : ""}`} onclick=${() => setPaused(!state.paused)}>
        ${state.paused ? `Resume · ${state.pending.length} new` : "Pause ( p )"}
      </button>
      <button class=${`btn${state.follow ? " on" : ""}`} onclick=${() => { state.follow = !state.follow; scheduleRender(); }}>Follow ( f )</button>
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
        </main>
      </div>
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
  }
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
  if (event.key === "/") {
    event.preventDefault();
    document.getElementById("query")?.focus();
  } else if (event.key === "p") {
    setPaused(!state.paused);
  } else if (event.key === "f") {
    state.follow = !state.follow;
    scheduleRender();
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
