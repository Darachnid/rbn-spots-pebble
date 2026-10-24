var Clay = require("@rebble/clay");
var clayConfig = require("./config");
var rbn = require("./rbn");

function sanitize(value) {
  var up = String(value || "").toUpperCase();
  var out = "";
  for (var i = 0; i < up.length; i++) {
    var c = up.charAt(i);
    if ((c >= "A" && c <= "Z") || (c >= "0" && c <= "9") || c === "/" || c === "*") out += c;
  }
  if (out.length > 12) out = out.slice(0, 12);
  return out;
}

function storedCallRaw() {
  try {
    return sanitize(localStorage.getItem("callsign") || "");
  } catch (e) {
    return "";
  }
}

function storedCall() {
  var saved = storedCallRaw();
  return saved.length >= 3 ? saved : "W1AW";
}

function hasUserCallsign() {
  return storedCallRaw().length >= 3;
}

function clayField(settings, key) {
  if (!settings) return undefined;
  var v = settings[key];
  if (v && typeof v === "object" && "value" in v) return v.value;
  return v;
}

function checkboxMask(arr, count) {
  var all = (1 << count) - 1;
  if (!arr || !arr.length) return all;
  var mask = 0;
  for (var i = 0; i < count && i < arr.length; i++) {
    if (arr[i]) mask |= (1 << i);
  }
  return mask || all;
}

function storedFilters() {
  var bandMask = 4095;
  var modeMask = 7;
  try {
    var b = parseInt(localStorage.getItem("bandMask"), 10);
    var m = parseInt(localStorage.getItem("modeMask"), 10);
    if (isFinite(b) && b > 0) bandMask = b;
    if (isFinite(m) && m > 0) modeMask = m;
  } catch (e) {}
  return { bandMask: bandMask, modeMask: modeMask };
}

function saveFilters(dict) {
  var bandMask = checkboxMask(dict.FilterBands, 12);
  var modeMask = checkboxMask(dict.FilterModes, 3);
  try {
    localStorage.setItem("bandMask", String(bandMask));
    localStorage.setItem("modeMask", String(modeMask));
  } catch (e) {}
  return { bandMask: bandMask, modeMask: modeMask };
}

function applyCallDefault(call) {
  function walk(items) {
    if (!items) return;
    for (var i = 0; i < items.length; i++) {
      if (items[i].messageKey === "Callsign") items[i].defaultValue = call;
      if (items[i].items) walk(items[i].items);
    }
  }
  walk(clayConfig);
}

var clay = new Clay(clayConfig, null, { autoHandleEvents: false });

function refreshSpots() {
  var filters = storedFilters();
  rbn.deliverSpots(storedCall(), filters);
}

Pebble.addEventListener("ready", function() {
  setTimeout(refreshSpots, 600);
  setInterval(refreshSpots, 30000);
});

Pebble.addEventListener("appmessage", function(e) {
  var payload = e.payload || {};

  if (payload.SpotsRequest) {
    var watchCall = sanitize(payload.Callsign);
    var reqCall = hasUserCallsign() ? storedCall() : watchCall;
    if (reqCall.length < 3) reqCall = storedCall();
    if (reqCall.length >= 3) {
      rbn.deliverSpots(reqCall, storedFilters());
      if (watchCall.length >= 3 && watchCall !== reqCall) {
        Pebble.sendAppMessage({ Callsign: reqCall });
      }
    }
    return;
  }

  var call = payload.Callsign;
  if (!call) return;
  var clean = sanitize(call);
  if (clean.length < 3) return;
  try { localStorage.setItem("callsign", clean); } catch (err) {}
  try { clay.setSettings("Callsign", clean); } catch (err) {}
});

Pebble.addEventListener("showConfiguration", function() {
  applyCallDefault(storedCall());
  Pebble.openURL(clay.generateUrl());
});

Pebble.addEventListener("webviewclosed", function(e) {
  if (!e || !e.response || e.response === "CANCELLED") return;
  var raw = clay.getSettings(e.response, false);
  saveFilters({
    FilterBands: clayField(raw, "FilterBands"),
    FilterModes: clayField(raw, "FilterModes")
  });
  var clean = sanitize(clayField(raw, "Callsign"));
  if (clean.length >= 3) {
    try { localStorage.setItem("callsign", clean); } catch (err) {}
    Pebble.sendAppMessage({ Callsign: clean });
  }
  refreshSpots();
});
