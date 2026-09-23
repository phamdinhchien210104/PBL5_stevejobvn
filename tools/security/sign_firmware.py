#!/usr/bin/env python3
"""
Firmware Signing Tool for Secure Boot v2 (Chapter 13)

Signs ESP32-C3 and ESP32-S3 application and bootloader binaries with an RSA-3072
private key using espsecure sign-data.
"""

import os
import sys
import argparse
import subprocess

def sign_binary(input_bin, key_pem, output_bin=None):
    if not os.path.exists(input_bin):
        print(f"[!] ERROR: Input binary not found: {input_bin}")
        sys.exit(1)

    if not os.path.exists(key_pem):
        print(f"[!] ERROR: Signing key not found: {key_pem}")
        sys.exit(1)

    if not output_bin:
        base, ext = os.path.splitext(input_bin)
        output_bin = f"{base}_signed{ext}"

    python_bin = sys.executable
    cmd = [
        python_bin, "-m", "espsecure",
        "sign-data",
        "--version", "2",
        "--keyfile", key_pem,
        "--output", output_bin,
        input_bin
    ]

    print(f"[*] Signing Binary: {input_bin}")
    print(f"[*] With Key: {key_pem}")
    print(f"[*] Output Target: {output_bin}")

    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        print(f"[!] Signing failed: {res.stderr}")
        sys.exit(res.returncode)

    orig_size = os.path.getsize(input_bin)
    signed_size = os.path.getsize(output_bin)
    diff = signed_size - orig_size

    print(f"[+] Successfully signed binary!")
    print(f"    - Original Size: {orig_size} bytes")
    print(f"    - Signed Size:   {signed_size} bytes (+{diff} bytes signature sector)")
    return output_bin

def main():
    parser = argparse.ArgumentParser(description="Sign ESP32 Binary for Secure Boot v2 (Chapter 13)")
    parser.add_argument("input_bin", help="Path to input binary image to sign")
    parser.add_argument("--key", default=os.path.join(os.path.dirname(__file__), "keys", "secure_boot_signing_key.pem"),
                        help="Path to RSA-3072 PEM signing key")
    parser.add_argument("--output", help="Path to output signed binary (default: <name>_signed.bin)")

    args = parser.parse_args()
    sign_binary(args.input_bin, args.key, args.output)

if __name__ == "__main__":
    main()
