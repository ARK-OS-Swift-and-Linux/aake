# aake
The ARK-OS wrapper for Build Steps, Meson and Ninja!

ARK-OS: https://ark-os.duckdns.org/Aarav90-cpu/ARK-OS


## Prerequisites

Arch Linux:

`sudo pacman -Syu make gcc ninja`

Debian:

`sudo apt-get install make gcc ninja`

### How to use compiler

```text

Usage: aake [options] [commands]

Commands:

build       Build ArkOS for the host architecture (default if options passed)
test        Run ArkOS in QEMU (automatically builds if needed)
clean       Clean the build directory
sign        Sign binaries
font-gen    Generate TTF fonts to Swift/C arrays
cursor-gen  Generate mouse cursor arrays

Options:

--x86_64    Build/test for x86_64 architecture
--arm64     Build/test for ARM64 architecture
--uefi      Test in UEFI mode (for x86_64)
--bios      Test in BIOS mode (for x86_64)
-v          Verbose output
-j          Number of parallel jobs
--no-ninja  Generate build files but don't compile
```

#### Cross-Architecture Testing

```bash
aake test --x86_64 --uefi
aake test --x86_64 --bios
aake test --arm64 --uefi
aake test --arm64 --bios
```

Test targets auto-detect the host CPU and use hardware acceleration (KVM) when possible, falling back to software emulation (TCG) for cross-architecture testing.