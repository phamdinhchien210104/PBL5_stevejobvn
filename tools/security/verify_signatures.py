#!/usr/bin/env python3
"""
Firmware Signature Verification Tool (Chapter 13)

Verifies Secure Boot v2 signatures on ESP32-C3 and ESP32-S3 binaries
using espsecure verify-signature and signature-info-v2.
"""

import os
import sys
import argparse
import subprocess

def verify_signature(target_bin, key_pem):
    if not os.path.exists(target_bin):
        print(f"[!] ERROR: Target binary not found: {target_bin}")
        sys.exit(1)

    if not os.path.exists(key_pem):
        print(f"[!] ERROR: Verification key not found: {key_pem}")
        sys.exit(1)

    python_bin = sys.executable

    print("=" * 75)
    print("  ESP32 Secure Boot v2 Signature Verification")
    print("=" * 75)
    print(f"[*] Target Binary: {target_bin}")
    print(f"[*] Verification Key: {key_pem}\n")

    # 1. Verify signature with espsecure verify-signature
    cmd_verify = [
        python_bin, "-m", "espsecure",
        "verify-signature",
        "--version", "2",
        "--keyfile", key_pem,
        target_bin
    ]
    res_verify = subprocess.run(cmd_verify, capture_output=True, text=True)
    if res_verify.returncode == 0:
        print("[+] Signature Verification: PASSED (Valid RSA-3072 signature matching key)")
    else:
        print("[!] Signature Verification: FAILED!")
        print(res_verify.stderr or res_verify.stdout)
        return False

    # 2. Inspect signature blocks
    cmd_info = [
        python_bin, "-m", "espsecure",
        "signature-info-v2",
        target_bin
    ]
    res_info = subprocess.run(cmd_info, capture_output=True, text=True)
    if res_info.returncode == 0 and res_info.stdout:
        print("\n[*] Signature Block Details:")
        for line in res_info.stdout.strip().split("\n"):
            print(f"    {line}")

    print("\n" + "=" * 75)
    return True

def main():
    parser = argparse.ArgumentParser(description="Verify Secure Boot v2 signature on ESP32 binary")
    parser.add_argument("target_bin", help="Path to signed binary image")
    parser.add_argument("--key", default=os.path.join(os.path.dirname(__file__), "keys", "secure_boot_signing_key.pem"),
                        help="Path to RSA-3072 PEM key")

    args = parser.parse_args()
    success = verify_signature(args.target_bin, args.key)
    sys.exit(0 if success else 1)

if __name__ == "__main__":
    main()
