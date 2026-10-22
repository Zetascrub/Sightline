# K230 field toolset architecture

The K230 firmware is an assessment appliance, not a collection of unrelated
payload buttons. Every tool is represented by one capability contract used by
the local UI and Reconclave remote dispatch.

## Operating states

- **Observe**: receive/read-only collection that emits no assessment traffic.
  It can run without a scope but sensitive evidence still carries project,
  operator, time and location metadata.
- **Assess**: bounded discovery and protocol interrogation. It requires an
  active signed scope or a provisioned standing scope grant.
- **Armed**: disruptive, credential-handling or transmit-heavy actions. It
  requires an explicit per-action confirmation in addition to scope and is
  never enabled by boot, schedule or proximity alone.

The current mode must remain visible in the status bar. Starting a tool creates
a cancellable job; leaving its screen must not silently stop or detach it.

## Tool families

| Family | First capabilities | K230 advantage |
|---|---|---|
| Wireless survey | `radio.wifi.scan`, BLE discovery | GNSS-tagged surveys and persistent AP history |
| Network discovery | ARP snapshot, bounded host discovery, TCP service checks, DNS | Can operate locally or as a Reconclave scan shard |
| Vision | Camera capture, OCR, QR/barcode decoding, equipment-label capture | K230 camera and KPU acceleration |
| Radio | LoRa receive/spectrum observations, scoped transmit tests | SX1262/LR2021 plus location-tagged RF evidence |
| Cellular/GNSS | Position, cellular registration and signal observations | nRF9151 keyboard-base modem |
| Evidence | Timeline, notes, screenshots, PCAP/media manifests, export/outbox | One case record across every sensor and remote job |
| Coordination | Node discovery, job queue, node/coordinator role, workflow launch | Reconclave distributed execution rather than isolated gadgets |
| Hardware | Keyboard shortcuts, battery/thermal/environment health | Reliable field operation and graceful low-power capture shutdown |

## Workflow model

Borrow the useful product pattern of autonomous action queues, but bind each
mission to an assessment record and scope:

1. Select or create an assessment.
2. Import/verify a signed scope or choose Observe-only mode.
3. Run a preset such as **Site survey**, **LAN inventory**, **Camera/OCR notes**
   or **Remote sensor post**.
4. Watch jobs, findings and sensor health in one mission timeline.
5. Export a manifest with hashes, timestamps, node identity, scope revision and
   optional GNSS fix.

## Current local workflow improvements

The UI has a Jobs & activity view for Wi-Fi work, evidence operations,
packet capture, Nmap and screenshots. The autonomous assessment is a persistent
one-pass job: its scope, interface, progress and latest report survive UI
restarts, and Jobs can stop it. Other UI jobs still reset on restart. This is
not yet a unified remote execution queue.

For a field assessment, set Project and Engagement in Session, connect Ethernet
or Wi-Fi, select that interface in Scan, enter the authorised IPv4 CIDR (the
attached network is the default), choose Balanced or Deep, then tap AUTO ASSESS. The same job can be started over SSH with
`reconclave-mission start 192.168.8.0/24 eth0`, inspected with
`reconclave-mission status`, and stopped with `reconclave-mission stop`. Add
`--profile deep` for up to four bounded full-TCP host checks after the standard
stages (30-minute overall budget). A stopped, interrupted or partial run can be
resumed with `reconclave-mission resume` or the Scan screen's RESUME LAST action;
finished stages are reused only when their saved evidence still matches the
checkpoint, and the same session, source address and link are present.
The explicit start authorizes one run only; a cable insertion or reboot does
not automatically start another scan. Local starts require a named session and
an attached IPv4 /24-or-smaller scope. Signed delegated scope applies to remote
dispatch; this local workflow relies on the operator selecting the attached
scope and is not a substitute for a client authorization record.
The mission requires 64 MiB free before starting and stops if the source
address, selected link identity, or Wi-Fi access point changes, or if free
evidence space falls below 24 MiB. A stopped run keeps its partial report and
reason. Results in Scan show responding hosts, open-TCP hosts, completed checks
and skipped checks; the full evidence report carries coverage limits.
The completed mission also writes `field-report-<mission-id>.json` to Evidence.
Transfer that file to the desktop and import it from the Evidence page into the
selected project. The desktop checks the embedded summary hash and keeps the
artifact inventory, while labelling the device origin as unverified; the JSON
alone is not a signed attestation of the listed raw files.
The same completed run now writes a Markdown operator report and CSV host table
with the same mission ID. These are human-readable handoffs; the JSON and raw
evidence remain the authoritative machine-readable record. The image recipe
selects `curl`, BIND DNS tools and `jq` for bounded manual follow-up alongside
Nmap, `tcpdump`, OpenSSL and `socat`. `reconclave-capabilities` reports which
tools are actually installed on the running image.

The local Scan screen offers a bounded ASSESS run: 45 seconds of receive-only
ARP, DNS, mDNS, LLMNR, NBNS, DHCP and broadcast/multicast sampling before active discovery, top-100 and
top-1000 TCP checks, top-20 UDP checks, light service identification, and
bounded HTTP security-header, TLS certificate, SSH banner, DNS and SMB2 signing
observations. The SMB2 check sends one unauthenticated negotiate request to an
observed open TCP/445 service and supports dialects through 3.0.2. These are
complemented by native Nmap NSE checks on observed services: `http-title`,
`http-security-headers`, `ssl-cert`, `ssh-hostkey` and `smb2-security-mode`.
The native stage checks at most 24 hosts and 64 relevant ports per host, with
per-script and per-host timeouts. It does not validate vulnerabilities. Each
stage saves evidence and partial
results survive later stage failures. A separate PASSIVE action only listens.
Passive sightings are stored with their own provenance and never create open
ports, replace a conflicting known MAC, or claim to see the whole switched LAN.
This follows the evidence distinction in [Ragnar's traffic analysis guide](https://github.com/PierreGode/Ragnar/blob/main/docs/traffic-analysis.md)
without copying its implementation. The Deep profile can check all TCP ports on
at most four responding hosts; other hosts retain top-1000-at-most coverage.
Full UDP sweeps and vulnerability scanning remain outside this K230 preset.

New local evidence is attributed to the project, engagement and operator context
captured at operation start. The evidence browser and session-only bundle export
use this attribution; legacy/remote files without metadata remain unknown.
Whole-store export remains available. See `docs/VALIDATION.md` for the checks
performed on these paths.

## Implementation roadmap

1. nRF9151 power control and continuous outdoor GNSS acquisition are validated;
   retain antenna/setup guidance and expand field testing.
2. TCA8418 navigation and text input are integrated; complete physical acceptance
   of the new job/browser screens.
3. Execution-key verification, replay protection, signed delegated scope,
   canonical JSON, SHA-256/HMAC and strict trust-key loading are implemented.
4. Bounded TCP connect, DNS, Wi-Fi, `/24` discovery and conservative service
   identification are remotely callable. Identification uses server-first
   banners or a fixed HTTP HEAD request and never attempts authentication.
5. The append-only mission timeline now hash-chains authenticated tool results
   with assessment context and verifies integrity in the UI. Build the durable
   per-host knowledge base now tracks services and state changes, with a
   deduplicated evidence-derived findings view.
6. Evidence manifests verify the timeline before publication, hash regular
   evidence files with SHA-256, bind assessment and firmware identity, exclude
   links, and enforce file-count and per-file limits for predictable field use.
7. Extend camera capture with OCR and QR/barcode evidence.
8. Extend receive-first LoRa and BLE surveys; put all transmit actions behind the
   Armed state.
9. Add coordinator discovery, job dispatch, progress and evidence outbox sync.

## Reference patterns

- Bjorn: autonomous queue, accumulated host knowledge and glanceable progress.
- Bruce / Evil-Cardputer: fast category navigation, persistent settings and
  hardware-aware feature availability.
- Hak5 devices: portable repeatable workflows and simple field activation.
- Ragnar: protocol/service-oriented modules and reusable tool interfaces.
- Raspyjack: compact headless deployment and remote-first control.

Do not copy unsafe defaults: no autonomous brute force, credential collection,
deauthentication, destructive payload, or unrestricted target input. Those
behaviours conflict with Reconclave's signed-scope and evidence model.
