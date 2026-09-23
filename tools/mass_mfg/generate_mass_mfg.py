#!/usr/bin/env python3
"""
Automated NVS Factory Partition Generator for Mass Manufacturing (Chapter 14)

Integrates with ESP-IDF v6.0.2 mfg_gen.py to generate per-device NVS factory
binaries (size 0x6000 = 24KB) populated with device credentials and configuration.
Supports plain and encrypted NVS partition modes.
"""

import os
import sys
import argparse
import subprocess
import glob

def find_idf_path():
    # 1. Check environment variable
    idf_path = os.environ.get("IDF_PATH")
    if idf_path and os.path.exists(idf_path):
        return idf_path

    # 2. Check standard known paths on Windows
    candidate_paths = [
        r"D:\esp\v6.0.2\esp-idf",
        r"C:\esp\v6.0.2\esp-idf",
        r"C:\Espressif\frameworks\esp-idf-v6.0.2",
    ]
    for p in candidate_paths:
        if os.path.exists(p):
            return p

    return None

def run_mass_mfg(schema_csv, values_csv, prefix="LIGHT", size="0x6000",
                 outdir="bin_outputs", encrypt=False, idf_path=None):
    if not idf_path:
        idf_path = find_idf_path()
        if not idf_path:
            print("[!] ERROR: Could not locate ESP-IDF installation. Please set IDF_PATH environment variable.")
            sys.exit(1)

    mfg_gen_script = os.path.join(idf_path, "tools", "mass_mfg", "mfg_gen.py")
    if not os.path.exists(mfg_gen_script):
        print(f"[!] ERROR: mfg_gen.py not found at: {mfg_gen_script}")
        sys.exit(1)

    python_bin = sys.executable
    print(f"[*] ESP-IDF Path: {idf_path}")
    print(f"[*] Manufacturing Tool: {mfg_gen_script}")
    print(f"[*] Schema Config: {schema_csv}")
    print(f"[*] Values File: {values_csv}")
    print(f"[*] Partition Size: {size}")
    print(f"[*] Output Directory: {outdir}")
    print(f"[*] Encrypted NVS: {'YES' if encrypt else 'NO'}")

    os.makedirs(outdir, exist_ok=True)

    cmd = [
        python_bin,
        mfg_gen_script,
        "generate",
        schema_csv,
        values_csv,
        prefix,
        size,
        "--fileid", "serial_no",
        "--outdir", outdir,
        "--version", "2"
    ]

    if encrypt:
        cmd.append("--keygen")

    print(f"[*] Executing: {' '.join(cmd)}")
    result = subprocess.run(cmd, cwd=os.path.dirname(os.path.abspath(schema_csv)),
                            capture_output=True, text=True)

    if result.returncode != 0:
        print("[!] Generation FAILED!")
        print("STDERR:\n" + result.stderr)
        print("STDOUT:\n" + result.stdout)
        sys.exit(result.returncode)

    print("[+] mfg_gen execution successful!")
    if result.stdout:
        print(result.stdout.strip())

    # Verification of generated binaries
    bin_dir = os.path.join(os.path.dirname(os.path.abspath(schema_csv)), outdir, "bin")
    expected_size = int(size, 16) if size.startswith("0x") else int(size)

    generated_bins = glob.glob(os.path.join(bin_dir, f"{prefix}-*.bin"))
    print(f"\n[+] Verification of Generated Binaries in '{bin_dir}':")
    print("=" * 70)
    print(f"{'Filename':<35} | {'Size (Bytes)':<12} | {'Status':<10}")
    print("-" * 70)

    all_valid = True
    for b in sorted(generated_bins):
        fname = os.path.basename(b)
        fsize = os.path.getsize(b)
        status = "OK" if fsize == expected_size else "SIZE MISMATCH"
        if status != "OK":
            all_valid = False
        print(f"{fname:<35} | {fsize:<12} | {status:<10}")

    print("=" * 70)
    if all_valid and len(generated_bins) > 0:
        print(f"[+] All {len(generated_bins)} binaries verified successfully ({expected_size} bytes each).")
    else:
        print("[!] Warning: Some binaries were missing or did not match expected partition size.")

def main():
    parser = argparse.ArgumentParser(description="Generate Mass Manufacturing NVS Binaries (Chapter 14)")
    parser.add_argument("--schema", default="nvs_schema.csv", help="Path to NVS schema CSV (default: nvs_schema.csv)")
    parser.add_argument("--values", default="mass_mfg_values.csv", help="Path to device values CSV (default: mass_mfg_values.csv)")
    parser.add_argument("--prefix", default="LIGHT", help="Filename prefix (default: LIGHT)")
    parser.add_argument("--size", default="0x6000", help="NVS partition size in bytes (default: 0x6000 / 24KB)")
    parser.add_argument("--outdir", default="bin_outputs", help="Output directory (default: bin_outputs)")
    parser.add_argument("--encrypt", action="store_true", help="Enable NVS partition encryption with generated keys")
    parser.add_argument("--idf-path", default=None, help="Explicit path to ESP-IDF installation")

    args = parser.parse_args()

    base_dir = os.path.dirname(os.path.abspath(__file__))
    schema_path = os.path.join(base_dir, args.schema) if not os.path.isabs(args.schema) else args.schema
    values_path = os.path.join(base_dir, args.values) if not os.path.isabs(args.values) else args.values

    run_mass_mfg(schema_path, values_path, args.prefix, args.size, args.outdir, args.encrypt, args.idf_path)

if __name__ == "__main__":
    main()
