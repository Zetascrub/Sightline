# Common assessment tools on a K230 field device

## Which tools have the strongest on-device fit?

### Takeaway
Prioritize small, bounded, evidence-producing network tools and existing native Nmap integration. A useful field workflow is observation, scoped identification, review, and export, with actual tool output retained alongside a concise UI summary.

### Cited Findings
- Nmap builds on Unix/Linux and uses libpcap for raw packet capture; its XML output is the format Nmap recommends for programmatic consumers. — [Nmap build guide](https://nmap.org/book/inst-source.html); [Nmap XML guidance](https://nmap.org/book/output-formats-xml-output.html)
- Nmap NSE has `safe`, `intrusive`, `brute`, `dos`, `exploit`, `external`, and other script categories. Scripts are not sandboxed, and the project explicitly warns that scripts can harm the running system. — [NSE usage guide](https://nmap.org/book/nse-usage.html)
- TShark can capture or read packets, save pcapng, print decoded fields, and rotate capture files with a ring buffer; live capture requires libpcap and capture privileges. — [TShark manual](https://www.wireshark.org/docs/man-pages/tshark.html); [capture privileges](https://wiki.wireshark.org/capturesetup/captureprivileges)
- A constrained embedded Linux distribution may omit libpcap, which Wireshark and TShark require for live capture. — [Wireshark libpcap note](https://wiki.wireshark.org/libpcap)
- curl supports interface selection and time limits; OpenSSL `s_client` supports SNI, certificate display, and TLS connection inspection. — [curl manual](https://curl.se/docs/manpage.html); [OpenSSL s_client manual](https://docs.openssl.org/4.0/man1/openssl-s_client/)
- `socat` supports interactive TCP connections and byte-stream transfers; it is a flexible plumbing tool rather than a finding engine. — [socat manual](https://man7.org/linux/man-pages/man1/socat.1.html)
- Sightline already has local Nmap XML ingestion and a `tcpdump` capture wrapper; the current device documentation reports Buildroot 2025.02.1, Linux 6.6.36, and RISC-V LP64D. — [Sightline README](https://github.com/Zetascrub/Sightline/blob/main/README.md); [development log](https://github.com/Zetascrub/Sightline/blob/main/DEVELOPMENT.md); [capture wrapper](https://github.com/Zetascrub/Sightline/blob/main/firmware/rootfs-overlay/usr/sbin/reconclave-capture)

### Inferences
- **Tier 1, integrate on board:** Nmap with explicit port/time/host limits and selected reviewed NSE scripts; existing `tcpdump` capture with size/time/space caps; `dig` or BusyBox DNS queries; `curl` and `openssl s_client` for bounded HTTP/TLS evidence; and simple socket/banner checks. Wrap each with scope, selected interface/source address, cancellation, exit status, start/end time, and raw output receipt. Surface `--version` and missing dependency status before use.
- **Tier 2, trial on board:** TShark only if the board has room for its binary, dissectors, and libraries and measurements show acceptable memory/CPU. For first release, capturing pcap on device and analyzing in desktop Wireshark is likely higher value than shipping a full graphical Wireshark UI.
- Retain Nmap XML/normal text, pcapng or pcap, HTTP headers/cert chains, DNS answers, and commands plus arguments in the evidence store. The UI should show coverage and provenance rather than call an open port or banner a confirmed vulnerability.

### Gaps
- Exact available RAM, storage, free space under the deployed image, binary sizes, current library inventory, and performance under simultaneous UI plus capture workloads need live board measurements.
- DNS command packaging and TShark cross-build availability in the pinned Buildroot SDK were not verified.

## Which wireless and multi-protocol suites can work, and what gates them?

### Takeaway
Wireless tooling depends mainly on the actual radio chipset, in-kernel driver, monitor-mode support, and sometimes full frame injection. The board's ordinary associated Wi-Fi connection is not evidence that these modes work.

### Cited Findings
- Kismet's Linux Wi-Fi capture requires a Linux driver with standard monitor-mode support; it cannot use a device without such a driver. Kismet can keep unified SQLite `kismetdb` logs with packets, devices, location, and source records, and export pcapng, JSON, and KML. — [Kismet Linux Wi-Fi source](https://kismetwireless.net/docs/readme/datasources/wifi-linux/); [Kismetdb logging](https://www.kismetwireless.net/docs/readme/logging/kismetdb/)
- Kismet pcapng can retain mixed radio packet types and original datasource identity; Kismet supports remote capture sources and GPS attachment. — [Kismet pcapng logging](https://kismetwireless.net/docs/readme/logging/pcap/); [Kismet datasources](https://www.kismetwireless.net/docs/readme/datasources/datasources/)
- Aircrack-ng includes monitor-mode management, raw 802.11 capture, packet injection/replay, and offline key analysis. Its Linux build needs a set of libraries/tools including libpcap, OpenSSL or libgcrypt, and optionally libnl; `airodump-ng` emits pcap, CSV, GPS, and other formats. — [Aircrack-ng tool list](https://aircrack-ng.org/documentation.html); [build requirements](https://www.aircrack-ng.org/doku.php?id=install_aircrack); [airodump-ng documentation](https://aircrack-ng.org/doku.php?id=airodump-ng%2F)
- Current hcxdumptool describes itself as headless-capable on small Linux systems but requires Linux kernel at least 5.15 and a WLAN driver with monitor mode **and full frame injection**; its author cautions that drivers may fail even where a chipset family seems supported. Its pcapng output can be used in TShark/Wireshark. — [hcxdumptool README](https://github.com/zerbea/hcxdumptool)
- bettercap covers Ethernet, Wi-Fi, BLE, CAN, and HID modules; its own Docker build installs libpcap, libusb, netfilter queue and wireless utilities. — [bettercap README](https://github.com/bettercap/bettercap/blob/master/README.md); [bettercap Dockerfile](https://github.com/bettercap/bettercap/blob/master/Dockerfile)
- TShark monitor mode is supported only for some Wi-Fi adapters and may disconnect that adapter from its associated network. — [TShark manual](https://www.wireshark.org/docs/man-pages/tshark.html)

### Inferences
- **Tier 2, conditional on external supported radio:** Kismet passive capture or Aircrack's `airodump-ng` can add real 802.11 observation, AP/client/channel survey, and GPS-tagged pcapng. This needs a USB adapter and live `iw phy`/driver/capture validation. Prefer an additional radio so connection to the coordinator does not disappear during monitor capture.
- **Tier 3, deliberately armed and explicitly scoped:** injection/replay features in Aircrack-ng, bettercap, and hcxdumptool should be individual reviewed capabilities, never a generic all-features launcher. Their transmit effects, radio exclusivity, target and channel filters, and output need to be visible before starting. These suites cannot be marked supported from successful compilation alone.
- Kismet as a complete on-board UI/server needs memory, storage, and runtime testing; a remote Kismet capture source feeding a desktop server is a useful alternative if local overhead is too high.

### Gaps
- No verified monitor-mode/injection test for the actual K230 Wi-Fi driver or a specific attached USB radio was found in the project record. Runtime capability and regulatory limits must be tested on hardware.
- No measured Kismet/bettercap memory or storage profile on this RISC-V Buildroot image is available; feasibility remains conditional.

## Which popular tools should stay on a companion, and what should Sightline exchange with them?

### Takeaway
Keep high-compute, large-runtime, broad-template, and high-rate tools primarily on a workstation or Reconclave coordinator. The K230 can collect and label evidence and dispatch tightly defined remote jobs without becoming a small-screen copy of a full Kali workstation.

### Cited Findings
- Hashcat needs a compatible compute backend; CPU-only operation needs a CPU OpenCL runtime, and its official Linux archive ships 64-bit binaries. The project recommends checking `hashcat -I` for usable devices. — [hashcat FAQ](https://hashcat.net/wiki/doku.php?id=frequently_asked_questions)
- Metasploit's supported installers include Ruby and PostgreSQL; its database-backed service also uses PostgreSQL. — [Metasploit installer guide](https://docs.metasploit.com/docs/using-metasploit/getting-started/nightly-installers.html); [Metasploit development setup](https://docs.metasploit.com/docs/development/get-started/setting-up-a-metasploit-development-environment.html)
- Nuclei is a template-driven scanner with community-maintained templates; its public template collection includes many protocol families and a default ignore list for `dos`, `fuzz`, and `bruteforce` tags. It also supports scanning locally stored HTTP responses in passive mode. — [Nuclei templates](https://github.com/projectdiscovery/nuclei-templates/blob/main/README.md); [template exclusions](https://github.com/projectdiscovery/nuclei-templates/blob/main/.nuclei-ignore); [Nuclei getting started](https://github.com/projectdiscovery/nuclei-docs/blob/main/docs/nuclei/get-started.md)
- Masscan is built for high-speed asynchronous packet transmission; its own documentation warns that high rates can overwhelm networks. — [masscan README](https://github.com/robertdavidgraham/masscan)
- Metasploit has a REST web service and database workspace model that a coordinator could use without embedding the whole framework on the device. — [Metasploit web service](https://docs.metasploit.com/docs/using-metasploit/advanced/metasploit-web-service.html); [database support](https://docs.metasploit.com/docs/using-metasploit/intermediate/metasploit-database-support.html)
- Nmap recommends written authorization before scanning networks that one does not administer. — [Nmap legal guide](https://nmap.org/book/legal-issues.html)

### Inferences
- **Companion first:** Hashcat/John offline password audits, Metasploit, broad Nuclei scans, and Masscan belong on a workstation. Sightline should export raw packet captures, selected service observations, HTTP response evidence, Nmap XML, and exact scan coverage to these tools, then import their reviewed reports with source-tool/version and artifact hashes.
- A tightly allowlisted remote Nuclei profile over already observed, in-scope URLs may be useful after the device has collected target inventory; never mirror the entire template catalog or update it silently on a field device.
- Offline credential auditing and exploit validation are separate operator-authorized jobs. Their results should enter a findings review queue; a product finding should cite the tested target, specific observation, method, and evidence file instead of inheriting a scanner severity uncritically.
- Netcat/socat-style ad hoc socket operations are better represented as bounded protocol-specific checks in the UI, while keeping an expert shell path for the operator.

### Gaps
- John the Ripper's current cross-build support and performance on this board were not verified. It may run on CPU, but there is no reason yet to expect useful audit throughput on the K230.
- No local Metasploit, Nuclei, hashcat, or Masscan workload has been profiled on this firmware; companion placement is an architectural recommendation based on runtime/compute/traffic characteristics, not a measured impossibility.
