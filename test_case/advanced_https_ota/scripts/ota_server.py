"""Prepare certificates, publish an ESP app image, and serve it over HTTPS."""
import argparse
from datetime import datetime, timedelta, timezone
from functools import partial
import http.server
import ipaddress
from pathlib import Path
import shutil
import ssl
import struct
import tempfile
from urllib.parse import urlsplit

DEFAULT_STATE = Path(__file__).resolve().parents[1] / ".local"


def prepare(state, host):
    from cryptography import x509
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import rsa
    from cryptography.x509.oid import ExtendedKeyUsageOID, NameOID

    try:
        san = x509.IPAddress(ipaddress.ip_address(host))
    except ValueError:
        if not host or any(c not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-" for c in host):
            raise ValueError("--host must be a PC IP address or DNS hostname, without scheme or port")
        san = x509.DNSName(host)

    certs = state / "certs"
    certs.mkdir(parents=True, exist_ok=True)
    ca_path, ca_key_path = certs / "ca_cert.pem", certs / "ca_key.pem"
    now = datetime.now(timezone.utc)

    def save_key(path, key):
        path.write_bytes(key.private_bytes(serialization.Encoding.PEM,
                                         serialization.PrivateFormat.PKCS8,
                                         serialization.NoEncryption()))

    if ca_path.exists() != ca_key_path.exists():
        raise ValueError("Incomplete CA pair in .local/certs; restore both CA files or regenerate and rebuild A/B")
    if ca_path.exists():
        ca = x509.load_pem_x509_certificate(ca_path.read_bytes())
        ca_key = serialization.load_pem_private_key(ca_key_path.read_bytes(), password=None)
        if ca.public_key().public_numbers() != ca_key.public_key().public_numbers():
            raise ValueError("CA certificate and key do not match")
    else:
        ca_key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
        name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "PBL5 Local OTA CA")])
        ca = (x509.CertificateBuilder().subject_name(name).issuer_name(name)
              .public_key(ca_key.public_key()).serial_number(x509.random_serial_number())
              .not_valid_before(now - timedelta(days=1)).not_valid_after(now + timedelta(days=3650))
              .add_extension(x509.BasicConstraints(ca=True, path_length=0), critical=True)
              .add_extension(x509.SubjectKeyIdentifier.from_public_key(ca_key.public_key()), critical=False)
              .add_extension(x509.AuthorityKeyIdentifier.from_issuer_public_key(ca_key.public_key()), critical=False)
              .add_extension(x509.KeyUsage(digital_signature=True, content_commitment=False,
                                          key_encipherment=False, data_encipherment=False,
                                          key_agreement=False, key_cert_sign=True, crl_sign=True,
                                          encipher_only=False, decipher_only=False), critical=True)
              .sign(ca_key, hashes.SHA256()))
        save_key(ca_key_path, ca_key)
        ca_path.write_bytes(ca.public_bytes(serialization.Encoding.PEM))

    server_key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    server = (x509.CertificateBuilder()
              .subject_name(x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, host)]))
              .issuer_name(ca.subject).public_key(server_key.public_key())
              .serial_number(x509.random_serial_number())
              .not_valid_before(now - timedelta(days=1)).not_valid_after(now + timedelta(days=365))
              .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
              .add_extension(x509.SubjectKeyIdentifier.from_public_key(server_key.public_key()), critical=False)
              .add_extension(x509.AuthorityKeyIdentifier.from_issuer_public_key(ca_key.public_key()), critical=False)
              .add_extension(x509.KeyUsage(digital_signature=True, content_commitment=False,
                                          key_encipherment=True, data_encipherment=False,
                                          key_agreement=False, key_cert_sign=False, crl_sign=False,
                                          encipher_only=False, decipher_only=False), critical=True)
              .add_extension(x509.SubjectAlternativeName([san]), critical=False)
              .add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH]), critical=False)
              .sign(ca_key, hashes.SHA256()))
    save_key(certs / "server_key.pem", server_key)
    (certs / "server_cert.pem").write_bytes(server.public_bytes(serialization.Encoding.PEM))
    print(f"Certificates ready for {host}; firmware trust anchor: {ca_path}")
    print("Existing CA is preserved, including when the PC IP changes.")


def publish(state, firmware):
    # Reject obvious wrong files (merged flash image, bootloader, HTML, etc.).
    # The ESP32 performs the actual chip compatibility and image validation.
    with firmware.open("rb") as source:
        header = source.read(288)
        if len(header) < 288 or header[0] != 0xE9 or struct.unpack_from("<I", header, 32)[0] != 0xABCD5432:
            raise ValueError("Expected an ESP application .bin, not a bootloader/partition/merged image")
    version = header[48:80].split(b"\0", 1)[0].decode("utf-8", errors="replace")
    project = header[80:112].split(b"\0", 1)[0].decode("utf-8", errors="replace")
    directory = state / "firmware"
    directory.mkdir(parents=True, exist_ok=True)
    # Copy outside firmware.bin first so a request never sees half a new image.
    with tempfile.NamedTemporaryFile(dir=directory, suffix=".tmp", delete=False) as target:
        temporary = Path(target.name)
    try:
        shutil.copyfile(firmware, temporary)
        temporary.replace(directory / "firmware.bin")
    finally:
        temporary.unlink(missing_ok=True)
    print(f"Published project={project} version={version} ({firmware.stat().st_size} bytes)")


class FirmwareHandler(http.server.SimpleHTTPRequestHandler):
    def do_GET(self):
        if urlsplit(self.path).path != "/firmware.bin":
            self.send_error(404, "Only /firmware.bin is served")
            return
        try:
            super().do_GET()
        except (BrokenPipeError, ConnectionResetError):
            # ESP32 closes after reading the descriptor of an unchanged image.
            pass

    def do_HEAD(self):
        if urlsplit(self.path).path != "/firmware.bin":
            self.send_error(404, "Only /firmware.bin is served")
            return
        super().do_HEAD()

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


def create_server(state, bind, port):
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(state / "certs/server_cert.pem", state / "certs/server_key.pem")
    handler = partial(FirmwareHandler, directory=str(state / "firmware"))
    server = http.server.ThreadingHTTPServer((bind, port), handler)
    server.daemon_threads = True
    server.socket = context.wrap_socket(server.socket, server_side=True)
    return server


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--state-dir", type=Path, default=DEFAULT_STATE)
    commands = parser.add_subparsers(dest="command", required=True)
    prepare_cmd = commands.add_parser("prepare", help="Create/reuse a local CA and issue a server certificate")
    prepare_cmd.add_argument("--host", required=True, help="PC LAN IPv4 address used in the firmware URL")
    publish_cmd = commands.add_parser("publish", help="Atomically publish an application firmware")
    publish_cmd.add_argument("firmware", type=Path)
    serve_cmd = commands.add_parser("serve", help="Run the HTTPS firmware server (Ctrl+C to stop)")
    serve_cmd.add_argument("--bind", default="0.0.0.0")
    serve_cmd.add_argument("--port", type=int, default=8070)
    args = parser.parse_args()
    try:
        if args.command == "prepare":
            prepare(args.state_dir, args.host)
        elif args.command == "publish":
            publish(args.state_dir, args.firmware)
        else:
            with create_server(args.state_dir, args.bind, args.port) as server:
                print(f"HTTPS listening on {args.bind}:{server.server_port}", flush=True)
                print("ESP32 URL: https://<PC-IP>:8070/firmware.bin (use the chosen port)", flush=True)
                print("Keep this terminal open. Publish B from another terminal.", flush=True)
                server.serve_forever()
    except KeyboardInterrupt:
        pass
    except (OSError, ValueError, ImportError) as error:
        parser.exit(1, f"Error: {error}\n")


if __name__ == "__main__":
    main()
