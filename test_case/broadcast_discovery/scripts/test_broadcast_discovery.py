#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Kịch bản kiểm thử UDP Broadcast Discovery (Mục 8.2.1)
Gửi gói tin quảng bá 'Are you Espressif IOT Smart Light' tới 255.255.255.255:3333
và chờ phản hồi Unicast từ ESP32 Smart Light.
"""

import socket
import time
import sys

# Đảm bảo in tiếng Việt an toàn trên Windows console
if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')

BROADCAST_PORT = 3333
QUERY_MESSAGE = b"Are you Espressif IOT Smart Light"


def test_broadcast():
    print("=" * 65)
    print("  KIỂM THỬ KHÁM PHÁ THIẾT BỊ QUA UDP BROADCAST (MỤC 8.2.1)")
    print(f"  Cổng phát: {BROADCAST_PORT} | Bản tin: '{QUERY_MESSAGE.decode()}'")
    print("=" * 65)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.settimeout(3.0)

    try:
        print("\n[1] Đang gửi gói tin UDP Broadcast tới 255.255.255.255:3333...")
        sock.sendto(QUERY_MESSAGE, ("255.255.255.255", BROADCAST_PORT))

        print("[2] Đang lắng nghe phản hồi Unicast từ ESP32...")
        data, addr = sock.recvfrom(1024)
        print(f"\n==> [THÀNH CÔNG] Nhận phản hồi từ thiết bị {addr[0]}:{addr[1]}:")
        print(f"    Dữ liệu dịch vụ: '{data.decode('utf-8', errors='ignore')}'")
        print("\nKiểm tra bo mạch: Thanh LED WS2812B đã nhấp nháy xanh lá xác nhận!")
    except socket.timeout:
        print("\n==> [TIMEOUT] Không nhận được phản hồi trong 3 giây.")
        print("    Gợi ý: Đảm bảo PC và ESP32 đang kết nối cùng một router Wi-Fi.")
    except Exception as e:
        print(f"\n==> [LỖI]: {e}")
    finally:
        sock.close()


if __name__ == "__main__":
    test_broadcast()
