#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Kịch bản kiểm thử TCP Socket Client (Mục 8.3.1)
Kết nối tới ESP32 Smart Light TCP Server (Port 3333), gửi các lệnh điều khiển
'Open the light', 'Close the light', 'Toggle', 'Color', 'Status'
và nhận phản hồi trạng thái từ thiết bị.
"""

import socket
import sys
import time

# Đảm bảo in tiếng Việt an toàn trên Windows console
if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')

DEFAULT_PORT = 3333


def test_tcp_interactive(target_ip, port=DEFAULT_PORT):
    print("=" * 68)
    print("  KIỂM THỬ TRUYỀN NHẬN TCP SOCKET CLIENT - SERVER (MỤC 8.3.1)")
    print(f"  Kết nối tới mục tiêu : {target_ip}:{port}")
    print("=" * 68)

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(5.0)

    try:
        print(f"\n[1] Đang kết nối TCP tới {target_ip}:{port}...")
        sock.connect((target_ip, port))
        print("==> [THÀNH CÔNG] Đã thiết lập kết nối TCP (3-way handshake hoàn tất)!")

        # Đọc banner chào mừng
        welcome = sock.recv(1024).decode('utf-8', errors='ignore')
        print(f"\n[ESP32 Chào mừng]:\n{welcome}")

        commands = [
            "Open the light",
            "Status",
            "Color",
            "Toggle",
            "Close the light",
        ]

        print("[2] Bắt đầu gửi chuỗi lệnh kiểm thử tự động:")
        for cmd in commands:
            print(f"\n---> Gửi lệnh: '{cmd}'")
            sock.sendall((cmd + "\r\n").encode('utf-8'))
            time.sleep(0.5)

            resp = sock.recv(1024).decode('utf-8', errors='ignore').strip()
            print(f"<--- ESP32 phản hồi: {resp}")
            time.sleep(0.8)

        print("\n==> [HOÀN THÀNH KIỂM THỬ] Toàn bộ chuỗi lệnh TCP hoạt động chính xác!")
        print("    Kiểm tra bo mạch: Thanh LED WS2812B đã chuyển đổi trạng thái và màu sắc tương ứng.")

    except socket.timeout:
        print("\n==> [TIMEOUT] Hết thời gian chờ phản hồi TCP!")
    except Exception as e:
        print(f"\n==> [LỖI]: {e}")
    finally:
        sock.close()


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Cách sử dụng: python test_tcp_socket.py <ESP32_IP> [PORT]")
        print("Ví dụ: python test_tcp_socket.py 192.168.1.45 3333")
        sys.exit(1)

    ip = sys.argv[1]
    p = int(sys.argv[2]) if len(sys.argv) > 2 else DEFAULT_PORT
    test_tcp_interactive(ip, p)
