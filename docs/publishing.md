# GitHub publishing checklist

This checklist separates repository preparation from the account-level choices
that must be made by the future GitHub owner.

## Stable release checklist

Use this checklist for every stable release. A stable release is complete only
after its immutable Git tag exists remotely and the approval-protected
`stable` workflow has built, signed, verified, and uploaded the release assets.

### 1. Establish the release range

- Fetch the current remote state and identify the most recent stable release
  tag. Confirm that the tag resolves to the commit published as that stable
  release; do not infer the baseline from `platformio.ini`, a branch name, or a
  development prerelease.
- Review every commit and the complete diff from that tag to the proposed
  release commit. Treat this Git range as the changelog source. Summarize new
  behavior, fixes, exact-model compatibility changes, setup or persistence
  migrations, safety changes, and still-open limitations in the release notes.
- If the prior stable release has no immutable Git tag, stop and repair the
  release-history gap before continuing. Every stable release, including the
  new one, must be traceable to a pushed Git tag.

### 2. Choose and apply the version

- Update `custom_firmware_version` in `platformio.ini`.
- For small changes that keep existing workflows and stored data compatible,
  advance to the next minor version. Reserve a major-version bump for a massive
  breaking release that deliberately invalidates important user workflows,
  compatibility, or stored data. Do not choose a different version policy
  without explicit user direction.
- Use the repository's established tag shape and make the stable tag match the
  configured firmware version exactly after any established prefix is removed.
  The release workflows reject a versioned tag that disagrees with
  `custom_firmware_version`.

### 3. Refresh both manuals

- Follow `docs/manual/README.md` for the full owner's guide. Find its previous
  source/PDF update, audit every later repository change, and update all
  relevant setup, controls, compatibility, safety, recovery, and
  troubleshooting guidance.
- Use the Humanizer skill for the writing pass. Every chapter except
  **Advanced: developers and builders** must sound like it was written for the
  person using Ble(e)p during a shoot: direct, familiar, and free of unnecessary
  implementation language or technical jargon.
- Rebuild the full PDF, copy it to the website download, compare checksums,
  render every page into a fresh directory, and visually inspect the complete
  manual plus changed or dense pages at full resolution.
- Update the compact package-insert manual tracked by Codex task
  `019ffb0c-362e-7262-89e1-3768c285453e`. Until that work is integrated, read
  the task handoff and synchronize it into the release branch. Once integrated,
  its maintained source is `hardware/packaging/generate_pocket_guide.py` and its
  generated artifact is `hardware/packaging/bleep-pocket-guide-2up.pdf`.
  Reconcile its setup, update, controls, safety, troubleshooting, compatibility,
  and full-manual link with the refreshed owner's guide. Regenerate it, render
  both duplex pages, check panel order and scale, and visually inspect it before
  release.

### 4. Verify and prepare the release commit

- Run all gates required by the changed code and documentation, including
  native tests when applicable, repository artifact checks, the complete manual
  render review, the insert-manual render review, and the full Montserrat
  `bleep` build. Keep software/build evidence separate from any required live
  hardware acceptance results.
- Record the exact release range, selected version and rationale, release-note
  summary, checks, measurements, hardware evidence, manual render inspection,
  and remaining limitations in `docs/progress.md`.
- Rebase the scoped release branch onto current `main`, rerun affected gates,
  and merge it from `main` with `--no-ff`. Confirm the release commit is the
  intended clean, reviewed state before pushing.

### 5. Push, tag, promote, and wait

- Push the release commit to `main`. Create the immutable version tag at that
  exact commit and push the tag. Confirm both the remote branch and remote tag
  resolve to the intended commits.
- Publish or promote the matching non-prerelease GitHub Release with the audited
  release notes. Approve the protected `stable` environment when prompted.
- Wait for the `stable` workflow to finish. Success requires the stable job to
  rebuild recovery and main, sign and independently verify the canonical
  release, and upload all stable and NVS-preserving USB migration assets.
  Queued, awaiting approval, in progress, cancelled, skipped, or failed does not
  count as released.
- Inspect the finished release and workflow rather than relying on the green
  summary alone: verify the expected version, commit, tag, manifest, signatures,
  recovery and main payloads, USB bundle, checksums, and owner's-guide asset.
  Record the run and immutable artifact evidence in `docs/progress.md`.

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
environment secret. Release assets are `bleep-update.bin`, `bleep-recovery.bin`,
`bleep-update.json`, `bleep-update.sig`, and the single-file
`bleep-update.bundle` (manifest bytes plus a base64 signature line) that the
panel fetches first; `verify_firmware_update.py --bundle` checks it matches.
The manifest key IDs currently accepted by firmware are
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
