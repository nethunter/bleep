# GitHub publishing checklist

This checklist separates repository preparation from the account-level choices
that must be made by the future GitHub owner.

## Repository checks

- [x] Public README explains the vision, current behavior, limitations,
  architecture, build, contribution path, and hardware safety.
- [x] Contribution, conduct, and security policies are present.
- [x] Issue forms, pull-request template, and CI workflow are present.
- [x] Generated output, editor files, raw captures, and mobile bugreports are
  ignored.
- [x] Raw packet captures have been removed from the current tree; extracted
  protocol vectors and research notes remain.
- [x] Apache License 2.0 is included in the top-level `LICENSE` file.
- [ ] Remove the historical capture blobs before pushing existing history, or
  publish a reviewed squashed history. Deleting them from the current tree does
  not remove them from earlier commits.
- [ ] Review Git author names and email addresses in history for intended public
  attribution.

## GitHub settings

- [x] The public repository and `origin` remote are configured for
  `nethunter/bleep`.
- [ ] Add a short description and topics such as `esp32`, `lvgl`, `bluetooth`,
  `remote-control`, `camera`, `studio`, and `platformio`.
- [ ] Enable Issues, Discussions, and private vulnerability reporting.
- [ ] Protect `main`; require the native-test and firmware-build checks.
- [ ] Disable force pushes and branch deletion on `main`.
- [ ] Enable Dependabot alerts, secret scanning, and push protection when the
  hosting plan makes them available.
- [ ] Confirm private vulnerability reporting is enabled and the instructions
  in `SECURITY.md` match the repository's Security interface.
- [x] Stable `0.3.5` was published after the signed build and physical OTA
  smoke test passed. The rolling **Latest development firmware**
  prerelease is an automated CI snapshot and must remain clearly labeled as
  hardware-unverified.

## Signed firmware environments

Repository code contains only the two public P-256 keys under `keys/`. Configure
the matching private key as an `OTA_SIGNING_PRIVATE_KEY` secret in each GitHub
Environment:

- `development` signs the rolling public `latest` prerelease after every
  successful `main` CI matrix;
- `stable` signs a published non-prerelease GitHub Release and should require
  environment reviewer approval.

Do not reuse a key between environments, place a private key in repository or
artifact storage, or make signing continue when the secret is missing. Rotate
a channel by embedding a newly identified public key first, shipping that trust
anchor through an already trusted release, and only then switching its
environment secret. The manifest key IDs currently accepted by firmware are
`stable-2026-01` and `development-2026-01`.

Both workflows build recovery and main independently, enforce their
`0xF0000`/`0x2C0000` raw ceilings and schema-2 geometry, package a canonical
manifest with `recovery_schema: 1` and detached ECDSA signature, independently
verify it, and upload a one-time USB migration bundle. Wi-Fi releases contain
both signed main and recovery payloads; normal installation updates and verifies
recovery before recovery installs main. Release sequences are
monotonically increasing UTC epoch seconds; tag text is only the stable version
label and is not parsed as a policy input. Development CI derives both the
compiled firmware version and manifest version from
`platformio.ini`'s `custom_firmware_version`, appending `-dev`, and carries that
exact value through the full-firmware artifact into the signing job.
The `latest` prerelease tag is a stable asset-container reference: CI updates
the release metadata and replaces its signed assets without force-moving the
Git tag. The signed manifest's commit and release sequence identify the actual
development build, avoiding GitHub workflow-token restrictions when a build
contains workflow-file changes.

## License

Ble(e)p uses Apache-2.0, matching Home Assistant Core's permissive license and
explicit patent grant. Third-party assets, fonts, libraries, and future
protocol contributions still need license-compatibility review before release.
