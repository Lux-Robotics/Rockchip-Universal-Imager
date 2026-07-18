# Shared CI helpers

These sit with the other packaging/bootstrap tooling (not under `.github/scripts`).

| File | Used by |
|------|---------|
| `ci-env.sh` | All workflow bash steps (`source packaging/ci/ci-env.sh`) |

Windows-only shell launcher lives next to the Windows bootstrap script:

- `packaging/windows/windows-bash.cmd`
- `packaging/windows/bootstrap-build-deps.ps1`

They ship **in the repo** and are used after `actions/checkout`. Bootstrap installs
system toolchains; it does not need to copy these files onto the machine.
