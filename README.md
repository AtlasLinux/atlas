# AtlasLinux

AtlasLinux is a from-scratch Linux distro, where we use as few external libraries as possible. The long-term goal is to only rely on the Linux kernel, with a custom [libc](https://github.com/atlaslinux/atlibc), bootloader, etc.

If you solely want the .iso, you can get it from the auto-built [release](https://github.com/atlaslinux/atlas/releases/tag/latest).

## Developing

### Makefile usage

To get started, run
```bash
make kernel
```
to build the kernel using our config (beware, this can take upwards of 1 hour and uses all available cores by default).

Once the kernel is built, the rest of the targets become available.

`all`: the main target. Builds everything under `src/` and installs into `atlas.img` for quick testing.

`modules`: installs the specified kernel modules into `build/`. Requires a space-seperated list of modules passed into `$(MODULES)` (i.e. `make modules MODULES=e1000` for the e1000 intel ethernet driver)

`build`: builds everything under `src/` without installing

`install`: installs everything under `src/` into `build/`

`img`: creates `atlas.img` from the contents of `build/`

`iso`: creates an `initramfs.cpio.gz` from the contents of `build/`, configures grub and creates `atlaslinux-x86_64.iso`

`run`: runs `atlas.img`

`run-iso`: runs `atlaslinux-x86_64.iso`

`crun`: wrapper for `clean` and `run`

`crun-iso`: wrapper for `clean` and `run-iso`

`clean`: removes build artifacts from `build/`, `iso/` and `src/**/build/`

### Creating an application for AtlasLinux

Atlas' top-level `Makefile` utilises a clever build system, where is automagically builds all `Makefile`s within the `src/` dir.  To get started, create a diractory where the eventual application will be located:
```
src/
└── bin/
    └── app/
```
Populate this directory with the following:
```
app/
├── Makefile
└── src/
    └── main.c      # entrypoint
```
The `Makefile` only has to follow a few rules:

1. Any outputted executables/libraries **must** be placed in `build/`
2. It must have a `clean` target that removes `build/` 