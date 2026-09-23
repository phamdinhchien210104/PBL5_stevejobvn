#!/usr/bin/env python3
"""
Device Credentials Generator for ESP32 Smart Light Mass Manufacturing (Chapter 14)

Generates test RSA/ECC device private keys and self-signed X.509 device certificates,
and outputs a ready-to-use mass_mfg_values.csv table for NVS Partition Generation.
"""

import os
import sys
import argparse
import datetime
import csv
from cryptography import x509
from cryptography.x509.oid import NameOID
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.hazmat.primitives import serialization

def generate_device_credentials(count=5, prefix="LIGHT_2026", base_mac="84f703000000", output_dir="."):
    keys_dir = os.path.join(output_dir, "keys")
    certs_dir = os.path.join(output_dir, "certs")
    os.makedirs(keys_dir, exist_ok=True)
    os.makedirs(certs_dir, exist_ok=True)

    base_mac_int = int(base_mac, 16)
    values_csv_path = os.path.join(output_dir, "mass_mfg_values.csv")

    rows = []
    print(f"[*] Generating credentials for {count} devices with prefix '{prefix}'...")

    for i in range(1, count + 1):
        serial_no = f"{prefix}_{i:04d}"
        mac_addr = f"{base_mac_int + i:012x}"
        key_filename = f"device_{i:02d}.key"
        cert_filename = f"device_{i:02d}.crt"
        key_rel_path = f"keys/{key_filename}"
        cert_rel_path = f"certs/{cert_filename}"
        key_full_path = os.path.join(keys_dir, key_filename)
        cert_full_path = os.path.join(certs_dir, cert_filename)

        # Generate RSA private key (2048-bit)
        private_key = rsa.generate_private_key(
            public_exponent=65537,
            key_size=2048
        )

        with open(key_full_path, "wb") as f:
            f.write(private_key.private_bytes(
                encoding=serialization.Encoding.PEM,
                format=serialization.PrivateFormat.TraditionalOpenSSL,
                encryption_algorithm=serialization.NoEncryption()
            ))

        # Generate self-signed X.509 certificate for device
        subject = issuer = x509.Name([
            x509.NameAttribute(NameOID.COUNTRY_NAME, "VN"),
            x509.NameAttribute(NameOID.STATE_OR_PROVINCE_NAME, "Da Nang"),
            x509.NameAttribute(NameOID.ORGANIZATION_NAME, "PBL5 Smart Light"),
            x509.NameAttribute(NameOID.COMMON_NAME, serial_no),
        ])

        cert = (
            x509.CertificateBuilder()
            .subject_name(subject)
            .issuer_name(issuer)
            .public_key(private_key.public_key())
            .serial_number(x509.random_serial_number())
            .not_valid_before(datetime.datetime.now(datetime.timezone.utc))
            .not_valid_after(datetime.datetime.now(datetime.timezone.utc) + datetime.timedelta(days=3650))
            .sign(private_key, hashes.SHA256())
        )

        with open(cert_full_path, "wb") as f:
            f.write(cert.public_bytes(serialization.Encoding.PEM))

        rows.append({
            "serial_no": serial_no,
            "mac_addr": mac_addr,
            "priv_key": key_rel_path,
            "node_cert": cert_rel_path
        })
        print(f"  + Device {i:02d}: Serial={serial_no} MAC={mac_addr} Key={key_rel_path} Cert={cert_rel_path}")

    with open(values_csv_path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=["serial_no", "mac_addr", "priv_key", "node_cert"])
        writer.writeheader()
        writer.writerows(rows)

    print(f"[+] Successfully wrote {len(rows)} devices to: {values_csv_path}")
    return values_csv_path

def main():
    parser = argparse.ArgumentParser(description="Generate device credentials and values CSV for mass production")
    parser.add_argument("--count", type=int, default=5, help="Number of devices to generate (default: 5)")
    parser.add_argument("--prefix", type=str, default="LIGHT_2026", help="Serial number prefix (default: LIGHT_2026)")
    parser.add_argument("--base-mac", type=str, default="84f703000000", help="Base MAC address (hex, default: 84f703000000)")
    parser.add_argument("--output-dir", type=str, default=os.path.dirname(os.path.abspath(__file__)), help="Output directory")

    args = parser.parse_args()
    generate_device_credentials(args.count, args.prefix, args.base_mac, args.output_dir)

if __name__ == "__main__":
    main()
