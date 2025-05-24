import argparse
import requests
import base64
import json
import os
import sys
import tempfile
from html2image import Html2Image
from PIL import Image, ImageDraw, ImageFont, ImageOps, ImageEnhance, ImageFilter
from io import BytesIO
import time

# url = http://pi-star/simple
# size=(1024, 800))
# crop = 11 174 11 68

# === Konfiguration ===
ESP_HOST = 'http://192.168.188.226:80'
DISPLAY_WIDTH = 960
DISPLAY_HEIGHT = 540

# Umgewandeltes Bild soll im Skriptordner gespeichert werden (klein und grau und optimiert)
DEBUG = False


def send_request(path: str, payload=None):
    #Sendet eine HTTP POST an den ESP und gibt den JSON-Status zurück.
    url = f"{ESP_HOST}/{path}"
    try:
        if payload is not None:
            res = requests.post(url, json=payload)
        else:
            res = requests.post(url)
        res.raise_for_status()
        data = res.json()
        status = data.get('status')
        message = data.get('message')
        print(f"[{path}] status: {status}, message: {message}")
        return data
    except requests.RequestException as e:
        print(f"[{path}] HTTP-Fehler: {e}")
    except (ValueError, json.JSONDecodeError):
        print(f"[{path}] Ungültiges JSON in der Antwort: {res.text}")
    return None


"""
def pack_4bit_grayscale(image: Image.Image) -> bytes:
    # Pack 8-bit grayscale image (16 levels) into 4-bit per pixel bytes.
    pixels = list(image.getdata())
    packed = bytearray()
    for i in range(0, len(pixels), 2):
        first = pixels[i] // 17  # 0-15
        if i + 1 < len(pixels):
            second = pixels[i + 1] // 17  # 0-15
        else:
            second = 0
        packed.append((first << 4) | second)
    return bytes(packed)
"""


def pack_4bit_grayscale(image: Image.Image, gamma: float = 2.2) -> bytes:
    # Pack 8-bit grayscale image into 4-bit grayscale with gamma correction. gamma: Perceptual correction (default ~2.2).
    pixels = list(image.getdata())
    packed = bytearray()
    for i in range(0, len(pixels), 2):
        def to_4bit(p):
            normalized = p / 255.0
            corrected = pow(normalized, 1 / gamma)
            return int(round(corrected * 15))

        first = to_4bit(pixels[i])
        second = to_4bit(pixels[i + 1]) if i + 1 < len(pixels) else 0
        packed.append((first << 4) | second)
    return bytes(packed)


def apply_gamma(image: Image.Image, gamma: float) -> Image.Image:
    inv_gamma = 1.0 / gamma
    table = [round((i / 255.0) ** inv_gamma * 255) for i in range(256)]
    return image.point(table)


def encode_and_send_pil(image: Image.Image, image_path: str, bright: float = 1.0, gamma: float = 1.0, crop: bool = False):
    url = f"{ESP_HOST}/setImage"
    max_width, max_height = 960, 540

    clear_screen()

    # Convert to grayscale with 16 levels
    image = image.convert("L")

    if (bright != 1.0):
        enhancer = ImageEnhance.Brightness(image)
        image = enhancer.enhance(bright)
    if (gamma != 1.0):
        image = apply_gamma(image, gamma)
    if crop:
        # Bild zuerst auf max. Breite skalieren (Höhe proportional)
        w_percent = (max_width / float(image.size[0]))
        new_height = int((float(image.size[1]) * float(w_percent)))
        image = image.resize((max_width, new_height), Image.Resampling.LANCZOS)

        # Falls die Höhe größer als max_height, dann crop top und bottom
        if new_height > max_height:
            top = (new_height - max_height) // 2
            bottom = top + max_height
            image = image.crop((0, top, max_width, bottom))

    # Resize to max 960x540 while maintaining aspect ratio
    image.thumbnail((max_width, max_height), Image.Resampling.LANCZOS)

    # Sharpen the image
    image = image.filter(ImageFilter.UnsharpMask(radius=1, percent=175, threshold=3))

    # Calculate offset to center the image on 960x540 canvas
    width, height = image.size
    offset_x = (max_width - width) // 2
    offset_y = (max_height - height) // 2

    # Save processed image to out.png
    if DEBUG:
        canvas = Image.new("L", (max_width, max_height), color=255)
        canvas.paste(image, (offset_x, offset_y))
        canvas.save("out.png")

    block_size = 100

    for y in range(0, max_height, block_size):
        for x in range(0, max_width, block_size):
            box = (x, y, min(x + block_size, max_width), min(y + block_size, max_height))
            block = canvas.crop(box)

            # Ensure block is full size (pad if necessary)
            if block.size != (block_size, block_size):
                padded = Image.new("L", (block_size, block_size), color=255)
                padded.paste(block, (0, 0))
                block = padded

            packed_data = pack_4bit_grayscale(block)
            encoded_image = base64.b64encode(packed_data).decode("ascii")

            payload = {
                "name": os.path.basename(image_path),
                "type": encoded_image,
                "startx": x,
                "starty": y,
                "width": box[2] - box[0],
                "height": box[3] - box[1]
            }

            response = requests.post(url, json=payload)

            if response.status_code != 201:
                print(f"Error sending block ({x},{y}): {response.status_code}")
                print(response.text)
                return
            else:
                print(f"Block ({x},{y}) ", end="")
        print(f"sent successfully.")


def screenshot_and_send(url: str, view_w: int, view_h: int, invert = False, crop: tuple = None):
    """Macht einen Web-Screenshot ohne Persistenz, sendet ihn ans Display."""
    # temporäre Datei anlegen
    with tempfile.NamedTemporaryFile(suffix='.png', delete=False) as tmp:
        tmp_path = tmp.name
        print(url)

    try:
        hti = Html2Image(output_path=os.path.dirname(tmp_path))
        # Screenshot ins tempfile
        hti.screenshot(
            url=url,
            save_as=os.path.basename(tmp_path),
            size=(view_w, view_h),
        )

        img = Image.open(tmp_path)
        if invert: img = ImageOps.invert(img)
        #encode_and_send_pil(img, os.path.basename(image_path))

        max_width, max_height = 960, 540
        width, height = img.size
        offset_x = (max_width - width) // 2
        offset_y = (max_height - height) // 2

        canvas = Image.new("L", (max_width, max_height), color=255)
        canvas.paste(img, (offset_x, offset_y))
        canvas.save("out.png")

        if crop and len(crop) == 4:
            width, height = img.size
            img = img.crop((crop[0], crop[1], width-crop[2], height-crop[3]))
            # img = img.crop(crop)
    except Exception as e:
        # Falls kein Screenshot erstellt werden kann, Textbild erzeugen
        print(f"Screenshot-Fehler: {e}")
        img = Image.new('L', (DISPLAY_WIDTH, DISPLAY_HEIGHT), color=255)
        draw = ImageDraw.Draw(img)
        text = "Screenshot konnte nicht erstellt werden"
        try:
            font = ImageFont.load_default()
        except Exception:
            font = None
        # Textgröße ermitteln
        if font:
            # neuer Ansatz mit textbbox
            bbox = draw.textbbox((0, 0), text, font=font)
            w = bbox[2] - bbox[0]
            h = bbox[3] - bbox[1]
        else:
            # fallback ohne Font
            w, h = draw.textlength(text), 10
        # Text zentrieren
        x = (DISPLAY_WIDTH - w) // 2
        y = (DISPLAY_HEIGHT - h) // 2
        draw.text((x, y), text, fill=0, font=font)
    finally:
        # temporäre Datei löschen, falls vorhanden
        if os.path.exists(tmp_path):
            os.remove(tmp_path)

    encode_and_send_pil(img, f"screenshot_{view_w}x{view_h}.png")


def send_image_auto(image_path: str, bright: float = 1.0, gamma: float = 1.0, crop: bool = False):
    """Lädt ein lokales Bild und sendet es an das Display."""
    print(image_path)
    try:
        img = Image.open(image_path)
        encode_and_send_pil(img, os.path.basename(image_path), bright, gamma, crop)
    except Exception as e:
        print(f"Fehler beim Laden des Bildes: {e}")


def clear_screen():
    payload = {
        "name": "clearScreen"
    }
    send_request('clearScreen', payload)


def turn_off():
    payload = {
        "name": "turnOff"
    }
    send_request('turnOff', payload)


def get_app_version():
    payload = {
        "name": "appVersion"
    }
    send_request('appVersion', payload)


def get_url(uri: str):
    payload = {
        "name": "url",
        "uri": uri
    }
    send_request('url', payload)


# === CLI-Interface ===
if __name__ == '__main__':
    parser = argparse.ArgumentParser(description="EPD47 Display Client")
    subparsers = parser.add_subparsers(dest='command', required=True)

    parser_image = subparsers.add_parser('setImage', help='Bild automatisch skalieren und anzeigen')
    parser_image.add_argument('image_path', help='Pfad zum Bild (z. B. test.png)')
    parser_image.add_argument('--bright', type=float, default=1.0, help='Helligkeitskorrektur (default: 1.0, optional)')
    parser_image.add_argument('--gamma',  type=float, default=1.0, help='Gammakorrektur (default: 1.0, optional)')
    parser_image.add_argument('--crop', action='store_true', default=False, help='Bild einpassen (optional)')

    parser_screenshot = subparsers.add_parser('screenshot', help='Screenshot einer Webseite nehmen & anzeigen')
    parser_screenshot.add_argument('url', help='Ziel-URL (inkl. http(s)://…)')
    parser_screenshot.add_argument('--width', type=int, default=1024, help='Viewport-Breite in Pixeln (default: 1024, optional)')
    parser_screenshot.add_argument('--height', type=int, default=800, help='Viewport-Höhe in Pixeln (default: 800, optional)')
    parser_screenshot.add_argument('--crop', nargs=4, type=int, metavar=('LEFT_OFFSET', 'TOP_OFFSET', 'RIGHT_OFFSET', 'BOTTOM_OFFSET'), help='Zuschneiden (optional)')
    parser_screenshot.add_argument('--invert', action='store_true', default=False, help='Webseite invertieren, z.B. für dunkle Webseiten (optional)')
    parser_screenshot.add_argument('--interval', type=int, default=None, help='Alle x Sekunden die Url abrufen (default: einmalig, optional)')

    subparsers.add_parser('clearScreen', help='Display löschen')
    subparsers.add_parser('turnOff', help='Display ausschalten')

    url = subparsers.add_parser('url', help='JPEG anzeigen')
    url.add_argument('uri', help='URL des JPEG-Bildes')

    subparsers.add_parser('appVersion', help='Versionsinfo abrufen')

    args = parser.parse_args()

    match args.command:
        case 'setImage': send_image_auto(args.image_path, args.bright, args.gamma, args.crop)
        case 'clearScreen': clear_screen()
        case 'turnOff': turn_off()
        case 'appVersion': get_app_version()
        case 'screenshot':
            crop_box = tuple(args.crop) if args.crop else None
            if args.interval is None:
                screenshot_and_send(args.url, args.width, args.height, args.invert, crop=crop_box)
            else:
                print(f"Rufe die URL alle {args.interval} Sekunden ab.")
                print("CTRL + C zum Abbrechen drücken.")
                try:
                    while True:
                        screenshot_and_send(args.url, args.width, args.height, args.invert, crop=crop_box)
                        time.sleep(args.interval)
                except KeyboardInterrupt:
                    print("Beende.")
                    sys.exit()

        case 'url': get_url(args.uri)
        case _: print("Fehler beim Parameter, Groß/kleinschreibung?")
