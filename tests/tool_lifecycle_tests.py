"""Exercise real wrapper cancellation using fake tools; no network traffic."""
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time

source = Path(sys.argv[1]) / "firmware/rootfs-overlay/usr/sbin"
with tempfile.TemporaryDirectory(prefix="sightline-tools-") as directory:
    root = Path(directory)
    fake = root / "bin"
    fake.mkdir()
    code = '''#!{python}
import os, signal, sys, time
from pathlib import Path
signal.signal(signal.SIGINT, lambda *_: sys.exit(0))
signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))
Path(os.environ['TOOL_PID']).write_text(str(os.getpid()))
if os.environ.get('TOOL_MODE') == 'fail': sys.exit(7)
if Path(sys.argv[0]).name == 'tcpdump':
    Path(sys.argv[sys.argv.index('-w') + 1]).write_bytes(b'pcap fixture')
else:
    Path(sys.argv[sys.argv.index('-oX') + 1]).write_text('<nmaprun/>')
    print('test scan output ' + ' '.join(sys.argv[1:]), flush=True)
    if os.environ.get('TOOL_MODE') == 'success': sys.exit(0)
while True: time.sleep(0.05)
'''.format(python=sys.executable)
    for name in ("tcpdump", "nmap"):
        tool = fake / name
        tool.write_text(code)
        tool.chmod(0o755)
    def run(name, args, mode, cancel=False):
        evidence = root / f"{name}-{args[1] if name.endswith('nmap') else args[2]}-{mode}-{cancel}"
        pid_file = root / "child.pid"
        pid_file.unlink(missing_ok=True)
        env = dict(os.environ, PATH=f"{fake}:{os.environ['PATH']}",
                   RECONCLAVE_EVIDENCE_DIR=str(evidence), TOOL_PID=str(pid_file), TOOL_MODE=mode)
        proc = subprocess.Popen(["sh", str(source / name), *args], env=env,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            if cancel:
                deadline = time.monotonic() + 5
                while not pid_file.exists() and time.monotonic() < deadline:
                    time.sleep(0.02)
                assert pid_file.exists(), "fake tool did not start"
                time.sleep(0.1)
                proc.send_signal(signal.SIGTERM)
            stdout, stderr = proc.communicate(timeout=10)
            expected = 130 if cancel else (0 if mode == "success" else 1 if name.endswith("capture") else 7)
            assert proc.returncode == expected, (name, proc.returncode, stdout, stderr)
            child = int(pid_file.read_text())
            try:
                os.kill(child, 0)
            except ProcessLookupError:
                pass
            else:
                raise AssertionError("tool child survived wrapper completion")
            assert not list(evidence.glob("*.partial"))
            if cancel and name.endswith("capture"):
                assert len(list(evidence.glob("*.pcap"))) == 1, "early-stop capture should be retained"
            if name.endswith("nmap"):
                reports = list(evidence.glob("*.txt"))
                assert len(reports) == 1
                assert f"Exit status: {expected}" in reports[0].read_text()
                if len(args) > 1 and args[1] == "nse":
                    assert "--script http-title,http-security-headers,ssl-cert,ssh-hostkey,smb2-security-mode" in reports[0].read_text()
                if len(args) > 1 and args[1] == "deep":
                    assert "-p-" in reports[0].read_text()
                if mode != "fail":
                    xml = list(evidence.glob("*.xml"))
                    assert len(xml) == 1
                    assert Path(str(xml[0]) + ".ready").read_text().strip() == str(expected)
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait()
            if pid_file.exists():
                try: os.kill(int(pid_file.read_text()), signal.SIGKILL)
                except ProcessLookupError: pass
    run("reconclave-capture", ["lo", "30", "dns"], "wait", True)
    run("reconclave-capture", ["lo", "1", "dns"], "success")
    run("reconclave-capture", ["lo", "1", "dns"], "fail")
    run("reconclave-capture", ["lo", "1", "passive"], "success")
    run("reconclave-nmap", ["192.0.2.1", "discovery"], "wait", True)
    run("reconclave-nmap", ["192.0.2.1", "discovery"], "success")
    run("reconclave-nmap", ["192.0.2.1", "discovery"], "fail")
    run("reconclave-nmap", ["192.0.2.1", "nse", "80,443,445"], "success")
    run("reconclave-nmap", ["192.0.2.1", "deep"], "success")
    for target in ("192.0.2.0/23", "192.0.2.0/33"):
        rejected = subprocess.run(["sh", str(source / "reconclave-nmap"), target, "discovery"],
                                  capture_output=True, text=True, timeout=2)
        assert rejected.returncode == 2 and "must be /24 or smaller" in rejected.stderr, rejected
    rejected = subprocess.run(["sh", str(source / "reconclave-nmap"), "192.0.2.1", "discovery"],
                              env=dict(os.environ, RECONCLAVE_SCAN_INTERFACE="unexpected0"),
                              capture_output=True, text=True, timeout=2)
    assert rejected.returncode == 2 and "must be eth0 or wlan0" in rejected.stderr, rejected
    for args in (["192.0.2.0/24", "focused", "22"], ["192.0.2.1", "focused", "0"],
                 ["192.0.2.1", "focused", "22,abc"], ["192.0.2.0/24", "nse", "445"],
                 ["192.0.2.1", "nse", "445;id"], ["192.0.2.0/24", "deep"],
                 ["192.0.2.1", "deep", "22"]):
        rejected = subprocess.run(["sh", str(source / "reconclave-nmap"), *args],
                                  capture_output=True, text=True, timeout=2)
        assert rejected.returncode == 2, rejected
print("Tool completion, failure, cancellation, artifact publication and child cleanup passed")
