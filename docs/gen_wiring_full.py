from pathlib import Path
from html import escape

W, H = 1800, 1220
P = []
FONT = "Helvetica Neue, Arial, sans-serif"
MONO = "Menlo, Consolas, monospace"


def add(s):
    P.append(s)


def text(x, y, s, size=14, fill="#0f172a", weight=400, anchor="start",
         family=FONT, opacity=None, letter=None):
    a = f' x="{x}" y="{y}" font-family="{family}" font-size="{size}" fill="{fill}"'
    if weight != 400:
        a += f' font-weight="{weight}"'
    if anchor != "start":
        a += f' text-anchor="{anchor}"'
    if opacity is not None:
        a += f' opacity="{opacity}"'
    if letter is not None:
        a += f' letter-spacing="{letter}"'
    add(f"<text{a}>{escape(s)}</text>")


def rect(x, y, w, h, fill, rx=0, stroke=None, sw=1, opacity=None, dash=None):
    a = f' x="{x}" y="{y}" width="{w}" height="{h}" fill="{fill}"'
    if rx:
        a += f' rx="{rx}"'
    if stroke:
        a += f' stroke="{stroke}" stroke-width="{sw}"'
    if opacity is not None:
        a += f' opacity="{opacity}"'
    if dash:
        a += f' stroke-dasharray="{dash}"'
    add(f"<rect{a}/>")


def line(x1, y1, x2, y2, stroke, sw=3, dash=None, opacity=None, cap="round"):
    a = (f' x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{stroke}" '
         f'stroke-width="{sw}" stroke-linecap="{cap}"')
    if dash:
        a += f' stroke-dasharray="{dash}"'
    if opacity is not None:
        a += f' opacity="{opacity}"'
    add(f"<line{a}/>")


def path(d, stroke, sw=4, fill="none", dash=None, opacity=None):
    a = (f' d="{d}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}" '
         f'stroke-linecap="round" stroke-linejoin="round"')
    if dash:
        a += f' stroke-dasharray="{dash}"'
    if opacity is not None:
        a += f' opacity="{opacity}"'
    add(f"<path{a}/>")


def circle(cx, cy, r, fill, stroke=None, sw=1):
    a = f' cx="{cx}" cy="{cy}" r="{r}" fill="{fill}"'
    if stroke:
        a += f' stroke="{stroke}" stroke-width="{sw}"'
    add(f"<circle{a}/>")


def wire(points, color, sw=4, dash=None):
    d = f"M {points[0][0]} {points[0][1]}"
    for x, y in points[1:]:
        d += f" L {x} {y}"
    path(d, color, sw, dash=dash)


def tag_right(x_right, y, label, color, bg):
    w = max(64, 18 + len(label) * 8.4)
    x = x_right - w
    rect(x, y - 13, w, 26, bg, rx=7, stroke=color, sw=1.5)
    text(x_right - 10, y + 5, label, size=13, fill=color, weight=700,
         anchor="end", family=MONO)


def tag_left(x_left, y, label, color, bg):
    w = max(64, 18 + len(label) * 8.4)
    rect(x_left, y - 13, w, 26, bg, rx=7, stroke=color, sw=1.5)
    text(x_left + 10, y + 5, label, size=13, fill=color, weight=700,
         family=MONO)


def pin_right(x, y, label):
    rect(x - 30, y - 9, 30, 18, "url(#gold)", rx=3, stroke="#b45309", sw=1)
    text(x - 40, y + 5, label, size=13, fill="#e2e8f0", weight=700,
         anchor="end", family=MONO)


def pin_left(x, y, label):
    rect(x, y - 9, 30, 18, "url(#gold)", rx=3, stroke="#b45309", sw=1)
    text(x + 40, y + 5, label, size=13, fill="#eff6ff", weight=700,
         family=MONO)


# background
add(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" '
    f'viewBox="0 0 {W} {H}">')
add('<defs>')
add('<pattern id="dots" width="24" height="24" patternUnits="userSpaceOnUse">'
    '<circle cx="1" cy="1" r="1" fill="#dbe3ee"/></pattern>')
add('<linearGradient id="pcb" x1="0" y1="0" x2="1" y2="1">'
    '<stop offset="0" stop-color="#111c31"/><stop offset="1" stop-color="#070d18"/>'
    '</linearGradient>')
add('<linearGradient id="metal" x1="0" y1="0" x2="0" y2="1">'
    '<stop offset="0" stop-color="#d7dee9"/><stop offset="1" stop-color="#8b98ab"/>'
    '</linearGradient>')
add('<linearGradient id="gold" x1="0" y1="0" x2="0" y2="1">'
    '<stop offset="0" stop-color="#fde68a"/><stop offset="1" stop-color="#d97706"/>'
    '</linearGradient>')
add('<linearGradient id="blue" x1="0" y1="0" x2="1" y2="1">'
    '<stop offset="0" stop-color="#1e40af"/><stop offset="1" stop-color="#1d4ed8"/>'
    '</linearGradient>')
add('<linearGradient id="teal" x1="0" y1="0" x2="1" y2="1">'
    '<stop offset="0" stop-color="#0f766e"/><stop offset="1" stop-color="#0d9488"/>'
    '</linearGradient>')
add('<linearGradient id="knob" x1="0" y1="0" x2="0" y2="1">'
    '<stop offset="0" stop-color="#e2e8f0"/><stop offset="1" stop-color="#64748b"/>'
    '</linearGradient>')
add('</defs>')
add(f'<rect width="{W}" height="{H}" fill="#f8fafc"/>')
add(f'<rect width="{W}" height="{H}" fill="url(#dots)"/>')

# header
rect(80, 42, 142, 30, "#dbeafe", rx=15)
text(151, 62, "ESP32 · TOTP", size=13, fill="#1d4ed8", weight=700,
     anchor="middle", letter="0.5")
text(80, 118, "Полная схема подключения", size=40, fill="#0b1220", weight=800)
text(80, 150, "LCD 1602 · DS3231 · KY-040 · питание", size=18, fill="#475569")
text(1720, 150, "одинаковые теги = соединены", size=12.5, fill="#64748b",
     anchor="end")
text(1720, 172, "все периферийные модули на одной странице", size=12.5,
     fill="#94a3b8", anchor="end")

# ESP32 board
rect(80, 200, 560, 640, "url(#pcb)", rx=24, stroke="#334155", sw=1.5)
rect(330, 182, 96, 34, "#cbd5e1", rx=5, stroke="#64748b", sw=1.2)
text(378, 204, "USB", size=11, fill="#334155", anchor="middle", weight=700,
     family=MONO)
text(110, 255, "ESP32", size=32, fill="#ffffff", weight=800)
text(110, 280, "NodeMCU-32S", size=14, fill="#94a3b8")

rect(140, 320, 320, 150, "url(#metal)", rx=8, stroke="#64748b", sw=1.2)
for i in range(9):
    x = 158 + i * 33
    line(x, 318, x, 302, "#b45309", 2.4)
text(300, 382, "ESP32-WROOM-32", size=17, fill="#1e293b", weight=800,
     anchor="middle", family=MONO)
text(300, 405, "Wi-Fi · BT · 240 МГц", size=11, fill="#475569",
     anchor="middle")
circle(440, 445, 3, "#94a3b8")
circle(452, 445, 3, "#94a3b8")

pin_rows = [("3V3", 310), ("GND", 360), ("P21", 440), ("P22", 490),
            ("P25", 560), ("P32", 630), ("P33", 680), ("5V", 750)]
for name, y in pin_rows:
    pin_right(640, y, name)

text(110, 690, "BOOT / GPIO0 — только для прошивки;", size=12, fill="#94a3b8")
text(110, 710, "в работе не используется.", size=12, fill="#94a3b8")
rect(105, 762, 505, 76, "#0f172a", rx=12, stroke="#1e3a8a", sw=1.2)
text(125, 787, "I²C на ESP32", size=13, fill="#93c5fd", weight=700)
text(125, 810, "P21 = SDA   ·   P22 = SCL   ·   P25 = SW", size=12.5,
     fill="#e2e8f0", family=MONO)
text(125, 830, "P32 = CLK   ·   P33 = DT", size=12.5, fill="#e2e8f0",
     family=MONO)

# LCD module
rect(1120, 190, 560, 230, "url(#blue)", rx=18, stroke="#1e40af", sw=1.4)
text(1170, 235, "LCD 1602", size=26, fill="#ffffff", weight=800)
text(1170, 258, "I²C backpack, адрес 0x27", size=12.5, fill="#bfdbfe")
for name, y in [("VCC", 270), ("GND", 310), ("SDA", 350), ("SCL", 390)]:
    pin_left(1120, y, name)
rect(1390, 245, 240, 110, "#0b1220", rx=8, stroke="#93c5fd", sw=1.5)
text(1400, 272, "TOTP", size=12, fill="#93c5fd", family=MONO)
text(1400, 305, "123 456", size=25, fill="#f8fafc", weight=700, family=MONO)
text(1400, 335, "████████", size=12, fill="#38bdf8", family=MONO)
text(1390, 378, "VCC — только 5 В", size=11.5, fill="#dbeafe")
text(1390, 398, "подсветка через I²C-плату", size=11.5, fill="#bfdbfe")

# DS3231 module
rect(1120, 470, 560, 200, "url(#teal)", rx=18, stroke="#0f766e", sw=1.4)
text(1170, 512, "DS3231 RTC", size=26, fill="#ffffff", weight=800)
text(1170, 535, "часы реального времени, адрес 0x68", size=12.5, fill="#ccfbf1")
for name, y in [("VCC", 550), ("GND", 585), ("SDA", 620), ("SCL", 655)]:
    pin_left(1120, y, name)
# battery
rect(1430, 520, 130, 62, "#0b1220", rx=9, stroke="#5eead4", sw=1.4)
rect(1450, 536, 72, 30, "#f8fafc", rx=4)
rect(1522, 544, 8, 14, "#f8fafc", rx=2)
text(1490, 557, "CR2032", size=10.5, fill="#0f172a", weight=700,
     anchor="middle", family=MONO)
text(1430, 606, "VCC — только 3.3 В", size=11.5, fill="#ccfbf1")
text(1430, 626, "SDA/SCL — параллельно LCD", size=11.5, fill="#99f6e4")

# KY-040 module
rect(1120, 730, 560, 270, "url(#blue)", rx=18, stroke="#1e40af", sw=1.4)
text(1170, 768, "KY-040", size=26, fill="#ffffff", weight=800)
text(1170, 791, "энкодер + кнопка", size=12.5, fill="#bfdbfe")
for name, y in [("GND", 820), ("+", 855), ("SW", 890), ("DT", 925), ("CLK", 960)]:
    pin_left(1120, y, name)
circle(1500, 875, 48, "#020617", stroke="#0f172a", sw=3)
circle(1500, 875, 40, "url(#knob)", stroke="#64748b", sw=1.2)
path("M1458 852 A40 40 0 0 1 1498 824", "#94a3b8", 2.4, opacity=0.55)
line(1500, 875, 1462, 840, "#f8fafc", 6)
text(1500, 945, "поворот / нажатие", size=10.5, fill="#dbeafe",
     anchor="middle")
text(1170, 982, "только 3.3 В; SW замыкает на GND", size=11.5, fill="#bfdbfe")

# I2C bus
SDA = "#2563eb"
SCL = "#0ea5e9"
line(900, 350, 900, 655, SDA, 4)
line(940, 390, 940, 655, SCL, 4)
# ESP32 to bus
wire([(640, 440), (900, 440)], SDA, 4)
wire([(640, 490), (940, 490)], SCL, 4)
# LCD to bus
wire([(1120, 350), (900, 350)], SDA, 4)
wire([(1120, 390), (940, 390)], SCL, 4)
# DS3231 to bus
wire([(1120, 620), (900, 620)], SDA, 4)
wire([(1120, 655), (940, 655)], SCL, 4)
# junctions
for x, y, c in [(900, 440, SDA), (940, 490, SCL), (900, 350, SDA),
                (940, 390, SCL), (900, 620, SDA), (940, 655, SCL)]:
    circle(x, y, 5.5, c, "#ffffff", 1.5)
text(900, 330, "SDA", size=12, fill=SDA, weight=700, anchor="middle",
     family=MONO)
text(940, 330, "SCL", size=12, fill=SCL, weight=700, anchor="middle",
     family=MONO)
text(900, 690, "общая шина I²C", size=11.5, fill="#475569", anchor="middle")

# net tags: ESP32 side
wire([(640, 310), (690, 310)], "#ea580c", 4)
tag_left(690, 310, "+3V3", "#ea580c", "#ffedd5")
wire([(640, 360), (690, 360)], "#334155", 4)
tag_left(690, 360, "GND", "#334155", "#e2e8f0")
wire([(640, 750), (690, 750)], "#dc2626", 4)
tag_left(690, 750, "+5V", "#dc2626", "#fee2e2")

# net tags: LCD
wire([(1120, 270), (1070, 270)], "#dc2626", 4)
tag_right(1070, 270, "+5V", "#dc2626", "#fee2e2")
wire([(1120, 310), (1070, 310)], "#334155", 4)
tag_right(1070, 310, "GND", "#334155", "#e2e8f0")

# net tags: DS3231
wire([(1120, 550), (1070, 550)], "#ea580c", 4)
tag_right(1070, 550, "+3V3", "#ea580c", "#ffedd5")
wire([(1120, 585), (1070, 585)], "#334155", 4)
tag_right(1070, 585, "GND", "#334155", "#e2e8f0")

# net tags: KY-040
wire([(1120, 820), (1070, 820)], "#334155", 4)
tag_right(1070, 820, "GND", "#334155", "#e2e8f0")
wire([(1120, 855), (1070, 855)], "#ea580c", 4)
tag_right(1070, 855, "+3V3", "#ea580c", "#ffedd5")

# encoder signals
wire([(640, 560), (850, 560), (850, 890), (1120, 890)], "#16a34a", 4)
wire([(640, 680), (820, 680), (820, 925), (1120, 925)], "#7c3aed", 4)
wire([(640, 630), (790, 630), (790, 960), (1120, 960)], "#d97706", 4)
text(870, 880, "SW", size=12, fill="#16a34a", weight=700, family=MONO)
text(840, 915, "DT", size=12, fill="#7c3aed", weight=700, family=MONO)
text(810, 950, "CLK", size=12, fill="#d97706", weight=700, family=MONO)

# power card
rect(80, 900, 560, 190, "#ffffff", rx=16, stroke="#e2e8f0", sw=1.2)
text(110, 940, "Питание", size=18, fill="#0b1220", weight=800)
for i, s in enumerate([
    "USB 5 В → VIN — прошивка и отладка",
    "Battery Shield 5 В → VIN — автономно",
    "GND — общий с ESP32",
    "18650 / Li-ion, 3.7–4.2 В",
]):
    text(110, 975 + i * 27, "• " + s, size=13.5, fill="#334155")
# power tags inside card
wire([(500, 1060), (470, 1060)], "#dc2626", 3)
tag_left(470, 1060, "+5V", "#dc2626", "#fee2e2")
wire([(620, 1060), (590, 1060)], "#334155", 3)
tag_left(590, 1060, "GND", "#334155", "#e2e8f0")
text(110, 1075, "те же теги +5V / GND — у ESP32", size=11.5, fill="#64748b")

# warning card
rect(680, 1020, 1040, 160, "#fff7ed", rx=16, stroke="#fdba74", sw=1.4)
text(710, 1054, "Важно", size=18, fill="#9a3412", weight=800)
warn = [
    "LCD питается от 5 В; DS3231 и KY-040 — только 3.3 В.",
    "I²C: если на LCD-модуле подтяжки SDA/SCL идут к 5 В — нужен level shifter BSS138",
    "(HV = 5 В, LV = 3.3 В) или удалить подтяжки на LCD. ESP32 не 5V-tolerant.",
    "BOOT / GPIO0 — только для прошивки; в работе не используется.",
]
for i, s in enumerate(warn):
    text(710, 1082 + i * 24, "• " + s, size=12.5, fill="#7c2d12")

text(80, 1198, "Порядок выводов на разных платах KY-040 отличается — ориентируйся на подписи GND, +, SW, DT, CLK.",
     size=12, fill="#64748b")

add("</svg>")

out = Path(__file__).with_name("wiring-full.svg")
out.write_text("\n".join(P), encoding="utf-8")
print(out)
