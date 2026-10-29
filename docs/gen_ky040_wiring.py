#!/usr/bin/env python3
"""Генерирует docs/ky040-wiring.svg — схему подключения KY-040 к ESP32 TOTP."""

from pathlib import Path

W, H = 1600, 1020
FONT = "Helvetica Neue, Arial, sans-serif"
MONO = "Menlo, Consolas, monospace"

parts = []


def esc(s: str) -> str:
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def text(x, y, s, size=16, fill="#0f172a", weight="400", anchor="start",
         family=FONT, opacity=None, spacing=None):
    attrs = (
        f'x="{x}" y="{y}" font-family="{family}" font-size="{size}" '
        f'fill="{fill}" font-weight="{weight}" text-anchor="{anchor}"'
    )
    if opacity is not None:
        attrs += f' opacity="{opacity}"'
    if spacing is not None:
        attrs += f' letter-spacing="{spacing}"'
    parts.append(f"<text {attrs}>{esc(s)}</text>")


def rect(x, y, w, h, fill, rx=0, stroke=None, sw=1, opacity=None,
         dash=None, filter_id=None):
    attrs = f'x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}"'
    if stroke:
        attrs += f' stroke="{stroke}" stroke-width="{sw}"'
    if opacity is not None:
        attrs += f' opacity="{opacity}"'
    if dash:
        attrs += f' stroke-dasharray="{dash}"'
    if filter_id:
        attrs += f' filter="url(#{filter_id})"'
    parts.append(f"<rect {attrs}/>")


def circle(cx, cy, r, fill, stroke=None, sw=1, opacity=None):
    attrs = f'cx="{cx}" cy="{cy}" r="{r}" fill="{fill}"'
    if stroke:
        attrs += f' stroke="{stroke}" stroke-width="{sw}"'
    if opacity is not None:
        attrs += f' opacity="{opacity}"'
    parts.append(f"<circle {attrs}/>")


def line(x1, y1, x2, y2, stroke, sw=1, cap="round", opacity=None, dash=None):
    attrs = (
        f'x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" '
        f'stroke="{stroke}" stroke-width="{sw}" stroke-linecap="{cap}"'
    )
    if opacity is not None:
        attrs += f' opacity="{opacity}"'
    if dash:
        attrs += f' stroke-dasharray="{dash}"'
    parts.append(f"<line {attrs}/>")


def path(d, stroke, sw=1, fill="none", cap="round", join="round",
         opacity=None, dash=None):
    attrs = (
        f'd="{d}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}" '
        f'stroke-linecap="{cap}" stroke-linejoin="{join}"'
    )
    if opacity is not None:
        attrs += f' opacity="{opacity}"'
    if dash:
        attrs += f' stroke-dasharray="{dash}"'
    parts.append(f"<path {attrs}/>")


def pill(x, y, w, h, fill, stroke, text_color, label, size=14):
    rect(x, y, w, h, fill, rx=h / 2, stroke=stroke, sw=1.5)
    text(x + w / 2, y + h / 2 + size * 0.36, label, size=size,
         fill=text_color, weight="700", anchor="middle")


def card(x, y, w, h, accent, number, title, lines):
    rect(x, y + 7, w, h, "#0f172a", rx=16, opacity=0.10, filter_id="blur")
    rect(x, y, w, h, "#ffffff", rx=16, stroke="#e2e8f0", sw=1.2)
    circle(x + 30, y + 31, 14, accent)
    text(x + 30, y + 36, str(number), size=13, fill="#ffffff",
         weight="700", anchor="middle")
    text(x + 54, y + 37, title, size=16, fill=accent, weight="700")
    for i, ln in enumerate(lines):
        text(x + 25, y + 68 + i * 22, ln, size=13.2, fill="#334155")


# ---------- defs ----------
parts.append(f'''<defs>
  <linearGradient id="bg" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#ffffff"/>
    <stop offset="1" stop-color="#eef2f7"/>
  </linearGradient>
  <linearGradient id="pcb" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#1e293b"/>
    <stop offset="1" stop-color="#0b1220"/>
  </linearGradient>
  <linearGradient id="metal" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#f1f5f9"/>
    <stop offset="0.45" stop-color="#cbd5e1"/>
    <stop offset="1" stop-color="#94a3b8"/>
  </linearGradient>
  <linearGradient id="blue" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#2563eb"/>
    <stop offset="1" stop-color="#1e3a8a"/>
  </linearGradient>
  <linearGradient id="knob" x1="0" y1="0" x2="1" y2="1">
    <stop offset="0" stop-color="#475569"/>
    <stop offset="0.55" stop-color="#1e293b"/>
    <stop offset="1" stop-color="#020617"/>
  </linearGradient>
  <linearGradient id="gold" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#fde68a"/>
    <stop offset="1" stop-color="#f59e0b"/>
  </linearGradient>
  <filter id="blur" x="-30%" y="-30%" width="160%" height="160%">
    <feGaussianBlur stdDeviation="12"/>
  </filter>
  <pattern id="dots" width="32" height="32" patternUnits="userSpaceOnUse">
    <circle cx="1" cy="1" r="1.15" fill="#cbd5e1" opacity="0.52"/>
  </pattern>
</defs>''')

# ---------- background ----------
rect(0, 0, W, H, "url(#bg)")
rect(0, 0, W, H, "url(#dots)")

# ---------- header ----------
rect(100, 46, 168, 30, "#dbeafe", rx=15)
text(184, 66, "ESP32 · TOTP", size=13, fill="#1d4ed8", weight="700",
     anchor="middle", spacing=0.6)
text(100, 122, "Подключение KY-040", size=43, fill="#0f172a", weight="700")
text(100, 158,
     "Энкодер с кнопкой к автономному TOTP-генератору: "
     "поворот — выбор аккаунта, нажатие — пробуждение",
     size=18.5, fill="#475569")
text(1500, 117, "схема соединений", size=13, fill="#64748b",
     anchor="end", spacing=0.4)

# ---------- ESP32 board ----------
rect(100, 228, 540, 550, "#0f172a", rx=24, opacity=0.16, filter_id="blur")
rect(100, 220, 540, 550, "url(#pcb)", rx=24, stroke="#334155", sw=1.5)

# USB-разъём
rect(330, 202, 96, 34, "#cbd5e1", rx=5, stroke="#64748b", sw=1.2)
rect(342, 210, 72, 12, "#475569", rx=2)
text(378, 196, "USB", size=10.5, fill="#64748b", anchor="middle",
     weight="700", spacing=1.2)

text(140, 280, "ESP32", size=31, fill="#f8fafc", weight="700")
text(140, 306, "NodeMCU-32S", size=15, fill="#94a3b8")

# экран ESP32-WROOM
rect(175, 340, 300, 170, "url(#metal)", rx=8, stroke="#64748b", sw=1.2)
path("M195 362 h30 v-9 h16 v9 h16 v-9 h16 v9 h16 v-9 h16 v9 h16 v-9 h16 v9 h16 v-9 h16 v9 h16",
     "#b45309", sw=2.4)
text(325, 435, "ESP32-WROOM-32", size=15.5, fill="#334155", weight="700",
     anchor="middle")
text(325, 456, "Wi-Fi · BT · 240 МГц", size=10.5, fill="#64748b",
     anchor="middle", spacing=0.4)
circle(455, 486, 4, "#94a3b8", stroke="#64748b", sw=1)
circle(445, 486, 4, "#94a3b8", stroke="#64748b", sw=1)

# BOOT note
text(140, 570, "BOOT / GPIO0 — только для прошивки", size=13, fill="#cbd5e1")
text(140, 590, "в прошивке больше не используется", size=12, fill="#94a3b8")

# I2C note
rect(135, 635, 470, 100, "#eff6ff", rx=12)
rect(135, 635, 470, 100, "none", rx=12, stroke="#93c5fd", sw=1.3, dash="7 6")
text(155, 665, "I²C — без изменений", size=15, fill="#1d4ed8", weight="700")
text(155, 692, "P21 SDA · P22 SCL  →  LCD 1602 + DS3231", size=12.5,
     fill="#1e3a8a", family=MONO)
text(155, 714, "Энкодер эти пины не занимает", size=12, fill="#475569")

text(140, 756, "Только задействованные пины — ищи подписи на плате", size=11.5,
     fill="#94a3b8")

# ---------- header pins ----------
rect(596, 298, 44, 286, "#0b1220", rx=7, stroke="#334155", sw=1.2)
PIN_Y = {"GND": 330, "3V3": 375, "P25": 420, "P33": 465, "P32": 510}
for name, y in PIN_Y.items():
    rect(602, y - 9, 32, 18, "url(#gold)", rx=3, stroke="#b45309", sw=1)
    text(586, y + 6, name, size=15.5, fill="#e2e8f0", weight="700",
         anchor="end", family=MONO)

# ---------- KY-040 module ----------
rect(1040, 258, 420, 330, "#0f172a", rx=18, opacity=0.16, filter_id="blur")
rect(1040, 250, 420, 330, "url(#blue)", rx=18, stroke="#1e40af", sw=1.4)

# mounting holes
for hx, hy in ((1060, 270), (1440, 270), (1060, 560), (1440, 560)):
    circle(hx, hy, 7, "#172554", stroke="#60a5fa", sw=1.2)
    circle(hx, hy, 2.5, "#93c5fd")

text(1082, 282, "KY-040", size=21, fill="#eff6ff", weight="700")
text(1082, 299, "ROTARY ENCODER", size=9.5, fill="#93c5fd", spacing=2.4)

# knob
circle(1290, 410, 72, "#020617", stroke="#0f172a", sw=3)
circle(1290, 410, 60, "url(#knob)", stroke="#64748b", sw=1.2)
path("M1244 385 A60 60 0 0 1 1288 351", "#94a3b8", sw=3, opacity=0.55)
line(1290, 410, 1249, 367, "#f8fafc", sw=7)
circle(1290, 410, 5, "#cbd5e1")
circle(1290, 410, 2, "#475569")
text(1290, 512, "поворот / нажатие", size=12.5, fill="#dbeafe",
     anchor="middle")

# module pins + labels
KY_PINS = [
    ("GND", "земля", 330, "#0f172a"),
    ("+", "3.3 В", 375, "#ef4444"),
    ("SW", "кнопка", 420, "#10b981"),
    ("DT", "канал B", 465, "#3b82f6"),
    ("CLK", "канал A", 510, "#f59e0b"),
]
for name, desc, y, color in KY_PINS:
    rect(1000, y - 9, 40, 18, "url(#gold)", rx=3, stroke="#b45309", sw=1)
    circle(1007, y, 2.2, "#78350f")
    text(1058, y + 6, name, size=17.5, fill="#eff6ff", weight="700",
         family=MONO)
    text(1112, y + 5, desc, size=11.5, fill="#bfdbfe")

text(1082, 548, "A / B / SW подтянуты к + через 10 кОм", size=11.5,
     fill="#bfdbfe")
text(1082, 566, "внешние резисторы не нужны", size=11.5, fill="#93c5fd")

# ---------- wires ----------
WIRES = [
    ("GND", "GND", 330, "#0f172a"),
    ("3V3", "+", 375, "#ef4444"),
    ("P25", "SW", 420, "#10b981"),
    ("P33", "DT", 465, "#3b82f6"),
    ("P32", "CLK", 510, "#f59e0b"),
]
for esp, ky, y, color in WIRES:
    line(634, y, 1000, y, "#ffffff", sw=10)
    line(634, y, 1000, y, color, sw=5)
    circle(634, y, 5, color, stroke="#ffffff", sw=2)
    circle(1000, y, 5, color, stroke="#ffffff", sw=2)

for esp, ky, y, color in WIRES:
    pill(752, y - 36, 178, 27, "#ffffff", color, color, f"{esp}  ↔  {ky}",
         size=13.5)

# ---------- bottom cards ----------
card(100, 812, 430, 150, "#dc2626", 1, "Питание", [
    "• KY-040 — только от 3.3 В, не от 5 В.",
    "• Иначе подтяжки модуля подадут 5 В",
    "  на GPIO32 / 33 / 25 — это опасно.",
    "• 10 кОм уже на модуле, внешние не нужны.",
])
card(550, 812, 430, 150, "#2563eb", 2, "Управление", [
    "• Поворот — предыдущий / следующий аккаунт.",
    "• Нажатие — пробуждение; наяву — следующий.",
    "• 60 секунд бездействия — экран гаснет.",
    "• Будит только кнопка SW (GPIO25).",
])
card(1000, 812, 500, 150, "#0d9488", 3, "Шина I²C — без изменений", [
    "• LCD 1602: SDA→P21, SCL→P22, VCC→5 В.",
    "• DS3231: SDA→P21, SCL→P22, VCC→3V3.",
    "• Энкодер занимает P32, P33, P25 —",
    "  шину I²C и RTC не трогает.",
])

text(100, 992,
     "Порядок выводов на разных платах KY-040 отличается — "
     "ориентируйся на подписи GND, +, SW, DT, CLK.",
     size=12.5, fill="#64748b")

svg = (
    f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" '
    f'viewBox="0 0 {W} {H}">\n' + "\n".join(parts) + "\n</svg>\n"
)
out = Path(__file__).with_name("ky040-wiring.svg")
out.write_text(svg, encoding="utf-8")
print(out)
