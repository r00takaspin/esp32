#!/usr/bin/env python3
# -*- coding: utf-8 -*-
from PIL import Image, ImageDraw, ImageFont

SCALE = 5  # 1 мм = 5 px
MM = lambda v: int(v * SCALE)

# --- шрифты ---
FONT = "/System/Library/Fonts/Supplemental/Arial.ttf"
FONT_B = "/System/Library/Fonts/Supplemental/Arial Bold.ttf"

def font(size, bold=False):
    try:
        return ImageFont.truetype(FONT_B if bold else FONT, size)
    except Exception:
        return ImageFont.load_default()

def center_text(d, cx, y, text, f, fill):
    bbox = d.textbbox((0, 0), text, font=f)
    w = bbox[2] - bbox[0]
    d.text((cx - w // 2, y), text, font=f, fill=fill)

# ============================================================
#  ПАНЕЛЬ 1 — ВИД СПЕРЕДИ
# ============================================================
def draw_front():
    W, H = 620, 400
    img = Image.new("RGB", (W, H), "white")
    d = ImageDraw.Draw(img)

    # заголовок
    center_text(d, W // 2, 14, "ВИД СПЕРЕДИ", font(26, True), "#222222")

    # корпус (100 x 60 мм)
    bx, by = 60, 60
    bw, bh = MM(100), MM(60)
    # тень
    d.rounded_rectangle([bx + 6, by + 6, bx + bw + 6, by + bh + 6], 12, fill="#CCCCCC")
    # корпус
    d.rounded_rectangle([bx, by, bx + bw, by + bh], 12, fill="#EFEFEF", outline="#555555", width=3)

    # окно LCD (70 x 20 мм), по центру
    lw, lh = MM(70), MM(20)
    lx = bx + (bw - lw) // 2
    ly = by + MM(12)
    # рамка окна
    d.rounded_rectangle([lx - 6, ly - 6, lx + lw + 6, ly + lh + 6], 6, fill="#333333")
    # экран
    d.rectangle([lx, ly, lx + lw, ly + lh], fill="#0B1E33")
    # текст на экране (2 строки)
    f_small = font(20)
    f_big = font(26, True)
    d.text((lx + 14, ly + 10), "Unix time:", font=f_small, fill="#A8D4F0")
    d.text((lx + 14, ly + 42), "1792345678", font=f_big, fill="#D8F0FF")

    # светодиод питания
    led_x, led_y = bx + 20, by + bh - 22
    d.ellipse([led_x, led_y, led_x + 10, led_y + 10], fill="#33CC33")
    d.text((led_x + 16, led_y - 4), "LED", font=font(13), fill="#555555")

    # кнопка BOOT (справа внизу)
    kx = bx + bw - 34
    ky = by + bh - 34
    d.ellipse([kx, ky, kx + 24, ky + 24], fill="#444444", outline="#222222", width=2)
    center_text(d, kx + 12, ky - 18, "BOOT", font(13, True), "#CC0000")

    # USB-порты (справа, на боковой грани)
    for i, (label, color) in enumerate([("USB прошивка", "#666666"), ("USB заряд", "#666666")]):
        uy = by + 30 + i * 34
        d.rectangle([bx + bw - 4, uy, bx + bw + 18, uy + 14], fill=color, outline="#333333")
        d.text((bx + bw + 24, uy - 2), label, font=font(14), fill="#444444")

    # подпись габаритов
    center_text(d, bx + bw // 2, by + bh + 16, "корпус ~100×60×35 мм", font(16), "#777777")

    return img

# ============================================================
#  ПАНЕЛЬ 2 — КОМПОНОВКА ВНУТРИ (вид сзади, схематично)
# ============================================================
def draw_inside():
    W, H = 620, 400
    img = Image.new("RGB", (W, H), "white")
    d = ImageDraw.Draw(img)

    center_text(d, W // 2, 14, "КОМПОНОВКА ВНУТРИ (схематично)", font(22, True), "#222222")

    bx, by = 60, 60
    bw, bh = MM(100), MM(60)
    d.rounded_rectangle([bx, by, bx + bw, by + bh], 12, fill="#FAFAFA", outline="#555555", width=3)

    # LCD (80 x 36 мм)
    lw, lh = MM(80), MM(36)
    lx = bx + (bw - lw) // 2
    ly = by + MM(4)
    d.rounded_rectangle([lx, ly, lx + lw, ly + lh], 4, fill="#2E6DA4", outline="#1B4F7A", width=2)
    center_text(d, lx + lw // 2, ly + lh // 2 - 10, "LCD 1602", font(16, True), "#FFFFFF")
    center_text(d, lx + lw // 2, ly + lh // 2 + 8, "80×36 мм", font(14), "#E0E0E0")

    # ESP32 (48 x 25 мм) — слева внизу
    ex, ey = bx + 14, by + bh - MM(28)
    ew, eh = MM(48), MM(25)
    d.rounded_rectangle([ex, ey, ex + ew, ey + eh], 4, fill="#1E8449", outline="#145A32", width=2)
    center_text(d, ex + ew // 2, ey + eh // 2 - 8, "ESP32", font(15, True), "#FFFFFF")
    center_text(d, ex + ew // 2, ey + eh // 2 + 8, "48×25", font(12), "#DFF0E5")

    # аккумулятор 18650 (20 x 70 мм) — горизонтально справа
    ax, ay = bx + bw - MM(76), by + bh - MM(24)
    aw, ah = MM(70), MM(20)
    d.rounded_rectangle([ax, ay, ax + aw, ay + ah], 10, fill="#E67E22", outline="#B85E12", width=2)
    center_text(d, ax + aw // 2, ay + ah // 2 - 8, "18650", font(15, True), "#FFFFFF")
    center_text(d, ax + aw // 2, ay + ah // 2 + 8, "3.7 В", font(12), "#FDEBD0")

    # TP4056 (26 x 17 мм)
    tx, ty = bx + 16, by + MM(8)
    tw, th = MM(26), MM(17)
    d.rounded_rectangle([tx, ty, tx + tw, ty + th], 3, fill="#8E44AD", outline="#6C3483", width=2)
    center_text(d, tx + tw // 2, ty + th // 2 - 7, "TP4056", font(11, True), "#FFFFFF")
    center_text(d, tx + tw // 2, ty + th // 2 + 7, "заряд", font(10), "#E8DAEF")

    # MT3608 (36 x 17 мм)
    mx = bx + 16 + MM(26) + 10
    my = by + MM(8)
    mw, mh = MM(36), MM(17)
    d.rounded_rectangle([mx, my, mx + mw, my + mh], 3, fill="#C0392B", outline="#922B21", width=2)
    center_text(d, mx + mw // 2, my + mh // 2 - 7, "MT3608", font(11, True), "#FFFFFF")
    center_text(d, mx + mw // 2, my + mh // 2 + 7, "boost 5В", font(10), "#FADBD8")

    # легенда
    d.text((bx + 10, by + bh + 16), "Все элементы — за LCD, крепятся на скотч/стойки", font=font(13), fill="#777777")

    return img

# ============================================================
#  СБОРКА И СОХРАНЕНИЕ
# ============================================================
front = draw_front()
inside = draw_inside()

gap = 20
W = front.width + gap + inside.width
H = max(front.height, inside.height)
canvas = Image.new("RGB", (W, H), "white")
canvas.paste(front, (0, 0))
canvas.paste(inside, (front.width + gap, 0))

out = "/Users/voldemar/pet-projects/esp32/device_sketch.png"
canvas.save(out)
print("saved", out, canvas.size)
