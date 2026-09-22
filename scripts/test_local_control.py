#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ESP32 Smart Light Project - Chapter 8: Local Control Verification Script (Practice 8.5.2)

This script acts as a LAN client to verify the Local Control HTTPS + mDNS server
running on the ESP32-S3 / ESP32-C3 device.

It communicates via the standard esp_local_ctrl Protobuf binary protocol over HTTPS POST,
implemented natively in pure Python without requiring external pip dependencies.

Usage:
    python scripts/test_local_control.py [--host <IP_OR_MDNS>] [--port <PORT>]

Examples:
    python scripts/test_local_control.py --host my_esp_ctrl_device.local
    python scripts/test_local_control.py --host 192.168.1.189
"""

import argparse
import json
import ssl
import sys
import time
import urllib.request
import urllib.error

# =========================================================================
# 1. PURE PYTHON PROTOBUF ENCODER & DECODER FOR ESP_LOCAL_CTRL
# =========================================================================

def encode_varint(val: int) -> bytes:
    """Encode an integer as a protobuf varint."""
    buf = bytearray()
    while val > 0x7F:
        buf.append((val & 0x7F) | 0x80)
        val >>= 7
    buf.append(val & 0x7F)
    return bytes(buf)

def encode_field(field_num: int, wire_type: int, payload) -> bytes:
    """Encode a single protobuf field."""
    key = (field_num << 3) | (wire_type & 0x07)
    key_bytes = encode_varint(key)
    if wire_type == 0:  # Varint
        return key_bytes + encode_varint(int(payload))
    elif wire_type == 2:  # Length-delimited (bytes, string, embedded message)
        if isinstance(payload, str):
            payload = payload.encode('utf-8')
        return key_bytes + encode_varint(len(payload)) + payload
    raise ValueError(f"Unsupported wire type {wire_type}")

def decode_protobuf(data: bytes) -> dict:
    """Simple protobuf parser returning dict mapping field_number -> list of values."""
    idx = 0
    fields = {}
    while idx < len(data):
        # Read varint key
        key = 0
        shift = 0
        while idx < len(data):
            b = data[idx]
            idx += 1
            key |= (b & 0x7F) << shift
            if not (b & 0x80):
                break
            shift += 7

        field_num = key >> 3
        wire_type = key & 0x07

        if wire_type == 0:  # Varint
            val = 0
            shift = 0
            while idx < len(data):
                b = data[idx]
                idx += 1
                val |= (b & 0x7F) << shift
                if not (b & 0x80):
                    break
                shift += 7
            fields.setdefault(field_num, []).append(val)
        elif wire_type == 2:  # Length-delimited
            length = 0
            shift = 0
            while idx < len(data):
                b = data[idx]
                idx += 1
                length |= (b & 0x7F) << shift
                if not (b & 0x80):
                    break
                shift += 7
            val = data[idx:idx + length]
            idx += length
            fields.setdefault(field_num, []).append(val)
        elif wire_type == 1:  # 64-bit
            val = data[idx:idx + 8]
            idx += 8
            fields.setdefault(field_num, []).append(val)
        elif wire_type == 5:  # 32-bit
            val = data[idx:idx + 4]
            idx += 4
            fields.setdefault(field_num, []).append(val)
        else:
            break
    return fields

# Message Types for LocalCtrlMsgType
TYPE_CMD_GET_PROP_COUNT = 0
TYPE_RESP_GET_PROP_COUNT = 1
TYPE_CMD_GET_PROP_VALS  = 4
TYPE_RESP_GET_PROP_VALS  = 5
TYPE_CMD_SET_PROP_VALS  = 6
TYPE_RESP_SET_PROP_VALS  = 7

def make_cmd_get_prop_count() -> bytes:
    """Build LocalCtrlMessage with CmdGetPropertyCount (payload 10)."""
    msg_field = encode_field(1, 0, TYPE_CMD_GET_PROP_COUNT)
    cmd_field = encode_field(10, 2, b"")  # Empty message
    return msg_field + cmd_field

def make_cmd_get_prop_vals(indices=(0,)) -> bytes:
    """Build LocalCtrlMessage with CmdGetPropertyValues (payload 12)."""
    inner = b"".join(encode_field(1, 0, idx) for idx in indices)
    msg_field = encode_field(1, 0, TYPE_CMD_GET_PROP_VALS)
    cmd_field = encode_field(12, 2, inner)
    return msg_field + cmd_field

def make_cmd_set_prop_vals(prop_list) -> bytes:
    """
    Build LocalCtrlMessage with CmdSetPropertyValues (payload 14).
    prop_list is a list of tuples: [(index, value_bytes), ...]
    """
    inner_props = bytearray()
    for idx, val in prop_list:
        if isinstance(val, str):
            val = val.encode('utf-8')
        prop_val = encode_field(1, 0, idx) + encode_field(2, 2, val)
        inner_props.extend(encode_field(1, 2, prop_val))
    msg_field = encode_field(1, 0, TYPE_CMD_SET_PROP_VALS)
    cmd_field = encode_field(14, 2, bytes(inner_props))
    return msg_field + cmd_field

def parse_resp_get_prop_vals(raw_body: bytes):
    """Parse RespGetPropertyValues response and extract property name and string value."""
    top = decode_protobuf(raw_body)
    resp_field = top.get(13)  # resp_get_prop_vals (field 13)
    if not resp_field:
        return None
    inner = decode_protobuf(resp_field[0])
    props = inner.get(2, [])  # repeated PropertyInfo props = 2;
    results = []
    for p in props:
        p_dict = decode_protobuf(p)
        name = p_dict.get(2, [b""])[0].decode('utf-8', errors='ignore')
        val_bytes = p_dict.get(5, [b""])[0]
        results.append((name, val_bytes))
    return results

# =========================================================================
# 2. HTTPS TRANSPORT LAYER
# =========================================================================

def send_https_request(url: str, data: bytes = None, content_type: str = "application/x-protobuf", timeout: float = 6.0):
    """Send an HTTPS POST or GET request with SSL verification disabled for self-signed certs."""
    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE

    headers = {}
    method = "POST" if data is not None else "GET"
    if content_type:
        headers["Content-Type"] = content_type

    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, context=ctx, timeout=timeout) as response:
            return response.status, response.read()
    except urllib.error.HTTPError as e:
        return e.code, e.read()
    except Exception as e:
        return None, str(e).encode('utf-8')

# =========================================================================
# 3. INTERACTIVE VERIFICATION SUITE
# =========================================================================

def test_local_control(host: str, port: int):
    base_url = f"https://{host}:{port}/esp_local_ctrl"
    control_url = f"{base_url}/control"
    version_url = f"{base_url}/version"

    print("=" * 70)
    print("  ESP32 SMART LIGHT - KIỂM THỬ ĐIỀU KHIỂN CỤC BỘ (PRACTICE 8.5.2)")
    print(f"  Target Host : {host} (Port: {port})")
    print(f"  Control URL : {control_url}")
    print(f"  Version URL : {version_url}")
    print("=" * 70)

    # 1. Kiểm tra Endpoint Version
    print("\n[Bước 1/5] Kiểm tra phiên bản Local Control Server (/version)...")
    status, body = send_https_request(version_url)
    if status == 200:
        print(f"  [+] Thành công (HTTP {status})! Phiên bản: {body.decode('utf-8', errors='ignore')}")
    else:
        print(f"  [*] Endpoint /version phản hồi: HTTP {status}")

    # 2. Lấy số lượng thuộc tính đã đăng ký (CmdGetPropertyCount)
    print("\n[Bước 2/5] Đọc số lượng thuộc tính đăng ký (CmdGetPropertyCount)...")
    req_payload = make_cmd_get_prop_count()
    status, body = send_https_request(control_url, data=req_payload)
    if status == 200:
        resp = decode_protobuf(body)
        resp_count = resp.get(11)  # resp_get_prop_count
        if resp_count:
            inner = decode_protobuf(resp_count[0])
            count = inner.get(2, [0])[0]
            print(f"  [+] Thành công! Số lượng thuộc tính trên thiết bị: {count}")
        else:
            print(f"  [+] Nhận phản hồi Protobuf hợp lệ từ thiết bị (Len: {len(body)} bytes)")
    else:
        print(f"  [-] Lỗi HTTP {status}: {body}")

    # 3. Đọc trạng thái đèn hiện tại (CmdGetPropertyValues)
    print("\n[Bước 3/5] Đọc trạng thái đèn hiện tại (CmdGetPropertyValues)...")
    req_payload = make_cmd_get_prop_vals(indices=[0])
    status, body = send_https_request(control_url, data=req_payload)
    current_state_str = "unknown"
    if status == 200:
        props = parse_resp_get_prop_vals(body)
        if props:
            for name, val in props:
                val_str = val.decode('utf-8', errors='ignore')
                print(f"  [+] Thuộc tính '{name}' = {val_str}")
                current_state_str = val_str
        else:
            print(f"  [*] Phản hồi thô: {body[:32]}...")
    else:
        print(f"  [-] Lỗi đọc thuộc tính: HTTP {status}")

    # 4. Gửi lệnh BẬT ĐÈN (CmdSetPropertyValues: {"status": true})
    print("\n[Bước 4/5] Gửi lệnh BẬT ĐÈN (CmdSetPropertyValues -> status: true)...")
    req_payload = make_cmd_set_prop_vals([(0, b'{"status": true}')])
    status, body = send_https_request(control_url, data=req_payload)
    if status == 200:
        print(f"  [+] Lệnh BẬT ĐÈN đã gửi thành công! (HTTP {status})")
        # Đọc lại trạng thái để xác nhận
        status_check, body_check = send_https_request(control_url, data=make_cmd_get_prop_vals([0]))
        props_check = parse_resp_get_prop_vals(body_check)
        if props_check:
            for name, val in props_check:
                print(f"  [+] Xác nhận trạng thái sau khi BẬT: '{name}' = {val.decode('utf-8', errors='ignore')}")
                print("  ==> KIỂM TRA MẮT THƯỜNG: Thanh 8 LED WS2812B đã BẬT SÁNG!")
    else:
        print(f"  [-] Gửi lệnh bật thất bại: HTTP {status}")

    time.sleep(2)

    # 5. Gửi lệnh TẮT ĐÈN (CmdSetPropertyValues: {"status": false})
    print("\n[Bước 5/5] Gửi lệnh TẮT ĐÈN (CmdSetPropertyValues -> status: false)...")
    req_payload = make_cmd_set_prop_vals([(0, b'{"status": false}')])
    status, body = send_https_request(control_url, data=req_payload)
    if status == 200:
        print(f"  [+] Lệnh TẮT ĐÈN đã gửi thành công! (HTTP {status})")
        # Đọc lại trạng thái để xác nhận
        status_check, body_check = send_https_request(control_url, data=make_cmd_get_prop_vals([0]))
        props_check = parse_resp_get_prop_vals(body_check)
        if props_check:
            for name, val in props_check:
                print(f"  [+] Xác nhận trạng thái sau khi TẮT: '{name}' = {val.decode('utf-8', errors='ignore')}")
                print("  ==> KIỂM TRA MẮT THƯỜNG: Thanh 8 LED WS2812B đã TẮT HOÀN TOÀN!")
    else:
        print(f"  [-] Gửi lệnh tắt thất bại: HTTP {status}")

    print("\n" + "=" * 70)
    print("  [*] HOÀN TẤT KIỂM THỬ ĐIỀU KHIỂN CỤC BỘ KÊNH WI-FI HTTPS (MỤC 8.5.2)!")
    print("  [*] Thiết bị hỗ trợ điều khiển song song qua Bluetooth LE (GATT UUID 0x00FF)")
    print("=" * 70)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Test ESP32 Local Control Server via Protobuf/HTTPS")
    parser.add_argument("--host", default="my_esp_ctrl_device.local", help="ESP32 IP address or mDNS hostname")
    parser.add_argument("--port", type=int, default=443, help="HTTPS port (default: 443)")
    args = parser.parse_args()

    test_local_control(args.host, args.port)
