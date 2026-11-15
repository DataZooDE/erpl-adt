---
name: erpl-adt-transport
description: >
  Managing SAP transports from the shell with erpl-adt: transport list --owner, transport create --desc
  --package, transport release, passing --transport to object create / source write / object delete, and the
  legacy YAML deploy workflow (erpl-adt deploy -c config.yaml, status, pull). Use when changes need a transport
  request, when releasing one, or when deploying a set of objects into an SAP system repeatably.
user-invocable: false
---

# erpl-adt — Transports Cheat Sheet

## Critical gotchas

1. **Releasing is irreversible.** A released request is exported to the transport directory; confirm the
   request and its contents with the user first (`/erpl-adt:release` does this).
2. **`$TMP` is local** — objects there cannot be transported. Use a transportable package for anything that
   should move.
3. **Exit code 9** is a transport error (missing request, wrong owner, request already released).

## Commands

```bash
erpl-adt --json transport list                      # the logon user's requests
erpl-adt --json transport list --owner DEVELOPER
erpl-adt --json transport create --desc 'Feature X' --package ZTEST_PKG
erpl-adt --json source write ZCL_X --file zcl_x.abap --transport A4HK900001 --activate
erpl-adt --json transport release A4HK900001
```

## Repeatable deployment (YAML)

```bash
erpl-adt deploy -c deploy.yaml      # idempotent: discover → create package → clone → pull → activate
erpl-adt status --config deploy.yaml
```

The config format is in the erpl-adt repository (`examples/flight.yaml`, `examples/multi-repo.yaml`).

See also: `erpl-adt-develop`, `erpl-adt-setup`.
