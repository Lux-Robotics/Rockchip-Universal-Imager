# Shared CI helpers

These sit with the other packaging/bootstrap tooling.

| File | Used by |
|------|---------|
| `ci-env.sh` | Workflow bash steps (`source packaging/ci/ci-env.sh`) |

Windows steps run under MSYS2 bash (`MSYS2_BASH` from bootstrap). Linux/macOS use
the runner’s normal `bash`. Bootstrap installs system toolchains only; this
helper ships in the repo and is used after `actions/checkout`.
