#!/usr/bin/env python3
"""Generate store and menu icons: black background, white RBN label."""

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
FONT_CANDIDATES = (
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
    "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
)


def load_font(size: int) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    for path in FONT_CANDIDATES:
        if Path(path).is_file():
            return ImageFont.truetype(path, size)
    return ImageFont.load_default()


def make_icon(size: int) -> Image.Image:
    img = Image.new("RGBA", (size, size), (0, 0, 0, 255))
    draw = ImageDraw.Draw(img)
    text = "RBN"
    font_size = max(8, int(size * 0.38))
    font = load_font(font_size)
    bbox = draw.textbbox((0, 0), text, font=font)
    tw = bbox[2] - bbox[0]
    th = bbox[3] - bbox[1]
    x = (size - tw) // 2 - bbox[0]
    y = (size - th) // 2 - bbox[1]
    draw.text((x, y), text, fill=(255, 255, 255, 255), font=font)
    return img


def main() -> None:
    targets = [
        (80, ROOT / "icon_80x80.png"),
        (144, ROOT / "icon_144x144.png"),
        (25, ROOT / "images" / "menu_icon.png"),
        (80, ROOT / "images" / "icon_80x80.png"),
        (144, ROOT / "images" / "icon_144x144.png"),
        (80, ROOT / "store" / "icon_80x80.png"),
        (144, ROOT / "store" / "icon_144x144.png"),
    ]
    for size, path in targets:
        path.parent.mkdir(parents=True, exist_ok=True)
        make_icon(size).save(path)
        print(f"Wrote {path} ({size}x{size})")


if __name__ == "__main__":
    main()
