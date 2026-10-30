"""Host-side integration checks; no ESP32 or legacy tiny_test_fw required."""
import http.client
from pathlib import Path
import socket
import ssl
import struct
import tempfile
import threading
import unittest

from ota_server import create_server, prepare, publish


class HttpsOtaServerTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.state = Path(self.temporary.name)
        prepare(self.state, "127.0.0.1")

    def image(self, version):
        # Synthetic descriptor fixture for transport/publishing checks, not a
        # flashable firmware or a substitute for ESP32 image validation.
        data = bytearray(1024)
        data[0] = 0xE9
        struct.pack_into("<I", data, 32, 0xABCD5432)
        data[48:48 + len(version)] = version.encode()
        data[80:98] = b"advanced_https_ota"
        path = self.state / f"{version}.bin"
        path.write_bytes(data)
        return path

    def server(self):
        server = create_server(self.state, "127.0.0.1", 0)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()

        def stop():
            server.shutdown()
            server.server_close()
            thread.join()

        self.addCleanup(stop)
        return server

    def test_verified_https_serves_new_image_without_restarting(self):
        first, second = self.image("1.0.0"), self.image("1.1.0")
        publish(self.state, first)
        server = self.server()
        context = ssl.create_default_context(cafile=str(self.state / "certs/ca_cert.pem"))
        for image in (first, second):
            publish(self.state, image)
            connection = http.client.HTTPSConnection("127.0.0.1", server.server_port, context=context)
            try:
                connection.request("GET", "/firmware.bin")
                response = connection.getresponse()
                self.assertEqual(response.status, 200)
                self.assertEqual(int(response.getheader("Content-Length")), image.stat().st_size)
                self.assertEqual(response.read(), image.read_bytes())
            finally:
                connection.close()

    def test_certificate_hostname_and_trust_are_verified(self):
        server = self.server()
        context = ssl.create_default_context(cafile=str(self.state / "certs/ca_cert.pem"))
        with socket.create_connection(server.server_address) as connection:
            with self.assertRaises(ssl.SSLCertVerificationError):
                context.wrap_socket(connection, server_hostname="wrong-host.example")
        with socket.create_connection(server.server_address) as connection:
            with self.assertRaises(ssl.SSLCertVerificationError):
                ssl.create_default_context().wrap_socket(connection, server_hostname="127.0.0.1")

    def test_private_files_are_not_served(self):
        server = self.server()
        context = ssl.create_default_context(cafile=str(self.state / "certs/ca_cert.pem"))
        for path in ("/", "/certs/ca_key.pem", "/../certs/server_key.pem"):
            connection = http.client.HTTPSConnection("127.0.0.1", server.server_port, context=context)
            try:
                connection.request("GET", path)
                response = connection.getresponse()
                self.assertEqual(response.status, 404)
                response.read()
            finally:
                connection.close()

    def test_invalid_publish_preserves_previous_image_and_ca_is_reused(self):
        image = self.image("1.0.0")
        publish(self.state, image)
        bad_image = self.state / "bad.bin"
        bad_image.write_bytes(b"not firmware")
        with self.assertRaises(ValueError):
            publish(self.state, bad_image)
        self.assertEqual((self.state / "firmware/firmware.bin").read_bytes(), image.read_bytes())
        ca = (self.state / "certs/ca_cert.pem").read_bytes()
        prepare(self.state, "localhost")
        self.assertEqual((self.state / "certs/ca_cert.pem").read_bytes(), ca)


if __name__ == "__main__":
    unittest.main()
