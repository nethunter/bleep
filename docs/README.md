# Ble(e)p Documentation

The name expands playfully to **Bluetooth Links Everything, Eventually,
Probably**. Its serious descriptive expansion is **Bluetooth Low Energy
Equipment Panel**.

This directory is the durable source of truth for Ble(e)p as it evolves from an
iFootage Shark Nano II remote into a compile-time configurable studio
controller.

## Documents

- [Architecture](architecture.md) — system boundaries, driver model,
  transports, runtime instances, groups, and UI structure.
- [Implementation roadmap](implementation-roadmap.md) — ordered phases,
  completion gates, and verification requirements.
- [Device support](device-support.md) — target hardware, transports,
  capabilities, protocol status, and future drivers.
- [Protocol research](protocols/README.md) — evidence-backed protocol notes,
  extracted command vectors, confidence labels, and capture-privacy policy.
- [GitHub publishing](publishing.md) — repository and host-setting checks that
  remain before the first public push.
- [Scenes](scenes.md) — ordered action model, waits, generated Stop behavior,
  execution journal, and failure handling.
- [Multiple-Ble(e)p manufacturing](multi-bleep-manufacturing.md) — per-unit
  identity, open setup APs, independent meshes, same-room coexistence, and
  factory acceptance gates.
- [Decisions](decisions.md) — accepted architectural decisions and their
  rationale.
- [Progress](progress.md) — current phase, completed work, measurements,
  blockers, and the next safe task.

## Update discipline

Every implementation session should:

1. Read this index, [Decisions](decisions.md), and [Progress](progress.md).
2. Work on one roadmap phase or explicitly documented spike.
3. Update affected documentation when behavior or architecture changes.
4. Record build, flash, memory, and hardware results in
   [Progress](progress.md).
5. Mark a phase complete only when its completion gate passes.

Protocol guesses must not be recorded as facts. Label unverified behavior as
`Research`, `Hypothesis`, or `Blocked`, and link captures or authoritative
documentation when available.
