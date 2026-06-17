# Network OTA Research

Status: investigation and tooling, not production-enabled.
Date: 2026-06-17.

## Short Answer

The OTA path can be changed, but the stock updater does not consume the same
single `.upt` file that people download from GitHub Releases. The R1 network
updater expects a static directory tree:

```text
ota_config.in
ota_vN/ota_vN.ok
ota_vN/ota_update.in
ota_vN/ota_md5_xImage.<md5>
ota_vN/xImage.0000.<md5>
ota_vN/ota_md5_rootfs.squashfs.<md5>
ota_vN/rootfs.squashfs.0000.<md5>
...
```

Some stock scripts use the root `ota_vN` path, while other recovery/main-os
flows look under `main_os/ota_vN`. The new `tools/build_r1_ota_site.py` helper
can prepare both layouts from a generated build tree.

## Local R1 Findings

Stock `/etc/ota_info` contains:

```text
ota_version=0
ota_site=/data/autoupdate/autoupdate
```

`/etc/ota_bin/ota_local_method.sh` reads that file. The network updater then:

1. Downloads `$ota_site/ota_config.in`.
2. Reads `current_version`.
3. Compares it with local `ota_version`.
4. Downloads `$ota_site/ota_vN/ota_vN.ok`.
5. Downloads `$ota_site/ota_vN/ota_update.in`.
6. Downloads MD5 manifest files and image chunks one at a time.

The generated audiobook packages previously used `current_version=0` and
`ota_version=0`. That works for manual SD-card `.upt` installs, but it is not
usable for real network OTA because installed devices would never have a
monotonic numeric version to compare. The build system now supports opt-in
numeric OTA versions.

## GitHub Constraint

GitHub Releases are still the right place for public downloads. GitHub documents
direct latest-release asset links with the form
`/releases/latest/download/asset-name.zip`, but those are single downloadable
assets, not a browsable OTA tree. GitHub's release-asset API can also redirect,
and API clients are expected to handle either a `200` or `302` response:

- https://docs.github.com/en/repositories/releasing-projects-on-github/linking-to-releases
- https://docs.github.com/en/rest/releases/assets?apiVersion=2026-03-10

GitHub Pages can serve a static tree, but GitHub documents HTTPS enforcement and
HTTP-to-HTTPS redirects:

- https://docs.github.com/en/pages/getting-started-with-github-pages/securing-your-github-pages-site-with-https

The R1's installed BusyBox `wget --help` only advertises HTTP and FTP support,
and the device rootfs does not include populated CA certificates. That makes
direct GitHub HTTPS OTA risky unless we add a custom downloader or test a proven
TLS path on-device.

## Safer Path

Recommended staged approach:

1. Keep public GitHub Releases as the normal human download path.
2. Generate a stock-compatible OTA tree for each release with
   `tools/build_r1_ota_site.py`.
3. Publish that tree as a release artifact or committed folder for transparency.
4. Only point firmware `ota_site` at a live URL after we have a known-good static
   host that the R1 can fetch.
5. Prefer a plain HTTP mirror for the first on-device OTA test, or build and
   test a small custom HTTPS downloader before using GitHub Pages directly.

Example OTA-site staging command:

```powershell
python tools\build_r1_ota_site.py `
  --source-tree work\audiobook-firmware-1.6.16.2-audiobook\ota-tree `
  --out-dir work\ota-site\v37 `
  --ota-version 37 `
  --firmware-version 1.6.37-network-ota-test `
  --upt work\audiobook-firmware-1.6.16.2-audiobook\r1-audiobooks-1.6.16.2-audiobook.upt `
  --force
```

Example opt-in firmware build knobs:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build_r1_audiobook_firmware.ps1 `
  -OtaVersion 37 `
  -OtaSite "http://example.test/hiby-r1-audiobook/ota"
```

Do not ship a production build with a network `ota_site` until the hosted tree,
download behavior, and recovery path have been tested on the actual R1.
