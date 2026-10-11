# Vendored shared code

`vendor/arcade-link/provenance.json` pins the Qt Link module and fixtures to
Arcade Link `v0.2.0`. `vendor/arcade-link/shared-provenance.json` pins the
shared Windows installers, release generator, and shortcut tools to committed
Arcade Link `arcade/tools-hub` commit `4cab38ddcad4ba4541d4727f0ca44c3a2151cb00`.

Run `python3 scripts/check-vendor.py` to verify all local hashes. To compare the
shared files with that source checkout, run:

```sh
python3 scripts/check-vendor.py --shared-upstream /path/to/Arcade-link
```

The shared files are kept behind a separate pin because they were committed
before the Qt `ArcadeTrayHost`, `ArcadeInstall`, and accelerator port. Shelf
continues to use the v0.2.0 Qt transport until that port is committed and
reviewed for this branch.
