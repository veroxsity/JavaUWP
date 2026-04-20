# generate_assets.py - Generate placeholder UWP tile images for MC Java port
from PIL import Image, ImageDraw
import sys
import os

pkg = sys.argv[1] if len(sys.argv) > 1 else "PackageContent"
assets = os.path.join(pkg, "Assets")
os.makedirs(assets, exist_ok=True)

def make(path, w, h, color=(29, 29, 29), text="MC"):
    img = Image.new("RGBA", (w, h), color)
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, w-1, h-1], outline=(100, 200, 100), width=4)
    d.text((w//2 - 10, h//2 - 8), text, fill=(100, 200, 100))
    img.save(path)
    print(f"Created {path}")

make(os.path.join(assets, "StoreLogo.png"),         50,  50)
make(os.path.join(assets, "Square44x44Logo.png"),   44,  44)
make(os.path.join(assets, "Square150x150Logo.png"), 150, 150)
make(os.path.join(assets, "Wide310x150Logo.png"),   310, 150)
make(os.path.join(assets, "SplashScreen.png"),      620, 300, text="Java Port")
