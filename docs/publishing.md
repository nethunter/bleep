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

- [ ] Create the GitHub repository and add it as this checkout's remote; no Git
  remote is currently configured.
- [ ] Add a short description and topics such as `esp32`, `lvgl`, `bluetooth`,
  `remote-control`, `camera`, `studio`, and `platformio`.
- [ ] Enable Issues, Discussions, and private vulnerability reporting.
- [ ] Protect `main`; require the native-test and firmware-build checks.
- [ ] Disable force pushes and branch deletion on `main`.
- [ ] Enable Dependabot alerts, secret scanning, and push protection when the
  hosting plan makes them available.
- [ ] Confirm private vulnerability reporting is enabled and the instructions
  in `SECURITY.md` match the repository's Security interface.
- [ ] Add a first production release only after a tagged firmware build and
  physical smoke test pass. The rolling **Latest development firmware**
  prerelease is an automated CI snapshot and must remain clearly labeled as
  hardware-unverified.

## License

Ble(e)p uses Apache-2.0, matching Home Assistant Core's permissive license and
explicit patent grant. Third-party assets, fonts, libraries, and future
protocol contributions still need license-compatibility review before release.
