from PIL import Image, ImageDraw, ImageFont, ImageFilter

SP = r"C:\Users\Jeff\AppData\Local\Temp\claude\C--Users-Jeff-Documents-Projects-PKTrade\4b1d424a-624c-4f3d-883e-417de5301984\scratchpad"

# Exact recipe from gen_boxes.py (the Boxes header) — Pokemon Solid font,
# navy outer ring + blue ring + yellow fill + soft navy shadow.
YELLOW = (255, 223, 0, 255)
BLUE   = (51, 95, 173, 255)
NAVY   = (11, 43, 102, 255)

TEXT = "Events"
FONT_SIZE = 80
font = ImageFont.truetype(SP + r"\Pokemon Solid.ttf", FONT_SIZE)

W, H = 800, 320
pad = 40
img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
x, y = pad, pad

def stamp(dx, dy, fill, stroke_w, stroke_fill, target):
    ImageDraw.Draw(target).text((x + dx, y + dy), TEXT, font=font, fill=fill,
                                stroke_width=stroke_w, stroke_fill=stroke_fill)

# 1) Soft drop shadow
shadow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
stamp(5, 6, (7, 20, 55, 150), 9, (7, 20, 55, 150), shadow)
shadow = shadow.filter(ImageFilter.GaussianBlur(2.5))
img = Image.alpha_composite(img, shadow)

# 2) Concentric outlines: navy (outer) -> blue (mid) -> yellow (fill)
layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
stamp(0, 0, NAVY,   9, NAVY, layer)
stamp(0, 0, BLUE,   6, BLUE, layer)
stamp(0, 0, YELLOW, 2, BLUE, layer)
img = Image.alpha_composite(img, layer)

bbox = img.getbbox(); m = 6
bbox = (max(0, bbox[0]-m), max(0, bbox[1]-m), min(W, bbox[2]+m), min(H, bbox[3]+m))
img = img.crop(bbox)
img.save(SP + r"\events_solid.png")
print("events_solid.png size:", img.size)

# comparison vs the real Boxes header
bx = Image.frombytes("RGBA",(281,118),open(SP+r"\h_boxes.bin","rb").read())
mw=max(img.width,bx.width)+10; mh=img.height+bx.height+20
mo=Image.new("RGB",(mw,mh),(72,72,80))
for i,im in enumerate([bx,img]):
    b=Image.new("RGBA",im.size,(72,72,80,255)); b.alpha_composite(im)
    mo.paste(b.convert("RGB"),(5, 5 if i==0 else bx.height+15))
mo.save(SP+r"\solid_compare.png")
