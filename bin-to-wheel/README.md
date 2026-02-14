# bin-to-wheel

Package pre-built binaries into Python wheels.

## Usage

```bash
bin-to-wheel \
  --name erpl-adt \
  --version 2026.2.14 \
  --binary ./build/erpl-adt \
  --platform linux-x86_64 \
  --output-dir ./dist \
  --entry-point erpl-adt
```

Single binary in, single `.whl` out. CI loops externally over platforms.

## Install

```bash
pip install bin-to-wheel
```
