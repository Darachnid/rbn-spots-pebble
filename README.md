# RBN Spots

Pebble watchapp for **Pebble Time 2 (emery)** and **Pebble Round 2 (gabbro)** that shows recent [Reverse Beacon Network](https://www.reversebeacon.net/) spots for your callsign.

Phone-side JavaScript fetches RBN data and filters; the watch shows a scrollable list with SNR, band, mode, grid, and spotter location. **SELECT** opens a detail screen. Scroll to the **top spot** and **hold UP** to refresh (animated progress bar in the header).

## Requirements

- Pebble **emery** (Pebble Time 2) or **gabbro** (Pebble Round 2)
- Rebble / Pebble app on your phone with this app installed
- Internet on the phone while using the app

## Configuration (phone)

Open app settings in the Pebble/Rebble app:

- **Callsign** — station to watch (default `W1AW`)
- **Band filters** — which bands to include
- **Mode filters** — CW, digital, other

## Controls

| Action | Result |
|--------|--------|
| UP / DOWN | Move through spots |
| SELECT | Spot details |
| Hold UP (on first row) | Refresh from RBN |
| BACK | Back from details / exit app |

## Build & install

```bash
cd rbn-spots
npm install   # Clay dependency
pebble build
pebble install --emulator emery    # QEMU
pebble install --cloudpebble       # watch via phone
```

Output: `build/rbn-spots.pbw`

Store icons: black square with “RBN” (`icon_80x80.png`, `icon_144x144.png`; regenerate with `python3 scripts/make_rbn_icons.py`).

## Publish to Pebble App Store

```bash
pebble login
pebble publish
```

Copy for non-interactive upload is in [`store/LISTING.md`](store/LISTING.md). Store screenshots and icons are in [`store/`](store/).

## Architecture

| Layer | Tech |
|-------|------|
| Watch | C — `MenuLayer`, AppMessage, detail window |
| Phone | PebbleKit JS — Clay settings, HTTPS to reversebeacon.net |

Data is sent in a compact text payload (typically ~5–7 spots per message due to AppMessage size limits).

## License

MIT — see [LICENSE](LICENSE).

## Credits

Built with the [pebble-watchface](https://github.com/coredevices/pebble-watchface) / Pebble SDK workflow. RBN data from reversebeacon.net.
