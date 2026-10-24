# Pebble App Store listing — RBN Spots

Use these fields with `pebble publish` or the store web UI.

## App name

**RBN Spots**

## Category

**tools**

## Short description (store)

Reverse Beacon Network spots on your wrist — who heard your callsign, SNR, band, and spotter location.

## Full description

RBN Spots brings [Reverse Beacon Network](https://www.reversebeacon.net/) reports to Pebble Time 2.

See who recently heard your station (or any callsign you configure), with signal-to-noise ratio, frequency, band, mode, Maidenhead grid, and spotter country. Scroll the list, tap **SELECT** for full details, and **hold UP** on the newest spot to pull a fresh list from the network.

**Phone setup:** Configure callsign, bands, and modes in the Pebble app settings (Clay). The phone fetches RBN over HTTPS; the watch stays battery-friendly with a native C UI.

**Target:** Pebble Time 2 (emery) and Pebble Round 2 (gabbro) color displays.

## Release notes (1.2.1)

- Pebble Round 2 (gabbro) support with round-safe layout insets

## Release notes (1.2.0)

- Spot detail screen on SELECT
- Hold UP at top of list to refresh with header progress animation
- Live spot ages and last-sync indicator
- Band/mode filters on phone (Clay)
- Stability fixes for AppMessage and RBN API handshake

## Screenshots (include in upload)

| File | Use |
|------|-----|
| `store/screenshot_list.png` | Primary — spot list |
| `store/screenshot_detail.png` | Detail view (emery) |
| `store/screenshot_list_gabbro.png` | List on Round 2 (if present) |
| `store/screenshot_detail_gabbro.png` | Detail on Round 2 (if present) |

## Icons

| File | Size |
|------|------|
| `store/icon_80x80.png` | Small store icon |
| `store/icon_144x144.png` | Large store icon |

## Non-interactive publish (optional)

```bash
cd rbn-spots
pebble login
pebble publish --non-interactive \
  --description "Reverse Beacon Network spots on your wrist — SNR, band, spotter location." \
  --release-notes "Detail view, hold-UP refresh, live ages, Clay filters." \
  --category tools \
  --is-published
```

Adjust `--is-published` if you want a draft release first.

## UUID

`bbcc0f48-ebac-49b4-bfc5-ea0eb353cc5e` (do not change for store updates)
