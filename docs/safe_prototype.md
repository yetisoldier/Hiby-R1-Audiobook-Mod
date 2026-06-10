# Safe Prototype Workflow

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
