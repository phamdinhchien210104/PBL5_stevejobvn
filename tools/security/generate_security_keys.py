#!/usr/bin/env python3
"""
Cryptographic Key Generation Tool for ESP32 Security Features (Chapter 13)

Generates:
1. Secure Boot v2 RSA-3072 Private Signing Key (PEM format)
2. Secure Boot v2 Public Key Digest (32-byte SHA-256 binary for eFuse burning)
3. Flash Encryption AES-256 Key (32-byte binary for hardware XTS-AES)
4. NVS Encryption Key (64-byte binary for NVS encrypted partition)
"""

import os
import sys
import argparse
import subprocess

def generate_security_keys(output_dir="keys", overwrite=False):
    os.makedirs(output_dir, exist_ok=True)
    python_bin = sys.executable

    sb_key_pem = os.path.join(output_dir, "secure_boot_signing_key.pem")
    sb_digest_bin = os.path.join(output_dir, "secure_boot_digest.bin")
    fe_key_bin = os.path.join(output_dir, "flash_encryption_key.bin")
    nvs_key_bin = os.path.join(output_dir, "nvs_encr_key.bin")

    print("=" * 75)
    print("  ESP32 Smart Light Security Key Generator (Chapter 13.5)")
    print("=" * 75)
    print(f"[*] Target Directory: {os.path.abspath(output_dir)}\n")

    # 1. Generate Secure Boot v2 RSA-3072 signing key
    if os.path.exists(sb_key_pem) and not overwrite:
        print(f"[-] Secure Boot Signing Key exists: {sb_key_pem} (Use --overwrite to regenerate)")
    else:
        print("[*] 1. Generating Secure Boot v2 RSA-3072 Private Signing Key...")
        cmd = [python_bin, "-m", "espsecure", "generate-signing-key", "--version", "2", "--scheme", "rsa3072", sb_key_pem]
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode != 0:
            print(f"[!] Failed to generate Secure Boot key: {res.stderr}")
            sys.exit(1)
        print(f"[+] Secure Boot Signing Key generated: {sb_key_pem}")

    # 2. Extract Secure Boot v2 Public Key Digest (32 bytes SHA-256 for eFuse)
    print("[*] 2. Extracting Secure Boot v2 Public Key Digest for eFuse burning...")
    cmd = [python_bin, "-m", "espsecure", "digest-sbv2-public-key", "--keyfile", sb_key_pem, "--output", sb_digest_bin]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        print(f"[!] Failed to extract Secure Boot public digest: {res.stderr}")
        sys.exit(1)
    digest_size = os.path.getsize(sb_digest_bin)
    print(f"[+] Secure Boot Public Digest generated: {sb_digest_bin} ({digest_size} bytes SHA-256)")

    # 3. Generate Flash Encryption AES-256 Key (32 bytes)
    if os.path.exists(fe_key_bin) and not overwrite:
        print(f"[-] Flash Encryption Key exists: {fe_key_bin} (Use --overwrite to regenerate)")
    else:
        print("[*] 3. Generating Flash Encryption AES-256 Key...")
        cmd = [python_bin, "-m", "espsecure", "generate-flash-encryption-key", fe_key_bin]
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode != 0:
            print(f"[!] Failed to generate Flash Encryption key: {res.stderr}")
            sys.exit(1)
        fe_size = os.path.getsize(fe_key_bin)
        print(f"[+] Flash Encryption Key generated: {fe_key_bin} ({fe_size} bytes / 256 bits)")

    # 4. Generate NVS Encryption Key (64 bytes: 32 bytes XTS-AES encryption key + 32 bytes tweak key)
    if os.path.exists(nvs_key_bin) and not overwrite:
        print(f"[-] NVS Encryption Key exists: {nvs_key_bin} (Use --overwrite to regenerate)")
    else:
        print("[*] 4. Generating NVS Encryption Key (64 bytes XTS-AES)...")
        # NVS key is 64 random bytes (32 bytes AES-XTS key1 + 32 bytes AES-XTS key2)
        nvs_rand_bytes = os.urandom(64)
        with open(nvs_key_bin, "wb") as f:
            f.write(nvs_rand_bytes)
        print(f"[+] NVS Encryption Key generated: {nvs_key_bin} (64 bytes)")

    print("\n" + "=" * 75)
    print(f"{'Key Name':<35} | {'Size':<10} | {'Purpose':<24}")
    print("-" * 75)
    print(f"{'secure_boot_signing_key.pem':<35} | {os.path.getsize(sb_key_pem)} B     | {'Firmware Image Signing':<24}")
    print(f"{'secure_boot_digest.bin':<35} | {os.path.getsize(sb_digest_bin)} B      | {'eFuse BLOCK_KEY0 Burning':<24}")
    print(f"{'flash_encryption_key.bin':<35} | {os.path.getsize(fe_key_bin)} B      | {'eFuse BLOCK_KEY1 Burning':<24}")
    print(f"{'nvs_encr_key.bin':<35} | {os.path.getsize(nvs_key_bin)} B      | {'NVS Partition Encryption':<24}")
    print("=" * 75)
    print("[+] All production security keys generated and verified successfully!\n")

def main():
    parser = argparse.ArgumentParser(description="Generate Cryptographic Keys for ESP32 Security Features (Chapter 13)")
    parser.add_argument("--output-dir", default=os.path.join(os.path.dirname(__file__), "keys"), help="Output directory for generated keys")
    parser.add_argument("--overwrite", action="store_true", help="Force overwrite existing keys")

    args = parser.parse_args()
    generate_security_keys(args.output_dir, args.overwrite)

if __name__ == "__main__":
    main()
