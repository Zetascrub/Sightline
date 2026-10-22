# Internal assessment field trial: feature backlog

Date: 2026-09-25. Status: observed gaps and proposed work; these features are not implemented by this document.

An authorised LAN assessment was run from the K230 using its wired interface, with a limited check through its associated Wi-Fi interface. Discovery, TCP/UDP scanning, HTTP responses, DNS-SD and bounded protocol handshakes were collected. Detailed network evidence is private under the locally excluded `assessments/` directory.

The device can collect useful evidence, but completing an assessment currently depends heavily on an SSH operator. The next milestone should be a repeatable **assessment → assets → services → findings → report** workflow on the device.

## Prioritised work

| Priority | Feature | Field evidence | Acceptance criteria |
|---|---|---|---|
| P0 | Structured asset and service inventory | ARP, DNS, mDNS, Nmap XML and HTTP responses required manual joining. Several IPs shared a MAC. | Store observations with timestamps, interface and source; correlate aliases without assuming one IP equals one physical device; show provenance and confidence. |
| P0 | Assessment presets with explicit coverage | Current quick/services wrappers use only the top 100/50 ports. Even a custom top-1000 scan missed an unauthenticated datastore. | Offer discovery, common services and bounded full TCP scan; show exactly which hosts/ports were tested, skipped, timed out or untested; retain XML alongside readable output. |
| P0 | Read-only service posture checks | Custom probes established unauthenticated datastore information access, unauthenticated Transmission RPC statistics and optional SMB signing. | Ship bounded protocol-aware checks; separate verified observations, potential impact and untested access; never equate a public login page with an auth bypass. |
| P0 | One persistent job model | SSH-launched assessment jobs were invisible to the touchscreen Jobs view. | Local UI and remote dispatch use the same job API, scope, lifecycle and persistent state; progress, cancellation and partial results survive UI restarts. |
| P0 | Findings and evidence report | Producing a report required workstation scripts and manual interpretation. | Each finding references immutable evidence and asset/service IDs; export Markdown, JSON and CSV with methodology, limitations, remediation and hashes. |
| P1 | DNS-SD inventory | ESPHome advertisements exposed device role, firmware version, API port and advertised encryption without a broad scan. | Parse bounded PTR/SRV/TXT/A/AAAA responses; validate lengths/compression; distinguish advertisements from verified connections; obey scope for follow-up targets. |
| P1 | Reliable identity and service classification | Port defaults mislabelled IoT protocols; a camera fingerprint suggested the wrong manufacturer. | Display port-number guesses as guesses; reconcile protocol, HTTP, MAC and discovery evidence; expose conflicting identities instead of inventing certainty. |
| P1 | TCP/UDP state accuracy | UDP silence produced many `open|filtered` results. | Keep open, closed, filtered and open-or-filtered distinct; only count response-confirmed UDP services as open. |
| P1 | Capability preflight | Nmap originally lacked Lua/NSE; the Wi-Fi driver exposed only 2.4 GHz channels. | Show supported bands, interfaces, scan engines and missing dependencies before a mission; disable unsupported checks with actionable reasons. |
| P1 | Wireless security metadata | WPA2/CCMP required combining driver and supplicant data; legacy `iwconfig` output was misleading. | Show BSSID, band, channel, AKM, ciphers and RSN PMF flags with provenance; represent unobserved SSIDs as unassessed, not secure or absent. |
| P1 | Interface-specific reachability comparison | Wired and Wi-Fi access must be explicitly distinguished. | Bind probes to the selected interface; record source address and route; produce an access matrix without inferring VLAN isolation from one successful request. |
| P1 | Nested evidence and external imports | Assessment files in a subdirectory were outside the flat evidence browser; custom probes lacked native session attribution. | Support safe nested browsing/import; attach project, operator, scope and job metadata at collection; preserve originals and hashes. |
| P1 | Storage/time budgets and resumable scans | Full TCP coverage is materially slower than quick discovery on this device. | Display estimated budget, enforce storage/rate/deadline limits, checkpoint results and resume only within the same authorised scope. |
| P2 | Version and advisory triage | Banners reveal versions but cannot establish vendor patch state or exploitability. | Preserve exact banners/CPE confidence; attach vendor advisories with retrieval dates and prerequisites; require validation before promoting a lead to a vulnerability. |
| P2 | Baselines and retesting | Single observations do not show drift or prove a fix. | Compare asset/service changes and repeat the same read-only check; retain both original and retest evidence. |

## Suggested delivery order

1. Define versioned asset, observation, finding and job schemas; add Nmap XML ingestion and coverage reporting.
2. Expose an on-device Assets view with service details and evidence links.
3. Extend the bounded HTTP, TLS and SMB2 modules with authentication-boundary and datastore-posture checks. Add an explicit capability registry and coverage display for the now-available native Nmap NSE runtime.
4. Make the workflow persistent and generate the report directly from stored observations.
5. Add discovery enrichment, wireless capability reporting, interface comparison and retesting.

## Verification approach

Use fixture-based parser tests for truncated/malformed XML, DNS compression, conflicting identity evidence and ambiguous UDP states. Use controlled local protocol fixtures for authentication and signing outcomes. Verify cancellation, deadlines, storage exhaustion and restart recovery on hardware. Run a scoped acceptance assessment and confirm every reported claim links to its actual observation.

A dual-band adapter/driver is a hardware enablement item, not a UI feature: software alone cannot provide 5 GHz assessment with a 2.4 GHz-only radio path.


## Delivered follow-up: assessment workspace

Implemented the first workflow increment: direct home access to Assets/Findings/
Evidence, inventory search and filtering, numeric/recent/review ordering, stable
list refreshes, explicit device-history and partial-coverage labels, response vs
port-inference labels, and versioned JSON inventory export with evidence metadata.
Existing node observations remain the inventory source. Nmap XML ingestion,
assessment-isolated storage, durable unified jobs and protocol posture modules
remain open; this increment does not claim to complete those backlog entries.


## Delivered follow-up: automatic Nmap ingestion

Nmap presets now publish XML and queue durable import work for the node. Assets
retain source filenames, requested coverage, completion state, hostnames and
TCP/UDP service observations. Persistent receipts explain import failures and
prevent routine re-imports. Parser/queue tests cover malformed input, limits,
ambiguous states, cancelled scans, persistence and identity preservation.

This completes the first Nmap ingestion path, not the broader persistent-job
backlog: full scan scheduling/resume, per-assessment inventories and additional
posture checks remain future work. Truncated XML is retained but not partially
salvaged. Historical opens are deliberately preserved rather than interpreted as
current comprehensive state.
