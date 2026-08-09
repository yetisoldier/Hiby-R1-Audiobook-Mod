# GitHub Release Publishing

This project has hit the same GitHub publishing trap more than once: pushing a
tag is not the same thing as publishing a GitHub Release with downloadable
assets. The web URL for a tag can load while the API release object is missing
and the `.upt` download is unavailable. Always verify the API release and its
assets before calling a release done.

## Preferred Publisher

Use the checked-in publisher instead of retyping REST calls by hand:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\publish_github_release.ps1 `
  -Tag v2.0.28 `
  -Name "HiBy R1 Audiobook Mod v2.0.28" `
  -TargetCommitish main `
  -BodyFile firmware\releases\v2.0.28\RELEASE_NOTES.md `
  -Assets "work\audiobook-firmware-2.0.28\r1-audiobooks-2.0.28.upt,firmware\releases\v2.0.28\MD5SUMS.txt,firmware\releases\v2.0.28\SHA256SUMS.txt,firmware\releases\v2.0.28\RELEASE_NOTES.md"
```

Then verify the public release object and assets:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\publish_github_release.ps1 `
  -Tag v2.0.28 `
  -VerifyOnly `
  -Assets "work\audiobook-firmware-2.0.28\r1-audiobooks-2.0.28.upt,firmware\releases\v2.0.28\MD5SUMS.txt,firmware\releases\v2.0.28\SHA256SUMS.txt,firmware\releases\v2.0.28\RELEASE_NOTES.md"
```

If an asset was uploaded incorrectly, re-run with `-ReplaceAssets` after
checking that the local files and hashes are correct.

## Credential Fix

Confirm `gh auth status` succeeds before publishing. The checked-in publisher
can also use Git for Windows plus Git Credential Manager. It first checks
`GITHUB_TOKEN`, `GH_TOKEN`, and `GITHUB_PAT`. If
those are not set, it asks Git Credential Manager for the saved GitHub token by
feeding this exact credential request through a temporary file:

```text
protocol=https
host=github.com
username=yetisoldier
```

Do not print, log, or commit the returned `password=` value. The helper keeps it
in memory only and deletes the temporary credential request file.

If no credential is available, log in once:

```powershell
git credential-manager github login
```

After that, re-run `tools\publish_github_release.ps1`.

## Release Checklist

1. Build and locally verify the firmware package.
2. Flash and run installed-device verification when device access is available.
3. Put release notes and checksum files in `firmware\releases\vX.Y.Z`; keep the
   large `.upt` under `work\` and upload it directly as a release asset.
4. Update `README.md`, `CHANGELOG.md`, and `docs\production_release_checklist.md`.
5. Commit the release files.
6. Fast-forward and push `main` to the tested release commit.
7. Create and push the tag.
8. Run `tools\publish_github_release.ps1` to create the GitHub Release and upload assets.
9. Run the same script with `-VerifyOnly` and confirm the asset count, names, and sizes.
10. Check the README download link points at the new GitHub Release.

The final verification target is the API release object, not just the browser
page:

```powershell
Invoke-RestMethod `
  -Method Get `
  -Uri "https://api.github.com/repos/yetisoldier/Hiby-R1-Audiobook-Mod/releases/tags/v2.0.28" `
  -Headers @{ "User-Agent" = "hiby-r1-audiobook-release-check"; Accept = "application/vnd.github+json" }
```

## v2.0.x release gotchas

These bit the v2.0.x (NativeApp) release process — learned the hard way across
v2.0.15 → v2.0.17:

- **Target `main`.** The complete NativeApp source and public release history
  are now maintained on `main`; older notes referring to a separate codex
  release branch are historical.
- **The release BodyFile MUST be pure ASCII.** PowerShell 5.1 `Get-Content -Raw`
  reads no-BOM UTF-8 as cp1252, so `ConvertTo-Json` fails with HTTP 400 on any
  non-ASCII byte. Verify `0` non-ASCII bytes before publishing (the v2.0.0
  release notes were re-checked for this). Use plain ASCII dashes (`-`), not
  em-dashes, in release body text.
- **Transient 503 → 422 "asset already exists".** A retry cleans it up — the
  asset from the failed attempt is removed and the re-run succeeds. Don't
  assume the release is broken on a 422; just re-run.
- **git-bash strips unquoted backslashes in PowerShell args.** From git-bash,
  `-OutputUpt work\release-v2.0.17\r1-audiobooks-2.0.17.upt` UNQUOTED becomes
  `workrelease-v2.0.17r1-audiobooks-2.0.17.upt` (a file in the repo root). The
  build still succeeds but writes to the wrong path. **Quote paths or use
  forward slashes** when invoking PowerShell from git-bash.
- **Checksums live at `firmware\releases\<ver>\{MD5SUMS,SHA256SUMS}.txt`.**
  Generate them for the `.upt` after the build and ship them as release assets
  alongside the `.upt`. Release binaries live on GitHub Releases, not in git.
- **The `.upt` is renamed to `r1.upt` by the end user** to install; the release
  asset keeps the versioned name (`r1-audiobooks-<ver>.upt`).
- **Two ADB devices on one machine.** Only `ingenic_2233` is the R1; a second
  device (e.g. `ZY22JFZHDT`) is unrelated. Target `adb -s ingenic_2233`.
