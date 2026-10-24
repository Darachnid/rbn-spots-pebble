module.exports = [
  {
    "type": "heading",
    "defaultValue": "RBN Spots"
  },
  {
    "type": "text",
    "defaultValue": "Who heard your station on the Reverse Beacon Network. Keep the Pebble app open on your phone. Filters apply in settings below."
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Station"
      },
      {
        "type": "input",
        "messageKey": "Callsign",
        "defaultValue": "W1AW",
        "label": "Callsign",
        "description": "Spots where this station was heard.",
        "attributes": {
          "placeholder": "W1AW",
          "limit": 12,
          "type": "text"
        }
      }
    ]
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Filters"
      },
      {
        "type": "checkboxgroup",
        "messageKey": "FilterBands",
        "label": "Bands",
        "defaultValue": [true, true, true, true, true, true, true, true, true, true, true, true],
        "options": ["160m", "80m", "60m", "40m", "30m", "20m", "17m", "15m", "12m", "10m", "6m", "2m"]
      },
      {
        "type": "checkboxgroup",
        "messageKey": "FilterModes",
        "label": "Mode",
        "defaultValue": [true, true, true],
        "options": ["CW", "Digital", "Other"]
      }
    ]
  },
  {
    "type": "submit",
    "defaultValue": "Save"
  }
];
