## Summary

What changed and why. One logical change per pull request.

## Type

- [ ] Bug fix
- [ ] Feature
- [ ] Documentation
- [ ] Build / CI
- [ ] Refactor (no behaviour change)

## Verification

Paste the commands and their results:

- [ ] `cmake --build build-host && ./build-host/unit_tests`
- [ ] `./tools/build.sh` (ARM, `-Werror`)
- [ ] `cppcheck --enable=warning,performance,portability ...` clean (CI runs it)
- [ ] Signature packaging still works (`firmware_tool.py pack` then `inspect`)
- [ ] Tested on real hardware — describe the setup, or state that it was not

## Checklist

- [ ] Flash layout, protocol and documentation changed together
  (`flash_layout.h`, `docs/protocol.md`, `README.md`, `README.zh-CN.md`)
- [ ] New untested behaviour is listed in `KNOWN_ISSUES.md`
- [ ] Assertion count in `KNOWN_ISSUES.md` matches the host test output
- [ ] No private keys, credentials or device-unique secrets added
- [ ] Commits are not force-pushed over published history

## Related issues

Closes #
