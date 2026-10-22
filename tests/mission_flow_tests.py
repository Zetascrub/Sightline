"""Exercise persistent mission lifecycle with no network traffic."""
import json
import csv
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time


SCRIPT = Path(sys.argv[1]) / "firmware/rootfs-overlay/usr/sbin/reconclave-mission"
with tempfile.TemporaryDirectory(prefix="sightline-mission-") as directory:
    root = Path(directory)
    binary = root / "bin"
    binary.mkdir()
    ip_record = root / "ip-address"
    ip_record.write_text("2: eth0 inet 192.168.8.140/24 scope global eth0\n")
    (binary / "ip").write_text("#!/bin/sh\n[ \"$6\" = eth0 ] && cat \"$TEST_IP_FILE\"\n")
    (binary / "ip").chmod(0o755)
    fake = binary / "reconclave-enumerate"
    fake.write_text("""#!{python}
import os, pathlib, signal, sys, time
evidence = pathlib.Path(os.environ['RECONCLAVE_EVIDENCE_DIR'])
evidence.mkdir(exist_ok=True)
report = evidence / 'enumeration-fixture.txt'
report.write_text('Outcome: complete\\nExpanded TCP stage completed: no\\n'
                  'Responding hosts:\\n192.168.8.141\\n192.168.8.142\\n'
                  'Observed hosts and services:\\n192.168.8.141: open TCP 80\\n'
                  '  TCP coverage: top 100 ports at most\\n'
                  'Deep TCP coverage:\\nnot requested\\n')
pathlib.Path(os.environ['RECONCLAVE_CHECKPOINT']).write_text('fixture')
print('STAGE discovery', flush=True)
print('SAVED', report, flush=True)
time.sleep(float(os.environ.get('TEST_MISSION_SLEEP', '0.1')))
print('ASSESSMENT COMPLETE 1 host; 0 warnings', flush=True)
""".format(python=sys.executable))
    fake.chmod(0o755)
    session = root / "session.conf"
    session.write_text("project=CLIENT\nengagement=FIELD\noperator=TEST\n")
    env = dict(os.environ, PATH=f"{binary}:{os.environ['PATH']}",
               RECONCLAVE_MISSION_DIR=str(root / "mission"),
               RECONCLAVE_EVIDENCE_DIR=str(root / "evidence"),
               RECONCLAVE_SESSION_PATH=str(session),
               RECONCLAVE_ENUMERATOR=str(fake), TEST_IP_FILE=str(ip_record))

    def call(*args, extra=None):
        return subprocess.run([sys.executable, str(SCRIPT), *args], env=extra or env,
                              text=True, capture_output=True, timeout=10)

    for scope, interface in (("192.168.8.0/23", "eth0"), ("192.168.9.0/24", "eth0"),
                             ("192.168.8.0/24", "wlan0")):
        assert call("start", scope, interface).returncode != 0
    session.write_text("project=UNASSIGNED\nengagement=UNASSIGNED\n")
    assert call("start", "192.168.8.0/24", "eth0").returncode != 0
    session.write_text("project=CLIENT\nengagement=FIELD\noperator=TEST\n")
    assert call("start", "192.168.8.0/24", "eth0").returncode == 0
    assert call("start", "192.168.8.0/24", "eth0").returncode != 0
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        state = json.loads(call("status").stdout)
        if state["state"] == "complete":
            break
        time.sleep(0.05)
    assert state["state"] == "complete", state
    assert state["metrics"]["outcome"] == "complete", state
    assert state["warning_count"] == 0, state
    assert state["scope"] == "192.168.8.0/24" and state["interface"] == "eth0"
    assert state["context"]["engagement_id"] == "FIELD"
    assert Path(state["summary"]).is_file()
    field_report = json.loads(Path(state["field_report"]).read_text())
    assert field_report["schema"] == "sightline-field-report/v1"
    assert field_report["scope"] == "192.168.8.0/24" and field_report["context"]["project_id"] == "CLIENT"
    assert field_report["hosts"] == [
        {"address": "192.168.8.141", "open_ports": [80], "tcp_coverage": "top-100-at-most"},
        {"address": "192.168.8.142", "open_ports": [], "tcp_coverage": "top-100-at-most"}]
    markdown = Path(state["field_report_markdown"])
    assert markdown.is_file() and "192.168.8.141 | 80 | top-100-at-most" in markdown.read_text()
    assert "does not establish vulnerabilities" in markdown.read_text()
    with Path(state["field_report_csv"]).open(newline="") as handle:
        rows = list(csv.DictReader(handle))
    assert len(rows) == 2 and rows[0]["ipv4"] == "192.168.8.141"
    assert rows[0]["open_tcp_ports"] == "80" and rows[1]["open_tcp_ports"] == ""
    assert json.loads((root / "evidence/.metadata" / (markdown.name + ".json")).read_text())["context"]["project_id"] == "CLIENT"
    metadata = json.loads((root / "evidence/.metadata/enumeration-fixture.txt.json").read_text())
    assert metadata["context"]["project_id"] == "CLIENT"
    assert call("start", "192.168.8.128/27", "eth0").returncode == 0
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        narrow = json.loads(call("status").stdout)
        if narrow["state"] == "complete":
            break
        time.sleep(0.05)
    assert narrow["state"] == "complete" and narrow["scope"] == "192.168.8.128/27", narrow
    slow = dict(env, TEST_MISSION_SLEEP="30")
    assert call("start", "192.168.8.0/24", "eth0", extra=slow).returncode == 0
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        state = json.loads(call("status").stdout)
        if state["state"] == "running":
            break
        time.sleep(0.05)
    assert state["state"] == "running", state
    assert call("stop").returncode == 0
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        state = json.loads(call("status").stdout)
        if state["state"] == "stopped":
            break
        time.sleep(0.05)
    assert state["state"] == "stopped", state
    assert state["stop_reason"] == "operator requested stop", state
    stopped_id = state["id"]
    session.write_text("project=OTHER\nengagement=FIELD\noperator=TEST\n")
    assert call("resume", extra=slow).returncode != 0
    session.write_text("project=CLIENT\nengagement=FIELD\noperator=TEST\n")
    assert call("resume", extra=slow).returncode == 0
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        state = json.loads(call("status").stdout)
        if state["state"] == "running":
            break
        time.sleep(0.05)
    assert state["state"] == "running" and state["resumed_from"] == stopped_id, state
    assert call("stop").returncode == 0
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        state = json.loads(call("status").stdout)
        if state["state"] == "stopped":
            break
        time.sleep(0.05)
    assert state["state"] == "stopped", state
    assert call("start", "192.168.8.0/24", "eth0", extra=slow).returncode == 0
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        state = json.loads(call("status").stdout)
        if state["state"] == "running":
            break
        time.sleep(0.05)
    assert state["state"] == "running", state
    ip_record.write_text("2: eth0 inet 192.168.9.140/24 scope global eth0\n")
    deadline = time.monotonic() + 7
    while time.monotonic() < deadline:
        state = json.loads(call("status").stdout)
        if state["state"] == "stopped":
            break
        time.sleep(0.1)
    assert state["state"] == "stopped" and "Scope" in state["stop_reason"], state
print("Mission scope, persistence, attribution, duplicate prevention, stop and link guard passed")
