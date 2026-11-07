---
name: Feature request
about: Propose a change or an addition
labels: enhancement
---

## Problem

What is missing or awkward today, and for which use case?

## Proposed change

Describe the behaviour you want. If it touches the flash layout, protocol
frames or partition sizes, note the impact.

## Alternatives considered

## Scope

- [ ] Application firmware
- [ ] Bootloader / install policy
- [ ] Host tools (`tools/`)
- [ ] Documentation
- [ ] CI

## Constraints to keep in mind

- Flash layout changes must stay consistent with `flash_layout.h`,
  `README.md`, `README.zh-CN.md` and `docs/protocol.md`.
- New untested behaviour must be recorded in `KNOWN_ISSUES.md`.
- The reference Ed25519 key pair is for examples only; real devices need
  their own key (see `docs/security.md`).
