(function () {
  "use strict";

  function byId(id) { return document.getElementById(id); }
  function duration(seconds) {
    seconds = Math.max(0, Math.floor(Number(seconds) || 0));
    return String(Math.floor(seconds / 3600)).padStart(2, "0") + ":" +
      String(Math.floor(seconds % 3600 / 60)).padStart(2, "0") + ":" +
      String(seconds % 60).padStart(2, "0");
  }
  function request(url, buffer) {
    return fetch(url, { cache: "no-store" }).then(function (response) {
      if (!response.ok) throw new Error("Stored trip data unavailable");
      return buffer ? response.arrayBuffer() : response.json();
    });
  }
  function metric(label, value, unit) {
    return "<div><small>" + label + "</small><b>" + value + unit + "</b></div>";
  }
  function drawChart(canvas, buffer, headerBytes, offsets, limit, colors) {
    var width = Math.max(280, canvas.clientWidth);
    var height = Math.max(140, canvas.clientHeight);
    var ratio = Math.min(devicePixelRatio || 1, 2);
    canvas.width = Math.round(width * ratio);
    canvas.height = Math.round(height * ratio);
    var context = canvas.getContext("2d");
    context.scale(ratio, ratio);
    var view = new DataView(buffer);
    var count = Math.max(0, Math.floor((buffer.byteLength - headerBytes) / 16));
    var stride = Math.max(1, Math.ceil(count / Math.max(1, width * 2)));
    context.fillStyle = "#061722";
    context.fillRect(0, 0, width, height);
    context.strokeStyle = "#164b5b";
    for (var grid = 0; grid <= 4; grid++) {
      var gridY = 8 + (height - 20) * grid / 4;
      context.beginPath();
      context.moveTo(35, gridY);
      context.lineTo(width - 6, gridY);
      context.stroke();
    }
    var lastSeconds = count ? view.getUint32(headerBytes + (count - 1) * 16, true) / 1000 : 1;
    offsets.forEach(function (offset, series) {
      context.strokeStyle = colors[series];
      context.lineWidth = 1.5;
      context.beginPath();
      var started = false;
      for (var index = 0; index < count; index += stride) {
        var sample = headerBytes + index * 16;
        var seconds = view.getUint32(sample, true) / 1000;
        var divisor = offset === 4 || offset === 6 ? 100 : 1000;
        var value = view.getInt16(sample + offset, true) / divisor;
        var x = 35 + seconds / Math.max(1, lastSeconds) * (width - 41);
        var y = 8 + (limit - value) / (2 * limit) * (height - 20);
        if (started) context.lineTo(x, y); else { context.moveTo(x, y); started = true; }
      }
      context.stroke();
    });
  }
  function drawHeatmap(canvas, bins) {
    var width = Math.max(180, canvas.clientWidth);
    var height = Math.max(140, canvas.clientHeight);
    canvas.width = width;
    canvas.height = height;
    var context = canvas.getContext("2d");
    var total = (bins || []).reduce(function (sum, value) { return sum + Number(value || 0); }, 0);
    context.fillStyle = "#061722";
    context.fillRect(0, 0, width, height);
    for (var y = 0; y < 20; y++) for (var x = 0; x < 20; x++) {
      var percent = total ? 100 * Number(bins[y * 20 + x] || 0) / total : 0;
      if (!percent) continue;
      context.fillStyle = "hsl(" + Math.max(0, 220 - percent * 14) + " 90% 55%)";
      context.fillRect(x * width / 20, height - (y + 1) * height / 20,
        width / 20 + 1, height / 20 + 1);
    }
  }
  function closeDetails() {
    document.querySelectorAll(".garmin-trip-detail").forEach(function (detail) { detail.hidden = true; });
    document.querySelectorAll(".garmin-trip > button").forEach(function (button) { button.classList.remove("open"); });
  }
  function expand(button, id) {
    var detail = byId("garminTrip" + id);
    var opening = detail.hidden;
    closeDetails();
    if (!opening) return;
    button.classList.add("open");
    detail.hidden = false;
    detail.innerHTML = "<p>Loading stored trip data...</p>";
    Promise.all([
      request("/api/trip/detail?id=" + id, false),
      request("/api/trip/samples?id=" + id, true)
    ]).then(function (result) {
      var data = result[0];
      var impacts = (data.impactCounts || []).reduce(function (sum, value) { return sum + value; }, 0);
      detail.innerHTML = '<div class="garmin-trip-metrics">' +
        metric("RSI", Number(data.rsiTrip).toFixed(1), "") +
        metric("MCI", Number(data.meiTrip).toFixed(1), "") +
        metric("MAX ROLL", Number(data.maxRoll).toFixed(1), "&deg;") +
        metric("MAX PITCH", Number(data.maxPitch).toFixed(1), "&deg;") +
        metric("MAX G", Number(data.maxResultantG).toFixed(2), " g") +
        metric("IMPACTS", impacts, "") +
        '</div><div class="garmin-trip-charts"><section><b>ATTITUDE</b><canvas class="garmin-attitude"></canvas></section><section><b>ACCELERATION</b><canvas class="garmin-acceleration"></canvas></section><section><b>HEAT MAP</b><canvas class="garmin-heatmap"></canvas></section></div>';
      drawChart(detail.querySelector(".garmin-attitude"), result[1], data.headerBytes, [4, 6], 45, ["#3de7ef", "#ff9a3c"]);
      drawChart(detail.querySelector(".garmin-acceleration"), result[1], data.headerBytes, [10, 8, 12], 1, ["#3de7ef", "#ff9a3c", "#ff4d57"]);
      drawHeatmap(detail.querySelector(".garmin-heatmap"), data.heatmap);
    }).catch(function (error) { detail.innerHTML = "<p>" + error.message + "</p>"; });
  }
  function loadHistory() {
    var list = byId("garminHistoryList");
    list.innerHTML = "<p>Loading trip history...</p>";
    request("/api/trips?garmin=" + Date.now(), false).then(function (data) {
      var trips = data.trips || [];
      list.innerHTML = trips.length ? trips.map(function (trip) {
        return '<article class="garmin-trip"><button type="button" data-trip="' + trip.id +
          '"><span><small>TRIP</small><b>TRIP ' + trip.id +
          '</b></span><span><small>DURATION</small><b>' + duration(trip.durationSeconds) +
          '</b></span><i>V</i></button><div class="garmin-trip-detail" id="garminTrip' +
          trip.id + '" hidden></div></article>';
      }).join("") : "<p>No completed trips are stored.</p>";
      list.querySelectorAll("[data-trip]").forEach(function (button) {
        button.onclick = function () { expand(button, button.dataset.trip); };
      });
    }).catch(function (error) { list.innerHTML = "<p>" + error.message + "</p>"; });
  }
  function install() {
    var nav = byId("nav");
    var footer = byId("foot");
    if (!nav || !footer || byId("tripHistory")) return;
    var button = document.createElement("button");
    button.type = "button";
    button.dataset.page = "tripHistory";
    button.textContent = "TRIP HISTORY";
    button.onclick = function () {
      document.querySelectorAll(".page").forEach(function (page) { page.classList.toggle("active", page.id === "tripHistory"); });
      document.querySelectorAll("#nav button").forEach(function (item) { item.classList.toggle("active", item === button); });
      loadHistory();
    };
    nav.appendChild(button);
    var page = document.createElement("div");
    page.className = "page";
    page.id = "tripHistory";
    page.innerHTML = '<div class="garmin-history-head"><b>TRIP HISTORY</b><small>LAST 5 COMPLETED TRIPS</small></div><div id="garminHistoryList" class="garmin-history-list"><p>Loading trip history...</p></div>';
    footer.parentNode.insertBefore(page, footer);
    var style = document.createElement("style");
    style.textContent = '#nav button{min-width:120px}.garmin-history-head{height:50px;padding:8px 12px;border:1px solid #12667d;background:#08202b}.garmin-history-head b{display:block;color:#aeeeff;font-size:17px}.garmin-history-head small{color:#43bed0;font-size:10px}.garmin-history-list{position:absolute;left:7px;right:7px;top:64px;bottom:7px;overflow:auto;border:1px solid #12667d;background:#061722}.garmin-history-list>p,.garmin-trip-detail>p{padding:16px;color:#789ba5}.garmin-trip{border-bottom:1px solid #164b5b}.garmin-trip>button{width:100%;min-height:54px;display:grid;grid-template-columns:2fr 1fr 24px;align-items:center;gap:12px;padding:7px 12px;border:0;background:transparent;color:#e8fbff;text-align:left}.garmin-trip>button.open{background:#0b2a36}.garmin-trip span{display:flex;flex-direction:column}.garmin-trip small{color:#789ba5;font-size:9px}.garmin-trip b{font-size:13px}.garmin-trip i{color:#3de7ef;font-style:normal}.garmin-trip-detail{padding:8px 12px 14px;background:#04131d}.garmin-trip-metrics{display:grid;grid-template-columns:repeat(6,1fr);gap:4px;margin-bottom:8px}.garmin-trip-metrics div{padding:5px;border-left:2px solid #236b7c;background:#08202b}.garmin-trip-metrics small,.garmin-trip-metrics b{display:block}.garmin-trip-charts{display:grid;grid-template-columns:1fr 1fr 1fr;gap:7px}.garmin-trip-charts section>b{display:block;margin-bottom:3px;color:#3de7ef;font-size:10px}.garmin-trip-charts canvas{display:block;width:100%;height:150px;background:#061722;border:1px solid #164b5b}@media(max-width:800px){#nav button{min-width:100px;padding:0 6px;font-size:11px}.garmin-trip-metrics{grid-template-columns:repeat(3,1fr)}.garmin-trip-charts{grid-template-columns:1fr}}';
    document.head.appendChild(style);
  }
  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", install); else install();
}());
