# Security notes

This page documents key management, credential storage and the threat model of
the reference firmware. It is a starting point for a product, not a guarantee.

## Firmware signing keys

### What is in the repository

`firmware/common/include/ota_public_key.h` contains a **reference/example
Ed25519 public key**. It ships so the example can be exercised out of the box
and must **never** be used on a real device: the matching private key is only
in the maintainer's local `tools/keys/` directory, which is git-ignored, but it
may still be leaked or recreated. The repository contains no private key, no
seed and no CI secret.

### Generate a production key pair

Do this on an offline machine with a trustworthy OS and no network access:

```bash
openssl genpkey -algorithm ED25519 -out production_ed25519_private.pem
openssl pkey -in production_ed25519_private.pem -pubout \
    -out production_ed25519_public.pem
```

Convert the public half to the 32-byte array expected by `ota_public_key.h`
(the `keygen --public` output of `tools/firmware_tool.py` prints the raw bytes
in the same order). Replace `OTA_ED25519_PUBLIC_KEY`, rebuild the bootloader,
and record the key fingerprint in the release notes.

### Custody

- Keep the private key in an HSM, a signing service, or at minimum an
  encrypted offline volume; never in this repository.
- CI signs with `OTA_SIGNING_KEY_B64` (base64 PKCS#8 PEM) stored as a
  repository/environment secret with restricted access. Do not print it, do
  not commit it, do not reuse it across products.
- Keep a second copy offline with an access log; a lost key means a bootloader
  update is required to trust new images.
- Keep the raw public key in the bootloader image inventory, one fingerprint
  per release.

### Rotation

Ed25519 verification uses one public key per bootloader. Rotating a key is
therefore a bootloader update:

1. Generate and custody the new pair.
2. Rebuild the bootloader with the new public key and flash it over SWD
   (OTA cannot replace the verifier that is checking the OTA).
3. From then on, sign application packages with the new private key.
4. Retire the old private key; keep the old public key only for audit.

If a private key is leaked, treat every image signed with it as untrusted,
rotate the bootloader key on all devices, and re-sign the application. There
is no anti-rollback fuse, so physical flash access can still restore an old
bootloader (see `KNOWN_ISSUES.md`).

## WiFi credentials at rest

The config record store does not keep the SSID or password in plaintext.
`wifi_config_save` XORs both strings with a SHA-256 keystream keyed by
`SHA-256(MCU UID || "stm-ble-ota/wifi/v2")` (see `config_store.c` and
`device_service_storage_key`). The UID is 96 bits and unique per die.

**Strength boundary.** This is obfuscation, not encryption:

- the key is derived from the UID, which anyone who can dump the flash or
  attach a debugger can read;
- readout protection (RDP) raises the effort, but it is not a secure element;
- the scheme does not protect a credential against a determined attacker with
  physical access.

It does prevent credentials from appearing verbatim in flash dumps, backups
and support logs. Treat WiFi passwords as low-value shared secrets: use a
dedicated network/account for the device, rotate them, and never reuse
credentials that protect other systems.

## Other boundaries

- OTA authenticity is Ed25519 over SHA-256, size, version, build id and
  hardware id. Images are not encrypted.
- The bootloader accepts an unsigned image only when the metadata carries no
  signature at all (factory provisioning path); OTA downloads always require
  a valid signature.
- The CLI and BLE provisioning paths validate credential length/characters
  and bound all host text lines before parsing (`host_link.c`).
- There is no secure boot, no debug lock by default and no attested identity.
  Provision readout protection and disable debug access on production units
  if the threat model requires it.

Report vulnerabilities according to [SECURITY.md](../SECURITY.md).
