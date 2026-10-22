<div align="center">
<img src="assets/zeta.png" alt="Zeta, the Reconclave mascot" width="120">

# Sightline

A touchscreen field instrument for the LILYGO T-Display K230.

Survey networks, record observations, and keep evidence together.

</div>

Sightline works locally and can participate in the [Reconclave](https://github.com/Zetascrub/Reconclave)
family as a camera, location, network and analysis node. It runs a native LVGL
interface alongside a separate HTTP discovery and protocol service.

**Status: hardware preview.** Display, touch, keyboard, Wi-Fi, local BLE and
continuous outdoor GNSS have hardware validation recorded in the engineering log.
Some assessment and remote workflows still need field validation. The latest UI
has been cross-compiled and deployed for on-board screenshot and workflow checks.
Physical touch/keyboard acceptance still needs operator verification.

## Start here

1. Open **Session** and save your project, engagement and operator details.
   New evidence uses this context.
2. To join Wi-Fi, open **Settings → Wi-Fi**, scan, choose a secured network,
   and enter its password. Ethernet can also provide the network connection.
3. Follow **1 Scan → 2 Explore → 3 Review** on Home. Choose Ethernet or Wi-Fi
   before scanning. **Evidence** holds saved records; **All tools** holds capture,
   camera and specialist tools.
4. Open **Evidence → Browse files** to inspect saved observations. Use
   **Verify + manifest** to check the timeline. **Export current session** includes
   attributed files matching your saved context; **Export entire store** also
   includes legacy and other-session files. Both create a TAR with SHA-256 hashes.

Use Sightline only where you are authorised to capture, inspect and retain data.

## Find the right tool

| I want to… | Open |
| --- | --- |
| Find nearby access points and compare channels | **Wi-Fi survey** |
| Discover hosts, ports and services | **1 Scan**, or **2 Explore → Survey LAN** |
| Record network traffic | **All tools → Packet capture** |
| Take a photograph | **All tools → Vision** |
| Browse saved observations and export a store archive | **Evidence** |
| Review noteworthy discovered services | **Findings** |
| Add location to supported evidence | **All tools → Location** |
| Track local operations, stop jobs and review their results | **Jobs** |
| Inspect remote work and trust state | **Settings → More options → Node & jobs** |
| Change Wi-Fi, brightness or device settings | **Settings** |

Wi-Fi surveys discover nearby networks; joining one is a separate action in
Settings. Surveys run in the background so navigation remains available.

See the [operator guide](docs/USER_GUIDE.md) for navigation, common workflows and
recovery steps, and the [on-board validation report](docs/VALIDATION.md) for
screenshots, tested paths and remaining hardware checks.

## Assessment workspace

**Assets** searches device history by IP, MAC, interface, port, service or banner.
Filter to assets needing review, known ports or unidentified services; sort by IP,
most recently seen or review priority. Tap a row for observation times, interface,
known services and a scoped re-check action. **Export** writes a versioned JSON
inventory and review items into Evidence, attributed to the exporting session.
The contents remain explicitly labelled **device history across sessions**.

Service labels inferred from a port number are distinguished from response-based
classification. Neither establishes a vulnerability. Quick Nmap checks 100 TCP
ports; Identify checks 50. The node automatically imports their XML results into Assets. Text reports, XML
and import receipts remain in Evidence. Asset details show the source file,
scan completion, requested port coverage and service observation times.

Only explicitly open TCP/UDP results enter the service inventory. Ambiguous
`open|filtered` results remain in raw evidence. Truncated XML is retained but
rejected, with a readable import receipt. Imports preserve historical open
observations; absence from a later scan does not establish that a port closed.
The node service must be running; queued imports survive a UI restart.

## Build and test

You need CMake 3.20 or newer, a C++17 compiler and Python 3 for host checks:

```sh
cmake -S . -B build-host -DK230_DEVICE_SYSROOT=/nonexistent -DBUILD_TESTING=ON
cmake --build build-host -j2
ctest --test-dir build-host --output-on-failure
```

The explicit sysroot setting excludes hardware-only UI targets from the host
build, even if you have already fetched the device's RISC-V libraries.

For the device, put the matching Xuantie RISC-V Linux toolchain on `PATH`, fetch
the device sysroot using [fetch-device-sysroot.sh](fetch-device-sysroot.sh), and run:

```sh
cmake -S . -B build-k230 \
  -DCMAKE_TOOLCHAIN_FILE=toolchain-riscv64-linux-gnu.cmake \
  -DBUILD_TESTING=OFF
cmake --build build-k230 -j2
```

With the device-matching LVGL source present, you can also run the headless UI
navigation tests without the board:

```sh
cmake -S tests/ui -B build/ui-tests
cmake --build build/ui-tests -j2
ctest --test-dir build/ui-tests --output-on-failure
```

These exercise nested Back navigation, dynamic result controls, hidden/disabled
controls, filter dropdowns and focus retention. Host tests also check evidence
previews, session selection, job states, subprocess cancellation and archive
interoperability using Python’s TAR reader. They do not verify physical display or touch.

The main outputs are `reconclave_k230` (protocol node) and `reconclave_k230_ui`
(touch interface). The UI requires the matching LVGL, DRM and libgpiod libraries
from the board. A host build does not emulate the hardware.

The image recipe also selects `curl` with OpenSSL, BIND's DNS tools (`dig`,
`host` and `nslookup`) and `jq` alongside Nmap, `tcpdump`, OpenSSL and `socat`.
Run `reconclave-capabilities` on the device for an installed-tool and interface
inventory (`--json` for automation); **Settings → Device info** shows the core
tool status. Presence alone does not validate Wi-Fi monitor/injection, USB
gadget mode or a capture workflow. Completed AUTO ASSESS jobs save JSON,
Markdown and CSV field reports in Evidence, with raw artifacts and SHA-256
inventory in the JSON and Markdown reports.

The installed services are `/etc/init.d/S90reconclave-node` and
`/etc/init.d/S99reconclave-ui`. For image creation, see the
[firmware guide](firmware/README.md). Building does not deploy or flash a device.

## Project map

| Location | Contents |
| --- | --- |
| `ui_app.cpp` | Screens, input, local operations and UI state |
| `src/ui_shell.*` | Shared navigation, status bars and keyboard focus |
| `src/evidence_browser.*` | Evidence inventory, session attribution, previews and archive exports |
| `src/ui_jobs.h` | Local operation states and elapsed time |
| `main.cpp` | Discovery and protocol service |
| `src/` | Network, camera, GNSS, BLE, trust and evidence implementations |
| `shared/` | Vendored Reconclave protocol and identity contracts |
| `tests/` | Host regression tests |
| `firmware/` | Image build scripts, packages and service configuration |

[Engineering and hardware history](DEVELOPMENT.md) preserves the detailed bring-up
log, hardware caveats and earlier design decisions.
[Field toolset](FIELD_TOOLSET.md) describes the assessment tools.
[K230 hardware and firmware survey](reports/K230%20field%20tool%20capability%20survey.md)
records the research, hardware limits and release priorities.
[Security policy](SECURITY.md) describes security expectations.
[Artwork licensing](ARTWORK_LICENSE.md) covers the visual assets.

The [field-trial feature backlog](docs/FIELD_TRIAL_BACKLOG.md) records assessment-driven gaps and the proposed delivery order.
