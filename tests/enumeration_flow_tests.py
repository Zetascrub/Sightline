"""Verify staged enumeration, scope checks and evidence without network traffic."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

source = Path(sys.argv[1]) / "firmware/rootfs-overlay/usr/sbin"
with tempfile.TemporaryDirectory(prefix="sightline-enumeration-") as directory:
    root = Path(directory)
    fake = root / "bin"
    fake.mkdir()
    ip = fake / "ip"
    ip.write_text("#!/bin/sh\nprintf '2: eth0 inet 192.168.8.140/24 brd 192.168.8.255 scope global eth0\\n'\n")
    ip.chmod(0o755)
    wrapper = fake / "reconclave-nmap"
    wrapper.write_text("""#!{python}
import os, pathlib, sys
target, preset = sys.argv[1:3]
evidence = pathlib.Path(os.environ['RECONCLAVE_EVIDENCE_DIR'])
evidence.mkdir(exist_ok=True)
path = evidence / ('nmap-' + preset + '-' + target.replace('/', '_') + '.txt')
status = 7 if os.environ.get('TEST_FAIL_PRESET') == preset else 0
path.write_text('Exit status: ' + str(status) + ' (0 complete)\\n' +
                ('Skipping host ' + target + ' due to host timeout\\n'
                 if preset == 'focused' and os.environ.get('TEST_HOST_TIMEOUT') == target else ''))
if preset == 'discovery':
    xml = '<nmaprun><host><status state="up"/><address addrtype="ipv4" addr="192.168.8.2"/></host><host><status state="up"/><address addrtype="ipv4" addr="192.168.8.3"/></host></nmaprun>'
elif preset in ('quick', 'expanded'):
    xml = '<nmaprun><host><address addrtype="ipv4" addr="192.168.8.2"/><ports><port protocol="tcp" portid="139"><state state="open"/></port><port protocol="tcp" portid="1234"><state state="open"/></port><port protocol="tcp" portid="5678"><state state="open"/></port></ports></host><host><address addrtype="ipv4" addr="192.168.8.3"/><ports><port protocol="tcp" portid="9090"><state state="open"/></port></ports></host></nmaprun>'
elif preset == 'udp':
    xml = '<nmaprun><host><address addrtype="ipv4" addr="192.168.8.2"/><ports><port protocol="udp" portid="161"><state state="open"/></port></ports></host></nmaprun>'
elif preset == 'focused' and target == '192.168.8.2':
    xml = '<nmaprun><host><ports><port protocol="tcp" portid="1234"><state state="open"/><service name="custom" product="Example"/></port></ports></host></nmaprun>'
elif preset == 'focused' and target == '192.168.8.3':
    xml = '<nmaprun><host><ports><port protocol="tcp" portid="9090"><state state="open"/><service name="irc" method="table" conf="3"/></port></ports></host></nmaprun>'
elif preset == 'nse':
    xml = '<nmaprun><host><hostscript><script id="smb2-security-mode" output="signing required"/></hostscript></host></nmaprun>'
elif preset == 'deep':
    xml = '<nmaprun><host><address addrtype="ipv4" addr="' + target + '"/><ports><port protocol="tcp" portid="50001"><state state="open"/></port></ports></host></nmaprun>'
else:
    xml = '<nmaprun/>'
path.with_suffix('.xml').write_text(xml)
with (evidence / 'calls').open('a') as calls: calls.write(' '.join(sys.argv[1:]) + '\\n')
print('SAVED', path)
sys.exit(status)
""".format(python=sys.executable))
    wrapper.chmod(0o755)
    passive = fake / "reconclave-passive"
    passive.write_text("""#!{python}
import json, os, pathlib
evidence = pathlib.Path(os.environ['RECONCLAVE_EVIDENCE_DIR'])
evidence.mkdir(exist_ok=True)
base = evidence / 'passive-fixture'
base.with_suffix('.pcap').write_bytes(b'fixture')
base.with_suffix('.txt').write_text('receive-only fixture\\n')
base.with_suffix('.json').write_text(json.dumps({{'hosts': [{{'address': '192.168.8.5'}}]}}))
for suffix in ('.pcap', '.txt', '.json'):
    print('SAVED', base.with_suffix(suffix))
""".format(python=sys.executable))
    passive.chmod(0o755)
    env = dict(os.environ, PATH=f"{fake}:{os.environ['PATH']}", RECONCLAVE_EVIDENCE_DIR=str(root / "evidence"),
               RECONCLAVE_SCAN_INTERFACE="eth0")
    run = lambda target, **kwargs: subprocess.run([sys.executable, str(source / "reconclave-enumerate"), target],
                                                    env=kwargs.get("env", env), capture_output=True, text=True, timeout=10)
    result = run("192.168.8.0/24")
    assert result.returncode == 0, result
    calls = (root / "evidence/calls").read_text().splitlines()
    assert calls == ["192.168.8.0/24 discovery", "192.168.8.0/24 quick", "192.168.8.0/24 expanded", "192.168.8.0/24 udp", "192.168.8.2 focused 139,1234,5678", "192.168.8.3 focused 9090", "192.168.8.2 nse 139"], calls
    summary = next((root / "evidence").glob("enumeration-*.txt")).read_text()
    assert "Service ports checked: 4" in summary and "Outcome: complete" in summary, summary
    assert "Responding hosts in discovery: 2" in summary and "192.168.8.2: open TCP 139, 1234, 5678" in summary, summary
    assert "Hosts observed before active scanning: 1" in summary, summary
    assert "1234/tcp custom Example" in summary and "UDP confirmed open: 161" in summary, summary
    assert "9090/tcp irc [port-table label only; protocol unverified]" in summary, summary
    assert "Read-only protocol checks completed: 0" in summary, summary
    assert "Native NSE host checks completed: 1" in summary and "smb2-security-mode: signing required" in summary, summary
    for target in ("192.168.9.0/24", "192.168.8.0/23", "0.0.0.0/0"):
        rejected = run(target)
        assert rejected.returncode == 2, rejected
    rejected = run("192.168.8.0/24", env=dict(env, RECONCLAVE_SCAN_INTERFACE="wlan0"))
    assert rejected.returncode == 2, rejected
    partial_env = dict(env, RECONCLAVE_EVIDENCE_DIR=str(root / "partial"), TEST_FAIL_PRESET="expanded")
    partial = run("192.168.8.0/24", env=partial_env)
    assert partial.returncode == 1, partial
    partial_summary = next((root / "partial").glob("enumeration-*.txt")).read_text()
    assert "Outcome: partial" in partial_summary and "Expanded TCP scan:" in partial_summary, partial_summary
    assert "Service ports checked: 4" in partial_summary, partial_summary
    timeout_env = dict(env, RECONCLAVE_EVIDENCE_DIR=str(root / "host-timeout"),
                       TEST_HOST_TIMEOUT="192.168.8.2")
    timeout = run("192.168.8.0/24", env=timeout_env)
    assert timeout.returncode == 1, timeout
    timeout_summary = next((root / "host-timeout").glob("enumeration-*.txt")).read_text()
    assert "Service ports checked: 1" in timeout_summary, timeout_summary
    assert "Service ports skipped by safety/time limits: 3" in timeout_summary, timeout_summary
    assert "Focused service check 192.168.8.2: focused host timed out" in timeout_summary, timeout_summary
    deep_dir = root / "deep"
    deep_env = dict(env, RECONCLAVE_EVIDENCE_DIR=str(deep_dir), RECONCLAVE_ASSESS_PROFILE="deep",
                    RECONCLAVE_CHECKPOINT=str(root / "deep-checkpoint.json"))
    deep = run("192.168.8.0/24", env=deep_env)
    assert deep.returncode == 0, deep
    deep_calls = (deep_dir / "calls").read_text().splitlines()
    assert "192.168.8.2 deep" in deep_calls and "192.168.8.3 deep" in deep_calls, deep_calls
    deep_summary = sorted(deep_dir.glob("enumeration-*.txt"))[-1].read_text()
    assert "Full TCP host checks completed: 2" in deep_summary, deep_summary
    assert "Additional TCP ports found in deep pass: 2" in deep_summary, deep_summary
    assert "TCP coverage: all 65535 ports" in deep_summary, deep_summary
    resumed = run("192.168.8.0/24", env=deep_env)
    assert resumed.returncode == 0 and "REUSED DEEP" in resumed.stdout, resumed
    assert (deep_dir / "calls").read_text().splitlines() == deep_calls, "Resume repeated a finished scan"
print("Staged enumeration, focused port selection and scope enforcement passed")
