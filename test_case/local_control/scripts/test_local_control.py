#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ESP32 Smart Light Project - Chapter 8: Local Control Verification Script (Practice 8.5.2)

This script acts as a LAN client to verify the Local Control HTTPS + mDNS server
running on the ESP32-S3 / ESP32-C3 device.

It communicates via the standard esp_local_ctrl Protobuf binary protocol over HTTPS POST,
implemented natively in pure Python without requiring external pip dependencies.

Usage:
    python test_case/local_control/scripts/test_local_control.py [--host <IP_OR_MDNS>] [--port <PORT>]

Examples:
    python test_case/local_control/scripts/test_local_control.py --host my_esp_ctrl_device.local
    python test_case/local_control/scripts/test_local_control.py --host 192.168.1.31
"""

import argparse
import concurrent.futures
import json
import os
import socket
import ssl
import sys
import time
import urllib.request
import urllib.error

# Tự động tìm kiếm rootCA.pem trong thư mục scripts hoặc thư mục main
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
DEFAULT_ROOT_CA = os.path.join(SCRIPT_DIR, "rootCA.pem")
if not os.path.isfile(DEFAULT_ROOT_CA):
    DEFAULT_ROOT_CA = os.path.join(SCRIPT_DIR, "..", "main", "rootCA.pem")


# Tự động chuyển đổi mã hóa stdout sang UTF-8 trên Windows console
if hasattr(sys.stdout, 'reconfigure'):
    try:
        sys.stdout.reconfigure(encoding='utf-8')
    except Exception:
        pass

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

def decode_varint(data: bytes, idx: int) -> tuple[int, int]:
    """Decode a protobuf varint starting at idx. Returns (value, next_idx)."""
    val, shift = 0, 0
    while idx < len(data):
        b = data[idx]
        idx += 1
        val |= (b & 0x7F) << shift
        if not (b & 0x80):
            break
        shift += 7
    return val, idx

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
        key, idx = decode_varint(data, idx)
        field_num = key >> 3
        wire_type = key & 0x07

        if wire_type == 0:  # Varint
            val, idx = decode_varint(data, idx)
            fields.setdefault(field_num, []).append(val)
        elif wire_type == 2:  # Length-delimited
            length, idx = decode_varint(data, idx)
            fields.setdefault(field_num, []).append(data[idx:idx + length])
            idx += length
        elif wire_type == 1:  # 64-bit fixed
            fields.setdefault(field_num, []).append(data[idx:idx + 8])
            idx += 8
        elif wire_type == 5:  # 32-bit fixed
            fields.setdefault(field_num, []).append(data[idx:idx + 4])
            idx += 4
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

def create_ssl_context(cafile: str = None, verify: bool = True) -> ssl.SSLContext:
    """Tạo SSL Context: nếu verify=True và có cafile thì nạp CA, ngược lại dùng CERT_NONE."""
    target_ca = cafile or (DEFAULT_ROOT_CA if os.path.isfile(DEFAULT_ROOT_CA) else None)
    if verify and target_ca and os.path.isfile(target_ca):
        try:
            ctx = ssl.create_default_context(cafile=target_ca)
            ctx.check_hostname = False
            return ctx
        except Exception:
            pass

    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    return ctx

def send_https_request(url: str, data: bytes = None, content_type: str = "application/x-protobuf", timeout: float = 6.0, cafile: str = None, verify: bool = True):
    """Gửi yêu cầu HTTPS POST/GET với SSL Context chuẩn hóa."""
    ctx = create_ssl_context(cafile, verify)
    headers = {"Content-Type": content_type} if content_type else {}
    req = urllib.request.Request(url, data=data, headers=headers, method="POST" if data is not None else "GET")
    try:
        with urllib.request.urlopen(req, context=ctx, timeout=timeout) as response:
            return response.status, response.read()
    except urllib.error.HTTPError as e:
        return e.code, e.read()
    except Exception as e:
        return None, str(e).encode('utf-8')

# =========================================================================
# 3. AUTO DISCOVERY (mDNS & FAST SUBNET PROBE)
# =========================================================================

def auto_discover_esp32(target_mdns: str = "my_esp_ctrl_device.local", port: int = 443, timeout_per_ip: float = 0.25) -> str:
    """
    Tự động tìm kiếm ESP32 trong mạng LAN theo 2 cơ chế:
    1. Phân giải mDNS hostname (my_esp_ctrl_device.local)
    2. Nếu mDNS bị chặn bởi router/VPN, quét song song các IP trong dải subnet nội bộ mở cổng 443
    """
    print("[*] Đang tự động tìm kiếm đèn ESP32 trong mạng LAN...")

    # 1. Thử phân giải mDNS
    try:
        ip = socket.gethostbyname(target_mdns)
        print(f"[+] Tìm thấy đèn qua mDNS: {target_mdns} -> {ip}")
        return ip
    except Exception:
        pass

    # 2. Quét nhanh dải Subnet cục bộ (Multi-threaded fast scan)
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(('192.168.1.1', 80))
        local_ip = s.getsockname()[0]
        s.close()
    except Exception:
        local_ip = '192.168.1.1'

    subnet_prefix = '.'.join(local_ip.split('.')[:3])
    print(f"[*] Quét nhanh dải mạng {subnet_prefix}.x trên cổng {port}...")

    def probe_candidate(ip_str):
        s_probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s_probe.settimeout(timeout_per_ip)
        try:
            s_probe.connect((ip_str, port))
            s_probe.close()
        except Exception:
            return None

        # Kiểm tra xem có đúng là endpoint esp_local_ctrl không
        url = f"https://{ip_str}:{port}/esp_local_ctrl/version"
        status, _ = send_https_request(url, timeout=1.0)
        if status in (200, 405):
            return ip_str
        return None

    with concurrent.futures.ThreadPoolExecutor(max_workers=50) as executor:
        futures = [executor.submit(probe_candidate, f"{subnet_prefix}.{i}") for i in range(1, 255)]
        for f in concurrent.futures.as_completed(futures):
            res = f.result()
            if res:
                print(f"[+] Đã tự động phát hiện đèn ESP32 tại: {res}:{port}")
                return res

    return None

# =========================================================================
# 4. INTERACTIVE CLI MODE (STATUS DASHBOARD & KEYBOARD CONTROLS)
# =========================================================================

def print_status_dashboard(props):
    """Hiển thị bảng trạng thái trực quan gồm Bật/Tắt, Độ sáng, Màu sắc RGB."""
    val_str = ""
    for _, val in props:
        val_str = val.decode("utf-8", errors="ignore")

    # Phân tích cú pháp JSON trạng thái
    try:
        data = json.loads(val_str)
    except Exception:
        data = {}

    is_on = data.get("status", False)
    brightness = data.get("brightness", 100)
    color_name = data.get("color", "N/A")
    color_idx = data.get("color_idx", 1)
    rgb = data.get("rgb", [255, 255, 255])
    if not isinstance(rgb, list) or len(rgb) < 3:
        rgb = [255, 255, 255]

    # Thanh tiến trình độ sáng trực quan (10 khối)
    num_bars = max(0, min(10, int(round(brightness / 10.0))))
    bar_str = "█" * num_bars + "░" * (10 - num_bars)

    status_str = "🟢 BẬT (ON)" if is_on else "⚪ TẮT (OFF)"

    print("\n" + "=" * 68)
    print("      ESP32 SMART LIGHT - BẢNG ĐIỀU KHIỂN CỤC BỘ (LOCAL CONTROL)")
    print("=" * 68)
    print(f"  [Trạng thái] : {status_str}")
    print(f"  [Độ sáng]    : {brightness:>3}%  [{bar_str}]")
    print(f"  [Màu sắc]    : [{color_idx}/8] {color_name} | RGB({rgb[0]}, {rgb[1]}, {rgb[2]})")
    print("-" * 68)
    print(f"  [JSON Server]: {val_str}")
    print("-" * 68)
    print("  PHÍM THAO TÁC:")
    print("   [1]  : BẬT đèn                     [0]  : TẮT đèn")
    print("   [↑]  : Tăng độ sáng (+20%)         [↓]  : Giảm độ sáng (-20%)")
    print("   [11] hoặc [c] : Đổi màu sắc (8 RGB) [r]  : Đọc lại trạng thái")
    print("   [q]  : Thoát chương trình")
    print("=" * 68)

def flush_keyboard_buffer():
    """Xóa sạch bộ đệm phím tồn đọng để ngăn hiện tượng spam phím khi bấm giữ."""
    try:
        import msvcrt
        while msvcrt.kbhit():
            msvcrt.getwch()
    except Exception:
        pass

def read_control_command():
    """
    Đọc lệnh điều khiển từ người dùng (chống spam phím tuyệt đối):
    - Nhấn phím mũi tên [↑] / [↓] một lần -> Tăng / Giảm 20% rồi dừng chờ lệnh tiếp theo
    - Phím [0] (Tắt đèn)
    - Phím [1] (Bật đèn) hoặc [11] / [c] (Đổi màu sắc)
    - Phím [+] / [-] (Tăng / Giảm 20%)
    - Phím [r] (Làm mới) / [q] (Thoát)
    """
    # 1. Xóa sạch mọi phím còn sót lại trong bộ đệm trước khi hiển thị lời nhắc
    flush_keyboard_buffer()

    prompt = "\nChọn hành động ([1]/[0]/[↑]/[↓]/[11]/[c]/[r]/[q]) > "
    sys.stdout.write(prompt)
    sys.stdout.flush()

    try:
        import msvcrt
        # Chờ chặn (blocking wait) cho đến khi người dùng nhấn 1 phím thực sự
        ch = msvcrt.getwch()

        # Xử lý phím điều hướng mở rộng Windows (\x00 hoặc \xe0)
        if ch in ('\x00', '\xe0'):
            ch2 = msvcrt.getwch()
            # Xóa ngay các sự kiện lặp lại (typematic repeat) nếu người dùng lỡ giữ phím
            time.sleep(0.12)
            flush_keyboard_buffer()

            if ch2 == 'H':  # Up Arrow
                print("[↑] TĂNG ĐỘ SÁNG (+20%)")
                return "up"
            elif ch2 == 'P':  # Down Arrow
                print("[↓] GIẢM ĐỘ SÁNG (-20%)")
                return "down"
            else:
                return "r"

        # Xử lý chuỗi ANSI Escape nếu chạy trong VSCode Terminal / Windows Terminal (\x1b[A hoặc \x1b[B)
        if ch == '\x1b':
            time.sleep(0.04)
            if msvcrt.kbhit() and msvcrt.getwch() == '[':
                if msvcrt.kbhit():
                    ch3 = msvcrt.getwch()
                    time.sleep(0.12)
                    flush_keyboard_buffer()
                    if ch3 == 'A':
                        print("[↑] TĂNG ĐỘ SÁNG (+20%)")
                        return "up"
                    elif ch3 == 'B':
                        print("[↓] GIẢM ĐỘ SÁNG (-20%)")
                        return "down"
            flush_keyboard_buffer()
            return "r"

        # Phím chức năng đơn
        if ch in ('\r', '\n'):
            print("")
            return "r"
        elif ch in ('q', 'Q'):
            print("q")
            return "q"
        elif ch in ('r', 'R'):
            print("r")
            return "r"
        elif ch == '+':
            print("[+] TĂNG ĐỘ SÁNG (+20%)")
            time.sleep(0.12)
            flush_keyboard_buffer()
            return "up"
        elif ch == '-':
            print("[-] GIẢM ĐỘ SÁNG (-20%)")
            time.sleep(0.12)
            flush_keyboard_buffer()
            return "down"
        elif ch in ('c', 'C'):
            print("11 (ĐỔI MÀU SẮC)")
            time.sleep(0.08)
            flush_keyboard_buffer()
            return "11"
        elif ch == '0':
            print("0 (TẮT ĐÈN)")
            time.sleep(0.08)
            flush_keyboard_buffer()
            return "0"
        elif ch == '1':
            sys.stdout.write("1")
            sys.stdout.flush()
            # Chờ tối đa 600ms xem người dùng có gõ thêm số 1 nữa (thành '11') không
            start_t = time.time()
            is_double = False
            while time.time() - start_t < 0.60:
                if msvcrt.kbhit():
                    ch_next = msvcrt.getwch()
                    if ch_next == '1':
                        sys.stdout.write("1 (ĐỔI MÀU SẮC)\n")
                        sys.stdout.flush()
                        is_double = True
                        break
                    elif ch_next in ('\r', '\n'):
                        break
                    else:
                        break
                time.sleep(0.02)
            time.sleep(0.08)
            flush_keyboard_buffer()
            if is_double:
                return "11"
            print(" (BẬT ĐÈN)")
            return "1"
        else:
            sys.stdout.write(ch)
            rest = input()
            flush_keyboard_buffer()
            return (ch + rest).strip()
    except (ImportError, Exception):
        pass

    # Fallback cho Linux / Non-TTY
    try:
        cmd = input().strip()
    except (KeyboardInterrupt, EOFError):
        return "q"

    if cmd in ("\x1b[A", "up", "UP", "+"):
        return "up"
    elif cmd in ("\x1b[B", "down", "DOWN", "-"):
        return "down"
    elif cmd.lower() in ("c", "color", "11"):
        return "11"
    return cmd

def interactive_local_control(host: str, port: int, cafile: str = None, verify: bool = True):
    base_url = f"https://{host}:{port}/esp_local_ctrl"
    control_url = f"{base_url}/control"
    version_url = f"{base_url}/version"

    effective_ca = cafile if cafile else DEFAULT_ROOT_CA
    if verify and effective_ca and os.path.isfile(effective_ca):
        print(f"[*] TLS Security: Xác thực chứng chỉ TLS nghiêm ngặt qua Root CA ({os.path.basename(effective_ca)})")
    else:
        print("[*] TLS Security: Chế độ kiểm thử linh hoạt (Bỏ qua xác thực Root CA)")

    print(f"\n++++ Connecting to {host}:{port} ++++")
    status, body = send_https_request(version_url, timeout=5.0, cafile=cafile, verify=verify)
    if status is None:
        err_msg = body.decode('utf-8', errors='ignore')
        print(f"\nConnection Failure : Không thể kết nối tới {host}:{port} ({err_msg})")
        print("  [*] Lưu ý: Nếu máy tính đang bật VPN hoặc Cloudflare WARP, vui lòng tạm dừng (Pause).")
        return

    print("\n==== Starting Session ====")
    print("==== Session Established ====")

    while True:
        # Đọc thuộc tính từ thiết bị
        req_payload = make_cmd_get_prop_vals(indices=[0])
        status, body = send_https_request(control_url, data=req_payload, cafile=cafile, verify=verify)
        if status != 200:
            print(f"\n[!] Lỗi khi đọc thuộc tính từ thiết bị (HTTP {status})")
            break

        props = parse_resp_get_prop_vals(body)
        if not props:
            print("\n[!] Không nhận được thuộc tính nào từ thiết bị.")
            break

        print_status_dashboard(props)

        cmd = read_control_command()
        if not cmd:
            continue

        if cmd.lower() in ('q', 'quit', 'exit'):
            print("Đang thoát chương trình...")
            break
        elif cmd.lower() in ('r', 'refresh'):
            print("--> Đang đọc lại trạng thái mới nhất...")
            continue

        # Ánh xạ lệnh người dùng sang payload gửi đi
        target_payload = None
        action_desc = ""
        if cmd == "1":
            target_payload = "1"
            action_desc = "BẬT ĐÈN"
        elif cmd == "0":
            target_payload = "0"
            action_desc = "TẮT ĐÈN"
        elif cmd == "11":
            target_payload = "11"
            action_desc = "ĐỔI MÀU SẮC (Kế tiếp 8 màu RGB)"
        elif cmd == "up":
            target_payload = "+"
            action_desc = "TĂNG ĐỘ SÁNG (+20%)"
        elif cmd == "down":
            target_payload = "-"
            action_desc = "GIẢM ĐỘ SÁNG (-20%)"
        else:
            target_payload = cmd
            action_desc = f"Lệnh tùy chỉnh: '{cmd}'"

        print(f"\n--> Đang gửi lệnh [{action_desc}] tới đèn ESP32...")
        req_set = make_cmd_set_prop_vals([(0, target_payload.encode('utf-8'))])
        set_status, set_body = send_https_request(control_url, data=req_set, cafile=cafile, verify=verify)
        if set_status == 200:
            print(f"[+] Lệnh [{action_desc}] thành công! Đang đồng bộ trạng thái...")
            time.sleep(0.3)
        else:
            print(f"[-] Gửi lệnh thất bại: HTTP {set_status} - {set_body}")

# =========================================================================
# 5. AUTOMATED VERIFICATION SUITE (--auto)
# =========================================================================

def test_local_control_auto(host: str, port: int, cafile: str = None, verify: bool = True):
    base_url = f"https://{host}:{port}/esp_local_ctrl"
    control_url = f"{base_url}/control"
    version_url = f"{base_url}/version"

    effective_ca = cafile if cafile else DEFAULT_ROOT_CA
    if verify and effective_ca and os.path.isfile(effective_ca):
        print(f"[*] TLS Security: Xác thực chứng chỉ TLS nghiêm ngặt qua Root CA ({os.path.basename(effective_ca)})")
    else:
        print("[*] TLS Security: Chế độ kiểm thử linh hoạt (Bỏ qua xác thực Root CA)")

    print("=" * 70)
    print("  ESP32 SMART LIGHT - KIỂM THỬ TỰ ĐỘNG ĐIỀU KHIỂN CỤC BỘ (PRACTICE 8.5.2)")
    print(f"  Target Host : {host} (Port: {port})")
    print(f"  Control URL : {control_url}")
    print(f"  Version URL : {version_url}")
    print("=" * 70)

    # 1. Kiểm tra Endpoint Version
    print("\n[Bước 1/7] Kiểm tra phiên bản Local Control Server (/version)...")
    status, body = send_https_request(version_url, cafile=cafile, verify=verify)
    if status == 200:
        print(f"  [+] Thành công (HTTP {status})! Phiên bản: {body.decode('utf-8', errors='ignore')}")
    else:
        print(f"  [*] Endpoint /version phản hồi: HTTP {status}")

    # 2. Lấy số lượng thuộc tính đã đăng ký (CmdGetPropertyCount)
    print("\n[Bước 2/7] Đọc số lượng thuộc tính đăng ký (CmdGetPropertyCount)...")
    req_payload = make_cmd_get_prop_count()
    status, body = send_https_request(control_url, data=req_payload, cafile=cafile, verify=verify)
    if status == 200:
        resp = decode_protobuf(body)
        resp_count = resp.get(11)
        if resp_count:
            inner = decode_protobuf(resp_count[0])
            count = inner.get(2, [0])[0]
            print(f"  [+] Thành công! Số lượng thuộc tính trên thiết bị: {count}")
    else:
        print(f"  [-] Lỗi HTTP {status}: {body}")

    def execute_and_log(step_num: int, cmd_val: str, action_name: str):
        print(f"\n[Bước {step_num}/7] Gửi lệnh {action_name} ('{cmd_val}')...")
        req_set = make_cmd_set_prop_vals([(0, cmd_val.encode('utf-8'))])
        set_st, _ = send_https_request(control_url, data=req_set, cafile=cafile, verify=verify)
        if set_st == 200:
            print(f"  [+] Lệnh {action_name} gửi thành công! (HTTP {set_st})")
            time.sleep(0.3)
            _, check_body = send_https_request(control_url, data=make_cmd_get_prop_vals([0]), cafile=cafile, verify=verify)
            verified_props = parse_resp_get_prop_vals(check_body)
            if verified_props:
                raw_json = verified_props[0][1].decode('utf-8', errors='ignore')
                print(f"  [+] Trạng thái cập nhật từ đèn: {raw_json}")
        else:
            print(f"  [-] Gửi lệnh {action_name} thất bại: HTTP {set_st}")

    # 3. Gửi lệnh BẬT ĐÈN (1)
    execute_and_log(3, "1", "BẬT ĐÈN (Phím 1)")
    time.sleep(1)

    # 4. Gửi lệnh TĂNG ĐỘ SÁNG (+)
    execute_and_log(4, "+", "TĂNG ĐỘ SÁNG (Phím [↑])")
    time.sleep(1)

    # 5. Gửi lệnh GIẢM ĐỘ SÁNG (-)
    execute_and_log(5, "-", "GIẢM ĐỘ SÁNG (Phím [↓])")
    time.sleep(1)

    # 6. Gửi lệnh ĐỔI MÀU SẮC (11)
    execute_and_log(6, "11", "ĐỔI MÀU SẮC (Gõ 11)")
    time.sleep(1)

    # 7. Gửi lệnh TẮT ĐÈN (0)
    execute_and_log(7, "0", "TẮT ĐÈN (Phím 0)")

    print("\n" + "=" * 70)
    print("  [*] HOÀN TẤT KIỂM THỬ ĐIỀU KHIỂN CỤC BỘ TOÀN DIỆN!")
    print("  [*] Hỗ trợ đầy đủ: BẬT/TẮT, Tăng/Giảm độ sáng, Đổi 8 màu sắc!")
    print("=" * 70)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Test ESP32 Local Control Server via Protobuf/HTTPS (Interactive CLI & Auto)")
    parser.add_argument("--host", default=None, help="ESP32 IP address or mDNS hostname (Mặc định: Tự động tìm kiếm qua mDNS / quét mạng LAN)")
    parser.add_argument("--port", type=int, default=443, help="HTTPS port (default: 443)")
    parser.add_argument("--auto", action="store_true", help="Chạy chuỗi kiểm thử tự động toàn diện (Bật, Tăng/Giảm sáng, Đổi màu, Tắt)")
    parser.add_argument("--cafile", default=None, help="Đường dẫn tới file rootCA.pem (Mặc định: tự nhận diện trong scripts/ hoặc main/)")
    parser.add_argument("--no-verify", action="store_true", help="Bỏ qua xác thực chữ ký Root CA")
    args = parser.parse_args()

    target_host = args.host
    if not target_host:
        target_host = auto_discover_esp32(target_mdns="my_esp_ctrl_device.local", port=args.port)
        if not target_host:
            print("\n[-] Không tìm thấy đèn ESP32 nào trong mạng LAN!")
            print("  Vui lòng kiểm tra:")
            print("  1. Bo mạch ESP32 đã được cấp nguồn và kết nối Wi-Fi chưa?")
            print("  2. Máy tính có đang bật Cloudflare WARP / VPN không (nếu có, hãy tạm dừng)?")
            print("  3. Hoặc bạn có thể truyền IP cụ thể: python test_local_control.py --host <IP>")
            sys.exit(1)

    verify = not args.no_verify
    if args.auto:
        test_local_control_auto(target_host, args.port, cafile=args.cafile, verify=verify)
    else:
        interactive_local_control(target_host, args.port, cafile=args.cafile, verify=verify)



