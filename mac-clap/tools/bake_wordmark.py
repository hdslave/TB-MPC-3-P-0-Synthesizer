"""Prepare the original Slink wordmark for the desktop renderer's screen blend."""
from pathlib import Path

from PIL import Image


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[2]
    with Image.open(root / "tools/sp16_thorn/media/wordmark.webp") as source:
        if source.size != (680, 108):
            raise ValueError("The desktop wordmark must be 680 by 108 pixels")
        metal = Image.new("RGB", source.size)
        metal.paste(source, mask=source.getchannel("A"))
    (root / "mac-clap/assets/thorn-wordmark.bgra").write_bytes(metal.convert("RGBA").tobytes("raw", "BGRA"))
