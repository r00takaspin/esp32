#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Распаковка otpauth-migration:// (экспорт Google Authenticator).

Зависимостей нет (только стандартная библиотека Python).

Использование:
    python3 unpack_migration.py "otpauth-migration://offline?data=..."
    или (чтобы URL не светился в истории шелла):
    cat migration.txt | python3 unpack_migration.py

Вывод — ТОЛЬКО в твой терминал (stdout). Ничего никуда не отправляется.
"""
import sys
import base64
import urllib.parse

ALGORITHMS = {0: "SHA1", 1: "SHA1", 2: "SHA256", 3: "SHA512", 4: "MD5"}


def parse_varint(buf, pos):
    result = 0
    shift = 0
    while pos < len(buf):
        b = buf[pos]
        pos += 1
        result |= (b & 0x7F) << shift
        if not (b & 0x80):
            return result, pos
        shift += 7
    return result, pos


def parse_otp_parameters(buf):
    """Разбор одного сообщения OtpParameters (вложенного)."""
    secret = b""
    name = ""
    issuer = ""
    algorithm = 1
    otp_type = 2
    counter = 0
    pos = 0
    while pos < len(buf):
        tag, pos = parse_varint(buf, pos)
        field = tag >> 3
        wire = tag & 7
        if wire == 0:  # varint
            val, pos = parse_varint(buf, pos)
            if field == 4:
                algorithm = val
            elif field == 5:
                otp_type = val
            elif field == 6:
                counter = val
        elif wire == 2:  # length-delimited
            length, pos = parse_varint(buf, pos)
            payload = buf[pos:pos + length]
            pos += length
            if field == 1:
                secret = payload
            elif field == 2:
                name = payload.decode("utf-8", "replace")
            elif field == 3:
                issuer = payload.decode("utf-8", "replace")
        else:
            break
    return secret, name, issuer, algorithm, otp_type, counter


def parse_migration(buf):
    """Разбор MigrationPayload -> список аккаунтов."""
    accounts = []
    pos = 0
    while pos < len(buf):
        tag, pos = parse_varint(buf, pos)
        field = tag >> 3
        wire = tag & 7
        if field == 1 and wire == 2:  # otp_parameters (вложенное сообщение)
            length, pos = parse_varint(buf, pos)
            accounts.append(parse_otp_parameters(buf[pos:pos + length]))
            pos += length
        elif wire == 0:
            _, pos = parse_varint(buf, pos)
        elif wire == 2:
            length, pos = parse_varint(buf, pos)
            pos += length
        elif wire == 5:
            pos += 4
        elif wire == 1:
            pos += 8
        else:
            break
    return accounts


def main():
    if len(sys.argv) >= 2:
        raw_input = sys.argv[1]
    else:
        raw_input = sys.stdin.read().strip()

    # отрезаем префикс "QR-Code:" от zbarimg, если он есть
    if raw_input.upper().startswith("QR-CODE:"):
        raw_input = raw_input.split(":", 1)[1].strip()

    if "otpauth-migration://" in raw_input:
        parsed = urllib.parse.urlparse(raw_input)
        qs = urllib.parse.parse_qs(parsed.query)
        if "data" not in qs:
            print("В URL не найден параметр data")
            return
        data_str = qs["data"][0]
    else:
        data_str = raw_input

    padding = "=" * (-len(data_str) % 4)
    try:
        buf = base64.urlsafe_b64decode(data_str + padding)
    except Exception as e:
        print("Ошибка base64-декодирования:", e)
        return

    accounts = parse_migration(buf)
    print(f"Найдено аккаунтов: {len(accounts)}\n")

    for i, (secret, name, issuer, alg, otp_type, counter) in enumerate(accounts, 1):
        secret_b32 = base64.b32encode(secret).decode().rstrip("=")
        algo_name = ALGORITHMS.get(alg, f"UNKNOWN({alg})")
        kind = "totp" if otp_type == 2 else ("hotp" if otp_type == 1 else "?")
        label = f"{issuer}:{name}" if issuer else name
        uri = f"otpauth://{kind}/{urllib.parse.quote(label)}?secret={secret_b32}"
        if issuer:
            uri += f"&issuer={urllib.parse.quote(issuer)}"
        if algo_name != "SHA1":
            uri += f"&algorithm={algo_name}"
        if otp_type == 1:
            uri += f"&counter={counter}"

        print(f"[{i}] {issuer} — {name}")
        print(f"    тип: {kind.upper()}, алгоритм: {algo_name}")
        print(f"    secret (base32): {secret_b32}")
        print(f"    URI: {uri}\n")


if __name__ == "__main__":
    main()
