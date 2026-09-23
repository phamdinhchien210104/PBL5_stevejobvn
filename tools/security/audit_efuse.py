#!/usr/bin/env python3
"""
eFuse Security Audit and Virtual Simulation Tool (Chapter 13)

Audits security-critical eFuses on ESP32-C3 and ESP32-S3 against the
production hardening baseline. Runs in safe virtual mode (--virt) by default
to eliminate any risk of bricking physical development hardware.
"""

import os
import sys
import argparse
import subprocess
import tempfile
import re

SECURITY_BASELINE = [
    {
        "fuse": "SECURE_BOOT_EN",
        "description": "Hardware Secure Boot V2 Enabled",
        "target_value": ["True", "1"],
        "criticality": "HIGH"
    },
    {
        "fuse": "SPI_BOOT_CRYPT_CNT",
        "description": "SPI Flash Encryption Boot Counter",
        "target_value": ["0b001", "0b111", "Enable", "1", "3"],
        "criticality": "HIGH"
    },
    {
        "fuse": "DIS_PAD_JTAG",
        "description": "Hardware GPIO JTAG Disabled",
        "target_value": ["True", "1"],
        "criticality": "MEDIUM"
    },
    {
        "fuse": "DIS_USB_JTAG",
        "description": "USB-to-JTAG Adapter Disabled",
        "target_value": ["True", "1"],
        "criticality": "MEDIUM"
    },
    {
        "fuse": "DIS_FORCE_DOWNLOAD",
        "description": "Forced ROM Download Mode Disabled",
        "target_value": ["True", "1"],
        "criticality": "LOW"
    },
    {
        "fuse": "DIS_DOWNLOAD_MANUAL_ENCRYPT",
        "description": "Manual Download Encryption Disabled",
        "target_value": ["True", "1"],
        "criticality": "MEDIUM"
    },
    {
        "fuse": "ENABLE_SECURITY_DOWNLOAD",
        "description": "Secure Download Mode (Restricted UART)",
        "target_value": ["True", "1"],
        "criticality": "HIGH"
    }
]

def run_espefuse_summary(chip="esp32c3", port=None, virt_file=None):
    python_bin = sys.executable
    cmd = [python_bin, "-m", "espefuse", "--chip", chip]

    if virt_file:
        cmd.extend(["--virt", "--path-efuse-file", virt_file])
    elif not port:
        cmd.append("--virt")
    else:
        cmd.extend(["-p", port])

    cmd.append("summary")

    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        print(f"[!] espefuse summary failed: {res.stderr}")
        return None
    return res.stdout

def parse_efuse_values(summary_text):
    values = {}
    for line in summary_text.splitlines():
        # Match lines like: SECURE_BOOT_EN (BLOCK0) ... = False R/W (0b0)
        match = re.search(r"(\w+)\s+\(BLOCK\d+\).*?=\s+([^\(\n\r]+)", line)
        if match:
            fuse_name = match.group(1).strip()
            fuse_val = match.group(2).strip()
            values[fuse_name] = fuse_val
    return values

def audit_efuses(efuse_values):
    print("=" * 80)
    print(f"{'eFuse Name':<28} | {'Current Value':<15} | {'Required':<10} | {'Status':<10} | {'Criticality'}")
    print("-" * 80)

    all_pass = True
    passed_count = 0
    total_count = len(SECURITY_BASELINE)

    for item in SECURITY_BASELINE:
        fuse = item["fuse"]
        curr = efuse_values.get(fuse, "NOT_FOUND")
        matched = any(tgt.lower() in curr.lower() for tgt in item["target_value"])

        if matched:
            status = "PASS"
            passed_count += 1
        else:
            status = "FAIL"
            all_pass = False

        print(f"{fuse:<28} | {curr:<15} | {str(item['target_value']):<10} | {status:<10} | {item['criticality']}")

    print("=" * 80)
    print(f"Compliance Score: {passed_count}/{total_count} ({passed_count/total_count*100:.1f}%)")
    return all_pass

def simulate_production_hardening(chip="esp32c3"):
    """
    Simulates a full production hardening cycle in virtual mode:
    Burns Secure Boot digest, Flash Encryption key, and security control eFuses.
    """
    python_bin = sys.executable
    with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as tf:
        virt_efuse_path = tf.name

    print(f"\n[*] Starting Virtual Production Hardening Simulation for {chip.upper()}...")
    print(f"[*] Virtual eFuse Storage: {virt_efuse_path}")

    # 1. Burn virtual security fuses
    fuses_to_burn = [
        ("SECURE_BOOT_EN", "1"),
        ("DIS_PAD_JTAG", "1"),
        ("DIS_USB_JTAG", "1"),
        ("DIS_FORCE_DOWNLOAD", "1"),
        ("DIS_DOWNLOAD_MANUAL_ENCRYPT", "1"),
        ("ENABLE_SECURITY_DOWNLOAD", "1"),
    ]

    for fname, fval in fuses_to_burn:
        cmd = [
            python_bin, "-m", "espefuse",
            "--chip", chip,
            "--virt", "--path-efuse-file", virt_efuse_path,
            "--do-not-confirm",
            "burn-efuse", fname, fval
        ]
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode != 0:
            print(f"[!] Warning: Virtual burn failed for {fname}: {res.stderr}")

    # Burn Flash Encryption boot counter (1 bit = development mode)
    cmd_fe = [
        python_bin, "-m", "espefuse",
        "--chip", chip,
        "--virt", "--path-efuse-file", virt_efuse_path,
        "--do-not-confirm",
        "burn-efuse", "SPI_BOOT_CRYPT_CNT", "1"
    ]
    subprocess.run(cmd_fe, capture_output=True, text=True)

    # 2. Audit the hardened virtual chip
    summary = run_espefuse_summary(chip=chip, virt_file=virt_efuse_path)
    if summary:
        vals = parse_efuse_values(summary)
        print("\n[+] Audit Results for Virtual Hardened Chip:")
        audit_efuses(vals)

    # Clean up virtual file
    if os.path.exists(virt_efuse_path):
        os.remove(virt_efuse_path)

def main():
    parser = argparse.ArgumentParser(description="eFuse Security Audit and Hardening Verification (Chapter 13)")
    parser.add_argument("--chip", choices=["esp32c3", "esp32s3"], default="esp32c3", help="Target SoC chip")
    parser.add_argument("--port", help="Serial COM port for live hardware audit (read-only)")
    parser.add_argument("--simulate-production", action="store_true", help="Run complete virtual hardening simulation")
    parser.add_argument("--virt-file", help="Path to existing virtual eFuse file")

    args = parser.parse_args()

    if args.simulate_production:
        simulate_production_hardening(args.chip)
        sys.exit(0)

    print(f"[*] Auditing {args.chip.upper()} eFuses (Mode: {'LIVE ' + args.port if args.port else 'VIRTUAL SIMULATION'})...")
    summary = run_espefuse_summary(chip=args.chip, port=args.port, virt_file=args.virt_file)
    if not summary:
        sys.exit(1)

    vals = parse_efuse_values(summary)
    audit_efuses(vals)

if __name__ == "__main__":
    main()
