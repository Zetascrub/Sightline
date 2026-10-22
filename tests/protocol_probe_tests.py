"""Exercise bounded protocol checks against local fixtures only."""
from http.server import BaseHTTPRequestHandler, HTTPServer
import ipaddress
from pathlib import Path
import runpy
import socket
import ssl
import struct
import subprocess
import sys
import tempfile
import threading

module = runpy.run_path(str(Path(sys.argv[1]) / "firmware/rootfs-overlay/usr/sbin/reconclave-enumerate"))


class HeadHandler(BaseHTTPRequestHandler):
    def do_HEAD(self):
        self.send_response(401)
        self.send_header("WWW-Authenticate", 'Basic realm="fixture"')
        self.send_header("Strict-Transport-Security", "max-age=31536000")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Location", "http://not-followed.invalid/")
        self.end_headers()

    def log_message(self, *_args):
        pass


http = HTTPServer(("127.0.0.1", 0), HeadHandler)
threading.Thread(target=http.handle_request, daemon=True).start()
result = module["http_probe"]("127.0.0.1", http.server_port, False, "", "")
http.server_close()
assert "401" in result and "www-authenticate" in result and "not-followed" not in result, result
assert "strict-transport-security: max-age=31536000" in result and "x-content-type-options: nosniff" in result, result

ssh = socket.socket()
ssh.bind(("127.0.0.1", 0))
ssh.listen(1)


def serve_ssh():
    connection, _ = ssh.accept()
    with connection:
        connection.sendall(b"SSH-2.0-Fixture_1.0\r\n")


threading.Thread(target=serve_ssh, daemon=True).start()
result = module["ssh_probe"]("127.0.0.1", ssh.getsockname()[1], "", "")
ssh.close()
assert result == "SSH-2.0-Fixture_1.0", result

smb = socket.socket()
smb.bind(("127.0.0.1", 0))
smb.listen(1)


def serve_smb():
    connection, _ = smb.accept()
    with connection:
        request = connection.recv(256)
        assert request[4:8] == b"\xfeSMB" and struct.unpack_from("<H", request, 68)[0] == 36, request
        response = bytearray(72)
        response[:4] = b"\xfeSMB"
        struct.pack_into("<H", response, 4, 64)
        struct.pack_into("<H", response, 64, 65)
        struct.pack_into("<H", response, 66, 1)
        struct.pack_into("<H", response, 68, 0x0302)
        connection.sendall(len(response).to_bytes(4, "big") + response)


threading.Thread(target=serve_smb, daemon=True).start()
result = module["smb2_probe"]("127.0.0.1", smb.getsockname()[1], "", "")
smb.close()
assert result == "SMB 3.0.2; signing enabled, not required", result

with tempfile.TemporaryDirectory(prefix="sightline-tls-") as directory:
    key = Path(directory) / "key.pem"
    cert = Path(directory) / "cert.pem"
    subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "2",
                    "-subj", "/CN=sightline-fixture", "-keyout", str(key), "-out", str(cert)],
                   check=True, capture_output=True)
    details = module["certificate_details"](ssl.PEM_cert_to_DER_cert(cert.read_text()))
    assert any("sightline-fixture" in line for line in details), details
    assert any("notAfter=" in line for line in details), details

dns = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
dns.bind(("127.0.0.1", 0))


def serve_dns():
    packet, peer = dns.recvfrom(2048)
    dns.sendto(packet[:2] + b"\x84\x03\x00\x01\x00\x00\x00\x00\x00\x00" + packet[12:], peer)


threading.Thread(target=serve_dns, daemon=True).start()
original_socket = module["bound_socket"]


def local_dns_socket(kind, interface, source_ip):
    connection = original_socket(kind, interface, source_ip)
    if kind == socket.SOCK_DGRAM:
        original_sendto = connection.sendto
        # The fixture's DNS socket listens on an ephemeral port; isolate the test from port 53.
        class Redirect:
            def __getattr__(self, key):
                return getattr(connection, key)

            def sendto(self, packet, _peer):
                return original_sendto(packet, dns.getsockname())

        return Redirect()
    return connection


module["dns_probe"].__globals__["bound_socket"] = local_dns_socket
result = module["dns_probe"]("127.0.0.1", ipaddress.ip_network("127.0.0.0/24"), "", "")
dns.close()
assert "NXDOMAIN" in result and "authoritative=True" in result, result

plan = module["probe_plan"]({"127.0.0.1": [22, 1234, 443, 445]}, {"127.0.0.1": ([53], [])},
                             {"127.0.0.1": {1234: ("http", "")}})
assert plan == [("127.0.0.1", 22, "ssh"), ("127.0.0.1", 1234, "http"),
                ("127.0.0.1", 443, "https"), ("127.0.0.1", 445, "smb2"),
                ("127.0.0.1", 53, "dns")], plan
nse_plan = module["nse_plan"]({"127.0.0.2": [80, 445, 9999], "127.0.0.1": [22, 1234]},
                              {"127.0.0.1": {1234: ("http", "")}})
assert nse_plan == [("127.0.0.2", [80, 445]), ("127.0.0.1", [22, 1234])], nse_plan
with tempfile.TemporaryDirectory(prefix="sightline-nse-") as directory:
    report = Path(directory) / "nse.txt"
    report.with_suffix(".xml").write_text(
        '<nmaprun><host><hostscript><script id="smb2-security-mode" output="signing optional"/>'
        '</hostscript><ports><port portid="80" protocol="tcp"><script id="http-title" output="Example"/>'
        '</port></ports></host></nmaprun>')
    report.write_text("Bug in http-security-headers: no string output.\n")
    assert module["nse_notes"](report) == ["host smb2-security-mode: signing optional", "80/tcp http-title: Example"]
    assert module["nse_errors"](report) == ["Bug in http-security-headers: no string output."]
print("Read-only HTTP, SMB2, SSH, DNS, NSE parsing and service routing passed")
