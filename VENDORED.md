# Vendored shared code

`vendor/arcade-link/VENDORED.json` pins the Qt Link module, the QML design kit and
Inter to Arcade Link `v1.0.10` (commit `2748ebf49eba78b131a1d41a40f92997d9141ffb`).
`vendor/arcade-link/shared-provenance.json` pins the shared Windows installers,
release generator and shortcut tools to the same tag and commit. Re-vendor with
`python3 scripts/vendor-link.py --link /path/to/Arcade-Link --revision v1.0.10`.

Run `python3 scripts/check-vendor.py` to verify all local hashes. To compare the
shared files with that source checkout, run:

```sh
python3 scripts/check-vendor.py --shared-upstream /path/to/Arcade-link
```

The Qt bundle includes the transport, accelerator and shortcut helpers,
`ArcadeTrayHost`, `ArcadeInstall`, shared install strings, tests and vectors.
Both provenance files record SHA-256 hashes of unmodified upstream files.
Use `--upstream /path/to/Arcade-link --shared-upstream /path/to/Arcade-link`
to compare both sets with the released source.
