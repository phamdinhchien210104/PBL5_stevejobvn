#!/usr/bin/env python3
"""
PBL5 Smart Light - Local OTA Firmware Test Server (Chapter 11)
Serves binary firmware images locally over HTTP with full Range-request (HTTP 206) support,
MD5/SHA256 checksum validation, and step-by-step RainMaker OTA commands.
"""

import os
import sys
import socket
import hashlib
import argparse
from http import HTTPStatus
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler

class OTARequestHandler(SimpleHTTPRequestHandler):
    """HTTP Request Handler supporting Range headers (HTTP 206) for ESP-IDF OTA clients."""

    def log_message(self, format, *args):
        client_ip = self.client_address[0]
        sys.stdout.write(f"[{self.log_date_time_string()}] [{client_ip}] {format % args}\n")
        sys.stdout.flush()

    def send_head(self):
        """Common code for GET and HEAD commands with HTTP 206 Range support."""
        path = self.translate_path(self.path)
        f = None
        if os.path.isdir(path):
            return super().send_head()

        try:
            f = open(path, 'rb')
        except OSError:
            self.send_error(HTTPStatus.NOT_FOUND, "File not found")
            return None

        try:
            fs = os.fstat(f.fileno())
            file_len = fs.st_size
            range_header = self.headers.get('Range')

            if range_header and range_header.startswith('bytes='):
                # Parse Range: bytes=start-end
                range_spec = range_header[6:].split('-')
                start_str = range_spec[0].strip()
                end_str = range_spec[1].strip() if len(range_spec) > 1 else ''

                start = int(start_str) if start_str else 0
                end = int(end_str) if end_str else file_len - 1

                if start >= file_len or end >= file_len or start > end:
                    self.send_error(HTTPStatus.REQUESTED_RANGE_NOT_SATISFIABLE)
                    self.send_header('Content-Range', f'bytes */{file_len}')
                    self.end_headers()
                    f.close()
                    return None

                length = end - start + 1
                self.send_response(HTTPStatus.PARTIAL_CONTENT)
                self.send_header('Content-Type', self.guess_type(path))
                self.send_header('Content-Range', f'bytes {start}-{end}/{file_len}')
                self.send_header('Content-Length', str(length))
                self.send_header('Accept-Ranges', 'bytes')
                self.send_header('Last-Modified', self.date_time_string(fs.st_mtime))
                self.end_headers()

                f.seek(start)
                return f
            else:
                self.send_response(HTTPStatus.OK)
                self.send_header('Content-Type', self.guess_type(path))
                self.send_header('Content-Length', str(file_len))
                self.send_header('Accept-Ranges', 'bytes')
                self.send_header('Last-Modified', self.date_time_string(fs.st_mtime))
                self.end_headers()
                return f
        except Exception:
            if f:
                f.close()
            raise


def get_local_ip_addresses():
    """Retrieve all available IPv4 addresses on the host system."""
    ip_list = []
    try:
        # Connect to an external address to identify the default routing interface
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.settimeout(0.5)
        s.connect(("8.8.8.8", 80))
        primary_ip = s.getsockname()[0]
        s.close()
        ip_list.append(primary_ip)
    except Exception:
        pass

    try:
        hostname = socket.gethostname()
        for ip in socket.gethostbyname_ex(hostname)[2]:
            if not ip.startswith("127.") and ip not in ip_list:
                ip_list.append(ip)
    except Exception:
        pass

    if not ip_list:
        ip_list.append("127.0.0.1")
    return ip_list


def compute_hashes(file_path):
    """Compute MD5 and SHA256 hashes of a file."""
    md5 = hashlib.md5()
    sha256 = hashlib.sha256()
    with open(file_path, "rb") as f:
        while chunk := f.read(65536):
            md5.update(chunk)
            sha256.update(chunk)
    return md5.hexdigest(), sha256.hexdigest()


def main():
    parser = argparse.ArgumentParser(description="PBL5 Local OTA Test Server (Chapter 11)")
    parser.add_argument("--port", type=int, default=8070, help="HTTP server port (default: 8070)")
    parser.add_argument("--dir", type=str, default=".", help="Directory to serve binaries from (default: current directory)")
    parser.add_argument("--bin", type=str, default=None, help="Path to specific .bin firmware file")
    args = parser.parse_args()

    serve_dir = os.path.abspath(args.dir)
    if not os.path.isdir(serve_dir):
        print(f"Error: Directory '{serve_dir}' does not exist.")
        sys.exit(1)

    os.chdir(serve_dir)
    ips = get_local_ip_addresses()

    print("=" * 70)
    print("        PBL5 SMART LIGHT - LOCAL OTA TEST SERVER (CHAPTER 11)        ")
    print("=" * 70)
    print(f"Serving Directory: {serve_dir}")
    print(f"Listening Port   : {args.port}")
    print("\nAvailable Host IP Addresses:")
    for ip in ips:
        print(f"  - http://{ip}:{args.port}/")

    # Scan for binary firmware files
    bin_files = []
    if args.bin and os.path.isfile(args.bin):
        bin_files.append(os.path.basename(args.bin))
    else:
        for f in os.listdir(serve_dir):
            if f.endswith(".bin"):
                bin_files.append(f)

    if bin_files:
        print("\nAvailable Firmware Binaries in Directory:")
        for bf in bin_files:
            bpath = os.path.join(serve_dir, bf)
            size_kb = os.path.getsize(bpath) / 1024.0
            md5_val, sha_val = compute_hashes(bpath)
            print(f"\n  [Binary] {bf} ({size_kb:.1f} KB)")
            print(f"    MD5   : {md5_val}")
            print(f"    SHA256: {sha_val}")
            for ip in ips:
                print(f"    URL   : http://{ip}:{args.port}/{bf}")
    else:
        print("\nNote: No .bin files found in serving directory yet.")
        print("Build your firmware first via 'idf.py build' to generate the binary.")

    print("\n" + "-" * 70)
    print("OTA Upgrade Test Commands:")
    if bin_files:
        sample_bin = bin_files[0]
        sample_url = f"http://{ips[0]}:{args.port}/{sample_bin}"
        print(f"1. RainMaker CLI Upgrade:")
        print(f"   python rainmaker.py otaupgrade <NODE_ID> {sample_url}")
        print(f"2. Verify URL accessibility via curl:")
        print(f"   curl -I {sample_url}")
    print("-" * 70)
    print("Server running... Press Ctrl+C to stop.\n")

    server_address = ("0.0.0.0", args.port)
    httpd = ThreadingHTTPServer(server_address, OTARequestHandler)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nServer shutting down.")
        httpd.server_close()


if __name__ == "__main__":
    main()
