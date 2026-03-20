# batman-adv Namespace Throughput Override

This repository contains a modified `batman-adv` kernel module together with a
small userspace utility for configuring per-neighbor throughput overrides.

The work was done for a project evaluating mesh routing protocols in Linux
network namespaces. The main use case is emulation where link rates are shaped
from userspace, for example with `tc netem rate`, but BATMAN_V cannot infer the
intended throughput to a specific neighbor from the hard interface alone.

## Contents

- `batman-adv/`
  Modified `batman-adv` out-of-tree kernel module source.
- `battpctl/`
  Small userspace utility for configuring and dumping per-neighbor throughput
  overrides via generic netlink.

## What Was Added

The patch adds per-neighbor throughput overrides for BATMAN_V.

Behavior:

- throughput selection precedence is:
  1. per-neighbor override
  2. hard-interface override
  3. normal autodetection/default behavior
- the override value unit is `100 kbit/s`
- a value of `10` means `1.0 Mbit/s`
- a value of `0` removes the configured per-neighbor override

The module also exposes dedicated generic-netlink commands for:

- setting one neighbor override
- dumping configured neighbor overrides

## Building The Module

Build from inside the `batman-adv/` directory:

```sh
make
```

There is also a `batman-adv/justfile` with recipes for building, loading,
unloading, viewing logs, and working with the namespace-based test setup. The
most relevant recipes are:

- `just build`
- `just ns-*`
- `just unload`
- `just reload`
- `just logs`
- `just insmod`

Notes:

- this is an out-of-tree module build
- the target machine must have matching kernel headers installed
- the build uses the local kernel build tree at `/lib/modules/$(uname -r)/build`

## Building The Userspace Utility

Build from inside the `battpctl/` directory:

```sh
make
```

The utility depends on:

- `libnl-3-dev`
- `libnl-genl-3-dev`
- `pkg-config`

On Ubuntu:

```sh
sudo apt update
sudo apt install libnl-3-dev libnl-genl-3-dev pkg-config
```

## Using The Userspace Utility

Commands:

```sh
./battpctl set <mesh-ifname> <hard-ifname> <neighbor-mac> <throughput-100kbps>
./battpctl del <mesh-ifname> <hard-ifname> <neighbor-mac>
./battpctl dump <mesh-ifname> [hard-ifname]
```

Examples:

```sh
./battpctl set bat0 ve0 32:3c:93:a3:6c:ee 12340
./battpctl dump bat0
./battpctl del bat0 ve0 32:3c:93:a3:6c:ee
```

The `dump` command shows configured override entries. It does not show the raw
neighbor metric currently displayed by `batctl`. BATMAN_V stores the neighbor
throughput as an EWMA-smoothed metric, so `batctl meshif <mesh> neighbors` may
move gradually toward the configured override instead of changing immediately.

## Notes For Experiments

- This work is intended for controlled experiments, not upstream submission.
- It is useful when the effective emulated link rate is known externally.
- In namespace-based setups, verify that you target the correct:
  - mesh interface, for example `bat0`
  - hard interface, for example `ve0`
  - neighbor MAC on that hard interface

## Licensing

The modified `batman-adv` code remains under its original licensing terms. Keep
existing license headers and copyright notices in the source files.
