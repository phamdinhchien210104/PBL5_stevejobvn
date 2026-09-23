#!/usr/bin/env python3
"""
Factory Data Flashing Utility for Smart Light Mass Production (Chapter 14)

Automatically detects the 'fctry' partition offset from partitions.csv or runtime
partition table, and flashes the per-device NVS factory binary via esptool.py.
Includes --dry-run mode for safe simulation without physical hardware.
"""

import os
import sys
import argparse
import subprocess
import glob

def find_fctry_offset_from_csv(csv_path):
    """
    Parses partitions.csv and calculates the exact start offset of the 'fctry' partition.
    Handles both explicitly defined offsets and auto-calculated offsets.
    """
    if not os.path.exists(csv_path):
        return None

    current_offset = 0x10000  # Default starting offset for partition table data
    with open(csv_path, "r") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = [p.strip() for p in line.split(",")]
            if len(parts) < 5:
                continue

            name, ptype, subtype = parts[0], parts[1], parts[2]
            offset_str = parts[3]
            size_str = parts[4]

            # Parse size
            if size_str.endswith("K") or size_str.endswith("k"):
                size = int(size_str[:-1]) * 1024
            elif size_str.endswith("M") or size_str.endswith("m"):
                size = int(size_str[:-1]) * 1024 * 1024
            elif size_str.startswith("0x") or size_str.startswith("0X"):
                size = int(size_str, 16)
            else:
                size = int(size_str)

            # Parse or calculate offset
            if offset_str:
                if offset_str.startswith("0x") or offset_str.startswith("0X"):
                    offset = int(offset_str, 16)
                else:
                    offset = int(offset_str)
                current_offset = offset
            else:
                # Align if necessary (app partitions 64K aligned, others 4K)
                align = 0x10000 if "app" in ptype.lower() else 0x1000
                if current_offset % align != 0:
                    current_offset = (current_offset + align - 1) & ~(align - 1)
                offset = current_offset

            if name == "fctry":
                return offset

            current_offset = offset + size

    return None

def flash_factory_partition(bin_file, port=None, baud=460800, offset=None, partitions_csv=None, dry_run=False):
    if not os.path.exists(bin_file):
        print(f"[!] ERROR: Target binary file not found: {bin_file}")
        sys.exit(1)

    resolved_offset = None
    if offset:
        resolved_offset = int(offset, 16) if offset.startswith("0x") else int(offset)
    elif partitions_csv and os.path.exists(partitions_csv):
        resolved_offset = find_fctry_offset_from_csv(partitions_csv)
    else:
        # Check standard locations
        default_csv = os.path.join(os.path.dirname(__file__), "..", "..", "device_firmware", "6_project_optimize", "partitions.csv")
        if os.path.exists(default_csv):
            resolved_offset = find_fctry_offset_from_csv(default_csv)

    if resolved_offset is None:
        # Fallback to standard 4MB dual 1920KB OTA offset
        resolved_offset = 0x3E0000
        print(f"[*] Warning: Could not detect fctry offset from partitions.csv. Using standard offset 0x{resolved_offset:X}.")
    else:
        print(f"[+] Detected 'fctry' partition offset: 0x{resolved_offset:X} ({resolved_offset} bytes)")

    cmd = [
        sys.executable,
        "-m", "esptool",
    ]
    if port:
        cmd.extend(["-p", port])
    cmd.extend(["-b", str(baud), "write_flash", hex(resolved_offset), bin_file])

    print(f"[*] Factory Flash Command: {' '.join(cmd)}")
    print(f"[*] Target Binary: {bin_file} (Size: {os.path.getsize(bin_file)} bytes)")

    if dry_run or not port:
        print("[+] DRY-RUN MODE: Flash command verified without touching physical hardware.")
        return 0

    print("[*] Executing esptool.py flash...")
    result = subprocess.run(cmd)
    if result.returncode == 0:
        print(f"[+] Successfully flashed {bin_file} to offset 0x{resolved_offset:X} on port {port}!")
    else:
        print(f"[!] Flashing failed with exit code {result.returncode}")
    return result.returncode

def main():
    parser = argparse.ArgumentParser(description="Factory Data Flashing Tool for Smart Light (Chapter 14)")
    parser.add_argument("--device", help="Device serial number (e.g., LIGHT_2026_0001)")
    parser.add_argument("--bin", help="Explicit path to factory binary file")
    parser.add_argument("--port", help="Serial COM port (e.g., COM3, COM4, /dev/ttyUSB0)")
    parser.add_argument("--baud", type=int, default=460800, help="Flashing baud rate (default: 460800)")
    parser.add_argument("--offset", help="Explicit fctry offset in hex (e.g., 0x3e0000 or 0x340000)")
    parser.add_argument("--partitions-csv", help="Path to partitions.csv to auto-detect fctry offset")
    parser.add_argument("--dry-run", action="store_true", help="Simulate flash command without touching physical hardware")

    args = parser.parse_args()

    target_bin = args.bin
    if not target_bin and args.device:
        candidate_bins = glob.glob(os.path.join(os.path.dirname(__file__), "bin_outputs", "bin", f"*-{args.device}.bin"))
        if candidate_bins:
            target_bin = candidate_bins[0]
        else:
            print(f"[!] Could not find generated binary for device '{args.device}' in bin_outputs/bin/")
            sys.exit(1)

    if not target_bin:
        # Look for first available in bin_outputs
        candidate_bins = glob.glob(os.path.join(os.path.dirname(__file__), "bin_outputs", "bin", "*.bin"))
        if candidate_bins:
            target_bin = candidate_bins[0]
        else:
            print("[!] No binary specified and no pre-generated binaries found. Run generate_mass_mfg.py first.")
            sys.exit(1)

    sys.exit(flash_factory_partition(target_bin, args.port, args.baud, args.offset, args.partitions_csv, args.dry_run))

if __name__ == "__main__":
    main()
