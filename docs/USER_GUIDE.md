# Using Sightline

## Navigation

Home shows the selected scan network and interface on the left. The three main
actions follow the field workflow: **1 Scan** chooses an interface and scan
depth, **2 Explore** opens the asset inventory, and **3 Review** opens findings.
The bottom row holds **Evidence**, **Jobs**, **All tools**, **Session** and
**Settings**. Settings opens to **Everyday** for Network, Wi-Fi and Brightness;
**More options** holds Bluetooth, Location, device and trust details. Wi-Fi survey,
packet capture, camera and location are under All tools.

Tap a card to open it. **Back** returns to the launching menu when you open tools
from Home, All tools or Settings. Detail views return to their parent view.
With the keyboard kit, arrows/Tab move focus, Enter activates, and Escape goes
back. A cyan outline marks the focused control. Hidden and disabled controls are
skipped; freshly populated result rows become available to keyboard navigation.

The top bar shows the time, battery reading and **Linked** or **Offline**.
Linked means the device has a local network address; it does not establish
Internet access or coordinator trust. Check **Settings → More options → Node & jobs** for trust and remote work.

## Set up an assessment

Open **Session**, enter project, engagement and operator identifiers, and tap
**Save**. Each field accepts up to 48 characters. The saved identifiers apply to
new operations; existing evidence keeps its original context. A Wi-Fi survey,
scan, capture or screenshot retains the context captured when it starts even if
you change Session before it finishes or before exporting a Wi-Fi CSV. Wi-Fi
location fields use the fix sampled when the survey finishes. Check the saved
field values, as unsupported characters are normalised by the application.

The touch keyboard is available when no hardware keyboard is detected. Save
before leaving the screen. If saving fails, the previous session stays active;
check storage and retry. Edits are not automatically saved.

## Track operations

**Jobs & activity** on Home shows Wi-Fi work, evidence exports/verification,
packet captures, network scans and screenshots. Each row retains its latest
result and elapsed time. **Open** returns to that tool without launching another
operation. **Stop** is available for running packet captures, network scans and
cancellable evidence exports; it remains disabled for other operations.

Stopping waits for the underlying tool to exit. Early-stop packet captures keep
the packets already recorded; cancelled scan reports are labelled as partial.
Failures and timeouts are distinct from successful completion. Local job history
is held in memory and resets when the UI restarts. The Home activity badge counts
local operations; remote discovery and GNSS/camera-preview state appear
separately at the bottom of Jobs. GNSS and camera controls remain on their own
screens. This dashboard does not schedule work or resume jobs after a crash.

## Survey and connect to Wi-Fi

**Wi-Fi survey** starts a nearby network survey when opened from a task card.
Use **Scan** to repeat it. Results show stronger signals first, along with
channels, security and signal quality. The summary compares bands and channel
congestion. Navigation remains available during scanning.

A successful empty scan means no networks were found. Move nearer an access
point and scan again. A failed scan keeps the previous successful results; those
results are not a new observation. The status message explains the failure.
Only one Wi-Fi scan, connection or disconnection operation runs at a time.

To join a network, use **Settings → Wi-Fi → Scan**. Choose a visible secured
network and enter its password. The current connection helper requires a
password of 8–63 characters; open networks and hidden-network setup are not
supported by this flow. A survey does not connect you to an access point.

## Collect and review evidence

Save your session first, then choose the tool for your task:

- **1 Scan** offers focused host and service scans, with an interface choice.
- **2 Explore** shows the asset inventory and LAN survey controls. Open an asset
  for a short summary; **Technical details** shows sources, coverage and exact
  service observations.
- **3 Review** opens **Service notes** first; use **Device changes** for
  ordinary inventory history. These notes are not confirmed vulnerabilities.
- **All tools → Packet capture** records scoped traffic to a PCAP file.
- **All tools → Camera** provides photographic evidence and preview controls.

Use **Evidence** for Wi-Fi CSV exports, browsing, session/store archives and timeline integrity operations.
On **Network scan** or **Assets → Survey LAN**, choose **Automatic route**,
**Ethernet**, or **Wi-Fi** before starting. The choice carries between both screens.
The target is the selected interface's attached IPv4 network,
bounded to `/24` or smaller. Explicit Ethernet and Wi-Fi scans use that link's
source address; the report records the interface and source. **Network** shows
both addresses and the kernel's automatic scan path, which may still be Wi-Fi
when both links share one subnet. Scans remain manual and cancellable.
Evidence lives under `/root/reconclave/evidence/` on the device's persistent
storage. This is not a separate removable evidence card. A failed Wi-Fi rescan
does not replace the last successful survey available for export.

## Browse and export saved files

Open **Evidence → Browse files**. The browser lists the most recently modified
files first, with filenames, sizes and modification times. Use the filter to
choose photos, packet captures, text/reports or other files. **Previous** and
**Next** page through six files at a time. **Refresh** reloads the inventory.
Select a row to see its metadata and a text preview of up to 8 KiB. Binary files
show guidance for inspecting them on a computer; this browser does not decode
photos or packet captures.

**Verify + manifest** checks the timeline and creates the existing evidence
manifest in the background. Completion or failure appears in the Evidence status
area. This check is separate from creating an archive.

**Export entire store** creates a TAR archive under
`/root/reconclave/evidence/exports/`. Each archive contains an `artifacts/` folder
and `bundle-manifest.json`, with sizes and SHA-256 hashes of the exact archived
bytes. The manifest records the session active when export started.

**Export entire store** can include several sessions. For a narrower archive,
choose **Export current session** instead. It includes only files attributed to
the exact saved project, engagement and operator identifiers; an empty match
reports an error and never falls back to exporting everything. Reusing all three
identifiers groups those observations together; this is not a separate session
UUID system.

New local Wi-Fi CSVs, photos, screenshots, scans and packet captures receive
sidecar metadata under `.metadata/`. The browser’s **Current session** filter
uses the same matching rule and file details show attribution. Legacy files,
remote/API artifacts and files created outside these local UI workflows remain
unknown unless they already have supported metadata. They stay accessible in
All files and whole-store exports. Changed/replaced files invalidate their prior
attribution. Keep `.metadata/` with the store when backing it up.

Export-time context does not reassign original session metadata. Archive hashes establish correspondence with the manifest;
they do not certify the timeline or prove who created the evidence.

Stop active scans and captures before exporting. The interface stays available
while the archive is built. Return to Evidence to see progress or tap **Cancel
export**. Cancellation completes after the current file; no partial archive is
published. A completed export does not delete or replace the original files.

The browser/export accepts up to 256 regular top-level files. Archive creation
is limited to 16 MiB per file, 128 MiB of source data in total and filenames up to
89 characters. It reports an error instead of silently omitting oversized files.
Hidden files, temporary files, symlinks and subdirectories are excluded. Existing
exports are therefore not recursively included. Copy larger artifacts separately
or move older evidence out of the store when a limit is reached.

Retrieve a completed archive with your usual SSH file-transfer tool. For example,
substitute the actual archive name and board address:

```sh
scp root@rc-k230-poc.local:/root/reconclave/evidence/exports/evidence-TIMESTAMP-N.tar .
```

The Evidence summary also shows available storage. If an export fails, read the
status, check free space and retry. Files that change during copying cause an
error; the archive is a collection of individual file snapshots, not an atomic
snapshot across all writers.

## Use location

Open **All tools → Location** and start GNSS. Put the ceramic GNSS patch antenna
with its white face toward unobstructed sky. A first fix can take several
minutes. The screen distinguishes off, starting failure, searching and acquired
fix states. Retry is available after a startup failure.

Once acquired, the receiver updates continuously until stopped. Supported Wi-Fi
exports can include the current position. Satellite counts may be unavailable
with the installed modem firmware; that alone does not mean the receiver failed.

## If something does not work

| Symptom | Next step |
| --- | --- |
| Offline in the top bar | Check Ethernet or join Wi-Fi through Settings. Local tools remain accessible. |
| Wi-Fi is busy | Wait for the current scan/connect/disconnect operation, then retry. |
| Scan failed | Read the status, check the radio/network configuration, and tap Scan again. |
| Session cannot be saved | Check free storage; the previous session remains active. |
| No GNSS fix | Move outdoors and check antenna orientation; allow several minutes. |
| Remote operations unavailable | Inspect Node & jobs for service, trust and job state. |
| Control is disabled | Check the screen's hardware or operation status before retrying. |

## Device acceptance checks after a UI update

The cross build and host tests cannot verify physical touch, display rendering or
radio operation. On the board, check:

1. Home cards, full descriptions and status text fit the 1232×568 landscape panel.
2. Open a tool through All tools and verify Back returns there; repeat from Home
   and Settings using touch and the keyboard. Check Jobs → Open returns to the
   existing tool state without starting work.
3. Run Wi-Fi scans from Survey and Settings, navigate away during a scan, and
   return. Check empty/failure messages and that repeated operations are gated.
4. Navigate newly refreshed host, findings and Wi-Fi setup rows with the keyboard.
5. Save session fields with both hardware and touch input; confirm the status is
   readable above the on-screen keyboard and saved context survives a restart.
6. Browse evidence with each filter; page through results and open text/binary
   files. Exercise the filter using both touch and the keyboard.
7. Export a store archive, navigate away and return, then retrieve it and verify
   the manifest. Repeat with cancellation and while a capture is active.
8. Exercise camera preview, location and ongoing job status.

## Remote diagnostics and rollback

This board currently advertises `rc-k230-poc.local`; its observed address during
validation was `192.168.8.134` (DHCP addresses can change). Discover it with:

```sh
avahi-browse -rt _reconclave._tcp
```

The root-only `reconclave-ui asset IPv4` helper opens a saved asset directly
for on-device diagnosis. The command-file workflow also supports `screen jobs`, `screen
evidence-files`, `evidence screenshot`, `evidence export-session`, `evidence
export-store`, and `jobs stop capture|nmap|evidence`. Write one complete command
at a time to `/run/reconclave/ui-command` with restrictive permissions; wait for
it to be consumed before writing another. `/run/reconclave/ui-jobs` reports local
job states and elapsed seconds without dumping tool output.

The board’s pre-overhaul UI and tool wrappers were retained as
`/usr/bin/reconclave-k230-ui.before-overhaul`,
`/usr/sbin/reconclave-capture.before-overhaul` and
`/usr/sbin/reconclave-nmap.before-overhaul`. To roll back, stop the UI, wait for its
process to exit, restore those three files to their original names and restart
`/etc/init.d/S99reconclave-ui`. These backups are on the board, not in Git.


## Assessment workspace and Assets

The home screen now puts Assets, Findings and Evidence first. Assessment details
are available in the bottom dock; capture and camera remain under All tools.

In Assets, search by IP, MAC, interface, port or software response text. The search
is case-insensitive. Tap the field for the touch keyboard, then use its checkmark
to close it. Clear empties the search. Filters select all assets, review leads,
known ports or records without service identification. Sort by address, last seen
or review priority. Addresses sort numerically.

Review includes service attention flags and changes in observed asset state;
it does not mean a confirmed vulnerability. Detail pages distinguish inferred
labels from observed responses and explain partial coverage. No known ports is
an unknown result, not proof that a device has no services.

Export writes `asset-inventory-*.json` to Evidence. The JSON includes the
`sightline-assets/v1` schema, partial-coverage marker, device-history scope,
observation times, service label basis and review items. Session metadata identifies
who exported it; it does not restrict the inventory to that session. Treat shared
MACs and separate IPs as observations rather than a definitive physical-device count.

The local root-controlled `/run/reconclave/ui-command` also accepts `screen assets`,
`assets search <text>`, `assets clear` and `assets export` for remote operation.
These commands do not start network probes. Nmap XML now imports automatically through the node service. The scan output
shows whether import is waiting, completed or rejected. Review the associated
`.xml.import.json` receipt in Evidence for the outcome. Asset details link results
by source filename and show requested coverage and completion status.


## Nmap import behaviour

The wrapper publishes a text report, XML file and a `.xml.ready` marker. The node
processes one ready file per tick and writes a persistent `.xml.import.json`
receipt after saving the inventory. It resumes pending imports after restart;
the UI never writes the shared host database. Importing existing evidence sends
no assessment traffic.

TCP and UDP services are stored separately. Only explicit `open` observations
are imported; ambiguous states and closed/filtered ports remain in raw XML.
Historical opens are retained, with service timestamps, so this is not a claim
that every displayed port is currently open. Older results and port-table guesses
do not overwrite newer or richer service identities. Product/version guesses
remain unvalidated classifications and do not automatically create findings.

Complete XML from cancelled/timed-out scans is labelled accordingly. Truncated
XML is rejected in full, retaining the evidence and recording the reason.
Requested port coverage is recorded; it does not prove every host completed.
IPv6 inventory import is not implemented. XML files are limited to 8 MiB, 4096
hosts and 65536 explicit port records. No external DTD/entity resources are loaded.

For a rejected import, keep the receipt for review. An operator can retry after
correcting the underlying file by removing its `.xml.import.json` receipt and
leaving the ready marker in place. Only import evidence you intend to add to the
device-wide history; imports are not isolated per assessment yet.
