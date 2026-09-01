from PIL import Image, ImageDraw, ImageFont, ImageFilter

FONT = r"tools/header-assets/Pokemon Solid.ttf"
YELLOW = (255, 223, 0, 255)
BLUE   = (51, 95, 173, 255)
NAVY   = (11, 43, 102, 255)
TEXT = "Pokedex"
FONT_SIZE = 80
font = ImageFont.truetype(FONT, FONT_SIZE)

W, H = 800, 320
pad = 40
img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
x, y = pad, pad

def stamp(dx, dy, fill, stroke_w, stroke_fill, target):
    ImageDraw.Draw(target).text((x + dx, y + dy), TEXT, font=font, fill=fill,
                                stroke_width=stroke_w, stroke_fill=stroke_fill)

shadow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
stamp(5, 6, (7, 20, 55, 150), 9, (7, 20, 55, 150), shadow)
shadow = shadow.filter(ImageFilter.GaussianBlur(2.5))
img = Image.alpha_composite(img, shadow)

layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
stamp(0, 0, NAVY,   9, NAVY, layer)
stamp(0, 0, BLUE,   6, BLUE, layer)
stamp(0, 0, YELLOW, 2, BLUE, layer)
img = Image.alpha_composite(img, layer)

bbox = img.getbbox(); m = 6
bbox = (max(0, bbox[0]-m), max(0, bbox[1]-m), min(W, bbox[2]+m), min(H, bbox[3]+m))
img = img.crop(bbox)

w, h = img.size
px = list(img.tobytes())
with open("include/pokedex_header.h", "w") as f:
    f.write("/*\n * Embedded \"Pokedex\" title for the main-menu Pokedex pane. Generated with\n"
            " * the SAME method and font as the Boxes/Events headers (Pokemon Solid,\n"
            " * size 80): navy outer ring + blue ring + yellow fill + soft navy drop\n"
            " * shadow. RGBA (PIXELFORMAT_UNCOMPRESSED_R8G8B8A8). Generated - do not hand-edit.\n */\n")
    f.write("#ifndef POKEDEX_HEADER_H\n#define POKEDEX_HEADER_H\n\n")
    f.write(f"#define POKEDEX_HEADER_W {w}\n#define POKEDEX_HEADER_H {h}\n\n")
    f.write(f"static unsigned char pokedex_header_px[{len(px)}] = {{")
    f.write(",".join(str(b) for b in px))
    f.write("};\n\n#endif\n")
print("wrote include/pokedex_header.h", w, h)
