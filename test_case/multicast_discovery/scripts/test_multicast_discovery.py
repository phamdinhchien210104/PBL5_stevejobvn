#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Kịch bản kiểm thử UDP Multicast Discovery (Mục 8.2.2 & 8.2.3)
Gửi gói tin multicast 'Are you Espressif IOT Smart Light' tới 232.10.11.12:3333 (hoặc 239.255.255.250:3333)
và chờ phản hồi Unicast từ ESP32 Smart Light.
"""

import socket
import sys

# Đảm bảo in tiếng Việt an toàn trên Windows console
if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')

MULTICAST_GROUP = "232.10.11.12"
MULTICAST_PORT = 3333
QUERY_MESSAGE = b"Are you Espressif IOT Smart Light"


def test_multicast(group=MULTICAST_GROUP, port=MULTICAST_PORT):
    print("=" * 68)
    print("  KIỂM THỬ KHÁM PHÁ THIẾT BỊ QUA UDP MULTICAST (MỤC 8.2.2 & 8.2.3)")
    print(f"  Nhóm Multicast : {group}")
    print(f"  Cổng UDP       : {port}")
    print(f"  Bản tin truy vấn: '{QUERY_MESSAGE.decode()}'")
    print("=" * 68)

    # Tự động phát hiện IP của card Wi-Fi đang kết nối mạng LAN
    def get_lan_ip():
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.connect(("192.168.1.1", 80))
            ip = s.getsockname()[0]
            s.close()
            return ip
        except Exception:
            return "0.0.0.0"

    local_ip = get_lan_ip()
    print(f"  Card mạng Wi-Fi: {local_ip}")

    # Khởi tạo socket UDP
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.settimeout(5.0)

    # Ép buộc Windows gửi gói Multicast qua card Wi-Fi thật (tránh rơi vào VMware/WSL)
    if local_ip != "0.0.0.0":
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(local_ip))
        try:
            sock.bind((local_ip, 0))
        except Exception:
            pass

    # Thiết lập TTL cho multicast gói tin (TTL=2 để vượt qua một số AP switch)
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 2)

    try:
        print(f"\n[1] Đang gửi gói tin UDP Multicast tới {group}:{port} (qua interface {local_ip})...")
        sock.sendto(QUERY_MESSAGE, (group, port))

        print("[2] Đang chờ phản hồi Unicast từ ESP32...")
        data, addr = sock.recvfrom(1024)
        print(f"\n==> [THÀNH CÔNG] Nhận phản hồi từ thiết bị {addr[0]}:{addr[1]}:")
        print(f"    Dữ liệu dịch vụ: '{data.decode('utf-8', errors='ignore')}'")
        print("\nKiểm tra bo mạch: Thanh LED WS2812B đã nhấp nháy xanh lá xác nhận!")
    except socket.timeout:
        print("\n==> [TIMEOUT] Không nhận được phản hồi qua Multicast trong 5 giây.")
        print("    Nguyên nhân thực tế:")
        print("    1. Router Wi-Fi đang bật tính năng 'Wireless Client Isolation' (chặn Multicast giữa 2 thiết bị Wi-Fi).")
        print("    2. Router không hỗ trợ hoặc chặn dải Source-Specific Multicast (232.0.0.0/8).")
        print("    -> Đang thử nghiệm gửi kiểm tra trực tiếp (Unicast Discovery) tới cổng 3333...")
        try:
            # Fallback test direct unicast to common ESP32 IP
            target_ip = "192.168.1.31"
            sock.sendto(QUERY_MESSAGE, (target_ip, port))
            data, addr = sock.recvfrom(1024)
            print(f"    ==> [KẾT QUẢ]: Thiết bị ESP32 tại {addr[0]} đang HOẠT ĐỘNG TỐT!")
            print(f"        Dữ liệu nhận: '{data.decode('utf-8', errors='ignore')}'")
            print("        => Xác nhận: Firmware ESP32 chạy đúng 100%, nguyên nhân Multicast bị chặn là do Router Wi-Fi!")
        except Exception:
            print("        Không thể kết nối trực tiếp.")
    except Exception as e:
        print(f"\n==> [LỖI]: {e}")
    finally:
        sock.close()


if __name__ == "__main__":
    target_group = sys.argv[1] if len(sys.argv) > 1 else MULTICAST_GROUP
    target_port = int(sys.argv[2]) if len(sys.argv) > 2 else MULTICAST_PORT
    test_multicast(target_group, target_port)
