# K230 display and keyboard expansion hardware

## What is the actual board, display, and software base?

### Takeaway
Sightline targets the LILYGO T-Display K230 running the vendor's Linux/Buildroot image. The 4.1-inch 568 × 1232 AMOLED and Goodix touch are substantially more capable as an interface than the small ESP32 handhelds, but the project's current interface and hardware acceptance remain works in progress.

### Cited Findings
- Sightline names the LILYGO T-Display K230 and calls its present state a hardware preview; it reports hardware validation for display, touch, keyboard, Wi-Fi, local BLE, and outdoor GNSS, while physical acceptance of the latest UI remains outstanding. — [Sightline README](/mnt/Storage/Coding/Sightline/README.md)
- LILYGO specifies a 4.1-inch RM69A10 AMOLED, native 568 × 1232 resolution, K230 dual 64-bit RISC-V CPU (listed at 1.6 GHz plus 800 MHz), and KPU/DPU/GPU/VPU. — [LILYGO product page](https://lilygo.cc/products/t-display-k230)
- LILYGO's board repository specifies a GT9895 Goodix touch controller, GC2093 camera, MIPI DSI panel and CSI camera path. Its pin map says its validated LVGL display route uses RGB565; HDMI is an optional diagnostic path sharing some touch-controller signals. — [Vendor board repository](https://github.com/Xinyuan-LilyGO/T-Display-K230); [vendor pin map](https://github.com/Xinyuan-LilyGO/T-Display-K230/blob/main/k230_bsp/docs/HARDWARE_PINMAP.md)
- The separate CanMV/RT-Smart variant repository lists 1 GiB LPDDR and SD-card flash/storage for this board, but Sightline records that it uses the Linux/Buildroot firmware family instead of CanMV/RT-Smart. — [LILYGO CanMV board specification](https://github.com/Xinyuan-LilyGO/T-Display-K230_canmv_rt/blob/main/README.md); [Sightline development log](/mnt/Storage/Coding/Sightline/DEVELOPMENT.md)
- The current vendor Linux board repository is built as an overlay on Kendryte's `k230_linux_sdk`, and provides an SD-card image; Kendryte documents Buildroot and optional Debian/Ubuntu image building. — [LILYGO Linux board repository](https://github.com/Xinyuan-LilyGO/T-Display-K230); [Kendryte Linux SDK](https://github.com/kendryte/k230_linux_sdk)
- Sightline's on-device engineering log records Buildroot 2025.02.1, kernel 6.6.36, and glibc 2.33 on the tested unit. It records `/dev/dri/card0` DRM/KMS rather than fbdev, LVGL 9.5.0, touch at `/dev/input/event1`, and physical confirmation that touch events toggled its proof UI. — [Sightline development log](/mnt/Storage/Coding/Sightline/DEVELOPMENT.md)

### Inferences
- A dense landscape dashboard can use 1232 × 568 logical pixels after 270-degree rotation, while portrait is better for forms and long evidence views; runtime rotation and touch coordinate calibration must be validated together. This follows from the native panel mode and Sightline's existing rotation notes.
- Native RISC-V Linux packages are promising for protocol assessment and evidence processing, but the exact image ABI, libraries, kernel configuration and memory headroom determine whether any specific package can run. The 1 GiB specification is not a free-memory measurement.

### Gaps
- The current unit's actual free RAM, SD-card capacity, partition free space, thermal throttling and battery runtime have not been measured in this research pass.
- The vendor's current Linux BSP may differ from the image on this specific unit; any version-dependent peripheral claim needs a live `uname`, device-tree, module and `/dev` inventory.

## How does the keyboard expansion work, and which pins and functions does it consume?

### Takeaway
The attached keyboard base is an optional nRF9151 cellular/GNSS assembly with a TCA8418 matrix controller, not a generic USB keyboard. Sightline's unit has working key events via a software-driven I2C4 pin pair; the base also consumes pins for charging, backlight, modem UART, and interrupts.

### Cited Findings
- LILYGO separates the main board, optional nRF52840 BLE/audio/sensor base, and optional nRF9151 cellular/GNSS/keyboard base; its pin map assigns the keyboard to TCA8418 at 7-bit address `0x34`, I2C4 SDA GPIO47/SCL GPIO46, reset GPIO43, IRQ GPIO42, and PWM keyboard backlight GPIO52. — [Vendor hardware pin map](https://github.com/Xinyuan-LilyGO/T-Display-K230/blob/main/k230_bsp/docs/HARDWARE_PINMAP.md)
- The keyboard base shares I2C4 with BQ25896 charger (`0x6B`), BQ27220 fuel gauge (`0x55`), XL9555 GPIO expander (`0x20`–`0x27`), and optional DRV2605 haptic driver (`0x5A`). The nRF9151 is enabled by GPIO2 and uses GPIO28/29 as UART3 (`/dev/ttyS3`, 115200 8N1). — [Vendor hardware pin map](https://github.com/Xinyuan-LilyGO/T-Display-K230/blob/main/k230_bsp/docs/HARDWARE_PINMAP.md)
- Sightline's live keyboard diagnosis says the board's controller is not accessible through the initially assumed `/dev/i2c-0`; it switches GPIO46/47 to open-drain lines and bit-bangs the I2C transactions through libgpiod. Its current driver polls the TCA8418 event FIFO, rather than consuming the IRQ. — [Sightline keyboard driver](/mnt/Storage/Coding/Sightline/src/tca8418_keyboard.h); [Sightline development log](/mnt/Storage/Coding/Sightline/DEVELOPMENT.md)
- Sightline's development log records a conflicting older product pin illustration that places the keyboard on another path; live key events and fuel-gauge telemetry came from GPIO46/47 on this actual unit, so the revision-specific BSP map is the supported assignment. — [Sightline development log](/mnt/Storage/Coding/Sightline/DEVELOPMENT.md)
- The vendor launcher supports hardware keyboard replacement of the touch keyboard, `Esc` back navigation, keyboard testing and backlight control, and F1–F11 hotkeys (including terminal and screenshot shortcuts). — [Vendor launcher app guide](https://github.com/Xinyuan-LilyGO/T-Display-K230/blob/main/k230_launcher/docs/APPS.md)
- Sightline's newer UI routes TCA8418 events to LVGL focus, activation, back navigation and text input. Its validation report says the full physical edit workflow and latest UI touch typing still need operator acceptance. — [Sightline development log](/mnt/Storage/Coding/Sightline/DEVELOPMENT.md); [Sightline validation report](/mnt/Storage/Coding/Sightline/docs/VALIDATION.md)

### Inferences
- Any new I2C accessory or daemon must arbitrate the shared GPIO46/47 bus with the existing userspace keyboard owner; opening a second independent I2C master risks collisions and lost input.
- The keyboard is well suited to a compact terminal, filters and command shortcuts, but a field workflow should preserve touch input and clearly show focus because the latest physical typing acceptance is incomplete.
- GPIOs listed on the 40-pin header are not all free: several are committed to display, radio, camera, keyboard base, I2S audio or modem. Hardware expansion needs a board-revision-aware pin allocation check.

### Gaps
- I did not find a reliable published physical key count, matrix wiring table, rollover limit or measured key-scan latency for this exact keyboard variant. Inspect the attached keyboard PCB or vendor keymap and run a complete physical key test before designing critical shortcuts around it.
- Whether the installed base has all optional haptic, battery, cellular, and audio parts requires on-device enumeration and component inspection.

## Which interfaces are practical for a field assessment tool, and what remains unproven?

### Takeaway
The board has usable wired and Wi-Fi networking, USB host and configurable gadget potential, microSD storage, optional BLE/cellular/GNSS and LoRa paths, plus camera and a substantial touchscreen. The radio hardware and kernel driver capabilities must be tested before promising monitor mode, injection, USB HID emulation, or HCI-compatible BLE tooling.

### Cited Findings
- Vendor BSP documents SD-card boot/storage, SDIO RTL8189FS or RTL8723DS Wi-Fi support, USB host support for Ethernet/modem/Bluetooth adapters, and main-board SX1262/LR2021 LoRa SPI wiring. — [Vendor pin map](https://github.com/Xinyuan-LilyGO/T-Display-K230/blob/main/k230_bsp/docs/HARDWARE_PINMAP.md); [vendor BSP guide](https://github.com/Xinyuan-LilyGO/T-Display-K230/blob/main/k230_bsp/README.MD)
- The vendor launcher describes Ethernet when an adapter is present, MTP over USB for file transfer, an on-device terminal, Wi-Fi/network configuration, and optional cellular/GNSS functions. — [Vendor launcher app guide](https://github.com/Xinyuan-LilyGO/T-Display-K230/blob/main/k230_launcher/docs/APPS.md)
- Kendryte's USB application guide shows the K230 USB controller can be built with Linux gadget support, including gadget functions such as UVC and HID, with USB1 OTG device role selected by the ID signal. This describes SDK capability, not confirmation that Sightline's port/image currently supports a particular gadget function. — [Kendryte USB guide](https://github.com/kendryte/k230_docs/blob/main/en/02_applications/tutorials/K230_USB_Application_Practice_UVC_Transmission_YUV_and_Encoding_Stream.md)
- Sightline's own tested unit ran Wi-Fi scans through `wlan0`, wired DHCP via a USB Ethernet connection, and BLE scans via the separate nRF52840 UART at `/dev/ttyS1`. It found no BlueZ `hci0` despite a loaded AIC Bluetooth driver, so ordinary HCI-dependent BLE packages cannot simply reuse the working UART bridge. — [Sightline development log](/mnt/Storage/Coding/Sightline/DEVELOPMENT.md)
- Sightline's engineering log identifies an AIC Wi-Fi/BT device on its tested image, whereas the latest vendor BSP names RTL8189FS/RTL8723DS. This is a hardware/firmware revision discrepancy, not evidence that one radio feature set applies to every board. — [Sightline development log](/mnt/Storage/Coding/Sightline/DEVELOPMENT.md); [vendor pin map](https://github.com/Xinyuan-LilyGO/T-Display-K230/blob/main/k230_bsp/docs/HARDWARE_PINMAP.md)
- The tested Sightline unit did not enumerate as a USB device when its OTG port was attached to a PC; this older observation should be revisited against the current vendor MTP implementation and the actual port role and cable. — [Sightline development log](/mnt/Storage/Coding/Sightline/DEVELOPMENT.md); [vendor launcher app guide](https://github.com/Xinyuan-LilyGO/T-Display-K230/blob/main/k230_launcher/docs/APPS.md)
- LILYGO marks Meshtastic, LoRaWAN and nRF9151 cellular/GNSS tools as beta in its public image; main-board optional modules should be detected at runtime. — [Vendor board repository](https://github.com/Xinyuan-LilyGO/T-Display-K230)

### Inferences
- Ethernet and conventional IP assessment should be the primary dependable tool path. USB Wi-Fi adapters may add radio capabilities if their exact chipset, driver and power budget are validated; the internal Wi-Fi's known scan function alone does not establish monitor mode or frame injection.
- USB host and USB gadget modes are potentially useful, but port topology and role switching need explicit detection; concurrent use as a USB Ethernet host and USB HID gadget on one OTG controller may be impossible without separate interfaces.
- The UART BLE bridge should be treated as a distinct capability from BlueZ HCI, with a dedicated adapter in the application or validated external HCI dongle.

### Gaps
- No primary source or live test established monitor mode, raw 802.11 frame injection, capture fidelity, simultaneous AP+STA, BLE HCI, USB HID gadget, USB serial gadget, or exact available USB host ports on this physical build.
- The exact radio and expansion-base BOM of the current unit should be inventoried before selecting firmware or promising RF tools. The antenna band and local regulatory settings also depend on the installed LoRa variant.
