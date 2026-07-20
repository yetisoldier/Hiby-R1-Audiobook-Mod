# Safe Prototype Workflow

> **⚠️ SUPERSEDED — historical record (pre-2.0, v1.6.x era).** This describes
> the old no-flash ADB prototype workflow that filtered a copied media
> database so `/Audiobooks` paths were removed from normal Music views. The
> current firmware (v2.0.17) is the NativeApp pivot — an in-process
> `LD_PRELOAD` audiobook app — and does not use this approach. Retained for
> historical/recovery context only. For the current build/flash workflow see
> [`build_flash_verify_runbook.md`](./build_flash_verify_runbook.md) and
> [`modding/`](./modding/).

This older workflow is kept for developers and recovery-minded tinkerers. Most users should install the release firmware from the main README instead.

The prototype path does not flash firmware. It uses ADB to collect the live R1 state, then filters a copied media database so `/Audiobooks` paths are removed from normal Music views.

Once ADB is enabled and the R1 is connected:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\adb_collect_r1_state.ps1
```

Then filter a copied media database:

```powershell
& "C:\Users\yetis\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe" `
  tools\filter_music_db.py `
  device-dump\<timestamp>\usrlocal_media.db `
  -o work\usrlocal_media.no-audiobooks.db
```

The default excluded prefixes are:

- `a:\Audiobooks\`
- `a:\Audiobook\`
- `a:\Audio Books\`

Custom prefixes can be added with repeated `--prefix` arguments.
