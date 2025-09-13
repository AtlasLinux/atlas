# AtlasLinux

AtlasLinux is a minimal, custom Linux distribution built entirely from scratch using the Linux kernel and glibc. Its primary goal is to provide a completely self-contained, statically-linked environment for development and experimentation, without relying on external tools or scripting languages.

---

## Features

- Minimal init system with TTY and console support
- Custom shell (`hermes`) and basic coreutils
- Fully static executables to avoid runtime dependencies
- Simple disk image creation and management
- Networking support via manually configured interfaces
- Supports multiple TTYs with independent shell sessions
- Fully controllable build system using Makefiles and submodules

---

## Project Layout

```

atlas/
├── src/                # Source code for all packages
│   ├── sbin/init       # Init system
│   ├── bin/hermes      # Custom shell
│   ├── bin/coreutils   # Basic utilities: ls, cat, mkdir, etc.
│   └── bin/curl        # Networking test utility
├── build/              # Build output (temporary)
├── kernel/             # Linux kernel source or configuration
├── Makefile            # Build system for creating image and binaries
└── atlas.img           # Final disk image (generated)

````

---

## Build Instructions

1. Clone the repository and initialize submodules:

```bash
git clone --recurse-submodules <repo-url>
cd atlas
````

2. Ensure you have a C compiler, `make`, and `qemu-system-x86_64`.

3. Build the entire system and generate the disk image:

```bash
make
```

4. Run the system in QEMU:

```bash
make run
```

or combine clean and run:

```bash
make crun
```

---

## How It Works

* The Makefile automatically detects submodules with a `Makefile`, builds them, and copies all binaries into the image.
* Executables are installed flattened: e.g., `src/bin/curl/build/curl` → `/bin/curl` in the disk image.
* Plain files in `src/` directories without a Makefile are copied as-is.
* Init mounts `/proc`, `/sys`, and `/dev`, then spawns shell processes on multiple TTYs.
* Networking is configured manually in init (`lo`, `eth0`) with a static IP and default route.
* The custom shell `hermes` supports basic command execution and environment handling.

---

## Usage

* After running the system in QEMU, you will have TTY1-3 available for logging in and testing commands.
* Networking can be tested with the `curl` or `iptest` utilities inside the VM.

---

## Notes

* All binaries are statically linked to avoid external dependencies.
* The system is intended for learning and experimentation; it is not production-ready.
* The disk image can be used with QEMU or written to a block device.

---

## Future Improvements

* Implement a package management system for adding new tools.
* Enhance shell with scripting capabilities.
* Add dynamic network configuration and DHCP support.
* Build kernel modules directly into the image for additional hardware support.

---

## License

AtlasLinux is provided under the MIT License. See [LICENSE](LICENSE) for details.