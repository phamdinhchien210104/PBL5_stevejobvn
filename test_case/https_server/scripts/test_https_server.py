#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Kịch bản kiểm thử HTTP & HTTPS Server cho Đèn Thông Minh (PBL5 Smart Light)
Mục 8.3 (HTTP) & Mục 8.4 (HTTPS)

Tính năng:
- Hỗ trợ cả 2 giao thức: HTTP (port 80) và HTTPS (port 443 TLS)
- Bỏ qua xác thực chứng chỉ tự ký (Self-Signed SSL) khi dùng HTTPS trong LAN
- Chế độ Tự Động (Auto-Test): Kiểm thử tuần tự GET /, GET /light, POST /light
- Chế độ Tương Tác (Interactive CLI):
    + Phím '0' : Tắt đèn
    + Phím '1' : Bật đèn
    + Gõ '11' : Đổi màu RGB (chuyển màu kế tiếp trong 8 màu)
    + Mũi tên Lên [↑] : Tăng độ sáng (+10%)
    + Mũi tên Xuống [↓] : Giảm độ sáng (-10%)
    + Phím 's' : Đọc lại trạng thái hiện tại
    + Phím 'q' : Thoát chương trình
"""

import sys
import json
import ssl
import time
import argparse
import urllib.request
import urllib.error

# Khởi tạo SSL Context bỏ qua kiểm tra chứng chỉ tự ký (Development mode)
ssl_ctx = ssl.create_default_context()
ssl_ctx.check_hostname = False
ssl_ctx.verify_mode = ssl.CERT_NONE


def send_request(url, method="GET", payload=None):
    """Gửi HTTP/HTTPS request và trả về (status_code, body_dict_or_str)"""
    data = None
    headers = {}
    if payload is not None:
        data = json.dumps(payload).encode("utf-8")
        headers["Content-Type"] = "application/json"

    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, context=ssl_ctx, timeout=5) as response:
            status_code = response.getcode()
            resp_body = response.read().decode("utf-8")
            try:
                json_data = json.loads(resp_body)
                return status_code, json_data
            except Exception:
                return status_code, resp_body
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", errors="ignore")
    except Exception as e:
        return 0, str(e)


def run_auto_test(base_url):
    print("\n" + "=" * 65)
    print("  BẮT ĐẦU CHU KỲ KIỂM THỬ TỰ ĐỘNG (AUTO-TEST)")
    print("  Target URL:", base_url)
    print("=" * 65)

    # 1. Kiểm tra GET / (Trang Web Dashboard)
    print("\n[Bước 1/6] Kiểm tra GET / (Giao diện Web Dashboard)...")
    code, body = send_request(f"{base_url}/")
    if code == 200 and "PBL5 SMART LIGHT" in body:
        print("  ==> [THÀNH CÔNG] Trang Web Dashboard phản hồi mã 200 OK (HTML kích thước:", len(body), "bytes)")
    else:
        print(f"  ==> [THẤT BẠI] Mã lỗi: {code}, Phản hồi: {body[:100]}...")

    # 2. Đọc trạng thái GET /light
    print("\n[Bước 2/6] Đọc trạng thái ban đầu GET /light...")
    code, data = send_request(f"{base_url}/light")
    if code == 200 and isinstance(data, dict):
        print(f"  ==> [THÀNH CÔNG] Trạng thái đèn: {'BẬT (ON)' if data.get('status') else 'TẮT (OFF)'} | "
              f"Độ sáng: {data.get('brightness')}% | Màu: {data.get('color_name')}")
    else:
        print(f"  ==> [THẤT BẠI] Mã: {code}, Lỗi: {data}")

    # 3. Gửi lệnh BẬT đèn POST /light
    print("\n[Bước 3/6] Gửi lệnh BẬT đèn: POST /light với payload {'status': true}...")
    code, data = send_request(f"{base_url}/light", method="POST", payload={"status": True})
    print(f"  ==> Phản hồi: {data}")
    time.sleep(1)

    # 4. Gửi lệnh TẮT đèn POST /light
    print("\n[Bước 4/6] Gửi lệnh TẮT đèn: POST /light với payload {'status': false}...")
    code, data = send_request(f"{base_url}/light", method="POST", payload={"status": False})
    print(f"  ==> Phản hồi: {data}")
    time.sleep(1)

    # 5. Gửi lệnh BẬT đèn và ĐỔI MÀU (next_color)
    print("\n[Bước 5/6] BẬT đèn và chuyển màu kế tiếp: {'action': 'next_color'}...")
    code, data = send_request(f"{base_url}/light", method="POST", payload={"status": True, "action": "next_color"})
    print(f"  ==> Phản hồi: {data}")
    time.sleep(1)

    # 6. Điều chỉnh độ sáng 50%
    print("\n[Bước 6/6] Cài đặt độ sáng 50%: {'brightness': 50}...")
    code, data = send_request(f"{base_url}/light", method="POST", payload={"brightness": 50})
    print(f"  ==> Phản hồi: {data}")

    print("\n" + "=" * 65)
    print("  HOÀN THÀNH TẤT CẢ CÁC BƯỚC KIỂM THỬ TỰ ĐỘNG! ✅")
    print("=" * 65 + "\n")


def run_interactive(base_url):
    print("\n" + "=" * 65)
    print("  CHẾ ĐỘ TƯƠNG TÁC ĐIỀU KHIỂN ĐÈN (INTERACTIVE CLI)")
    print("  Target:", base_url)
    print("  Phím điều khiển:")
    print("    • '0'  : Tắt đèn")
    print("    • '1'  : Bật đèn")
    print("    • '11' : Đổi màu đèn (chu kỳ 8 màu RGB - giống đúp nút Boot)")
    print("    • 'u' hoặc Mũi tên Lên [↑]   : Tăng độ sáng (+10%)")
    print("    • 'd' hoặc Mũi tên Xuống [↓] : Giảm độ sáng (-10%)")
    print("    • 's'  : Đọc lại trạng thái đèn")
    print("    • 'q'  : Thoát")
    print("=" * 65 + "\n")

    # Đọc trạng thái ban đầu
    code, st = send_request(f"{base_url}/light")
    if code == 200 and isinstance(st, dict):
        print(f"==> [Khởi tạo] Đèn: {'BẬT (ON)' if st.get('status') else 'TẮT (OFF)'} | "
              f"Độ sáng: {st.get('brightness')}% | Màu: {st.get('color_name')}\n")

    while True:
        try:
            cmd = input("Nhập lệnh [0/1/11/u/d/s/q]: ").strip()
        except (KeyboardInterrupt, EOFError):
            print("\nThoát chương trình.")
            break

        if not cmd:
            continue

        if cmd == "q":
            print("Tạm biệt!")
            break
        elif cmd == "0":
            print("==> Gửi lệnh: TẮT ĐÈN (status: false)")
            code, resp = send_request(f"{base_url}/light", method="POST", payload={"status": False})
            print(f"    Phản hồi: {resp}")
        elif cmd == "1":
            print("==> Gửi lệnh: BẬT ĐÈN (status: true)")
            code, resp = send_request(f"{base_url}/light", method="POST", payload={"status": True})
            print(f"    Phản hồi: {resp}")
        elif cmd == "11":
            print("==> Gửi lệnh: ĐỔI MÀU KẾ TIẾP (action: next_color)")
            code, resp = send_request(f"{base_url}/light", method="POST", payload={"action": "next_color"})
            print(f"    Phản hồi: {resp}")
        elif cmd in ("u", "+"):
            print("==> Gửi lệnh: TĂNG ĐỘ SÁNG (+10%)")
            code, resp = send_request(f"{base_url}/light", method="POST", payload={"action": "brightness_up"})
            print(f"    Phản hồi: {resp}")
        elif cmd in ("d", "-"):
            print("==> Gửi lệnh: GIẢM ĐỘ SÁNG (-10%)")
            code, resp = send_request(f"{base_url}/light", method="POST", payload={"action": "brightness_down"})
            print(f"    Phản hồi: {resp}")
        elif cmd == "s":
            code, resp = send_request(f"{base_url}/light")
            if code == 200 and isinstance(resp, dict):
                print(f"==> [Trạng thái] Đèn: {'BẬT' if resp.get('status') else 'TẮT'} | "
                      f"Độ sáng: {resp.get('brightness')}% | Màu: {resp.get('color_name')} | RGB: {resp.get('color')}")
            else:
                print(f"    Phản hồi: {resp}")
        else:
            print("Lệnh không hợp lệ. Hãy nhập 0, 1, 11, u, d, s hoặc q.")


def main():
    parser = argparse.ArgumentParser(description="PBL5 Smart Light HTTP/HTTPS Server Test Client")
    parser.add_argument("--host", default="192.168.1.31", help="Địa chỉ IP của ESP32 (mặc định: 192.168.1.31)")
    parser.add_argument("--proto", choices=["http", "https"], default="https", help="Giao thức (http hoặc https)")
    parser.add_argument("--port", type=int, default=None, help="Cổng kết nối (mặc định: 443 cho https, 80 cho http)")
    parser.add_argument("--auto", action="store_true", help="Chạy chế độ kiểm thử tự động toàn diện")
    args = parser.parse_args()

    port = args.port
    if port is None:
        port = 443 if args.proto == "https" else 80

    base_url = f"{args.proto}://{args.host}:{port}"
    print(f"Kết nối tới máy chủ Smart Light tại: {base_url}")

    if args.auto:
        run_auto_test(base_url)
    else:
        run_interactive(base_url)


if __name__ == "__main__":
    main()
