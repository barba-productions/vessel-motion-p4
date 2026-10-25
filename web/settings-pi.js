(function () {
  "use strict";
  var requestedTab = new URLSearchParams(location.search).get("tab") || location.hash.slice(1);

  function activate(name) {
    document.querySelectorAll(".settings-tabs [data-tab]").forEach(function (button) {
      button.classList.toggle("active", button.dataset.tab === name);
    });
    document.querySelectorAll(".tab-panel").forEach(function (panel) {
      panel.classList.toggle("active", panel.dataset.panel === name);
    });
    history.replaceState(null, "", "#" + name);
  }

  function message(node, text, ok) {
    node.textContent = text;
    node.style.color = ok ? "#55e49a" : "#ff8c93";
  }

  function cardWithHeading(text) {
    return Array.from(document.querySelectorAll(".settings-card")).find(function (card) {
      var heading = card.querySelector("h2");
      return heading && heading.textContent.trim() === text;
    });
  }

  function enableCard(card) {
    card.querySelectorAll("input,select,button").forEach(function (control) {
      control.disabled = false;
    });
  }

  function installFallbackStatus(panels) {
    var note = document.querySelector(".network-note");
    var wifiGrid = panels.querySelector('[data-panel="wifi"] .panel-grid');
    if (!note || !wifiGrid) return;
    var card = note.closest(".settings-card");
    var status = document.createElement("span");
    var heading = document.createElement("h2");
    heading.textContent = "Fallback access point";
    status.className = "fallback-ap-status unknown";
    status.textContent = "STATUS UNAVAILABLE";
    heading.appendChild(status);
    card.insertBefore(heading, card.firstChild);
    wifiGrid.appendChild(card);

    function refresh() {
      fetch("/api/wifi/status", {cache: "no-store"}).then(function (response) {
        if (!response.ok) throw new Error("Wi-Fi status unavailable");
        return response.json();
      }).then(function (data) {
        status.className = "fallback-ap-status " + (data.accessPoint ? "active" : "inactive");
        status.textContent = data.accessPoint ? "ACTIVE" : "INACTIVE";
      }).catch(function () {
        status.className = "fallback-ap-status unknown";
        status.textContent = "STATUS UNAVAILABLE";
      });
    }
    refresh();
    setInterval(refresh, 5000);
  }

  function organizeSettings(tabs, panels) {
    var networkTab = tabs.querySelector('[data-tab="network"]');
    var motionTab = tabs.querySelector('[data-tab="motion"]');
    var motionPanel = panels.querySelector('[data-panel="motion"]');
    var vesselTab = document.createElement("button");
    var vesselPanel = document.createElement("section");
    var vesselCard = cardWithHeading("Vessel identity");
    var axisCard = cardWithHeading("Axis mapping");
    var calibrationCard = cardWithHeading("Calibration");

    vesselTab.type = "button";
    vesselTab.className = "regular-only";
    vesselTab.dataset.tab = "vessel";
    vesselTab.textContent = "VESSEL";
    tabs.insertBefore(vesselTab, networkTab);

    vesselPanel.className = "tab-panel regular-only";
    vesselPanel.dataset.panel = "vessel";
    vesselPanel.innerHTML = '<div class="panel-grid"></div>';
    panels.appendChild(vesselPanel);
    vesselPanel.querySelector(".panel-grid").appendChild(vesselCard);

    motionTab.className = "regular-only";
    motionPanel.className = "tab-panel regular-only";
    tabs.insertBefore(motionTab, networkTab);
    motionPanel.querySelector(".panel-grid").prepend(axisCard, calibrationCard);
    [vesselCard, axisCard, calibrationCard, motionPanel].forEach(enableCard);
    installFallbackStatus(panels);

    var modelSelect = panels.elements.vesselModel;
    var originalOption = modelSelect.querySelector('option[value="original"]');
    var originalWasSelected = originalOption && originalOption.selected;
    if (originalOption) originalOption.remove();
    ["Scale", "Vertical", "Elevation", "Yaw"].forEach(function (name) {
      var legacy = panels.elements["original" + name];
      if (legacy) legacy.remove();
    });
    if (originalWasSelected || !modelSelect.value || modelSelect.value === "original") {
      modelSelect.value = "tug";
      modelSelect.dispatchEvent(new Event("change"));
    }
    document.getElementById("resetModelProfile").onclick = function () {
      var defaults = {
        tug: {Scale: 100, Vertical: 20, Elevation: -10, Yaw: 0},
        yacht: {Scale: 130, Vertical: 30, Elevation: -10, Yaw: 0}
      };
      var profile = defaults[modelSelect.value] || {Scale: 100, Vertical: 0, Elevation: 0, Yaw: 0};
      Object.keys(profile).forEach(function (name) {
        panels.elements[modelSelect.value + name].value = profile[name];
        document.getElementById("model" + name).value = profile[name];
      });
    };

    var activeTab = document.createElement("input");
    activeTab.type = "hidden";
    activeTab.name = "activeTab";
    panels.appendChild(activeTab);
    panels.addEventListener("submit", function () {
      var selectedTab = tabs.querySelector("[data-tab].active");
      activeTab.value = selectedTab ? selectedTab.dataset.tab : "";
    });

    var vertical = document.getElementById("modelVertical");
    vertical.min = "-100";
    vertical.max = "100";
    vertical.step = "1";
    vertical.closest("label").querySelector("small").textContent = "-100 to +100";
    vertical.setAttribute("inputmode", "numeric");

    var style = document.createElement("style");
    style.textContent = ".form-footer .save{font-size:26px!important}" +
      ".fallback-ap-status{float:right;margin-left:18px;padding:4px 9px;border:1px solid;border-radius:3px;font-size:12px;letter-spacing:0}" +
      ".fallback-ap-status.active{border-color:#36efad;background:rgba(54,239,173,.12);color:#36efad}" +
      ".fallback-ap-status.inactive{border-color:#78a4b2;background:rgba(120,164,178,.08);color:#9bbbc4}" +
      ".fallback-ap-status.unknown{border-color:#f5c451;color:#f5c451}" +
      ".field.disabled{opacity:.45}" +
      ".field.disabled input{pointer-events:none}" +
      ".storage-line{margin:0 0 8px;color:var(--text-muted);font-size:12px}" +
      ".storage-line b{color:#dffcff}" +
      ".storage-meter{height:8px;margin:0 0 16px;border:1px solid var(--border);border-radius:3px;background:#04131d}" +
      ".storage-meter i{display:block;height:100%;width:0;background:var(--cyan);transition:width .3s}";
    document.head.appendChild(style);
    vesselTab.onclick = function () { activate("vessel"); };

    var dhcpSelect = panels.elements.dhcp;
    if (dhcpSelect) {
      var syncStaticIp = function () {
        var off = dhcpSelect.value === "no";
        ["ip", "subnet", "gateway", "dns"].forEach(function (n) {
          var el = panels.elements[n];
          if (!el) return;
          el.disabled = !off;
          var label = el.closest("label");
          if (label) label.classList.toggle("disabled", !off);
        });
      };
      dhcpSelect.addEventListener("change", syncStaticIp);
      /* the form fills from /api/settings asynchronously; re-sync once it lands */
      fetch("/api/settings", {cache: "no-store"}).then(function (r) { return r.json(); }).then(function (s) {
        dhcpSelect.value = s.dhcp ? "yes" : "no";
        syncStaticIp();
      }).catch(function () {});
      syncStaticIp();
    }

    fetch("/api/storage", {cache: "no-store"}).then(function (r) { return r.json(); }).then(function (d) {
      var text = document.getElementById("logStorageText");
      if (!text) return;
      if (!d.logs || !d.logs.mounted) { text.textContent = "NO SD CARD"; return; }
      var gb = function (b) { return (b / 1073741824).toFixed(1) + " GB"; };
      var pct = d.logs.totalBytes ? Math.round(1000 * d.logs.usedBytes / d.logs.totalBytes) / 10 : 0;
      text.textContent = gb(d.logs.usedBytes) + " OF " + gb(d.logs.totalBytes) + " USED (" + pct + "%)";
      var bar = document.getElementById("logStorageBar");
      if (bar) bar.style.width = Math.min(100, Math.max(0, pct)) + "%";
    }).catch(function () {});
  }

  /* --- HELP + DISCLAIMER tabs (static educational content, no live data) --- */

  /* Same palette as the live dashboard heatmap (app.js heatColor). */
  function helpHeatColor(value) {
    var stops = [[6, 29, 92], [23, 75, 213], [35, 221, 236], [72, 237, 140], [240, 232, 60], [255, 149, 47], [255, 59, 67]];
    var position = Math.max(0, Math.min(1, value)) * (stops.length - 1);
    var index = Math.min(stops.length - 2, Math.floor(position));
    var mix = position - index;
    return "rgb(" + stops[index].map(function (channel, i) {
      return Math.round(channel + (stops[index + 1][i] - channel) * mix);
    }).join(",") + ")";
  }

  /* Static replica of the dashboard severity-gauge SVG (same geometry/gradient).
   * Marker position uses the real formula: angle = (180 + value*18) degrees. */
  function helpGaugeSvg(id, value, stateText, stateColor, leftLabel, rightLabel, ariaLabel) {
    var angle = (180 + Math.min(10, Math.max(0, value)) * 18) * Math.PI / 180;
    var mx = (256 + 226 * Math.cos(angle)).toFixed(1);
    var my = (240 + 226 * Math.sin(angle)).toFixed(1);
    return '<svg class="help-gauge" viewBox="0 0 512 256" role="img" aria-label="' + ariaLabel + '">' +
      '<defs><linearGradient id="' + id + 'Gradient" x1="30" y1="240" x2="482" y2="240" gradientUnits="userSpaceOnUse">' +
      '<stop offset="0" stop-color="#32e889"/><stop offset=".38" stop-color="#dff044"/>' +
      '<stop offset=".68" stop-color="#ff9d32"/><stop offset="1" stop-color="#ff3b43"/></linearGradient></defs>' +
      '<path class="help-gauge-track" d="M30 240A226 226 0 0 1 482 240"/>' +
      '<path class="help-gauge-color" d="M30 240A226 226 0 0 1 482 240" stroke="url(#' + id + 'Gradient)"/>' +
      '<circle class="help-gauge-marker" cx="' + mx + '" cy="' + my + '" r="9"/>' +
      '<text class="help-gauge-value" x="256" y="164">' + value.toFixed(1) + '</text>' +
      '<text class="help-gauge-state" x="256" y="192" fill="' + stateColor + '">' + stateText + '</text>' +
      '<text class="help-gauge-scale" x="55" y="252">' + leftLabel + '</text>' +
      '<text class="help-gauge-scale end" x="457" y="252">' + rightLabel + '</text></svg>';
  }

  /* One-shot static heatmap: same grid/axes/palette as the live dashboard,
   * with a single red example cell near the bottom-left and a callout. */
  function drawHelpHeatmap() {
    var canvas = document.getElementById("helpHeatmap");
    if (!canvas) return;
    var rect = canvas.getBoundingClientRect();
    if (!rect.width || !rect.height) return;   /* hidden tab: nothing to draw yet */
    var ratio = Math.min(devicePixelRatio, 2);
    var width = Math.max(1, Math.round(rect.width * ratio));
    var height = Math.max(1, Math.round(rect.height * ratio));
    if (canvas.width !== width || canvas.height !== height) { canvas.width = width; canvas.height = height; }
    var ctx = canvas.getContext("2d");
    var left = 46 * ratio, right = 24 * ratio, top = 24 * ratio, bottom = 40 * ratio;
    var plotW = width - left - right, plotH = height - top - bottom, plotBottom = top + plotH;
    var cellW = plotW / 20, cellH = plotH / 20;
    ctx.clearRect(0, 0, width, height);
    ctx.strokeStyle = "rgba(48,184,209,.22)";
    ctx.lineWidth = 1;
    ctx.font = 11 * ratio + 'px Chakra, sans-serif';
    ctx.fillStyle = "#78a4b2";
    var tick;
    for (tick = 0; tick <= 10; tick += 2) {
      var tx = left + plotW * tick / 10, ty = plotBottom - plotH * tick / 10;
      ctx.beginPath(); ctx.moveTo(tx, top); ctx.lineTo(tx, plotBottom);
      ctx.moveTo(left, ty); ctx.lineTo(left + plotW, ty); ctx.stroke();
      ctx.textAlign = tick === 0 ? "left" : (tick === 10 ? "right" : "center");
      ctx.fillText(String(tick), tx, height - 6 * ratio);
      ctx.textAlign = "right";
      ctx.fillText(String(tick), left - 7 * ratio, ty + 4 * ratio);
    }
    /* Example distribution: one red (15%+) cell near the bottom-left corner. */
    var exCol = 1, exRow = 1;
    ctx.globalAlpha = 1;
    ctx.fillStyle = helpHeatColor(1);
    ctx.fillRect(left + exCol * cellW, plotBottom - (exRow + 1) * cellH, cellW + 1, cellH + 1);
    /* Callout pointing at the example cell */
    var cellX = left + (exCol + 0.5) * cellW, cellY = plotBottom - (exRow + 0.5) * cellH;
    var textX = left + plotW * 0.38, textY = top + 16 * ratio;
    ctx.strokeStyle = "#e8fbff";
    ctx.beginPath(); ctx.moveTo(textX - 6 * ratio, textY + 5 * ratio); ctx.lineTo(cellX, cellY); ctx.stroke();
    ctx.beginPath(); ctx.arc(cellX, cellY, 3 * ratio, 0, 6.2832); ctx.fillStyle = "#e8fbff"; ctx.fill();
    ctx.fillStyle = "#e8fbff";
    ctx.textAlign = "left";
    ctx.font = 12 * ratio + 'px Chakra, sans-serif';
    ctx.fillText("Your example: most time in the", textX, textY);
    ctx.fillText("calm, comfortable area.", textX, textY + 15 * ratio);
    /* Corner guides */
    ctx.fillStyle = "#8bb6c1";
    ctx.font = 9 * ratio + 'px Chakra, sans-serif';
    ctx.textAlign = "left";
    ctx.fillText("Higher comfort burden", left + 5 * ratio, top + 11 * ratio);
    ctx.fillText("Comfortable + calm", left + 5 * ratio, plotBottom - 6 * ratio);
    ctx.textAlign = "right";
    ctx.fillText("Harsh + high comfort burden", left + plotW - 5 * ratio, top + 11 * ratio);
    ctx.fillText("Harsh but brief", left + plotW - 5 * ratio, plotBottom - 6 * ratio);
  }

  function installInfoTabs(tabs, panels) {
    var firmwareTab = tabs.querySelector('[data-tab="firmware"]');
    var nextTab = firmwareTab && firmwareTab.nextSibling;

    var helpTab = document.createElement("button");
    helpTab.type = "button";
    helpTab.className = "regular-only";
    helpTab.dataset.tab = "help";
    helpTab.textContent = "HELP";
    helpTab.onclick = function () {
      activate("help");
      requestAnimationFrame(function () { requestAnimationFrame(drawHelpHeatmap); });
    };

    var disclaimerTab = document.createElement("button");
    disclaimerTab.type = "button";
    disclaimerTab.className = "regular-only";
    disclaimerTab.dataset.tab = "disclaimer";
    disclaimerTab.textContent = "DISCLAIMER";
    disclaimerTab.onclick = function () { activate("disclaimer"); };

    tabs.insertBefore(helpTab, nextTab);
    tabs.insertBefore(disclaimerTab, nextTab);

    var helpPanel = document.createElement("section");
    helpPanel.className = "tab-panel regular-only";
    helpPanel.dataset.panel = "help";
    helpPanel.innerHTML =
      '<div class="help-wrap">' +
      '<header class="help-head"><h1>Understanding your ride</h1>' +
      '<p>A quick guide to comfort, ride severity and your trip heat map.</p></header>' +
      '<div class="help-grid">' +
      '<div class="settings-card help-card"><h2>MCI • MOTION COMFORT INDEX</h2>' +
      '<p class="help-sub">HOW THE MOTION FEELS ON BOARD</p>' +
      helpGaugeSvg("helpMci", 0.1, "COMFORTABLE", "#32e889", "MORE COMFORTABLE", "LESS COMFORTABLE",
        "Example MCI gauge reading 0.1, comfortable, on a semicircular scale from more comfortable to less comfortable") +
      '<p class="help-note">Illustrative examples — not live readings</p>' +
      '<p class="help-rule">Lower = more comfortable</p>' +
      '<h3>What does MCI tell me?</h3>' +
      '<p>An overall indication of the motion experienced on board. It combines ongoing movement, stronger jolts and accumulated exposure.</p>' +
      '<h3>Why can it rise over time?</h3>' +
      '<p>Repeated motion can add to the comfort burden, even without a single large impact. MCI can remain elevated after the ride becomes gentler.</p>' +
      '<h3>Practical example</h3>' +
      '<p>A long passage with steady movement may feel tiring, even when RSI is relatively low.</p>' +
      '<p class="help-takeaway">Gentle motion can still become tiring over a long trip.</p></div>' +
      '<div class="settings-card help-card"><h2>RSI • RIDE SEVERITY INDEX</h2>' +
      '<p class="help-sub">HOW HARSH THE RIDE IS</p>' +
      helpGaugeSvg("helpRsi", 0.0, "CALM", "#32e889", "CALMER SEA", "MORE SEVERE",
        "Example RSI gauge reading 0.0, calm, on a semicircular scale from calmer sea to more severe") +
      '<p class="help-note">Illustrative examples — not live readings</p>' +
      '<p class="help-rule">Lower = less severe</p>' +
      '<h3>What does RSI tell me?</h3>' +
      '<p>How harsh the measured ride is. It combines acceleration peaks, sustained acceleration, sudden changes, roll and pitch rates, and repeated impacts.</p>' +
      '<h3>Why does it change quickly?</h3>' +
      '<p>A hard slam or sudden jolt can raise RSI quickly. The score then settles gradually as the motion eases.</p>' +
      '<h3>Practical example</h3>' +
      '<p>A brief hard impact may produce high RSI while the overall comfort burden is still low.</p>' +
      '<p class="help-takeaway">A short hard impact can raise RSI even on an otherwise comfortable trip.</p></div>' +
      '</div>' +
      '<div class="settings-card help-card"><h2>Example gauge modes</h2>' +
      '<div class="help-modes" aria-label="Gauge mode labels"><span class="on">INSTANT</span><span>TRIP AVG</span><span>LAST HOURS</span></div>' +
      '<ul class="help-modelist"><li><b>INSTANT</b> — Current reading</li>' +
      '<li><b>TRIP AVG</b> — Trip summary</li>' +
      '<li><b>LAST HOURS</b> — Recent history</li></ul>' +
      '<p class="help-note">Explanatory labels — the examples on this page do not change mode.</p></div>' +
      '<div class="settings-card help-card"><div class="help-compare"><span><b>MCI:</b> overall motion and exposure</span>' +
      '<span><b>RSI:</b> harshness and impacts</span></div>' +
      '<p class="help-compare-note">Read both together. Their trends can differ, and the numbers are not interchangeable. Use the scores to compare conditions and trends. They do not predict seasickness, wave height or a safe operating limit.</p></div>' +
      '<div class="settings-card help-card help-heat"><h2>How to read the heat map</h2>' +
      '<p>Position shows the ride. Color shows how long you spent there.</p>' +
      '<div class="help-heat-grid">' +
      '<div class="help-heat-chart"><span class="help-heat-y">MOTION COMFORT INDEX</span>' +
      '<canvas id="helpHeatmap" role="img" aria-label="Illustrative heat map example: horizontal axis ride severity index 0 to 10, vertical axis motion comfort index 0 to 10, with one red cell near the bottom left showing that most trip time was spent in calm comfortable conditions"></canvas>' +
      '<span class="help-heat-x">RIDE SEVERITY INDEX</span></div>' +
      '<div class="help-heat-legend"><span class="help-heat-legend-title">% OF TRIP TIME</span>' +
      '<span class="help-heat-legend-top">15%+</span><i class="help-heat-bar"></i><span>0%</span></div>' +
      '<ol class="help-steps">' +
      '<li><b>1&nbsp;&nbsp;Read left to right</b><br>Farther right = more severe motion.</li>' +
      '<li><b>2&nbsp;&nbsp;Read bottom to top</b><br>Higher up = less comfort in this model.</li>' +
      '<li><b>3&nbsp;&nbsp;Read the color</b><br>Warmer colors = more trip time in that area.</li></ol>' +
      '</div>' +
      '<p class="help-note">Illustrative data — not a live reading. Corner guides are qualitative interpretation aids, not validated boundaries or operating limits; horizontal position represents severity, not event duration.</p>' +
      '<p class="help-clarify">Red does not mean danger.</p>' +
      '<p>A red cell near the bottom left means a lot of time was spent in comfortable, calm conditions.</p>' +
      '<p class="help-note">Example: 30 minutes in one area during a 2-hour trip = 25% of trip time. This is hypothetical arithmetic, not a measured percentage from the device. The legend saturates at 15%+ — a 15%+ cell can represent any percentage at or above that point.</p></div>' +
      '<p class="help-footer">See disclaimers page for model limitations.</p>' +
      '</div>';
    panels.appendChild(helpPanel);

    var disclaimerPanel = document.createElement("section");
    disclaimerPanel.className = "tab-panel regular-only";
    disclaimerPanel.dataset.panel = "disclaimer";
    disclaimerPanel.innerHTML =
      '<div class="help-wrap"><div class="settings-card help-card">' +
      '<h2>Model indications only</h2>' +
      '<p>Motion Comfort Index (MCI), Ride Severity Index (RSI) and heatmaps are estimates produced by the mathematical model. Their interpretation can vary with hull design, vessel size, loading, speed, heading, stabilization, sensor location and individual sensitivity. Correct installation, sensor calibration and commissioning for the specific vessel are required.</p>' +
      '<p>These indications do not establish safe operating conditions, predict seasickness, assess structural integrity or replace the judgment of the captain and crew. A low or green reading does not guarantee safety or comfort. Heatmap colors indicate time spent in each score range—not danger.</p>' +
      '</div></div>';
    panels.appendChild(disclaimerPanel);

    var style = document.createElement("style");
    style.textContent =
      ".settings-tabs button:focus-visible{outline:2px solid var(--cyan);outline-offset:2px}" +
      ".help-wrap{display:flex;flex-direction:column;gap:16px}" +
      ".help-head h1{margin:0;color:#dffcff;font-size:26px;font-weight:500}" +
      ".help-head p{margin:6px 0 0;color:var(--text-muted);font-size:13px}" +
      ".help-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:16px}" +
      "@media(max-width:900px){.help-grid{grid-template-columns:1fr}}" +
      ".help-card h3{margin:16px 0 4px;color:#aeeeff;font-size:14px;font-weight:600}" +
      ".help-card p{margin:0 0 6px;color:#c2d8df;font-size:13px;line-height:1.55}" +
      ".help-sub{margin:0 0 10px!important;color:#2fbfd9!important;font-size:11px!important;letter-spacing:1px}" +
      ".help-note{color:var(--text-muted)!important;font-size:11px!important;font-style:italic}" +
      ".help-rule{color:#8cecf4!important;font-size:12px!important;letter-spacing:.5px;text-transform:uppercase}" +
      ".help-takeaway{margin-top:10px!important;padding:9px 12px;border-left:3px solid var(--cyan);background:rgba(61,231,239,.08);color:#dffcff!important}" +
      ".help-gauge{width:100%;height:auto;display:block;margin:4px 0}" +
      ".help-gauge-track{fill:none;stroke:#0e2a36;stroke-width:22}" +
      ".help-gauge-color{fill:none;stroke-width:22}" +
      ".help-gauge-marker{fill:#e8fbff;stroke:#3de7ef;stroke-width:2}" +
      ".help-gauge-value{fill:#e8fbff;font-size:96px;font-weight:500;text-anchor:middle}" +
      ".help-gauge-state{font-size:34px;letter-spacing:2px;text-anchor:middle}" +
      ".help-gauge-scale{fill:#8bb6c1;font-size:16px}" +
      ".help-gauge-scale.end{text-anchor:end}" +
      ".help-modes{display:flex;border:1px solid var(--border);border-radius:3px;overflow:hidden;margin:6px 0 10px}" +
      ".help-modes span{flex:1;padding:8px 0;text-align:center;color:#7899a4;font-size:12px;letter-spacing:1px;cursor:default}" +
      ".help-modes span.on{color:#8cecf4;background:rgba(61,231,239,.08);box-shadow:inset 0 -2px 0 var(--cyan)}" +
      ".help-modelist{margin:0 0 6px;padding-left:20px;color:#c2d8df;font-size:13px;line-height:1.7}" +
      ".help-modelist b{color:#aeeeff}" +
      ".help-compare{display:flex;gap:16px;flex-wrap:wrap;margin-bottom:6px}" +
      ".help-compare span{color:#dffcff;font-size:13px}" +
      ".help-compare b{color:#8cecf4}" +
      ".help-compare-note{margin:0;color:var(--text-muted);font-size:12px}" +
      ".help-heat-grid{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:18px;align-items:start}" +
      "@media(max-width:700px){.help-heat-grid{grid-template-columns:1fr}}" +
      ".help-heat-chart{position:relative;min-height:240px;margin:0 8px 28px 34px}" +
      ".help-heat-chart canvas{position:absolute;inset:0;width:100%;height:100%}" +
      ".help-heat-x{position:absolute;left:0;right:0;bottom:-24px;text-align:center;color:#739ba5;font-size:9px;letter-spacing:1px}" +
      ".help-heat-y{position:absolute;left:0;top:50%;transform:translate(-50%,-50%) rotate(-90deg);color:#739ba5;font-size:9px;letter-spacing:1px;white-space:nowrap;text-align:center}" +
      ".help-heat-legend{display:flex;flex-direction:column;align-items:center;gap:5px;color:#739ba5;font-size:9px}" +
      ".help-heat-legend-title{letter-spacing:1px}" +
      ".help-heat-bar{width:10px;flex:1;min-height:120px;border-radius:2px;background:linear-gradient(to top,rgb(6,29,92),rgb(23,75,213),rgb(35,221,236),rgb(72,237,140),rgb(240,232,60),rgb(255,149,47),rgb(255,59,67))}" +
      ".help-steps{margin:0;padding-left:4px;list-style:none;color:#c2d8df;font-size:12px;line-height:1.5;display:flex;flex-direction:column;gap:10px}" +
      ".help-steps b{color:#aeeeff;font-size:12px}" +
      ".help-clarify{margin:10px 0 4px!important;padding:10px 14px;border:1px solid #ff4d57;border-radius:4px;color:#ff8c93!important;font-size:16px!important;font-weight:600;letter-spacing:.5px}" +
      ".help-footer{margin:4px 0 0;padding:12px 16px;border-top:1px solid var(--border);color:var(--text-muted);font-size:12px;line-height:1.5}" +
      ".time-now{margin:0 0 14px!important;padding:9px 12px;border:1px solid var(--border);border-radius:4px;background:#04131d;color:var(--text-muted)!important;font-size:11px!important;letter-spacing:1px;line-height:1.9}" +
      ".time-now b{color:#dffcff;font-size:15px;font-variant-numeric:tabular-nums}";
    document.head.appendChild(style);

    window.addEventListener("resize", function () {
      if (helpPanel.classList.contains("active")) drawHelpHeatmap();
    });
    return drawHelpHeatmap;
  }

  function install() {
    var tabs = document.querySelector(".settings-tabs");
    var panels = document.querySelector(".settings-form");
    if (!tabs || !panels || tabs.querySelector('[data-tab="time"]')) return;
    organizeSettings(tabs, panels);

    var timeTab = document.createElement("button");
    timeTab.type = "button";
    timeTab.dataset.tab = "time";
    timeTab.textContent = "TIME";
    var firmwareTab = tabs.querySelector('[data-tab="firmware"]');
    tabs.insertBefore(timeTab, firmwareTab);

    var panel = document.createElement("section");
    panel.className = "tab-panel regular-only";
    panel.dataset.panel = "time";
    panel.innerHTML =
      '<div class="panel-grid">' +
      '<div class="settings-card wide"><h2>Time and timezone</h2>' +
      '<p>The onboard RTC preserves the clock while power is off. The clock can also sync from internet NTP servers (Google, Microsoft, Cloudflare). Trip timestamps are stored in UTC and displayed in the selected timezone.</p>' +
      '<p class="time-now">CURRENT UTC <b id="timeNowUtc">--</b><br>CURRENT LOCAL <b id="timeNowLocal">--</b></p>' +
      '<label class="field"><span>Timezone<small>Used for dashboard and trip labels</small></span><select id="piTimezone" name="timezone"></select></label>' +
      '<label class="field"><span>Time format<small>Used for trip names and dashboard times</small></span><select id="piTimeFormat"><option value="24">24-hour</option><option value="12">12-hour (AM/PM)</option></select></label>' +
      '<label class="field"><span>Date format<small>Used for trip names</small></span><select id="piDateFormat"><option value="mdy">MM/DD/YY</option><option value="dmy">DD/MM/YY</option></select></label>' +
      '<label class="field"><span>Set date and time<small>Local time in the selected timezone — UTC is calculated automatically</small></span><input id="piLocalTime" type="datetime-local" step="1"></label>' +
      '<div class="action-row"><button class="settings-action" id="savePiTime" type="button">APPLY TIME SETTINGS</button><button class="settings-action" id="syncInternetTime" type="button">SYNC TIME VIA INTERNET</button></div>' +
      '<p id="piTimeStatus"></p></div>' +
      '<div class="settings-card wide"><h2>Kiosk maintenance</h2>' +
      '<p>Exiting the HDMI kiosk requires the maintenance PIN. The initial PIN is 159 and should be changed before commissioning.</p>' +
      '<label class="field"><span>Hide mouse cursor in kiosk mode</span><select id="hideKioskCursor"><option value="true">YES</option><option value="false">NO</option></select></label>' +
      '<label class="field"><span>Maintenance PIN</span><input id="maintenancePin" type="password" inputmode="numeric" autocomplete="off"></label>' +
      '<div class="action-row"><button class="settings-action danger" id="unlockKiosk" type="button">EXIT KIOSK</button></div>' +
      '<p id="kioskStatus"></p></div></div>';
    panels.appendChild(panel);

    timeTab.onclick = function () { activate("time"); };

    /* live UTC/local clock readout (device time, every second) */
    function updateTimeNow() {
      fetch("/api/time", {cache: "no-store"}).then(function (r) { return r.json(); }).then(function (t) {
        var utc = document.getElementById("timeNowUtc");
        var loc = document.getElementById("timeNowLocal");
        if (!utc || !loc) return;
        var d = new Date(Number(t.epochMs) || 0);
        utc.textContent = d.toISOString().replace("T", " ").slice(0, 19);
        loc.textContent = (t.localTime || "").replace("T", " ") + " (" + (t.timezone || "UTC") + ")";
      }).catch(function () {});
    }
    updateTimeNow();
    setInterval(updateTimeNow, 1000);
    Promise.all([
      fetch("/api/timezones").then(function (response) { return response.json(); }),
      fetch("/api/time").then(function (response) { return response.json(); }),
      fetch("/api/settings").then(function (response) { return response.json(); })
    ]).then(function (result) {
      var select = document.getElementById("piTimezone");
      result[0].timezones.forEach(function (zone) {
        var option = document.createElement("option");
        option.value = option.textContent = zone;
        option.selected = zone === result[1].timezone;
        select.appendChild(option);
      });
      document.getElementById("piLocalTime").value = result[1].localTime.slice(0, 19);
      document.getElementById("piTimeFormat").value = result[2].timeFormat || "24";
      document.getElementById("piDateFormat").value = result[2].dateFormat || "mdy";
      document.getElementById("hideKioskCursor").value = result[2].hideKioskCursor === false ? "false" : "true";
      message(document.getElementById("piTimeStatus"), "RTC " + (result[1].rtc ? "detected" : "not detected"), result[1].rtc);
    }).catch(function (error) {
      message(document.getElementById("piTimeStatus"), error.message, false);
    });

    document.getElementById("savePiTime").onclick = function () {
      var zone = document.getElementById("piTimezone").value;
      var local = document.getElementById("piLocalTime").value;
      var formats = {
        timeFormat: document.getElementById("piTimeFormat").value,
        dateFormat: document.getElementById("piDateFormat").value,
        hideKioskCursor: document.getElementById("hideKioskCursor").value === "true"
      };
      Promise.all([
        fetch("/api/time", {
          method: "PUT",
          headers: {"Content-Type": "application/json"},
          body: JSON.stringify({timezone: zone, localTime: local})
        }),
        fetch("/api/settings", {
          method: "PATCH",
          headers: {"Content-Type": "application/json"},
          body: JSON.stringify(formats)
        })
      ]).then(function (responses) {
        return Promise.all(responses.map(function (response) {
          if (!response.ok) return response.json().then(function (data) { throw new Error(data.detail || "Time update failed"); });
          return response.json();
        }));
      }).then(function () {
        message(document.getElementById("piTimeStatus"), "Time settings applied", true);
      }).catch(function (error) {
        message(document.getElementById("piTimeStatus"), error.message, false);
      });
    };

    document.getElementById("unlockKiosk").onclick = function () {
      fetch("/api/maintenance/unlock", {
        method: "POST",
        headers: {"Content-Type": "application/json"},
        body: JSON.stringify({pin: document.getElementById("maintenancePin").value})
      }).then(function (response) {
        if (!response.ok) return response.json().then(function (data) { throw new Error(data.detail || "Unlock failed"); });
        message(document.getElementById("kioskStatus"), "Kiosk session is closing", true);
      }).catch(function (error) {
        message(document.getElementById("kioskStatus"), error.message, false);
      });
    };

    var redrawHelp = installInfoTabs(tabs, panels);

    /* SYNC TIME VIA INTERNET: enabled only while Wi-Fi reports internet access */
    var syncTimeBtn = document.getElementById("syncInternetTime");
    function refreshSyncTimeBtn() {
      fetch("/api/wifi/status", {cache: "no-store"}).then(function (r) { return r.json(); }).then(function (d) {
        syncTimeBtn.disabled = !d.internet;
        syncTimeBtn.title = d.internet ? "Synchronize the clock from internet NTP servers" : "No internet connection";
      }).catch(function () { syncTimeBtn.disabled = true; });
    }
    refreshSyncTimeBtn();
    setInterval(refreshSyncTimeBtn, 5000);
    syncTimeBtn.onclick = function () {
      var statusNode = document.getElementById("piTimeStatus");
      syncTimeBtn.disabled = true;
      message(statusNode, "Synchronizing via internet NTP...", true);
      fetch("/api/time", {cache: "no-store"}).then(function (r) { return r.json(); }).then(function (before) {
        var count0 = Number(before.syncCount) || 0;
        return fetch("/api/time/sync", {method: "POST"}).then(function () {
          var tries = 0;
          var poll = setInterval(function () {
            tries++;
            fetch("/api/time", {cache: "no-store"}).then(function (r) { return r.json(); }).then(function (t) {
              if ((Number(t.syncCount) || 0) > count0) {
                clearInterval(poll);
                document.getElementById("piLocalTime").value = t.localTime.slice(0, 19);
                message(statusNode, "Time synchronized via internet (NTP)", true);
                refreshSyncTimeBtn();
              }
            }).catch(function () {});
            if (tries >= 12) {
              clearInterval(poll);
              message(statusNode, "NTP sync timed out — check the internet connection", false);
              refreshSyncTimeBtn();
            }
          }, 1000);
        });
      }).catch(function () {
        message(statusNode, "NTP sync request failed", false);
        refreshSyncTimeBtn();
      });
    };

    tabs.addEventListener("click", function (event) {
      var button = event.target.closest("[data-tab]");
      if (button) { try { sessionStorage.setItem("vmSettingsTab", button.dataset.tab); } catch (e) {} }
    });
    var storedTab = null;
    try { storedTab = sessionStorage.getItem("vmSettingsTab"); } catch (e) {}
    var startTab = requestedTab || storedTab || "vessel";
    if (tabs.querySelector('[data-tab="' + startTab + '"]')) activate(startTab);
    if (startTab === "help") requestAnimationFrame(function () { requestAnimationFrame(redrawHelp); });
  }

  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", install);
  else install();
}());
