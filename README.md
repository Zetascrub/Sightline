<div align="center">

<img src="assets/zeta.png" alt="Zeta, the Reconclave mascot" width="140">

# Sightline

### See the edge. Understand the scene. Share the signal.

[![Hardware](https://img.shields.io/badge/hardware-T--Display_K230-0e222e?style=flat-square)](#hardware-and-implementation-status)
[![Build](https://img.shields.io/badge/build-CMake_%2B_Buildroot-00cdd7?style=flat-square)](#build-once-the-toolchain-is-on-path)
[![Status](https://img.shields.io/badge/status-hardware_preview-ffaa1c?style=flat-square)](#hardware-and-implementation-status)
[![Family](https://img.shields.io/badge/family-Reconclave-fff2d7?style=flat-square)](https://github.com/Zetascrub/Reconclave)

**The vision, positioning, and edge-analysis node for the Reconclave family.**

[Reconclave](https://github.com/Zetascrub/Reconclave) ·
[ZetaDongle](https://github.com/Zetascrub/ZetaDongle) ·
[FieldDeck](https://github.com/Zetascrub/FieldDeck) ·
[Relay](https://github.com/Zetascrub/Relay) ·
[Command](https://github.com/Zetascrub/Reconclave-Command)

</div>

Sightline targets the LILYGO T-Display K230. It operates as a standalone visual
field instrument and as a capability-advertising Reconclave node, contributing
camera, location, network, and local-analysis observations when available.

> Use Sightline only where you are authorised to capture, inspect, and retain
> data. Camera and location data require particular care.

## Relationship to Reconclave

- **Standalone:** the local touch UI, diagnostics, capture workflows, and
  on-device evidence remain available without a coordinator.
- **Collective:** discovery, protocol requests, trust policy, and evidence
  records support cooperation with Reconclave Command and peer nodes.
- **Shared contract:** `shared/protocol/` and `shared/identity/` are vendored
  snapshots of [Reconclave](https://github.com/Zetascrub/Reconclave), the
  canonical protocol and product-family specification.
- **Maturity:** the notes below distinguish proved hardware paths from planned
  or partially validated work.

## Hardware and implementation status

Planned role: capability-aware touch console plus edge perception node (see
`Reconclave_Design_Document_v0.1.md` §4.1 and §18). The K230 is not on the
critical path for proving Reconclave — see `docs/pre-k230-plan.md` for why the
Cardputer ADV and Unit PoE-P4 establish the protocol first.

## Approach: native app plus reproducible firmware image

The first hardware-validation phase used small native binaries on the vendor's
Linux image (the Canaan/LILYGO CanMV-K230 Buildroot + NuttX image), reusing this
repository's `shared/protocol` C++ library. That proved the protocol, display,
touch, Wi-Fi scan and evidence paths on the real board. The next phase packages
those binaries as a reproducible custom appliance image; see
`firmware/` and the Custom firmware image section below.

Confirmed from the shipped SD card image:

- Big-core OS: Linux, dynamically linked against **glibc 2.33**
  (`riscv64`, `rv64gc`, `lp64d` ABI) — not RT-Smart/CanMV-MicroPython.
- Small core: NuttX RTOS firmware (`nuttx-*-uart2.bin`).
- No mDNS or HTTP server runtime is present on the stock image, so the K230
  app must bundle its own rather than relying on the OS providing one (unlike
  the P4, which uses ESP-IDF's `espressif/mdns` managed component and
  `esp_http_server`).

Note: `docs/pre-k230-plan.md` and the design document describe WebSocket as
the intended transport, but neither the P4 (`esp_http_server`) nor the
desktop coordinator (`Reconclave Command reference implementation`, using
`zeroconf` + `http.server.ThreadingHTTPServer`) actually implement it —
both speak plain HTTP + JSON today. The K230 app should match what's
actually deployed (HTTP + JSON, `zeroconf`-discoverable mDNS), not the docs.

This means the matching cross toolchain comes from
[kendryte/k230_sdk](https://github.com/kendryte/k230_sdk) (the Linux/Buildroot
SDK), specifically its `Xuantie-900-gcc-linux-5.10.4-glibc` toolchain
(`riscv64-unknown-linux-gnu-` prefix) — **not**
[LILYGO's `T-Display-K230_canmv_rt`](https://github.com/Xinyuan-LilyGO/T-Display-K230_canmv_rt)
repo, which builds a different firmware family (CanMV on RT-Smart, a
different OS/libc) than what ships on the board today.

The toolchain (V2.6.1) defaults to `-march=rv64gcxthead`, T-Head's custom
vector-extension ABI, which links against
`ld-linux-riscv64xthead-lp64d.so.1` — **not present on-device**. The plain
`ld-linux-riscv64-lp64d.so.1` interpreter the device actually ships is a
different multilib within the same toolchain; pass `-march=rv64gc
-mabi=lp64d` to select it (already set in
`toolchain-riscv64-linux-gnu.cmake`). The running device (Buildroot
2025.02.1, kernel 6.6.36, built with toolchain V3.0.2) is newer than
ours (V2.6.1), but userspace glibc is still 2.33 on both, which is what
actually matters for a prebuilt binary to run — confirmed by executing
`reconclave_k230` on real hardware over SSH, not just under
`qemu-riscv64-static`.

### Device access

The stock image runs an SSH server with **passwordless root login** on
whatever network it picks up (DHCP over Ethernet in testing;
`ap.sh`/`sta.sh` in `/bin` suggest Wi-Fi is also configurable). No serial
adapter needed for day-to-day work. The board's USB OTG port did not
enumerate as any USB device when connected to a PC (tested with both the
Power and OTG ports connected) — it likely defaults to USB host mode
rather than device/peripheral mode, so it is not a console option; UART2
via the 40-pin header remains the fallback if SSH/network access is ever
unavailable (e.g. recovery), but is unexplored since SSH already works.

**Passwordless root SSH is a real exposure** once this device is on a
shared or untrusted network — worth locking down (key-only auth, or a
password) before this leaves a bench/lab setting, separately from
Reconclave's own trust bootstrap (milestone 9 below).

### Autostart

Both `reconclave_k230` (installed on-device as `/usr/bin/reconclave-k230-node`)
and `reconclave_k230_ui` (`/usr/bin/reconclave-k230-ui`) now autostart via
`/etc/init.d/S90reconclave-node` and `/etc/init.d/S99reconclave-ui`
respectively - tracked in this repo at
`firmware/rootfs-overlay/etc/init.d/` (this section previously described an
older `S90reconclave` name and claimed the UI still needed manual starts;
both were stale - the device has moved on since). `S90reconclave-node`
confirmed surviving real reboots — **twice** on the first attempt, since the
app itself had a real bug: it started checking for a non-loopback IPv4
address immediately, but the init script runs right after `S40network`
brings the interface up, before DHCP (`udhcpc`, backgrounded) has actually
completed - the app would see no address yet and exit. Fixed in `main.cpp`
by retrying for up to 20 seconds instead of checking once, matching the
same wait-and-retry pattern `S99zz_k230_phone_ui`'s own script already uses
for `/dev/dri/card0`.

**Both init scripts had a real bug of their own, found and fixed**: each
used `start-stop-daemon -S -b -m -p "$PIDFILE" -x "$DAEMON" -- >>"$LOGFILE" 2>&1`,
which looks like it redirects the daemon's stdout/stderr into `$LOGFILE` but
does not - `-b` backgrounds and detaches the daemon's own stdio as part of
daemonizing, independently of whatever fds the parent shell had redirected,
so the shell-level `>>` only ever captured `start-stop-daemon`'s own
output, never the daemon's. Both `$LOGFILE`s were **silently empty in
production the entire time**, regardless of what either binary printed -
not a buffering issue (though see below), a detachment one. Fixed by using
BusyBox's dedicated `-O "$LOGFILE"` flag instead, which makes
`start-stop-daemon` itself append the daemon's real stdout/stderr.
Confirmed fixed on real hardware: `reconclave-k230-ui`'s log now shows its
own startup lines including live keyboard/battery status, and
`reconclave-k230-node`'s shows its device-id/port and shutdown lines.

Separately, `main.cpp` and `ui_app.cpp` had a handful of `std::printf()`
startup diagnostics going to `stdout` instead of `stderr`, unlike every
other diagnostic in both files - harmless when stdout is a tty, but
fully-buffered once redirected to a regular file, so those specific lines
would still never reach the log even after the `-O` fix, sitting in the
buffer until the process happened to exit. Switched to `std::fprintf(stderr, ...)`
to match the rest of both files' convention. Both bugs were compounding:
either alone still left the log empty.

## Build (once the toolchain is on PATH)

```sh
cmake -B build-k230 -S . \
  -DCMAKE_TOOLCHAIN_FILE=toolchain-riscv64-linux-gnu.cmake \
  -DRECONCLAVE_BUILD_K230=ON -DRECONCLAVE_BUILD_TESTS=OFF
cmake --build build-k230
```

Produces `reconclave_k230`, a `riscv64`/glibc binary intended to run directly
on the device's existing rootfs.

## Milestones

1. **Toolchain + protocol proof — done.** A headless binary that links
   `shared/protocol`, builds a `NodeAnnouncement`, and validates it. No
   networking, display, or camera yet. This is `main.cpp`,
   cross-compiled and verified running on real hardware over SSH.
2. **Network proof — done.** A bundled mDNS responder
   ([mjansson/mdns](https://github.com/mjansson/mdns), vendored at
   `third_party/mdns.h`) and a small hand-rolled HTTP/1.1
   server answer exactly `GET /reconclave/v1/announce` and
   `POST /reconclave/v1/message` with `system.info`, matching
   `Reconclave Command reference implementation`'s contract byte-for-byte. Verified
   on real hardware over the real network: `avahi-browse` and Python's
   `zeroconf` (the exact library the desktop coordinator uses) both resolve
   `rc-k230-poc._reconclave._tcp.local.` to the right address/port/TXT
   record, and `system.info` round-trips correctly including two rejection
   edge cases (unknown capability, missing `payload`). This is the
   `discover -> advertise -> request -> execute -> response` success
   condition from `Reconclave_Design_Document_v0.1.md` §19, achieved
   node-to-node over the network rather than against a mock. Small JSON
   (`src/json.h`) and HTTP (`src/http_server.h`) layers were hand-rolled
   rather than vendored, since the device ships neither and the actual
   contract surface is small; mDNS/DNS-SD was vendored instead, since
   hand-rolling that protocol well enough to interoperate reliably with
   `zeroconf` was a worse risk/effort trade than pulling in a small,
   widely-used public-domain implementation.
3. **First real recon capability — done (scanner; dispatch pending scope
   work).** `radio.wifi.scan` (`src/wifi_scanner.{h,cpp}`), matching
   `FieldDeck`'s capability of the same name and CSV evidence
   shape (`ssid,bssid,channel,rssi_dbm,security,observer_node`). Verified on
   real hardware via `iwlist wlan0 scanning` (no shell involved - direct
   fork+exec), including bringing the interface up itself first (`wlan0`
   starts down on every boot) and correctly handling a hidden-SSID AP.
   Advertised in the announcement but **not** wired into the message
   dispatcher: `docs/capabilities.md` classifies it `Assessment` permission,
   not `system.info`'s read-only/non-sensitive class, and Cardputer itself
   keeps it local-UI-only rather than remotely dispatchable for the same
   reason - engagement-scope enforcement needs to land first. Verified this
   holds: the capability appears in `GET /reconclave/v1/announce`, and a
   `POST /reconclave/v1/message` invoking it correctly gets
   `rejected/CAPABILITY_UNAVAILABLE`, not a live scan.
4. **Keyboard and expansion base — live and integrated.**
   LILYGO's current BSP identifies this as the nRF9151 cellular/GNSS/keyboard
   base. The TCA8418 is at I2C4 address `0x34` (SDA GPIO47, SCL GPIO46, IRQ
   GPIO42), alongside the BQ25896 charger and BQ27220 battery gauge. The
   nRF9151 GNSS/cellular module uses `/dev/ttyS3` at 115200 baud and GPIO2 as
   its enable line. Earlier work against an older image incorrectly treated
   the expansion bus as unavailable and probed `/dev/i2c-0`; that address
   `0x37` is the GC2093 camera, not the keyboard. The current firmware now
   reports expansion runtime availability. The TCA8418 now drives LVGL
   focus, activation, back navigation and text input; the BQ27220 supplies
   voltage and fuel-gauge telemetry; and GNSS NMEA acquisition is active.
   Note that the product-wiki pin illustration labels a separate GPIO32/33
   I2C path and GPIO46 interrupt for the pictured expansion. On this unit,
   live probing is decisive: GPIO46/47 returns real TCA8418 key events and
   BQ27220 telemetry, while GPIO32/33 returns no key events. Keep the BSP's
   revision-specific I2C4 mapping unless a different board is detected and
   verified at runtime.
5. **Display — done (proof).**
   No fbdev; DRM/KMS only (`/dev/dri/card0`). Decided against vendoring/
   building our own LVGL: `k230_phone_ui` already ships a proven, working
   LVGL 9.5.0 + libdrm combination on this exact hardware
   (`/usr/lib/liblvgl*.so`, `/usr/lib/libdrm.so.2.124.0`), so UI targets
   link against those actual on-device libraries instead. Regenerate the
   local build sysroot with `fetch-device-sysroot.sh` (pulls
   matching LVGL v9.5.0 and libdrm-2.4.124 headers, kendryte/k230_sdk's own
   `lv_conf.h` for this exact Buildroot package
   (`src/little/buildroot-ext/package/lvgl/port_src/lv_conf.h`), and the
   real `.so` files off the device — not committed, regenerate locally).
   `lv_init()` from a cross-compiled binary linked this way runs
   successfully on real hardware (exit 0, no crash) — meaningful because a
   `lv_conf.h` mismatch with what `liblvgl.so` was actually built with
   would very likely crash immediately given how much internal state LVGL
   sets up during init. `--allow-shlib-undefined` is required at link time:
   `liblvgl.so`'s transitive dependencies (ffmpeg, freetype's libpng,
   OpenSSL, ...) go many layers deep and are all genuinely resolvable by
   the real device's dynamic linker, so they aren't all pulled into the
   local sysroot too.

   `drm_proof.cpp` opens `/dev/dri/card0` via
   `lv_linux_drm_create()`, reads touch via `lv_evdev_create()` on
   `/dev/input/event1`, and renders one screen (title label + a button
   that toggles colour on touch). **Verified on the physical panel**: the
   screen displays it correctly and touch toggles the button reliably
   (operator-confirmed on the device, with matching
   `touch: button toggled -> ...` lines in the log for every tap). Native
   panel mode is 568x1232 (portrait) — `k230_phone_ui`'s 270-degree
   rotation to a 1232x568 landscape layout is not replicated yet, deferred
   to whichever screen actually needs landscape.

6. **Touch UI (§18 mockup) — done.** `ui_app.cpp`: status bar,
   the six capability tiles (NETWORK/WIRELESS live; RECON/VISION visibly
   present but marked unavailable rather than silently omitted, matching
   §18's own capability-aware principle; EVIDENCE/DEVICES live), touch
   navigation with a consistent back-button affordance, the Zeta Mascot
   palette. WIRELESS wires the real scanner in
   (`src/wifi_scanner.cpp`, live scan + result list); EVIDENCE exports the
   last scan to CSV under `/root/reconclave/evidence/` (this board has no
   separate removable microSD slot - confirmed only one `mmc` host has a
   card - so "evidence storage" here means a directory on the same
   persistent storage as everything else, not a swappable card like
   Cardputer's evidence workflow); DEVICES shows live system diagnostics.
   Every element keeps a `kSafeMargin` clearance from all four screen
   edges, not just the corners specifically - the RM69A10 panel's
   physically rounded corners clip anything placed flush against an edge,
   confirmed by an early layout attempt getting visibly cut off.

   **Landscape rotation works**, via a real find worth recording: the
   panel's native mode is 568x1232 portrait, and upstream LVGL's
   `lv_linux_drm.c` (v9.5.0) has no 90/270 rotation support at all (its
   DIRECT-mode buffers are sized to the native mode before rotation is
   ever considered). A from-scratch fix was attempted first
   (`third_party/lvgl_drm_rotate/` - kept for its documented
   history, not compiled into the app) and, after fixing one real bug
   (a resolution double-swap), still had an unresolved pixel-mapping bug
   after several hardware iterations. Before digging further, `nm -D` on
   `k230_phone_ui`'s own binary settled it: **Canaan/LILYGO's own
   `liblvgl.so`** (the core library, not `liblvgl_linux.so`) **already has
   a natively-built, working `lv_linux_drm_set_rotation`** - that's what
   `k230_phone_ui` itself calls for its Display > Rotation setting.
   `ui_app.cpp` declares that one function itself (`extern "C"`, it's
   missing from the public LVGL header) and links against the vendor's
   proven implementation instead. Confirmed correct on the physical panel:
   landscape orientation and touch both work. Lesson for future K230 work
   here: when something seems unsupported upstream, check whether Canaan/
   LILYGO already patched their own build (`nm -D` against
   `k230_phone_ui` and its linked `.so`s) before reimplementing it.

   **UI polish pass, done.** Mascot branding: `assets/` (own
   `LICENSE` file, matching `FieldDeck/assets/`'s convention -
   the art itself is not MIT-licensed, see `ARTWORK_LICENSE.md`) holds a
   56x56 status-bar icon generated from
   `Zeta_Mascot_Headshot_transparent.png` via LVGL's own
   `scripts/LVGLImage.py` (ARGB8888 C array,
   `assets/zeta_k230_logo.{h,c}`) - reusing LVGL's real converter rather
   than hand-rolling the binary image format avoided a repeat of the
   rotation-style debugging cycle. The home-screen tile grid was rebuilt
   twice: the first version used LVGL's `LV_LAYOUT_GRID` (`lv_obj_set_grid_cell`
   etc.), which produced garbage on-device geometry (`h=268435442`-style
   huge values, despite `LV_USE_GRID` being enabled in `lv_conf.h`) —
   confirmed via logging each tile's actual `x/y/w/h` after a forced
   layout pass, not guessed from photos. `k230_phone_ui`'s own binary
   uses `lv_obj_set_flex_flow` (`strings` search), not grid, for its own
   layouts, so `ui_shell.cpp`'s `createTileGrid()`/`createTile()` use
   `LV_FLEX_FLOW_ROW_WRAP` with an explicitly computed fixed tile size
   instead - confirmed correct via the same diagnostic logging, then
   visually. Correction from the now-confirmed real `lv_conf.defaults` (see
   "Device sysroot" below): `LV_USE_SYSMON` is actually `1` on this device,
   only `LV_USE_PERF_MONITOR` is `0` - this section originally guessed
   SYSMON itself was off, inferred indirectly from the symbol issue below
   rather than read from the real config. Do not call the vendor-only
   `lv_sysmon_hide_performance()` symbol regardless: clean SDK builds may
   leave that optional weak symbol unresolved, causing a null-function call
   at startup.
7. **Perception.** Camera capture, OCR, object detection via the vendor KPU
   runtime. `/dev/video0`-`video4` plus dedicated ISP/VPU device nodes
   confirmed present.
8. **BLE — working through the onboard nRF52840.** The initial investigation
   followed the loaded `aic_btusb` driver and found no matching USB device or
   `hci0`. LILYGO's K230 nRF52840 AT guide documents the actual BLE route on
   this hardware: `/dev/ttyS1` at 115200 baud. On this unit, `AT+VER?`
   returned `K230_NRF52840_AT,2026-08-16-k230-uart-dfu-r3`, `AT+STATUS?`
   reported `READY`, and `AT+SCAN=5` found nearby BLE peripherals. The
   Bluetooth Settings row now opens a BLE screen backed by this UART link;
   it scans, lists devices, and allows a user to connect or disconnect.
   The AIC USB endpoint remains absent, so BlueZ still has no `hci0` and
   Linux applications expecting an HCI adapter cannot use that path.
9. **Trust bootstrap - device side done, fleet-tool side still open.** A
   64-character hex execution key generated on-device from `/dev/urandom`
   and written to `/root/reconclave/trust/execution.key` (the path
   `main.cpp` already reads by default) is enough on its own: restarting
   `reconclave-k230-node` picked it up, `security.mode` in
   `GET /reconclave/v1/announce` moved from `"unprovisioned"` to
   `"hmac-sha256-128"`, and every previously advertised-only capability
   (`radio.wifi.scan`, `net.discovery.scan`, `assessment.findings.snapshot`,
   ...) now reports `permission: "trusted"` instead of being absent from
   dispatch. Confirmed this isn't just cosmetic: a `radio.wifi.scan` request
   with no `auth` block still gets `rejected`/`UNAUTHENTICATED` - the device
   enforces its own signature check regardless of what generated the key.
   What's still missing is the other half of the pairing: nothing outside
   this device knows this key yet, so nothing can actually construct a
   valid signed request against it. That's `tools/provision_fleet.py`
   (lives in the sibling `Reconclave` repo, not here) - adding K230 support
   there so a coordinator is issued/registers the same key remains
   unstarted follow-up work.

## Boot splash — paused, unresolved, needs serial console to continue

Wanted: custom branded art (from the maintainer's private mascot workspace, not in this
repo) shown during boot, before Linux/our app are running. Two real fixes
were applied and verified correct against source; the splash still doesn't
show. Documenting the full path here so this doesn't need re-deriving.

**Layer 1 - the boot logo file itself, done and verified, but confirmed
inert on this device.** `/boot/logo.xrgb` on the SD card's boot partition
is a **raw, headerless 568x1232 XRGB8888 framebuffer dump** (no PNG/BMP
structure at all - `RM69A10_LOGO_WIDTH * RM69A10_LOGO_HEIGHT * 4` bytes,
confirmed from source, see Layer 2). Converted the user's splash art with
ImageMagick (`magick input.png -resize 568x1232! -depth 8 BGRA:logo.xrgb`
for the portrait version; the landscape version needs `-rotate 270` first,
matching the same rotation direction as `LV_DISPLAY_ROTATION_270` above,
since the panel's raw scan order is always native-portrait regardless of
how the content should look to a keyboard-attached, landscape-holding
viewer). Byte order (BGRX, i.e. `magick ... BGRA:` with alpha discarded)
was **not** guessed - verified by decoding the *original* stock
`logo.xrgb` this same way first and confirming it rendered as the correct,
correctly-coloured LILYGO logo before ever writing anything. Original
backed up to `/boot/logo.xrgb.orig-backup` on-device before overwriting.
Despite this being verified byte-correct, the device showed a **blank
screen then straight into `k230_phone_ui`** on reboot - no splash phase at
all, which Layer 2 explains.

**Layer 2 - root cause: this device's actual flashed U-Boot has no
boot-logo code at all.** Read the raw sectors before partition 1 starts
(`fdisk -l /dev/mmcblk1` shows partition 1 at LBA 61440; U-Boot lives in
the raw space before that) and searched for `logo.xrgb`/`logo.yuv`/
`CONFIG_K230_BARE_DISP_LOGO` strings - **zero matches**. The feature isn't
compiled in on this exact firmware build, regardless of what's in the
boot-partition files. This is a **hardware-independent, no-reboot-needed**
diagnostic worth remembering: reading raw pre-partition sectors and
grepping for strings tells you definitively what a bootloader does or
doesn't contain, which a boot-cycle-and-look-at-the-screen test cannot
(silence is ambiguous - it could mean "ran and failed" or "never ran").

**Layer 3 - building a custom U-Boot with the feature.** The actual
source for this board's U-Boot is **not** in the public `kendryte/k230_sdk`
(no RM69A10 anywhere in it - confirmed via GitHub code search) - it's a
LILYGO-authored overlay in a **third, previously-unfound LILYGO repo**:
[`Xinyuan-LilyGO/T-Display-K230`](https://github.com/Xinyuan-LilyGO/T-Display-K230)
(distinct from both `T-Display-K230_canmv_rt`, the RT-Smart/CanMV repo
ruled out early on, and the generic `kendryte/k230_sdk`). Its
`k230_bsp/README.MD` documents the full pipeline precisely, including a
pinned upstream commit - this is the actual BSP the shipped image was
built from:

- Base: `kendryte/k230_linux_sdk` at commit `22d02c6b6783a57a3aca7eb3160e313e772cb710`
- Overlay: `Xinyuan-LilyGO/T-Display-K230`'s `k230_bsp/overlay/`, applied by plain `rsync`
- U-Boot itself: the overlay's `buildroot-overlay/boot/uboot/u-boot-2022.10-overlay/`
  is rsync'd onto a downloaded **official** `https://ftp.denx.de/pub/u-boot/u-boot-2022.10.tar.bz2`
  (confirmed via `buildroot-overlay/boot/uboot/uboot.mk`'s `UBOOT_OVERLAY_DIRS`/rsync rule)
- Defconfig: `configs/k230_canmv_t_display_defconfig` (added by the overlay), which
  does have `CONFIG_K230_BARE_DISP_LOGO_RM69A10=y` and `CONFIG_LAST_STAGE_INIT=y`

Reproduced locally (sparse-cloned, not full clones - both source repos are
large) at `.toolchains/u-boot-build/` (gitignored, not committed - the
patches below are only in that working tree, not preserved anywhere else
yet). Built with the same Xuantie toolchain as the app
(`riscv64-unknown-linux-gnu-`) via plain `make ARCH=riscv
CROSS_COMPILE=riscv64-unknown-linux-gnu- k230_canmv_t_display_defconfig`
then `make -jN`. One host-side build fix was needed, unrelated to the K230
itself: U-Boot 2022.10's `mkimage` signing tools (`lib/rsa/rsa-sign.c`,
`lib/aes/aes-encrypt.c`) use OpenSSL's `ENGINE` API, whose header
(`openssl/engine.h`) is gone in this machine's OpenSSL 3.5 even though the
`ENGINE` *type* itself still exists (via `openssl/types.h`) - patched both
files to guard the include behind `OPENSSL_VERSION_NUMBER < 0x30000000L`
and stub the handful of `ENGINE_*` functions actually called for
OpenSSL >= 3.0 (mkimage's `-engine`/PKCS11 flag was never going to be used
anyway; `CONFIG_FIT_SIGNATURE` itself was left enabled rather than
disabled, since that's a real boot-chain behaviour, not just host tooling).

**Flashing.** The SDK's own build produces a demonstration `sd.iso`
showing the exact recipe: SPL (`u-boot-spl-k230.bin`, wrapped with a
`firmware_gen_no_securiy.py`-generated "K230" header) at raw offset
`0x100000`, and the main image (`fn_u-boot.img`, gzip'd U-Boot wrapped
with `mkimage` then the same K230 header) at `0x200000`. Verified this
matches the *real device* before writing anything: read 16 bytes at both
offsets on-device first and found the literal `K230` magic at both,
confirming the offsets are correct for this device's actual layout, not
just the build's assumption. **Three independent recovery layers** were
established/confirmed before flashing anything: (1) the full SD card
image backup (stored outside the repository, from early in
this project), (2) a fresh raw-region backup of just the first 30MB
(a separate boot-region backup, covering both bootloader
stages), (3) the K230 SoC's own boot ROM automatically falls into a USB
recovery/burning mode if it fails to boot from storage at all (confirmed
via the official Kendryte docs, independent of anything on the SD card -
a hardware-level fallback, not something a bad flash could disable).
Flashed via `dd ... seek=$((0x100000/512)) conv=notrunc,fsync` and the
same for `0x200000`, then verified with a checksum read-back before every
reboot.

**First boot with the custom U-Boot**: successful - Linux booted
normally, both `reconclave_k230` and `k230_phone_ui` came up via their
init scripts, no regression at all. But still no splash.

**Layer 4 - second root cause, found and fixed, still didn't resolve it.**
`k230_logo.c`'s loader does `ext4load mmc ${mmc_boot_dev_num}:1 ... /logo.xrgb`,
falling back to hardcoded `mmc 1:1` then `mmc 0:1` if that env var is
unset. Compared `board/canaan/k230_canmv/board.c` (our board) against
sibling board files (`k230_canmv_01studio`, `_dongshanpi`, `_gt6700`,
`_mrt`, `k230_evb`) and found every one of them sets
`env_set_ulong("mmc_boot_dev_num", g_bootmod - SYSCTL_BOOT_SDIO0)` in
`board_late_init()` - **except ours**, which has a `board_late_init()`
already (for WiFi reset) but is simply missing that one line. This isn't
a guess: `g_bootmod - SYSCTL_BOOT_SDIO0` is the same expression already
used in `board/canaan/common/k230_img.c` to find the boot MMC device for
loading the *kernel* - which we know works, since Linux boots. Added the
missing line, rebuilt, reflashed (same verified process as above),
rebooted. **Still blank screen, still straight to the demo UI.** Real,
source-verified fix, confirmed not to be the (or not the only) blocker.

**Current status: stopped here.** Every path forward from here needs to
see U-Boot's own console output (`printf`s already exist in
`k230_logo.c` for exactly this - "logo.xrgb load failed", "size
mismatch", etc.) to know *why* it's still not drawing anything, and this
board's OTG port isn't a console (confirmed much earlier - behaves as USB
host, not device). Continuing without that would mean guessing through
more multi-minute reboot cycles with no better signal than "worked" or
"didn't", which is exactly the unproductive pattern flagged before
starting the U-Boot rebuild at all. **A USB-to-TTL serial adapter wired to
the UART2 pins on the 40-pin header (3.3V only, per the K230 FAQ's
explicit 5V warning) is the actual next step**, not more blind rebuilds.

The currently-flashed custom U-Boot is left in place (it boots correctly,
no regression versus stock) rather than reverted, since it's a strict
superset of stock behaviour and the fixes are real and may matter for a
future attempt. The original stock U-Boot is recoverable from the raw
region backup above if ever needed. The build tree with both patches
already applied is at `.toolchains/u-boot-build/` locally (gitignored) -
regenerate via the recipe above rather than trusting that directory to
still exist in a future session.

## Vision / camera preview — parked, on-screen thumbnail unresolved

The GC2093 camera itself works and is confirmed end-to-end: the ISP capture
pipeline (`/dev/video2`, the `vvcam-isp`/`vvcam-mipi` driver stack) produces
real frames, and `ffmpeg` (present on-device, `--enable-libv4l2`) captures
and JPEG-encodes a still in about a second. `src/camera_capture.cpp`
does this via a bounded `execlp("ffmpeg", ...)` subprocess. The VISION
screen's **Capture photo** button uses this directly and works correctly -
it saves a real, valid JPEG into the evidence store every time.

What doesn't work yet: showing that image *on the device's own screen*.
Two separate bugs were found and fixed on the way, and a third,
unresolved issue remains:

1. **Sizing bug (fixed)**: the preview thumbnail's "fit to box" scale was
   computed from the `lv_image` object's own (pre-load) size, which is
   meaningless before an image is loaded. Fixed by sizing against the
   image's *parent* panel instead (see `setImageContain()`/`showLastPhoto()`).
2. **JPEG format bug (fixed, real root cause of the literal black
   screen)**: ffmpeg's auto-negotiated pixel format for this sensor's
   native 4:2:2 output encodes a JPEG with Cb/Cr sampling factor `0x12`.
   That's valid JPEG, but LVGL's on-device decoder is TJpgDec - a
   deliberately minimal embedded decoder (confirmed via the vendored
   `tjpgd.c` source: `lib/tjpgd/tjpgd.c`'s `jd_prepare()`) that only
   accepts Cb/Cr sampling factor `0x11` and returns `JDR_FMT3` ("not
   supported JPEG standard") for anything else - silently, with no
   visible error, just a black image. Fixed by forcing
   `-pix_fmt yuvj420p` on the ffmpeg command, which encodes standard
   4:2:0 (`0x22`/`0x11`/`0x11`) that TJpgDec accepts. Confirmed via a
   temporary `lv_image_decoder_get_info()` probe logging `JDR_FMT3` (as
   an LVGL `[Warn] ... jd_prepare error: 8 lv_tjpgd.c:114` line) before
   the fix, and a clean decode (`result=1`, correct 1920x1080 dimensions)
   after it.
3. **Still black, cause unresolved**: even with both fixes and correct
   decode results confirmed by direct probing, the thumbnail still didn't
   visibly render on the physical screen. Not yet root-caused - candidate
   explanations not yet checked: an LVGL image-cache/redraw-invalidation
   issue specific to swapping a file-backed image's source repeatedly, a
   z-order/opacity issue in how the preview panel's placeholder hint label
   and the image widget stack, or something specific to this on-device
   LVGL build's TJpgDec/draw-buffer integration that a decode-success
   result doesn't capture. A serial console (see the Boot splash section
   above - same underlying need) or building a tiny standalone
   LVGL+TJpgDec test program (bypassing this app entirely) would be the
   next real steps, not further guessing via redeploy cycles.

**Separately, a serious operational finding**: while iterating on a
~1.2s-interval automatic live-preview loop (calling `captureStill()`
directly from an LVGL timer on the main UI thread), the device's network
stack went fully unreachable (ICMP included, not just SSH) and required a
physical power cycle to recover - not just an unresponsive app, an
apparent full board hang. `vvcam_isp` is an out-of-tree, kernel-tainting
module (see its own probe log line), and the leading hypothesis was that
rapid repeated open/close of the camera device was destabilizing it. A
follow-up controlled test - 60 consecutive `ffmpeg` capture cycles over
SSH alone (no GUI process running), roughly matching the app's own
cadence - completed cleanly with zero failures, and a further ~4-minute
soak test with the GUI's capture work moved onto a background thread
(`previewWorkerLoop()`/`checkPreviewWorker()` in `ui_app.cpp`, replacing
the blocking-timer design) also showed no reachability drops. So the hang
was likely specific to blocking the single UI thread for the majority of
every cycle (starving `lv_timer_handler()`, which also dispatches touch
input and - per the DRM driver - display refresh) rather than the camera
driver itself being unable to tolerate cycling; that's not proven,
though, only the least-bad explanation the recreations available so far.

Given both the unresolved black-screen bug and that operational history,
the automatic live-preview loop is **not wired to auto-start** and its
**Preview button is disabled** in the current build - parked rather than
deleted (`startPreviewWorker`/`checkPreviewWorker`/`previewWorkerLoop`
are still present and correct as far as they've been tested). Capture
photo, which does one deliberate, bounded, synchronous capture per tap
with no loop, is unaffected and is the recommended way to use the camera
until this is revisited.

## Driver parity check against the stock firmware backup

To check whether the custom firmware rebuild was missing any drivers, the
full stock SD card backup (stored outside the repository,
~15.6GB uncompressed) was decompressed and its two partitions extracted
with `dd` (boot: sectors 61440-225279; rootfs: sectors 262144-30468750,
from the stock image's own partition table), then mounted read-only with
`fuse2fs` (no root/loop device needed - useful for future backup
inspection without `sudo`, which isn't available in this environment).
`debugfs -R "rdump ..."` was used to pull specific files/directories out
regardless of Unix permissions (e.g. `/root/app/...`, otherwise
unreadable as a non-root mount).

Findings:

- **Kernel modules**: exactly identical - `find -name '*.ko'` under
  `/lib/modules` produced the same 288 files on both, byte-for-byte
  matching names. Nothing is missing from the custom rebuild.
- **DTB**: `k230-canmv-rm69a10.dtb` is byte-identical (same md5) between
  stock and current. Hardware description hasn't changed; any gaps are
  about firmware/config, not devicetree.
- **BT/Wi-Fi firmware blobs**: identical file sets under
  `/lib/firmware/aic8800*` (config `.txt` and the actual `.bin`
  blobs - the first pass of this check used `find -iname '*aic*'`,
  which only matches basenames and missed every `.bin` file since
  they're named e.g. `fmacfw.bin`, not `aic*.bin`; re-run without that
  filter to get the real picture).
- **The devicetree has no BT/GNSS/modem/keyboard-controller/LoRa nodes at
  all** - grepping every `compatible = "..."` string in the decompiled
  DTB turns up only real SoC/board peripherals (sensors, ISP, MIPI, USB
  OTG, etc.). AIC8800 (Wi-Fi/BT) and the TCA8418 keyboard aren't DT
  devices on this board (USB/SDIO enumeration and GPIO-bit-banged I2C
  respectively, as already documented elsewhere in this file) - matches
  what's implemented.
- **BT driver investigation (historical)**: `aic_btusb` loads but its USB
  endpoint is absent, so BlueZ has no `hci0`. Wi-Fi uses the AIC chip over
  SDIO. This observation remains true, but the K230's working BLE path is
  the separate nRF52840 UART bridge described in milestone 8 above.
- The vendor's own software references an **nRF9151** modem
  (`[nrf9151] IO2 iomux ...` in `k230_phone_ui`'s own startup log, see
  below) - `src/gnss_receiver.cpp`'s Nordic-style AT command set wasn't
  speculative after all, though it's still unconfirmed against this
  board's actual `/dev/ttyS3`.

## Reviving k230_phone_ui from the backup, and the Settings screen

The vendor demo app (`k230_phone_ui`, single ~6MB binary, no separate
per-app executables - "apps" are internal screens/pages within it) was
extracted from the stock rootfs backup (same `debugfs rdump` approach as
above, path `/root/app/k230_phone_ui/`) and actually **run directly on
the current custom firmware** - not just statically analysed. Every one
of its 18 shared library dependencies (`readelf -d`, checked against
`find` on the live device) already exists on the custom image, so it
just worked: `HOME=/root ./k230_phone_ui` from its own directory, no
LD_LIBRARY_PATH juggling needed. This is a generally useful technique for
future UI/behaviour questions about the stock app - cheaper and more
reliable than reverse-engineering strings from the binary.

Its Settings app's structure (confirmed via on-device photos, navigated
live) is grouped sections, each a bold title + grey subtitle, containing
rows with an icon, bold title, grey description, and a chevron (or an
inline toggle/slider for a couple of rows):

- **Connections** (Network, Bluetooth and modem): Wi-Fi, Ethernet,
  Bluetooth, Cellular, USB Modem
- **Display & input** (Screen, language and keyboard): Display, Language,
  Date & time, Keyboard settings, Edge back (toggle)
- **Sound & hardware** (Audio, sensors and power): Audio, Sensors
  (AHT20), Charger (BQ25896), Battery
- **System & about** (Device information and diagnostics): System,
  About phone

`ui_app.cpp`'s new `buildSettingsScreen()` follows this same
visual pattern (`addSettingsSection()`/`addSettingsRow()`) but doesn't
duplicate the home tiles that are already full top-level tools (Recon,
Wireless, Evidence, ...) - it covers what didn't otherwise have a
discoverable home: **Connections** (links to Network/Wireless/Location),
**Display & hardware** (Brightness, moved here from DEVICES - which is
now diagnostics-only, matching phone_ui's own System/Display split),
**Trust & security** (execution-key status, new - previously only
visible by reading `/run/reconclave/node-status` directly or via the
NODE screen's raw text dump), and **System & about** (links to Device
info/Node & jobs).

Process hygiene when reviving/testing `k230_phone_ui` on real hardware:
stop `S99reconclave-ui` and kill any stray `reconclave-k230-ui`/`ui-test`
processes *by PID* first - `pkill -f` matched against `./k230_phone_ui`
has been unreliable in this session when the process was launched with a
relative path (argv[0] didn't match the full-path pattern), leaving a
stray process that then fights a newly-launched one for the DRM device
(`drmModeAtomicCommit failed: Permission denied` is the symptom - two
clients contending for DRM master). Verify with `ps w | grep <name>`
before assuming a kill worked.

## A real LVGL alignment bug found while adding the SETTINGS tile

Adding a 10th home tile (see below) meant changing the tile grid from 3x3
to first 3x4, then 5x2 (5 columns matches `k230_phone_ui`'s own home
screen layout exactly, confirmed via its touch-trace log:
`HOME_LAYOUT ... cols=5 tile=156x118`, captured while reviving it above -
divides 10 tiles evenly, unlike 3x4's orphaned single tile in an
otherwise-empty last row). That made tiles taller (~224px vs ~144px),
which is what actually exposed a **real, previously-invisible bug**: each
tile's subtitle text (`LV_ALIGN_BOTTOM_LEFT`, via `createTile()` in
`src/ui_shell.cpp`) rendered bunched up near the top of the tile, right
under the title, instead of near the actual bottom.

Root-caused with `lv_obj_get_y()`/`lv_obj_get_content_height()` probes
(not guessed) to two compounding issues:

1. `lv_obj_align()` (the plain, non-`_to` function used almost
   everywhere in this codebase) does **not** compute a position
   immediately in this LVGL build - reading its own source
   (`lv_obj_pos.c`) shows it just sets a style "align" property plus a
   raw offset, for the layout system to resolve later. `lv_obj_align_to()`
   is a genuinely different function that computes the position
   immediately (confirmed by reading its body: it calls
   `lv_obj_get_content_height(base)` etc. directly and calls
   `lv_obj_set_pos()` with the final numbers) - switched the subtitle to
   this.
2. That immediate computation depends on `lv_obj_get_content_height()` of
   the tile, which was returning **78 against an actual 224px-tall
   tile** - because `createTile()`'s tile object never had its padding
   explicitly zeroed (`lv_obj_set_style_pad_all(tile, 0, 0)`), unlike
   *every other* container in this file, so it was silently inheriting a
   large default theme padding. `LV_ALIGN_TOP_LEFT` (the title) doesn't
   depend on content height at all, which is exactly why it looked fine
   and masked this for a long time - anything anchored BOTTOM/RIGHT/
   CENTER on a container that skips explicitly zeroing padding is worth
   treating as suspect elsewhere in this codebase too, not just here.

Both are now fixed in `createTile()`. Worth knowing for next time: reading
position back via `lv_obj_get_y()`/`_get_x()` **immediately** after
`lv_obj_align()`/`align_to()`/`set_pos()` is unreliable even when the
underlying fix is correct - `lv_obj_set_pos()` stores the target but the
object's real `coords` (what `get_x`/`get_y` actually report) only
update on the next layout/render pass, not synchronously. The
`content_height` reading (a `lv_obj_set_size`-driven property, not a
deferred position) was trustworthy immediately; the `_y`/`_x` readings
after realigning were not - don't chase a "still wrong" diagnostic
number after fixing something that's provably a static/`_size`-based
property; verify position-related fixes visually instead.

## Keyboard-driven navigation

Milestone 4/6 above already had most of this working: `main()` creates an
`lv_group_t` and calls `lv_group_set_default()` before any screen is built,
so every focusable widget on all 18 screens auto-joins it; an
`LV_INDEV_TYPE_KEYPAD` indev is wired to the TCA8418 via
`pollHardwareKeyboard()`/`readHardwareKeyboard()`; Esc dispatches directly to
`activateScreenBackButton()`; letters/numbers/symbols (with shift/caps
state) feed focused textareas. The Cardputer-parity table's "Not started"
row for this was simply never updated once that landed.

**Confirmed on real hardware and one real gap found**: Esc/back worked
immediately. Arrow keys did nothing - confirmed by operator testing, not
guessed. Root cause: LVGL's keypad indev only treats `LV_KEY_NEXT`/`LV_KEY_PREV`
as group-navigation triggers by default (mapped here to one specific
physical key, code 6); `LV_KEY_UP`/`DOWN`/`LEFT`/`RIGHT` are just forwarded
to whichever widget is currently focused as a plain key event, and a bare
button or tile has no built-in reaction to them - only widgets with their
own key handling (a slider adjusting its value, a textarea moving its
cursor) do anything with them. A plain focus grid of tiles, which is what
most of this app's screens are, silently swallowed every arrow press.

Fixed in `pollHardwareKeyboard()` (`ui_app.cpp`): arrow keys are now
redirected to `lv_group_focus_next()`/`lv_group_focus_prev()` (right/down =
next, left/up = prev) *unless* the currently focused widget is an
`lv_slider` or `lv_textarea` (checked via `lv_obj_check_type()` against
`lv_slider_class`/`lv_textarea_class`), in which case the raw key still
reaches the widget so the brightness slider and the password/session text
fields keep their native arrow-key behavior. `g_keyboard_group` was made a
global (previously a `main()`-local) so the poll callback can reach it.

**Two more real bugs found redeploying this fix, in the same session -
each looked fixed after the previous one but wasn't, all three confirmed
against actual operator testing, not assumed:**

1. **Wrong screen scope.** `lv_group_set_default()` is called once, before
   any of the 18 screens are built, so *every* focusable widget across
   *every* screen ends up in the same single group, in whole-app creation
   order. Pressing an arrow key on the home screen genuinely moved group
   focus - just onto a widget belonging to a completely different, hidden
   screen (home's own tiles are built last, after all 17 other screens),
   so nothing visible ever changed. Fixed by rescoping group membership to
   just the active screen: `createScreen()` (`src/ui_shell.cpp`) now
   registers an `LV_EVENT_SCREEN_LOADED` handler that calls
   `lv_group_remove_all_objs()` then walks that screen's descendant tree
   adding only its own widgets back in - since every screen goes through
   this one shared helper, no per-screen wiring was needed.
   `setKeyboardGroup()` (new, `ui_shell.h`) lets `main()` hand the group to
   `ui_shell.cpp` once.
2. **Wrong descendant filter.** The first attempt at that same
   `LV_EVENT_SCREEN_LOADED` walk added anything with
   `LV_OBJ_FLAG_CLICK_FOCUSABLE` set - which turned out to be nearly every
   object in the tree, not just the real interactive ones:
   `lv_obj`'s base constructor sets that flag on *every* object by
   default (`lv_obj.c`), and `lv_label` only clears
   `LV_OBJ_FLAG_CLICKABLE`, never `CLICK_FOCUSABLE`. So the group ended up
   correctly scoped to the active screen but stuffed with every decorative
   label/icon inside each card (title, detail, badge, the "01"/"02" index,
   the symbol glyph) plus every plain `lv_obj_create()` layout wrapper,
   diluting navigation across dozens of non-visual stops instead of the
   handful of real ones - still looked like "nothing happens" from the
   operator's seat. Fixed by checking widget *class* instead
   (`lv_obj_has_class()`, which walks the base-class chain, against
   `lv_button_class`/`lv_textarea_class`/`lv_slider_class` - `has_class`
   rather than the exact-match `lv_obj_check_type()` specifically so
   `lv_list`'s row buttons are included too, since `lv_list_button_class`'s
   base class is `lv_button_class`).
3. **No visible focus indicator at all.** Even with group membership
   finally correct, arrow-key presses still weren't visibly doing
   anything - because this app never applies an LVGL theme anywhere
   (`lv_theme_default_init()`/`lv_display_set_theme()` are never called),
   so every visual is this app's own explicit style and nothing styles the
   focused state for buttons (only the textareas' border does). Fixed by
   adding one shared `lv_style_t` (a 3px accent-colored outline) applied
   with the `LV_STATE_FOCUSED` selector to every widget as it's added to
   the group - LVGL toggles that state on an object automatically as group
   focus moves onto/off of it, so this needed no manual add/remove
   synchronization, just applying the style once per widget
   (`lv_obj_add_style()` already dedupes repeat calls, confirmed by
   reading its source, so re-visiting a screen many times in a long-running
   session doesn't accumulate anything).

Confirmed working end-to-end by the operator on real hardware after all
three fixes: a visible outline appears on a home tile as soon as the
screen loads (the first object added to a freshly-emptied group is
auto-focused - confirmed by reading `lv_group_add_obj()`'s source, not
assumed), and arrow keys move it between real tiles/buttons.

## GNSS: the modem was just never powered on

`ui_app.cpp` had a `constexpr bool kGnssAvailable = false;` with a comment
claiming "the shipped modem firmware does not currently accept the GNSS
configuration required by Reconclave" - this was wrong, and the mechanism
behind that wrong diagnosis is worth recording. Manually opening
`/dev/ttyS3` and sending plain `AT` got **zero bytes back**, which reads
identically to "this firmware doesn't understand our commands" and to "this
UART is connected to nothing powered on" - the two look the same from the
serial port alone, and whoever wrote that comment reasonably picked the
firmware explanation without another way to distinguish them.

The actual cause: this device's nRF9151 has a dedicated enable line - the
milestone-4 note above already documented it as "GPIO2", but nothing in
this codebase ever drove it. Global GPIO2 is gpiochip0 line 2 (line < 32,
so chip0 - see `tca8418_keyboard.h`'s numbering note). Confirmed by hand
first, safely, before touching any code: `gpioset -c gpiochip0 -C
reconclave-gnss-enable 2=1` held in the background, then the exact same
`AT` command that had gone completely unanswered got `OK` back within
~2 seconds. Every AT command the existing `GnssReceiver::start()` sends,
replayed by hand in the same order with the line held high, succeeded all
the way through `AT+CFUN=31` (GNSS-only mode) and `AT#XGPS=1,0,0,0`
(legacy GPS start) - this modem's firmware (`mfw_nrf91x1_2.0.3`) simply
doesn't support the *modern* `#XGNSS` API (`AT#XGNSS=?` returns `ERROR`),
which `start()` already correctly detects and falls back from; the legacy
`#XGPS` API it does support (`AT#XGPS=?` returns a real capability
string) was never actually reached in previous attempts because the modem
was never on to reach in the first place.

Fixed in `GnssReceiver::start()`/`stop()` (`src/gnss_receiver.cpp`): drives
gpiochip0 line 2 high via libgpiod before opening the UART (300ms settle
delay, cheap insurance ahead of the first exchange, which itself already
retries for 1500ms), releases it on `stop()`. `kGnssAvailable` is now
`true`.

**A second, independently-confirmed bug found while verifying this**:
`processLine()`'s position-fix parser looked for a `"#XGPSPOS:"` prefix,
which this (or probably any) nRF91 Serial LTE Modem build never actually
sends - confirmed against Nordic's own documented format, `#XGPS:
<latitude>,<longitude>,<altitude>,<accuracy>,<speed>,<heading>,<datetime>`,
and against this modem's real synchronous status line observed during
testing (`#XGPS: 1,1`). The same `"#XGPS:"` prefix is used for both that
2-field status echo and a real 7-field fix notification; the existing
`fields.size() >= 7` check already correctly told them apart once the
prefix itself was fixed to match. The modern API's `"#XGNSSPOS:"` prefix
is left unverified and unchanged, since this modem can't exercise that
path at all to confirm or deny it against.

**What's confirmed and what isn't**: every AT command in the real start
sequence, replayed by hand in the exact order the code uses, succeeded
end-to-end through starting legacy GPS. No position report arrived in a
20-second window, indoors, which is the expected outcome for GPS with no
sky visibility, not a failure signal. Cross-compiled, deployed, and
smoke-tested `reconclave_k230_ui` with this change - starts and runs
cleanly. What's *not* confirmed: an actual live fix flowing through the
real C++ code's parser (as opposed to my manual byte-for-byte replay of
the protocol), since that needs either open sky or a much longer indoor
window than was tested, and pressing the on-screen "Start GNSS" button is
a physical/UI step, not something drivable over SSH.

Follow-up: the LOCATION button still called an older `enableNrf9151()` helper
before `GnssReceiver::start()`. Both requested gpiochip0 line 2 exclusively,
so the receiver's request would fail after the button's request succeeded.
Removed the duplicate UI request and its shutdown cleanup; the receiver now
owns the enable line for the entire GNSS session. Also updated the Settings
row, which still claimed GNSS required new modem firmware. The corrected UI
build was deployed and restarted successfully; a live fix still needs an
outdoor test.

Further live UI testing found intermittent startup timing: the modem can emit
`Ready` before it will answer `AT`. Startup now retries `AT` until it receives
`OK` (up to five 1.2-second attempts), and a rejected legacy GPS start checks
whether GPS is already active before one delayed retry and the existing mode
recovery. The LOCATION screen now distinguishes **GNSS OFF**, **START FAILED**,
**SEARCHING FOR FIX**, and **FIX ACQUIRED**, with Start/Stop/Retry control and
modem/API diagnostics. The same UI start path can be exercised through the
root-only `/run/reconclave/ui-command` file (`gnss start` / `gnss stop`). On the
device, the deployed UI reported `RUNNING yes` and `Legacy GPS active; waiting
for position report` after `gnss start`. An outdoor position fix remains to be
verified.

During a later window test, the live modem still reported `#XGPS: 1,1`
(legacy GPS active) but no position notification. This firmware rejects
`AT#XNMEA=1`, so `NMEA_COUNT 0` and the initialized satellite count of zero
do **not** prove that no satellites are visible; the modem provides no
intermediate satellite telemetry through this UART path. The LOCATION screen
now says the satellite count is unavailable when NMEA is disabled, and the
runtime status includes `NMEA_ENABLED`. A clear open-sky test is needed to
separate window signal loss from an RF/antenna problem.

`src/gnss_nmea.{h,cpp}` (new): `parseNmeaSentence()` and its checksum/
coordinate helpers were split out of `gnss_receiver.cpp` into their own
files with no libgpiod dependency. This wasn't a style preference - once
`gnss_receiver.cpp` needed `<gpiod.h>` for the enable-line fix, it stopped
being buildable for `reconclave_k230` (the headless node binary, which
compiled this file for `parseNmeaSentence`'s tests even though `main.cpp`
never actually references GNSS at all) and `sightline_recon_tests` (the
host test target, no libgpiod available on the host either). `main.cpp`'s
disuse of GNSS meant `reconclave_k230` no longer needed either new file at
all; the test target and `reconclave_k230_ui` link `gnss_nmea.cpp`, and
only `reconclave_k230_ui` also links `gnss_receiver.cpp`.

## Device sysroot: the correct `lv_conf.h` recipe

`fetch-device-sysroot.sh` previously fetched
`kendryte/k230_sdk`'s `.../lvgl/port_src/lv_conf.h` as "the" LVGL config -
that path is a stale **v8.2.0** config, silently wrong for this device's
real **v9.5.0** `liblvgl.so` (a mismatch here has crashed this exact app
before - see the boot-splash section's unrelated but similarly-shaped
lesson about not trusting an unverified source). The real, confirmed-working
recipe, now scripted in `fetch-device-sysroot.sh` itself: LVGL source is
pinned by LILYGO's own BSP overlay
(`Xinyuan-LilyGO/T-Display-K230`'s `k230_bsp/overlay/.../lvgl/lvgl.mk`) at
the exact commit `59dc7e436ae97a25e32656739ea6a943f9f11b6a` in upstream
`lvgl/lvgl` (not a release tag); that same overlay carries this device's
`lv_conf.defaults` (board value overrides) and the patch that's the actual
source of the `lv_linux_drm_set_rotation` symbol documented above; `lv_conf.h`
itself is *generated*, not copied, via LVGL's own
`scripts/generate_lv_conf.py` against that commit's `lv_conf_template.h` +
the overlay's `lv_conf.defaults` - reproducing exactly what the overlay's
own `CMakeLists.txt` does at build time. libgpiod (needed for the TCA8418's
bit-banged I2C, previously missing from this script entirely) is fetched
the same way as libdrm - a close v2.x tag's `gpiod.h`, verified against
every `gpiod_*` symbol this codebase actually calls, plus the real
`libgpiod.so` pulled from the device.

Rebuilding `reconclave_k230_ui` against this reconstructed sysroot and
smoke-testing it standalone on real hardware (while the old process still
held the DRM/GPIO devices) produced exactly the expected resource-busy
conflicts and *no* crash or undefined-symbol error - strong evidence the
generated config is correct, since a mismatch here has previously crashed
`lv_init()` outright rather than failing gracefully later. Deployed and
confirmed stable post-swap.

## Home screen: 10 tiles, 5x2

`main()`'s tile grid is `createTileGrid(home, 5, 2)`: NETWORK, WIRELESS,
RECON, LOCATION, ASSESSMENT, NODE, EVIDENCE, DEVICES, VISION, SETTINGS.
See the two sections above for why 5x2 (not 3x3/3x4) and what SETTINGS
covers.

## Cardputer capability parity

Audited against `FieldDeck/README.md`'s feature list, confirmed
against real K230 hardware rather than assumed:

| Cardputer capability | K230 | Notes |
|---|---|---|
| `_reconclave._tcp` discovery, `system.info` | Done | Milestones 1-2 |
| `radio.wifi.scan` + channel data | Done | Live survey includes band, frequency, signal quality, security classification, open/hidden counts and 1/6/11 congestion scoring; enriched CSV evidence is available locally |
| `net.arp.snapshot` | Done | Passive kernel neighbour inventory is shown locally and remotely callable through the Reconclave protocol without transmitting probe traffic |
| Authenticated network tools | Implemented; field validation pending | Provisioned nodes expose signed Wi-Fi survey, bounded DNS, scoped TCP connect and scoped `/24` discovery with authenticated responses, replay rejection, progress and cancellation |
| Persistent host knowledge | Implemented; field validation pending | Complete ARP neighbours and scoped discovery results maintain first/last-seen, service, observation and change state; authenticated coordinators can retrieve `net.hosts.snapshot` |
| Safe service identification | Implemented; field validation pending | Scoped single-port identification captures bounded server greetings or sends a fixed HTTP HEAD request; it cannot authenticate, run scripts, follow links or accept arbitrary probe payloads |
| Findings view | Implemented; field validation pending | Deduplicated `assessment.findings.snapshot` derives review-level cleartext-service observations and informational asset changes without claiming unverified vulnerabilities |
| Hash-chained evidence timeline | Implemented; field validation pending | Authenticated tool results and host changes carry assessment context in an append-only timeline whose integrity and record count are shown in the UI |
| Evidence manifest | Implemented; field validation pending | UI and authenticated API can verify the timeline and publish a bounded SHA-256 inventory bound to node, firmware, project, engagement and operator identity; symlinks are excluded |
| Evidence CSV export | Done | Not a removable microSD like Cardputer - see milestone 6 |
| BLE discovery | Done locally | nRF52840 UART BLE scan is proven on hardware and available from the Bluetooth screen; no BlueZ HCI adapter is present |
| Touchscreen UI | Done | Milestone 6 - §18 mockup, landscape, touch navigation |
| Keyboard-driven navigation | Done | See "Keyboard-driven navigation" below - this row was stale, milestone 4/6 had already wired most of it |
| GNSS / cellular | Modem enable + legacy GPS API confirmed on real hardware; live sky fix still needs on-site testing | See "GNSS: the modem was just never powered on" below. Non-blocking nRF9151 receiver uses `/dev/ttyS3` at 115200 baud, drives the modem's enable GPIO, starts the legacy `#XGPS` sequence (this firmware doesn't support the modern `#XGNSS` API), validates NMEA checksums, parses GGA/RMC and geotags Wi-Fi evidence. Cellular assessment tools remain |
| Assessment sessions | Done | Persistent project, engagement and operator context is editable on-device and embedded into exported observations |
| Node/job dashboard | Done | Local UI shows trust state, node address, capability count and live remote discovery progress/findings through an atomic runtime-status channel |
| CC1101 sub-GHz, NFC | **Not applicable** | Cardputer-specific accessory hardware; no direct K230 equivalent |
| LoRa | Hardware present | Main-board SX1262/LR2021 SPI support is provided by the LILYGO BSP; application integration remains |
| Physical keyboard | Present (kit) | Not yet readable from Linux, see milestone 4 |
| Camera/vision capabilities | **K230-only** | Not on Cardputer at all; hardware confirmed present, not yet exercised |

## Custom firmware image

The reproducible image layer now lives in `firmware/`. It stages
Reconclave as a Buildroot package on top of LILYGO's pinned Kendryte SDK/BSP,
replaces the competing phone launcher with the Reconclave UI, starts the
protocol node as a separate service, installs a Reconclave hostname/evidence
directory, and converts the mascot art into the raw U-Boot splash format.
It deliberately builds an image without flashing any block device; see the
firmware README for the build command and current host-build validation.

### Rationale and board-support baseline

The original plan here was "app on stock firmware now, full custom image
later" (see the top of this file) - that decision was explicitly revisited
mid-project (checked the public `kendryte/k230_sdk` for driver source
coverage: WiFi/BT/touch/camera present, but no RM69A10 panel driver
anywhere, matching what's on this exact board) and the app-based approach
was kept. In practice, though, the boot-splash work above ended up doing
a real **partial** custom-firmware build anyway (a from-source U-Boot
rebuild, patched and reflashed onto this exact device) once a low-risk
recovery story was established first (three independent layers - see
above). That precedent - toolchain, source locations, exact flash offsets,
verified recovery path - is the actual starting point if a full custom
image is ever pursued, not a cold start via `kendryte/k230_sdk`
`make CONF=...` as originally envisioned; the real board support (kernel,
device tree, rootfs overlay, not just U-Boot) lives in
`Xinyuan-LilyGO/T-Display-K230`'s `k230_bsp/`, pinned to a specific
`kendryte/k230_linux_sdk` commit, not the generic `kendryte/k230_sdk`.
