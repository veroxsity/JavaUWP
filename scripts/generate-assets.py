# generate-assets.py - Build UWP tile images from the project icon.
from PIL import Image, ImageDraw
import os
import sys

pkg = sys.argv[1] if len(sys.argv) > 1 else "PackageContent"
repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
source_icon = sys.argv[2] if len(sys.argv) > 2 else os.path.join(repo, "MC.Xbox", "Assets", "Java_UWP_Icon.png")
assets = os.path.join(pkg, "Assets")
os.makedirs(assets, exist_ok=True)

if not os.path.isfile(source_icon):
    raise FileNotFoundError(f"Icon source not found: {source_icon}")


def load_icon():
    return Image.open(source_icon).convert("RGBA")


def resize_cover(img, width, height):
    scale = max(width / img.width, height / img.height)
    resized = img.resize((round(img.width * scale), round(img.height * scale)), Image.Resampling.LANCZOS)
    left = (resized.width - width) // 2
    top = (resized.height - height) // 2
    return resized.crop((left, top, left + width, top + height))


def resize_contain(img, width, height, background=(0, 0, 0, 0), fill=0.72):
    canvas = Image.new("RGBA", (width, height), background)
    target = min(width, height) * fill
    scale = min(target / img.width, target / img.height)
    resized = img.resize((round(img.width * scale), round(img.height * scale)), Image.Resampling.LANCZOS)
    canvas.alpha_composite(resized, ((width - resized.width) // 2, (height - resized.height) // 2))
    return canvas


def save(path, img):
    img.save(path)
    print(f"Created {path}")


icon = load_icon()
save(os.path.join(assets, "StoreLogo.png"), resize_cover(icon, 50, 50))
save(os.path.join(assets, "Square44x44Logo.png"), resize_cover(icon, 44, 44))
save(os.path.join(assets, "Square150x150Logo.png"), resize_cover(icon, 150, 150))
save(os.path.join(assets, "Wide310x150Logo.png"), resize_contain(icon, 310, 150))

splash_source = os.path.join(repo, "MC.Xbox", "Assets", "SplashScreen.png")

if not os.path.isfile(splash_source):
    raise FileNotFoundError(f"Splash source not found: {splash_source}")

splash = Image.open(splash_source).convert("RGBA")
save(os.path.join(assets, "SplashScreen.png"), resize_cover(splash, 1240, 600))
