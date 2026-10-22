"""Check receive-only PCAP parsing and conservative host inference."""
import ipaddress
from pathlib import Path
import runpy
import struct
import sys
import tempfile

module = runpy.run_path(str(Path(sys.argv[1]) / "firmware/rootfs-overlay/usr/sbin/reconclave-passive"))
mac_a = bytes.fromhex("02aabbccddee")
mac_b = bytes.fromhex("021122334455")
broadcast = b"\xff" * 6


def arp(address, mac):
    sender = ipaddress.IPv4Address(address).packed
    payload = b"\x00\x01\x08\x00\x06\x04\x00\x01" + mac + sender + b"\0" * 6 + b"\0" * 4
    return broadcast + mac + b"\x08\x06" + payload


def ipv4(address, mac, destination=broadcast):
    header = b"\x45\x00" + b"\x00\x14" + b"\0" * 8 + ipaddress.IPv4Address(address).packed + b"\xff\xff\xff\xff"
    return destination + mac + b"\x08\x00" + header


frames = [arp("192.168.8.5", mac_a), ipv4("192.168.8.6", mac_b),
          arp("192.168.9.4", mac_a), ipv4("192.168.8.7", mac_b, mac_a),
          arp("192.168.8.140", mac_a), arp("192.168.8.143", mac_b)]
pcap = struct.pack("<IHHIIII", 0xA1B2C3D4, 2, 4, 0, 0, 256, 1)
for frame in frames:
    pcap += struct.pack("<IIII", 0, 0, len(frame), len(frame)) + frame
with tempfile.TemporaryDirectory(prefix="sightline-passive-") as directory:
    path = Path(directory) / "sample.pcap"
    path.write_bytes(pcap)
    hosts, packets = module["read_pcap"](path, ipaddress.ip_network("192.168.8.0/24"),
                                          {ipaddress.ip_address("192.168.8.140"), ipaddress.ip_address("192.168.8.143")})
    assert packets == 6 and [host["address"] for host in hosts] == ["192.168.8.5", "192.168.8.6"], hosts
    assert hosts[0]["signals"] == ["arp"] and hosts[1]["signals"] == ["broadcast-or-multicast-ipv4"], hosts
    def udp_frame(source, destination, src_port, dst_port, payload, destination_mac=broadcast):
        udp_length = 8 + len(payload)
        ip_length = 20 + udp_length
        header = (b"\x45\x00" + struct.pack("!H", ip_length) + b"\0\0\0\0\x40\x11\0\0" +
                  ipaddress.IPv4Address(source).packed + ipaddress.IPv4Address(destination).packed)
        udp = struct.pack("!HHHH", src_port, dst_port, udp_length, 0) + payload
        return destination_mac + mac_a + b"\x08\x00" + header + udp

    question = b"\0" * 12 + b"\x04host\x05local\0\0\x01\0\x01"
    dhcp = bytearray(240)
    dhcp[236:240] = b"\x63\x82\x53\x63"
    dhcp.extend(b"\x0c\x07printer\x32\x04" + ipaddress.IPv4Address("192.168.8.10").packed + b"\xff")
    examples = [udp_frame("192.168.8.9", "224.0.0.251", 5353, 5353, question),
                udp_frame("192.168.8.9", "224.0.0.252", 5355, 5355, question),
                udp_frame("192.168.8.9", "192.168.8.255", 137, 137, question),
                udp_frame("192.168.8.9", "192.168.8.1", 53000, 53, question, mac_b),
                udp_frame("0.0.0.0", "255.255.255.255", 68, 67, bytes(dhcp))]
    enriched = struct.pack("<IHHIIII", 0xA1B2C3D4, 2, 4, 0, 0, 1500, 1)
    for frame in examples:
        enriched += struct.pack("<IIII", 0, 0, len(frame), len(frame)) + frame
    path.write_bytes(enriched)
    hosts, packets = module["read_pcap"](path, ipaddress.ip_network("192.168.8.0/24"), set())
    assert packets == 5 and [host["address"] for host in hosts] == ["192.168.8.9", "192.168.8.10"], hosts
    assert hosts[0]["protocols"] == ["dns", "llmnr", "mdns", "nbns"] and hosts[0]["names"] == ["host.local"], hosts
    assert hosts[1]["protocols"] == ["dhcp"] and hosts[1]["names"] == ["printer"], hosts
print("Passive ARP/broadcast parsing, scope and own-host exclusion passed")
