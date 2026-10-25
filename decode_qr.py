#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Декодирование QR-кода из изображения и вывод его содержимого.
Зависимость: opencv-python  ->  pip3 install opencv-python

Использование:
    python3 decode_qr.py <путь_к_изображению>

Вывод идёт ТОЛЬКО в твой терминал (stdout). Ничего никуда не отправляется.
"""
import sys

def main():
    if len(sys.argv) < 2:
        print("Использование: python3 decode_qr.py <путь_к_изображению>")
        sys.exit(1)

    path = sys.argv[1]

    try:
        import cv2
    except ImportError:
        print("Не установлен opencv-python. Установи командой:")
        print("    pip3 install opencv-python")
        sys.exit(2)

    img = cv2.imread(path)
    if img is None:
        print(f"Не удалось открыть изображение: {path}")
        sys.exit(3)

    # 1) прямое декодирование
    data, _, _ = cv2.QRCodeDetector().detectAndDecode(img)
    if data:
        print(data)
        return

    # 2) повтор с масштабированием (для мелких/размытых QR)
    for scale in (2, 3, 4):
        big = cv2.resize(img, None, fx=scale, fy=scale, interpolation=cv2.INTER_CUBIC)
        data, _, _ = cv2.QRCodeDetector().detectAndDecode(big)
        if data:
            print(data)
            return

    print("QR-код не найден или не распознан.")

if __name__ == "__main__":
    main()
