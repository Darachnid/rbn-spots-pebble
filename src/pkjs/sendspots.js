/* One AppMessage per update — chunking is unreliable on some links. */

var MAX = 240;

function sendPayload(payload, done) {
  if (!payload) return done("empty");
  if (payload.length > MAX) return done("big");
  Pebble.sendAppMessage({ SpotsPayload: payload }, function() { done(); }, function() { done("send"); });
}

module.exports = { sendPayload: sendPayload, MAX: MAX };
