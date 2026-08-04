# Security policy

Ble(e)p is pre-release firmware and does not yet publish supported release
branches or security-update guarantees.

## Reporting a vulnerability

Please do not open a public issue for a vulnerability, leaked pairing material,
credential, or device identifier. Once the repository is hosted on GitHub, use
the repository's private vulnerability reporting form under **Security → Report
a vulnerability**. Include the affected revision, reproduction steps, impact,
and any suggested mitigation.

Until private reporting is enabled, do not attach sensitive material to the
repository. Open a public issue asking a maintainer to establish a private
contact channel without including vulnerability details.

Areas that deserve particular care include:

- BLE bonds and pairing identities;
- future Bluetooth Mesh keys;
- future Wi-Fi credentials and Canon CCAPI endpoints;
- configuration import/export;
- malformed wireless frames and fixed-size queues;
- temporary Portal-mode access and timeout behavior.

Please allow maintainers time to reproduce and fix the issue before public
disclosure. The project will credit reporters unless they prefer anonymity.
