# Portable pentest firmware ecosystem, surveyed 8 October 2026

## What do Cardputer ADV and CardputerZero firmware demonstrate?

### Takeaway
Cardputer ADV has a mature, device-specific ESP32 firmware ecosystem; CardputerZero is a different, Linux-class platform. The reusable ideas for Sightline are quick keyboard navigation, modular tool menus, evidence export, and explicit support matrices for each radio or accessory.

### Cited Findings
- M5Stack specifies CardputerZero as Raspberry Pi CM0, quad-core Cortex-A53, 512 MB RAM, microSD, 320 x 170 display, 46-key keyboard, USB host/device switching, 2.4 GHz Wi-Fi, Ethernet, IR, and expansion I/O. Its product documentation still says packaging and software development are work in progress. It is not an ESP32 Cardputer ADV firmware target. — [M5Stack CardputerZero documentation](https://docs.m5stack.com/en/CardputerZero)
- Bruce's current public source lists Wi-Fi scanning, host/TCP-port discovery, wardriving, raw sniffing, SSH/Telnet/TCP clients, BLE scanning, IR, RF, RFID/NFC, file managers, Web UI, USB HID scripts, and a JavaScript interpreter. Its own matrix marks several features as requiring external CC1101, NRF24, FM, or PN532 hardware and says some devices have a reduced launcher-compatible build. Bruce is AGPL-3.0. — [Bruce source README and capability matrix](https://github.com/BruceDevices/firmware)
- ESP32 Marauder describes itself as a portable Wi-Fi/Bluetooth analysis tool for frame capture, enumeration, and transmission; its CLI exposes AP/station scans, beacon/deauth/PMKID sniffing, target selection, channel control, and scan stopping. Its source license is MIT. — [Marauder FAQ](https://github.com/justcallmekoko/ESP32Marauder/wiki/faq), [Marauder CLI](https://github.com/justcallmekoko/ESP32Marauder/wiki/CLI), [MIT license](https://github.com/justcallmekoko/ESP32Marauder/blob/master/LICENSE)
- Marauder documents separate binaries for hardware revisions and warns that a mismatched binary can cause blank displays, unusable controls, missing storage, or partition mismatch. — [Marauder versions](https://github.com/justcallmekoko/ESP32Marauder/wiki/marauder-versions)
- NEMO's public source includes Wi-Fi/BLE/IR functions and passive Deauth, BLE, PineAP, and BadUSB detection. BadUSB Hunter reports USB VID/PID, class and interfaces on Cardputer/ADV using ESP32-S3 USB host capability; its README notes that SD support is enabled by default only on Cardputer. The author states dual GPL-2 or GPL-3 licensing in its license file. — [NEMO README](https://github.com/n0xa/m5stick-nemo), [NEMO license](https://github.com/n0xa/m5stick-nemo/blob/main/LICENSE)
- M5Launcher documents SD-card firmware selection and board-specific support for the ADV's TCA8418 I2C keyboard rather than the original Cardputer's GPIO matrix. — [Launcher Cardputer ADV port](https://github.com/bmorcelli/Launcher/blob/main/boards/m5stack-cardputer/CardputerADV.md)

### Inferences
- For Sightline, use the pattern **discover → select target → run one operation → save artifact → inspect history**, while keeping each radio's support and external module requirements visible. This follows the source-documented Marauder CLI and Bruce hardware matrix.
- Keyboard shortcuts, a task switcher, persistent recent targets, and a terminal-like detail view are likely more transferable than copying an ESP32 screen layout. CardputerZero reinforces the usefulness of a Linux terminal form factor, but its software stack is not a direct K230 port.
- Favor passive detection, inventory and packet capture in the default field workflow. Put active transmission functions behind a scoped target selection and clear operation state.

### Gaps
- These sources are project documentation, not independent evidence that every listed function works on current physical ADV hardware. In particular, attached radio modules, firmware versions, and region settings alter actual availability.
- I did not locate a maintained CardputerZero equivalent of Bruce or Marauder; M5Stack documents the hardware but labels its software work in progress.

## What do Biscuit Pro, Ultra, and Crumb add to the comparison?

### Takeaway
Biscuit is useful as a workflow reference for phone-assisted wireless surveys, durable captures, and distributed radio nodes. Its public flasher and documentation should not be mistaken for a confirmed public source tree of the device firmware.

### Cited Findings
- Biscuit Pro uses an ESP32-C5 scanner plus an ESP32-WROOM control/BLE gateway, with app control; Ultra shares the dual-chip design and adds microSD, external antennas, and a larger 3200 mAh battery. Crumb is a single ESP32-C5 with no microSD and uses on-board flash for portal templates and captures. — [Pro product page](https://biscuitshop.us/pages/biscuit-pro), [Ultra device documentation](https://codehedge.github.io/Biscuit-Wiki/devices/ultra.html), [Crumb device documentation](https://codehedge.github.io/Biscuit-Wiki/devices/crumb.html)
- Biscuit documents AP/station discovery, beacon/probe/deauth observation, RSSI/channel/security filters, a channel analyzer, PMKID/EAPOL and raw PCAP capture, history, and PCAP export. Ultra can continue writing an SD capture after the phone disconnects; BLE transport can skip packets under load. — [Wi-Fi scanning](https://codehedge.github.io/Biscuit-Wiki/features/wifi-scanning.html), [capture and analysis](https://codehedge.github.io/Biscuit-Wiki/features/wifi-capture.html)
- Biscuit documents Bluetooth inventory and special-purpose detection, including Remote ID drone broadcasts; its Node design distributes scanning across ESP32-C5 devices and explicitly assigns each single-radio node either Wi-Fi or BLE work during wardriving. — [Bluetooth scanning](https://codehedge.github.io/Biscuit-Wiki/features/bluetooth-scanning.html), [Node documentation](https://codehedge.github.io/Biscuit-Wiki/devices/node.html)
- Its OTA UI updates the C5 and WROOM independently, reports verification of the installed version, and distinguishes connection, download, validation, and installation failures. — [Firmware updates](https://codehedge.github.io/Biscuit-Wiki/features/firmware-updates.html)
- CodeHedge publishes a public flasher that downloads firmware from the cloud, and a public DIY/Node web flasher. The repositories inspected are flashing utilities, not identified as the device firmware source. — [Pro/Ultra flasher repository](https://github.com/CodeHedge/biscuit_flasher), [DIY/Node flasher repository](https://github.com/CodeHedge/diy_node_web_flasher)

### Inferences
- Sightline should copy the *workflow pattern*: uninterrupted local PCAP/scan logging, visible skipped-packet counts, explicit channel dwell and band settings, reconnect/resume after controller loss, and standard export formats.
- The single-C5 Node constraint is a useful reminder that concurrency claims require radio-level proof. A K230 with separate radios could coordinate jobs, but the UI should disclose when two activities contend for one interface.
- Firmware provenance and license review must precede any code reuse from Biscuit. Feature descriptions are sufficient to inspire a clean implementation.

### Gaps
- I did not find an official, maintained public source tree or license for Pro/Ultra/Crumb firmware in the inspected official CodeHedge repositories. The availability of downloadable binaries does not establish source availability or reuse rights.
- The documentation gives manufacturer feature claims; I did not validate capture performance, attack behavior, or battery runtime on hardware.

## What can be learned from Hak5 gear without treating its firmware as open source?

### Takeaway
Hak5's public repositories are primarily payload and customization ecosystems. The strong transferable patterns are named, reviewable task recipes, target and interface selection, status feedback, local artifacts, and safe update checks; they do not grant access to Hak5 firmware source.

### Cited Findings
- Hak5's public GitHub organization pins separate payload repositories for USB Rubber Ducky, Bash Bunny, Packet Squirrel, Shark Jack, Key Croc, and WiFi Pineapple Pager. The Pager umbrella repo links payloads, themes, and ringtones, not a firmware source tree. — [Hak5 GitHub organization](https://github.com/hak5), [Pager repository](https://github.com/hak5/wifipineapplepager)
- Hak5's Pager payload repository describes DuckyScript plus Bash payloads that execute as files on the Linux-based device. Submission guidance asks authors to describe targets, dependencies, configurable placeholders, and LED states, and requires staged code to be included and user hosted rather than silently fetched from a live URL. — [Official Pager payload repository](https://github.com/hak5/wifipineapplepager-payloads)
- The official USB Rubber Ducky, Packet Squirrel, and Shark Jack repositories publish payloads/scripts and link Hak5's separate Software License Agreement. Public payload source does not imply that their device firmware or DuckyScript implementation is open source. — [Rubber Ducky payloads](https://github.com/hak5/usbrubberducky-payloads), [Packet Squirrel payloads](https://github.com/hak5/packetsquirrel-payloads), [Shark Jack payloads](https://github.com/hak5/sharkjack-payloads)
- WiFi Pineapple documents recon-based handshake collection and directed capture that parks on the selected network channel. Hak5's setup guide describes a slim firmware stager, downloading the current firmware, and verifying a manual image's SHA-256 before installation. — [Pineapple handshakes](https://docs.hak5.org/wifi-pineapple/ui-overview/recon-1), [setup and firmware validation](https://github.com/hak5/hak5-docs/blob/master/setup/setting-up-your-wifi-pineapple.md)

### Inferences
- A Sightline recipe format could expose **name, purpose, scope/target, interfaces, prerequisites, outputs, and operator-visible state**, with a dry-run preview and explicit stop action. This draws on Hak5's payload authoring conventions without implementing DuckyScript or copying payloads.
- For field reliability, save captures and structured results alongside the recipe and firmware version used; expose hashes for exported evidence and signed or hash-verified update status. The latter is motivated by Hak5's documented firmware hash check.
- Linux-class Hak5 tools show that the differentiator is orchestration and clear field UX around common utilities, rather than the raw number of attack menu entries.

### Gaps
- I did not identify a public Hak5 device firmware source repository or license granting reuse of the firmware. Each individual payload repository may have its own terms, so any actual import needs per-file license and provenance review.
- These sources describe product functions but do not establish that a K230 radio, USB port, or expansion module can reproduce them.
