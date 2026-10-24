# Sharing on GitHub

## Option A — Standalone repo (recommended for store users)

Create a public repo (e.g. `yourname/rbn-spots`) and push this directory:

```bash
cd rbn-spots
git init
git add README.md LICENSE package.json wscript src resources images store \
  .gitignore
git add store/screenshot_*.png store/icon_*.png store/LISTING.md
# Omit store/rbn-spots.pbw if you prefer releases-only binaries
git commit -m "RBN Spots 1.2.0 — Pebble Time 2 watchapp"
git remote add origin git@github.com:YOURUSER/rbn-spots.git
git push -u origin main
```

Attach **`store/rbn-spots.pbw`** to a GitHub **Release** tagged `v1.2.0`.

Use **`store/screenshot_list.png`** as the social preview (repo Settings → Social preview).

## Option B — Under `pebble-watchface` monorepo

If you maintain [coredevices/pebble-watchface](https://github.com/coredevices/pebble-watchface) or a fork:

```text
pebble-watchface/
  examples/
    rbn-spots/    ← copy this project (exclude node_modules, build/)
```

Link from the skill doc: [examples/rbn-spots.md](../../.cursor/skills/pebble-watchface/examples/rbn-spots.md) (Cursor skill) or add `examples/rbn-spots/README.md` in the repo pointing to upstream.

## README badges (optional)

```markdown
![Pebble](https://img.shields.io/badge/Pebble-Time%202-emery-blue)
![License](https://img.shields.io/badge/license-MIT-green)
```

## What not to commit

- `node_modules/`, `build/`, `*.pbw` (optional), log files — see root `.gitignore`
