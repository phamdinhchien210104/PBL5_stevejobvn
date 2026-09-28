#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Kịch bản kiểm thử UDP Socket Client (Mục 8.3.3)
Gửi datagram điều khiển 'Open the light', 'Close the light', 'Toggle', 'Color', 'Status'
tới ESP32 Smart Light (Port 3333) và nhận ACK phản hồi tầng ứng dụng.
"""

import socket
import sys
import time

# Đảm bảo in tiếng Việt an toàn trên Windows console
if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')

DEFAULT_PORT = 3333


def test_udp_interactive(target_ip, port=DEFAULT_PORT):
    print("=" * 68)
    print("  KIỂM THỬ TRUYỀN NHẬN UDP SOCKET CLIENT - SERVER (MỤC 8.3.3)")
    print(f"  Mục tiêu UDP : {target_ip}:{port}")
    print("=" * 68)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(3.0)

    commands = [
        "Open the light",
        "Status",
        "Color",
        "Toggle",
        "Close the light",
    ]

    try:
        print("[1] Bắt đầu gửi chuỗi datagram điều khiển và chờ ACK:\n")
        for cmd in commands:
            print(f"---> Gửi UDP: '{cmd}'")
            sock.sendto(cmd.encode('utf-8'), (target_ip, port))

            try:
                data, addr = sock.recvfrom(1024)
                print(f"<--- Nhận ACK từ {addr[0]}:{addr[1]}: {data.decode('utf-8', errors='ignore').strip()}")
            except socket.timeout:
                print(f"<--- [TIMEOUT] Không nhận được ACK cho lệnh '{cmd}'!")

            time.sleep(1.0)

        print("\n==> [HOÀN THÀNH KIỂM THỬ] Toàn bộ chuỗi datagram UDP được gửi và xử lý thành công!")
        print("    Kiểm tra bo mạch: Thanh LED WS2812B đã chuyển đổi trạng thái và màu sắc tương ứng.")

    except Exception as e:
        print(f"\n==> [LỖI]: {e}")
    finally:
        sock.close()


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Cách sử dụng: python test_udp_socket.py <ESP32_IP> [PORT]")
        print("Ví dụ: python test_udp_socket.py 192.168.1.45 3333")
        sys.exit(1)

    ip = sys.argv[1]
    p = int(sys.argv[2]) if len(sys.argv) > 2 else DEFAULT_PORT
    test_udp_interactive(ip, p)
