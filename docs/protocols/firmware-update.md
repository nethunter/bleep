# Signed recovery-based firmware update protocol

Status: Implemented software path; live network, migration, power-loss, and
endurance acceptance gates remain open.

## Discovery and scheduling

Stores, display/touch, and Home initialize before the updater clock begins. If
saved Wi-Fi exists, each boot schedules one check five seconds later on eligible
idle Home, regardless of the prior check time. Opening a device, starting a
scene or Portal, or acquiring foreground ownership defers a pending check. The
same activity cancels an active HTTP operation without disconnecting equipment.
Missing Wi-Fi starts no radio and produces no startup error.

After the startup attempt, ten continuous idle Home minutes gate automatic
work. Successful/no-update checks repeat after 24 hours; network failures retry
after 1, 6, then 24 hours. Opening **Settings > Firmware update** requests an
immediate check. Active operations offer **Disconnect & check**; the operator,
not the updater, decides whether retained equipment may be released. Every
updater-owned completion, failure, timeout, signature rejection, or cancellation
returns the station to `WIFI_OFF`. Background checks download only the manifest
and detached signature.

A newer signed release displays **Install now / Later** once per sequence.
Later persists the dismissed sequence but leaves the release in Settings. Only
a newer sequence prompts again after reboot.

## Canonical manifest and trust

The manifest is compact sorted UTF-8 JSON with a trailing newline. It contains
`schema`, `channel`, `release_sequence`, `version`, `commit`, `hardware`,
`profile`, `partition_schema`, `recovery_schema`, `image_size`, `sha256`,
`payload_url`, `recovery_sequence`, `recovery_image_size`, `recovery_sha256`,
`recovery_payload_url`, and `key_id`. ECDSA P-256 signs the exact bytes using SHA-256.
Stable and development have separate embedded public keys and protected GitHub
Environments.

Main and recovery verify the signature before trusting fields. They reject an
unknown key, wrong channel/target, partition schema other than 2, recovery schema
other than 1, malformed or oversized metadata, an unallowlisted URL, replayed
sequence, invalid ESP header, byte-count mismatch, or SHA-256 mismatch. Main
persists the exact verified manifest and signature before entering recovery;
recovery verifies that request again.

HTTPS uses a bounded trust bundle for GitHub and its release CDN (DigiCert
Global Root G2, USERTrust ECC, and ISRG Root X1), bounded time synchronization,
fixed buffers, and a GitHub release/CDN redirect allowlist. A missing signed
manifest is reported separately from transport failure. Main checks are
asynchronous and main-loop-owned. Recovery has no AP or upload portal and
writes only `ota_0`.

Updater-owned Wi-Fi teardown is two-phase: main requests station disconnect,
then stops the radio from its loop after a bounded settling window. Recovery
handoff does not deinitialize the network stack immediately before reset; the
reset itself returns the radio to its boot state.

## Recovery journal, install, and health

Partition schema 2 is:

| Region | Offset | Size |
| --- | ---: | ---: |
| NVS | `0x9000` | `0x5000` |
| OTA metadata | `0xE000` | `0x2000` |
| Factory recovery | `0x10000` | `0x100000` |
| Recovery journal | `0x110000` | `0x10000` |
| Main `ota_0` | `0x120000` | `0x2D0000` |
| Coredump | `0x3F0000` | `0x10000` |

Raw ceilings are `0xF0000` for recovery and `0x2C0000` for main. Confirmed
installation cancels commands, releases retained links, warns about USB power,
and journals the signed request. Main selects its own valid `ota_0` and
restarts. Before normal services initialize, that boot creates a circular
progress UI with only display, touch, and LVGL active, then downloads the
release's signed recovery payload into the non-running factory partition while
checking the ESP header, bounded byte count, SHA-256, and application
descriptor. Device runtimes, scenes, Portal, Home, and normal Settings remain
dormant during this transfer. It records the independent recovery sequence and
selects factory recovery only after the written image validates. If the
recovery sequence is already installed, this write is skipped.

The newly updated recovery verifies the exact journaled request again, then
downloads, validates, and hashes main before selecting `ota_0`. Main marks
itself valid only after approximately ten seconds of healthy stores,
display/touch, Home, and main-loop operation, then clears the completed install
journal. A failed pending main falls back to the newly installed recovery.

Power loss before factory erasure leaves the signed journal available for the
next valid main boot to resume. A transfer or verification failure returns to
the normal main UI with **Retry update** and leaves recovery unselected. A newly
installed main still pending its ten-second health gate never runs the early
recovery replacement. Power loss or flash failure after erasure begins can leave the
factory image unusable. Main remains selected when still valid, but if it cannot
boot, recovery requires the NVS-preserving USB web flash flow. This accepted
failure boundary avoids a guardian or second recovery slot. Factory Reset may
still refresh recovery after the new main becomes healthy because the reset is
initiated from recovery itself.

Manual **Recovery mode** writes a distinct `RecoveryModeRequested` journal
operation before selecting factory recovery. Recovery therefore remains on its
menu until the operator chooses an action; **Boot firmware** clears that manual
request before selecting `ota_0`. Recovery ignores touch during a 1.5-second
boot guard and then requires 300 ms of continuous release before arming menu
input, so touch-controller startup cannot make the initiating hold look like a
new **Boot firmware** tap. A blank journal from the USB migration automatically
selects a valid main image and reboots; a corrupt or unreadable journal stays in
recovery so an interrupted transaction cannot be bypassed. The install view is
painted once; progress callbacks update only the bounded bar and percentage
region. After verification and boot selection, recovery shows **Update
successful** and waits for a separate **Restart** tap instead of rebooting
immediately. Factory Reset uses the same confirmation boundary after its
verified image install and transactional NVS erase complete.

The journal has two alternating CRC-protected records with generation counters.
Journal load/save uses checked heap buffers for the bounded encoded records so
main-loop recovery handoff cannot exhaust the Arduino loop-task stack.
Factory reset advances through `FactoryResetRequested`,
`ImageVerifiedResetPending`, and `ResetComplete`. It reads existing Wi-Fi,
downloads and verifies latest stable, writes main, then and only then erases the
complete NVS partition. Power loss resumes idempotently. If Wi-Fi or validation
fails, NVS is untouched and recovery offers Retry, valid main boot, or USB
recovery.

## Publication and migration

Successful `main` CI signs and replaces the `latest` development prerelease.
Publishing a non-prerelease GitHub Release builds and signs stable without
interpreting tag format. Private keys are `OTA_SIGNING_PRIVATE_KEY` secrets in
the `development` and approval-protected `stable` environments. Each signed
release publishes `bleep-update.bin` for main and `bleep-recovery.bin` for the
fixed recovery refresh; both hashes, sizes, and URLs are covered by the same
canonical signature.

The one-time migration bundle writes bootloader `0x0000`, partition table
`0x8000`, blank OTA metadata `0xE000`, recovery `0x10000`, blank journal
`0x110000`, and main `0x120000`. It never writes NVS `0x9000..0xDFFF` and does
not perform a full-chip erase. On its first boot, recovery recognizes the blank
journal, validates `ota_0`, selects it, and continues to main without requiring
an operator tap.
