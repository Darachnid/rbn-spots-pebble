/* Phone-side RBN fetch, band/mode filter, compact payload for the C watch app. */

var send = require("./sendspots");
var PAYLOAD_MAX = send.MAX;

function encodeCall(cs) {
  var out = "";
  for (var i = 0; i < cs.length; i++) {
    var c = cs.charAt(i);
    if (c === "/") out += "%2F";
    else if (c === "*") out += "%2A";
    else out += c;
  }
  return out;
}

function locOf(arr) {
  if (!arr || arr.length < 8) return null;
  var lat = Number(arr[6]);
  var lon = Number(arr[7]);
  if (!isFinite(lat) || !isFinite(lon)) return null;
  if (Math.abs(lat) > 90 || Math.abs(lon) > 200) return null;
  return {
    grid: arr[8] ? String(arr[8]) : "",
    where: arr[1] ? String(arr[1]) : ""
  };
}

function bandIndex(khz) {
  var f = Number(khz);
  if (f < 2000) return 0;
  if (f < 4000) return 1;
  if (f < 5500) return 2;
  if (f < 7500) return 3;
  if (f < 11000) return 4;
  if (f < 15000) return 5;
  if (f < 19000) return 6;
  if (f < 22000) return 7;
  if (f < 25000) return 8;
  if (f < 30000) return 9;
  if (f < 100000) return 10;
  return 11;
}

function bandLabel(khz) {
  var labels = ["160", "80", "60", "40", "30", "20", "17", "15", "12", "10", "6", "2"];
  return labels[bandIndex(khz)] || "?";
}

function modeBucket(row) {
  var m = Number(row[8]) || 0;
  if (m === 1) return 0;
  if (m === 2) return 1;
  if (Number(row[4]) >= 8) return 0;
  return 2;
}

function modeWire(row) {
  var b = modeBucket(row);
  if (b === 0) return "CW";
  if (b === 1) return "D";
  return "";
}

function spotPath(callsign, hash) {
  var path = "/spots.php?cdx=" + encodeCall(callsign) + "&s=0&r=32";
  if (hash) path += "&h=" + hash;
  return path;
}

function xhrJSON(host, path, done) {
  var xhr = new XMLHttpRequest();
  xhr.open("GET", "https://" + host + path, true);
  xhr.onreadystatechange = function() {
    if (xhr.readyState !== 4) return;
    var data = null;
    try {
      if (xhr.responseText) data = JSON.parse(xhr.responseText);
    } catch (e) {}
    /* RBN returns HTTP 400 with {"error":888,"ver_h":...} until hash is supplied. */
    if (data) return done(null, data);
    if (xhr.status < 200 || xhr.status > 299) return done("http " + xhr.status);
    done("json");
  };
  xhr.onerror = function() { done("net"); };
  xhr.send();
}

function buildPayload(callsign, data, filters) {
  var bandMask = (filters && filters.bandMask) || 4095;
  var modeMask = (filters && filters.modeMask) || 7;
  var info = data.call_info || {};
  var now = Number(data.now) || Math.floor(Date.now() / 1000);
  var next = [];
  var raw = data.spots || {};
  for (var id in raw) {
    if (!raw.hasOwnProperty(id)) continue;
    var row = raw[id];
    if (!row || row.length < 4) continue;
    var bi = bandIndex(row[1]);
    if (!(bandMask & (1 << bi))) continue;
    var mb = modeBucket(row);
    if (!(modeMask & (1 << mb))) continue;
    var loc = locOf(info[String(row[0])]);
    var ts = Number(row[10]);
    var age = isFinite(ts) ? Math.max(0, now - ts) : Math.max(0, Number(row[6]) || 0);
    var where = loc ? loc.where : "";
    var grid = loc ? loc.grid : "";
    if (where.length > 8) where = where.slice(0, 8);
    if (grid.length > 6) grid = grid.slice(0, 6);
    next.push({
      de: String(row[0]),
      freq: String(row[1]),
      snr: Number(row[3]) || 0,
      age: age,
      band: bandLabel(row[1]),
      mode: modeWire(row),
      grid: grid,
      where: where
    });
  }
  next.sort(function(a, b) { return a.age - b.age; });
  if (next.length === 0) {
    return ["2", callsign, "0"].join("\n");
  }
  var lines = ["2", callsign, "0"];
  var payload = "";
  for (var i = 0; i < next.length; i++) {
    var s = next[i];
    var row = [s.de, s.freq, String(s.snr), String(s.age), s.band, s.mode, s.grid, s.where].join("\t");
    lines = ["2", callsign, String(i + 1)];
    for (var j = 0; j <= i; j++) {
      var t = next[j];
      lines.push([t.de, t.freq, String(t.snr), String(t.age), t.band, t.mode, t.grid, t.where].join("\t"));
    }
    payload = lines.join("\n");
    if (payload.length > PAYLOAD_MAX) break;
  }
  while (payload.length > PAYLOAD_MAX && lines.length > 3) {
    lines.pop();
    lines[2] = String(lines.length - 3);
    payload = lines.join("\n");
  }
  return payload;
}

function fetchOnce(callsign, filters, hash, done) {
  xhrJSON("www.reversebeacon.net", spotPath(callsign, hash), function(err, data) {
    if (err) return done(err);
    if (!data) return done("empty");
    if (data.ver_h) {
      try { localStorage.setItem("rbnHash", String(data.ver_h)); } catch (e) {}
    }
    if (data.error) {
      if (data.error === 888 && data.ver_h) {
        return fetchOnce(callsign, filters, String(data.ver_h), done);
      }
      return done("rbn");
    }
    var payload;
    try {
      payload = buildPayload(callsign, data, filters);
    } catch (e) {
      console.log("rbn pack " + (e && e.message ? e.message : String(e)));
      return done("pack");
    }
    done(null, payload);
  });
}

var spotsBusy = false;
var spotsQueued = null;

function runSpotsJob(callsign, filters, cb) {
  spotsBusy = true;
  var hash = "";
  try { hash = localStorage.getItem("rbnHash") || ""; } catch (e) {}
  fetchOnce(callsign, filters, hash, function(err, payload) {
    spotsBusy = false;
    if (err) {
      console.log("rbn phone " + err);
      Pebble.sendAppMessage({ SpotsError: 1 });
    } else {
      var n = payload.split("\n").length - 3;
      console.log("rbn spots " + n + " bytes " + payload.length);
      send.sendPayload(payload, function(sendErr) {
        if (sendErr) console.log("rbn send " + sendErr);
      });
    }
    if (typeof cb === "function") cb(err);
    if (spotsQueued) {
      var next = spotsQueued;
      spotsQueued = null;
      runSpotsJob(next.call, next.filters, next.cb);
    }
  });
}

function deliverSpots(callsign, filters, cb) {
  if (spotsBusy) {
    spotsQueued = { call: callsign, filters: filters, cb: cb };
    return;
  }
  runSpotsJob(callsign, filters || { bandMask: 4095, modeMask: 7 }, cb);
}

module.exports = {
  deliverSpots: deliverSpots
};
