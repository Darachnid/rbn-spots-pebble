# Store & share assets

Ready for [Pebble App Store](https://apps.repebble.com/) upload and GitHub README.

| Asset | File |
|-------|------|
| Installable bundle | `rbn-spots.pbw` |
| List screenshot (Time 2) | `screenshot_list.png` |
| Detail screenshot (Time 2) | `screenshot_detail.png` |
| List screenshot (Round 2) | `screenshot_list_gabbro.png` |
| Detail screenshot (Round 2) | `screenshot_detail_gabbro.png` |
| Icon 80×80 | `icon_80x80.png` (also project root and `images/icon_80x80.png`) |
| Icon 144×144 | `icon_144x144.png` (also project root and `images/icon_144x144.png`) |
| Listing copy | `LISTING.md` |

Regenerate screenshots (emery emulator):

```bash
pebble install --emulator emery
pebble screenshot --no-open --emulator emery store/screenshot_list.png
pebble emu-button click select --emulator emery
pebble screenshot --no-open --emulator emery store/screenshot_detail.png
cp store/screenshot_list.png screenshot_emery.png
python3 scripts/make_rbn_icons.py
```

See also [GITHUB.md](GITHUB.md) for repo layout.
