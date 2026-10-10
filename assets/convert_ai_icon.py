"""Turn the saved AI illustration into native Windows icons; never edit the master."""
from pathlib import Path
from PIL import Image, ImageChops, ImageDraw, ImageOps

assets = Path(__file__).resolve().parent
master = assets / "homepod-anime-ai.png"
with Image.open(master) as source:
    source.load()
    badge = ImageOps.fit(source.convert("RGBA"), (1024, 1024), method=Image.Resampling.LANCZOS)

mask = Image.new("L", badge.size, 0)
ImageDraw.Draw(mask).rounded_rectangle((16, 16, 1007, 1007), radius=270, fill=255)
badge.putalpha(ImageChops.multiply(badge.getchannel("A"), mask))
sizes = [(n, n) for n in (16, 20, 24, 32, 40, 48, 64, 128, 256)]
badge.save(assets / "homepod-anime.ico", format="ICO", sizes=sizes)
badge.resize((512, 512), Image.Resampling.LANCZOS).save(assets / "homepod-anime-preview.png")

warning = badge.copy()
draw = ImageDraw.Draw(warning)
draw.ellipse((696, 696, 992, 992), fill="#11223e")
draw.ellipse((724, 724, 964, 964), fill="#ffc45c")
draw.rounded_rectangle((832, 774, 856, 859), radius=12, fill="#11223e")
draw.ellipse((832, 892, 856, 916), fill="#11223e")
warning.save(assets / "homepod-anime-warning.ico", format="ICO", sizes=sizes)

print("Created anime icons: " + str(assets / "homepod-anime.ico"))
print("Master bytes: " + str(master.stat().st_size))
print("Icon sizes: " + str(sizes))
