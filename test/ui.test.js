const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");
const path = require("node:path");
const app = fs.readFileSync(path.join(__dirname, "..", "web", "app.js"), "utf8");
const settings = fs.readFileSync(path.join(__dirname, "..", "web", "settings.html"), "utf8");
const settingsControls = fs.readFileSync(path.join(__dirname, "..", "web", "settings-pi.js"), "utf8");

function definition(source, name) {
  const start = source.indexOf("function " + name + "(");
  assert.notEqual(start, -1, name);
  let depth = 0, quote = "", escaped = false;
  for (let i = source.indexOf("{", start); i < source.length; i++) {
    const ch = source[i];
    if (quote) {
      if (escaped) escaped = false;
      else if (ch === "\\") escaped = true;
      else if (ch === quote) quote = "";
    } else if (ch === '"' || ch === "'" || ch === "`") quote = ch;
    else if (ch === "{") depth++;
    else if (ch === "}" && --depth === 0) return source.slice(start, i + 1);
  }
  throw new Error("Unterminated function " + name);
}

function historyContext() {
  const pending = [], list = {innerHTML: "", firstChild: {}, querySelectorAll: () => []};
  const context = vm.createContext({
    Date, performance: {now: () => 0}, document: {getElementById: () => list},
    activeView: "trip-history", tripHistoryLoaded: true, tripHistoryRequest: null,
    tripHistoryRefreshPending: false, state: {to: null}, serialPort: null,
    lastResetSequence: null, overviewImpacts: [], charts: {},
    integrateTelemetry() {}, destroyTripHistoryCharts() {},
    durationClock: String,
    fetch: () => new Promise(resolve => pending.push(resolve))
  });
  ["loadTripHistory", "invalidateTripHistory", "accept"].forEach(name =>
    vm.runInContext(definition(app, name), context));
  const finish = async trips => {
    assert.ok(pending.length, "request expected");
    pending.shift()({ok: true, json: async () => ({trips})});
    await new Promise(resolve => setImmediate(resolve));
  };
  return {context, pending, list, finish};
}

test("confirmed stop refreshes history once, including remote/automatic stop", async () => {
  const {context, pending, list, finish} = historyContext();
  context.state.to = {trip: true};
  context.accept({trip: false});
  assert.equal(pending.length, 1);
  context.accept({trip: false});
  assert.equal(pending.length, 1);
  await finish([{id: 2, label: "New trip", samples: 1}]);
  assert.match(list.innerHTML, /New trip/);
  assert.equal(context.tripHistoryLoaded, true);
});

test("history is invalidated but not fetched outside Trip History", () => {
  const {context, pending} = historyContext();
  context.activeView = "overview";
  context.invalidateTripHistory();
  assert.equal(context.tripHistoryLoaded, false);
  assert.equal(pending.length, 0);
});

test("a stop during an old history fetch queues a fresh request", async () => {
  const {context, pending, list, finish} = historyContext();
  context.loadTripHistory(true);
  context.invalidateTripHistory();
  await finish([]);
  assert.equal(pending.length, 1);
  await finish([{id: 3, label: "Queued trip"}]);
  assert.match(list.innerHTML, /Queued trip/);
  assert.equal(context.tripHistoryRefreshPending, false);
});

test("failed history request is visible and remains retryable", async () => {
  const {context, pending, list} = historyContext();
  context.loadTripHistory(true);
  pending.shift()({ok: false});
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(list.firstChild.textContent, "Trip history unavailable");
  assert.equal(context.tripHistoryLoaded, false);
  context.loadTripHistory();
  assert.equal(pending.length, 1);
});

test("empty and whitespace vessel names hide the whole bordered element", () => {
  const node = {};
  const context = vm.createContext({document: {getElementById: () => node}});
  vm.runInContext(definition(app, "setVesselName"), context);
  for (const value of ["", "   \t\n", undefined]) {
    context.setVesselName(value);
    assert.equal(node.hidden, true);
    assert.equal(node.textContent, "");
  }
  context.setVesselName("  TUG ONE  ");
  assert.equal(node.hidden, false);
  assert.equal(node.textContent, "TUG ONE");
});

test("all live and archived attitude charts share the selected symmetric range", () => {
  const context = vm.createContext({attitudeYRange: 15});
  vm.runInContext(definition(app, "attitudeChartRange"), context);
  assert.equal(JSON.stringify(context.attitudeChartRange()), "[-15,15]");
  context.attitudeYRange = 30;
  assert.equal(JSON.stringify(context.attitudeChartRange()), "[-30,30]");
  assert.match(definition(app, "createCharts"), /attitudeChartRange/);
  assert.match(definition(app, "renderArchivedCharts"), /attitudeChartRange/);
});

test("wireless-disabled or missing capabilities never start Wi-Fi polling", () => {
  let requests = 0;
  const context = vm.createContext({window: {}, fetch() {requests++;}});
  vm.runInContext(definition(settings, "loadWifiStatus"), context);
  context.loadWifiStatus();
  context.window.vmUiSettings = {wirelessAllowed: false};
  context.loadWifiStatus();
  assert.equal(requests, 0);
});

test("all inline settings scripts remain syntactically valid", () => {
  for (const match of settings.matchAll(/<script[^>]*>([\s\S]*?)<\/script>/g))
    new vm.Script(match[1]);
});

function impactContext(saved = {impactStart: 0.25, impactEnd: 0.15}) {
  function node(value = "") {
    return {
      value: String(value), disabled: false, textContent: "", listeners: {},
      classList: {toggle() {}}, attributes: {}, validityMessage: "",
      details: {open: false},
      addEventListener(type, handler) { this.listeners[type] = handler; },
      setCustomValidity(message) { this.validityMessage = message; },
      setAttribute(name, value) { this.attributes[name] = value; },
      closest() { return this.details; }
    };
  }
  const nodes = Object.fromEntries([
    "impactThresholdMode", "impactThresholdLevel", "impactThresholdLevelField",
    "impactCustomField", "impactThresholdEffective", "impactThresholdStatus"
  ].map(id => [id, node()]));
  nodes.impactThresholdMode.value = "preset";
  nodes.impactThresholdLevel.value = "3";
  const form = Object.assign(node(), {elements: {impactStart: node(0.25), impactEnd: node(0.15)}});
  const context = vm.createContext({document: {getElementById: id => nodes[id]}});
  vm.runInContext(definition(settingsControls, "installImpactControls"), context);
  const controls = context.installImpactControls(form);
  const levels = [
    {startG: 0.10, endG: 0.06}, {startG: 0.17, endG: 0.102},
    {startG: 0.25, endG: 0.15}, {startG: 0.40, endG: 0.24}, {startG: 0.60, endG: 0.36}
  ];
  controls.loaded({...saved, impactThresholdLevels: levels});
  function formData() {
    const data = new Map();
    form.listeners.formdata({formData: data});
    return data;
  }
  function input(id, value, event = "input") {
    nodes[id].value = String(value);
    nodes[id].listeners[event]();
  }
  return {nodes, form, controls, levels, formData, input};
}

test("impact presets map all five levels to exact firmware pairs and persist disabled fields", () => {
  const {nodes, form, controls, levels, input, formData} = impactContext();
  assert.equal(nodes.impactThresholdMode.value, "preset");
  assert.equal(nodes.impactThresholdLevel.disabled, true, "unknown trip state locks controls");
  controls.setTripActive(false);
  levels.forEach((level, index) => {
    input("impactThresholdLevel", index + 1);
    assert.equal(Number(form.elements.impactStart.value), level.startG);
    assert.equal(Number(form.elements.impactEnd.value), level.endG);
    assert.equal(form.elements.impactStart.disabled, true);
    assert.equal(Number(formData().get("impactEnd")), level.endG);
  });
  assert.match(nodes.impactThresholdEffective.textContent, /Level 5.*start: 0.6 g \/ end: 0.36 g/);
});

test("custom saved pairs survive load/save without snapping, and can switch back to preset", () => {
  const {nodes, form, controls, input, formData} = impactContext({impactStart: 0.3333, impactEnd: 0.1234});
  controls.setTripActive(false);
  assert.equal(nodes.impactThresholdMode.value, "custom");
  assert.equal(nodes.impactThresholdLevel.disabled, true);
  assert.equal(Number(formData().get("impactStart")), 0.3333);
  assert.equal(form.elements.impactEnd.disabled, false);
  input("impactThresholdMode", "preset", "change");
  assert.equal(Number(formData().get("impactStart")), 0.25);
  input("impactThresholdMode", "custom", "change");
  assert.equal(nodes.impactCustomField.details.open, true);
  form.elements.impactEnd.value = "0.3";
  form.elements.impactEnd.listeners.input();
  assert.match(form.elements.impactEnd.validityMessage, /end below start/);
});

test("trip active, unknown status, or invalid preset configuration never submits threshold edits", () => {
  const {nodes, controls, formData} = impactContext();
  controls.setTripActive(true);
  assert.equal(nodes.impactThresholdMode.disabled, true);
  assert.equal(formData().has("impactStart"), false);
  controls.setTripActive(false);
  assert.equal(formData().has("impactEnd"), true);
  controls.tripError(new Error("Trip status unavailable"));
  assert.equal(nodes.impactThresholdMode.disabled, true);
  assert.equal(formData().has("impactStart"), false);
  controls.loaded({impactThresholdLevels: []});
  controls.setTripActive(false);
  assert.equal(nodes.impactThresholdMode.disabled, true);
  assert.equal(formData().has("impactEnd"), false);
});

test("matching firmware pairs tolerate float serialization, not genuinely custom thresholds", () => {
  const {nodes} = impactContext({impactStart: 0.250000001, impactEnd: 0.150000006});
  assert.equal(nodes.impactThresholdMode.value, "preset");
  assert.equal(nodes.impactThresholdLevel.value, 3);
  assert.equal(impactContext({impactStart: 0.251, impactEnd: 0.15}).nodes.impactThresholdMode.value, "custom");
});

test("restoring defaults reloads preset controls only after command success", async () => {
  let reloads = 0, success = false;
  const context = vm.createContext({
    fetch: async () => ({ok: success, status: 400}),
    statusNode: {},
    location: {reload() { reloads++; }}
  });
  vm.runInContext(definition(settings, "command"), context);
  await context.command("defaults");
  assert.equal(reloads, 0);
  assert.match(context.statusNode.textContent, /Unable to restore defaults/);
  success = true;
  await context.command("defaults");
  assert.equal(reloads, 1);
});

test("firmware status loads the installed version without starting an update", async () => {
  const nodes = {};
  const requests = [];
  const context = vm.createContext({
    document: {getElementById: id => nodes[id] || (nodes[id] = {})},
    firmwareButton: {}, localPreview: false, firmwareTimer: null,
    renderConnection() {},
    fetch: async (url, options) => {
      requests.push({url, options});
      return {ok: true, json: async () => ({
        currentVersion: "1.0.5", state: "idle", message: "Ready to check for updates"
      })};
    }
  });
  ["renderFirmware", "loadFirmwareStatus"].forEach(name =>
    vm.runInContext(definition(settings, name), context));
  await context.loadFirmwareStatus();
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(nodes.firmwareCurrent.textContent, "1.0.5");
  assert.equal(nodes.firmwareAvailable.textContent, "NOT CHECKED");
  assert.equal(context.firmwareButton.disabled, false);
  assert.equal(requests.length, 1);
  assert.equal(requests[0].url, "/api/ota/status");
  assert.equal(requests[0].options.cache, "no-store");
});
