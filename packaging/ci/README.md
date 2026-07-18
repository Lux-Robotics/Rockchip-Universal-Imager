# CI helpers

| File | Role |
|------|------|
| `ci-env.sh` | PATH / workspace helpers for bash steps on self-hosted runners |
| `package-dist.sh` | Assemble portable + installer zips from prebuilt app + rkdeveloptool |

## Workflows

```
package.yaml
  ├─ build-rkdeveloptool.yaml  → artifact rkdeveloptool-<os>-<arch>
  ├─ build-app.yaml            → artifact app-<os>-<arch>
  └─ package-dist.sh           → portable-all + installer-all zips
```

Portable vs installer: same two binaries + `loader_binaries/`; portable adds
an empty `portable` marker file. Logs always use OS system directories (see
`src-tauri/src/logging.rs`), not the app folder.
