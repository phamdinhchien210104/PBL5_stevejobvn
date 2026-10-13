#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Kịch bản kiểm thử mDNS Zero-Config Discovery (Mục 8.2.4)
1. Phân giải tên miền 'my_smart_light.local' sang địa chỉ IP thực của ESP32.
2. Gửi HTTP GET request tới http://my_smart_light.local:80/foobar và in phản hồi.
"""

import socket
import urllib.request
import sys
import subprocess

# Đảm bảo in tiếng Việt an toàn trên Windows console
if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')

MDNS_HOSTNAME = "my_smart_light.local"
MDNS_PORT = 80
URL_PATH = "/foobar"


def test_mdns():
    print("=" * 68)
    print("  KIỂM THỬ KHÁM PHÁ DỊCH VỤ MẠNG CỤC BỘ QUA mDNS (MỤC 8.2.4)")
    print(f"  Tên miền thử nghiệm : {MDNS_HOSTNAME}")
    print(f"  Dịch vụ HTTP        : http://{MDNS_HOSTNAME}:{MDNS_PORT}{URL_PATH}")
    print("=" * 68)

    # 1. Thử phân giải IP qua socket.gethostbyname
    print(f"\n[1] Đang phân giải địa chỉ IP cho '{MDNS_HOSTNAME}' qua mDNS...")
    ip_addr = None
    try:
        ip_addr = socket.gethostbyname(MDNS_HOSTNAME)
        print(f"==> [THÀNH CÔNG] '{MDNS_HOSTNAME}' đã được phân giải thành IP: {ip_addr}")
    except socket.gaierror as e:
        print(f"==> [LƯU Ý] Không phân giải trực tiếp được qua socket: {e}")
        print("    Đang thử gọi công cụ hệ điều hành (ping)...")
        try:
            res = subprocess.run(["ping", "-n", "1", MDNS_HOSTNAME], capture_output=True, text=True, timeout=3)
            print(res.stdout)
        except Exception:
            pass

    # 2. Thử gửi HTTP GET request
    url = f"http://{MDNS_HOSTNAME}:{MDNS_PORT}{URL_PATH}"
    print(f"\n[2] Gửi HTTP GET request tới: {url}...")
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "PBL5-mDNS-Tester/1.0"})
        with urllib.request.urlopen(req, timeout=5) as response:
            content = response.read().decode("utf-8")
            print(f"==> [HTTP 200 OK] Phản hồi từ Smart Light:")
            print("--------------------------------------------------")
            print(content.strip())
            print("--------------------------------------------------")
            print("\nKiểm tra bo mạch: Thanh LED WS2812B đã nhấp nháy xanh lá xác nhận!")
    except Exception as e:
        print(f"==> [LỖI TRUY CẬP HTTP]: {e}")
        print("    Gợi ý: Nếu chưa phân giải được tên miền .local, hãy đảm bảo:")
        print("    1. Máy tính và ESP32 cùng kết nối một mạng Wi-Fi.")
        print("    2. Windows đã bật 'Bonjour Service' hoặc 'mDNS Support' (mặc định có sẵn từ Windows 10/11).")


if __name__ == "__main__":
    test_mdns()
