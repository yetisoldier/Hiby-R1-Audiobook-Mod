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
  -Tag v1.4.0 `
  -Name "HiBy R1 Audiobook Mod v1.4.0" `
  -BodyFile firmware\releases\v1.4.0\README.md `
  -Assets "firmware\releases\v1.4.0\r1-audiobooks-1.6.15-audiobook.upt,firmware\releases\v1.4.0\MD5SUMS.txt,firmware\releases\v1.4.0\SHA256SUMS.txt"
```

Then verify the public release object and assets:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\publish_github_release.ps1 `
  -Tag v1.4.0 `
  -VerifyOnly `
  -Assets "firmware\releases\v1.4.0\r1-audiobooks-1.6.15-audiobook.upt,firmware\releases\v1.4.0\MD5SUMS.txt,firmware\releases\v1.4.0\SHA256SUMS.txt"
```

If an asset was uploaded incorrectly, re-run with `-ReplaceAssets` after
checking that the local files and hashes are correct.

## Credential Fix

`gh` is useful if installed, but it has not been available in this development
environment. The reliable path is Git for Windows plus Git Credential Manager.
The publisher first checks `GITHUB_TOKEN`, `GH_TOKEN`, and `GITHUB_PAT`. If
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
3. Copy the verified package and checksum files into `firmware\releases\vX.Y.Z`.
4. Update `README.md`, `CHANGELOG.md`, and `docs\production_release_checklist.md`.
5. Commit the release files.
6. Push `main`.
7. Create and push the tag.
8. Run `tools\publish_github_release.ps1` to create the GitHub Release and upload assets.
9. Run the same script with `-VerifyOnly` and confirm the asset count, names, and sizes.
10. Check the README download link points at the new GitHub Release.

The final verification target is the API release object, not just the browser
page:

```powershell
Invoke-RestMethod `
  -Method Get `
  -Uri "https://api.github.com/repos/yetisoldier/Hiby-R1-Audiobook-Mod/releases/tags/v1.4.0" `
  -Headers @{ "User-Agent" = "hiby-r1-audiobook-release-check"; Accept = "application/vnd.github+json" }
```
