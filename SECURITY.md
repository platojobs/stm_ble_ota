# Security policy

## Reporting a vulnerability

Please **do not** open a public issue for a security problem. Report it
privately to the maintainer:

- GitHub: open a private security advisory in the repository's *Security* tab,
  or
- email the address in the repository profile.

Include the affected version/tag, a description of the issue, the impact on a
device, and reproduction steps if available. You will get an acknowledgement
within a few days. Please allow time for a fix before public disclosure.

## Supported versions

This is a reference project; only the latest tagged release and `main` receive
security fixes. Older tags are kept for history and are not patched.

## Scope and known boundaries

The reference firmware is **not validated on hardware**. Security work assumes
the threat model in [docs/security.md](docs/security.md):

- image authenticity (Ed25519) is in scope; image **confidentiality** is not;
- WiFi credentials are obfuscated at rest with a UID-derived key, which
  prevents plaintext leakage but not a physical attacker;
- there is no secure boot, no anti-rollback fuse and no secure element;
- the bootloader public key in this tree is a **reference/example key** and
  provides no real security until replaced for production.

Issues that depend on physical flash/debug access, a leaked production key or
the lack of hardware validation are documented in
[KNOWN_ISSUES.md](KNOWN_ISSUES.md) and are not treated as new vulnerabilities
unless they contradict that documented boundary.

## Secrets in the repository

Private signing keys, WiFi passwords for real networks, and CI secrets must
never be committed. `tools/keys/` is git-ignored; CI uses the
`OTA_SIGNING_KEY_B64` secret. If you find a committed secret, report it
privately and rotate it immediately.
