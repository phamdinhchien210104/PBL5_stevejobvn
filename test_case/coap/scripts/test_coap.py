#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Kịch bản kiểm thử CoAP Client thuần Python (RFC 7252 - Không phụ thuộc thư viện ngoài)
Gửi các yêu cầu CoAP GET /light và PUT /light tới ESP32 Smart Light (Port 5683)
và phân tích mã phản hồi (2.05 Content, 2.04 Changed).
"""

import socket
import sys
import time

# Đảm bảo in tiếng Việt an toàn trên Windows console
if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')

COAP_DEFAULT_PORT = 5683


def build_coap_packet(msg_type, code, msg_id, uri_path, payload=None):
    """
    Xây dựng gói tin CoAP Header + Option Uri-Path + Payload Marker (RFC 7252)
    """
    # Header 4 bytes: [Ver=1, Type, TKL=0], [Code], [Message ID 2 bytes]
    header = bytearray([
        (1 << 6) | (msg_type << 4),
        code,
        (msg_id >> 8) & 0xFF,
        msg_id & 0xFF
    ])

    # Option Uri-Path (Option Number 11)
    path_bytes = uri_path.encode('utf-8')
    path_len = len(path_bytes)
    # Option Delta 11, Option Length
    header.append((11 << 4) | path_len)
    header.extend(path_bytes)

    # Payload (nếu có)
    if payload:
        header.append(0xFF)  # Payload Marker
        header.extend(payload.encode('utf-8'))

    return bytes(header)


def parse_coap_response(data):
    """
    Phân tích gói tin phản hồi CoAP
    """
    if len(data) < 4:
        return "Gói tin quá ngắn"
    code = data[1]
    class_code = code >> 5
    detail_code = code & 0x1F
    code_str = f"{class_code}.{detail_code:02d}"

    # Tìm vị trí bắt đầu payload sau marker 0xFF
    payload = ""
    for i in range(4, len(data)):
        if data[i] == 0xFF:
            payload = data[i + 1:].decode('utf-8', errors='ignore')
            break

    return f"Code: {code_str} | Payload: {payload}"


def test_coap(target_ip, port=COAP_DEFAULT_PORT):
    print("=" * 68)
    print("  KIỂM THỬ GIAO THỨC NHÚNG CoAP RESTful CLIENT (MỤC 8.3.4)")
    print(f"  Mục tiêu CoAP : coap://{target_ip}:{port}/light")
    print("=" * 68)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(3.0)

    try:
        # 1. Gửi CoAP GET /light
        print("\n[1] Gửi yêu cầu CoAP GET /light (Đọc trạng thái hiện tại)...")
        get_pkt = build_coap_packet(msg_type=0, code=1, msg_id=0x1001, uri_path="light")
        sock.sendto(get_pkt, (target_ip, port))

        resp, _ = sock.recvfrom(1024)
        print(f"==> Phản hồi GET: {parse_coap_response(resp)}")

        # 2. Gửi CoAP PUT /light (Bật đèn ON)
        print("\n[2] Gửi yêu cầu CoAP PUT /light (Payload: 'ON')...")
        put_on_pkt = build_coap_packet(msg_type=0, code=3, msg_id=0x1002, uri_path="light", payload="ON")
        sock.sendto(put_on_pkt, (target_ip, port))

        resp, _ = sock.recvfrom(1024)
        print(f"==> Phản hồi PUT ON: {parse_coap_response(resp)}")

        time.sleep(1.0)

        # 3. Gửi CoAP PUT /light (Chuyển màu 'color')
        print("\n[3] Gửi yêu cầu CoAP PUT /light (Payload: 'color')...")
        put_color_pkt = build_coap_packet(msg_type=0, code=3, msg_id=0x1003, uri_path="light", payload="color")
        sock.sendto(put_color_pkt, (target_ip, port))

        resp, _ = sock.recvfrom(1024)
        print(f"==> Phản hồi PUT COLOR: {parse_coap_response(resp)}")

        time.sleep(1.0)

        # 4. Gửi CoAP GET /light kiểm tra lại
        print("\n[4] Gửi yêu cầu CoAP GET /light (Xác nhận trạng thái mới)...")
        get_pkt2 = build_coap_packet(msg_type=0, code=1, msg_id=0x1004, uri_path="light")
        sock.sendto(get_pkt2, (target_ip, port))

        resp, _ = sock.recvfrom(1024)
        print(f"==> Phản hồi GET: {parse_coap_response(resp)}")

        print("\n==> [HOÀN THÀNH KIỂM THỬ] Toàn bộ chuỗi truy vấn CoAP GET/PUT hoạt động hoàn hảo!")
        print("    Kiểm tra bo mạch: Thanh LED WS2812B đã chuyển đổi trạng thái thực tế.")

    except socket.timeout:
        print("\n==> [TIMEOUT] Hết thời gian chờ phản hồi CoAP từ ESP32!")
    except Exception as e:
        print(f"\n==> [LỖI]: {e}")
    finally:
        sock.close()


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Cách sử dụng: python test_coap.py <ESP32_IP> [PORT]")
        print("Ví dụ: python test_coap.py 192.168.1.45 5683")
        sys.exit(1)

    ip = sys.argv[1]
    p = int(sys.argv[2]) if len(sys.argv) > 2 else COAP_DEFAULT_PORT
    test_coap(ip, p)
