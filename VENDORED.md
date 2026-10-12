# Vendored shared code

`vendor/arcade-link/provenance.json` pins the Qt Link module and fixtures to
Arcade Link `v0.3.0`. `vendor/arcade-link/shared-provenance.json` pins the
shared Windows installers, release generator, and shortcut tools to committed
same released commit `33650a87b6999fbd553ac678cc7bde7a308cb852`.

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
