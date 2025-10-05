# Contributing to Atlas Linux

Thanks for your interest in contributing.  
Atlas Linux is a minimal operating system built entirely from scratch - correctness, simplicity, and reproducibility are our highest priorities.

---

## 🧭 Ground Rules

- **No external runtime dependencies.**  
  Everything in the target system must build from source and rely only on the kernel and libc.

- **Code must compile cleanly** with `-Wall -Wextra -Werror` and be free of undefined behaviour.  
  If you're not sure about a C standard edge case, ask in your pull request.

- **Keep it small and auditable.**  
  Fewer moving parts are preferred over more features.

- **Portable, not pretty.**  
  Code should work on any x86_64 Linux toolchain with standard POSIX utilities.

---

## 🧱 Project Layout

```

atlas/
├─ linux/          # Linux kernel sources and config
├─ src/            # Core userland and libraries
├─ build/          # Host-side build outputs
├─ iso/            # ISO image tree
└─ docs/           # Documentation and guides

````

---

## 🛠️ Development Workflow

1. **Fork and clone** the repository.
2. **Create a branch** for your change:
   ```bash
   git checkout -b feature/short-description
   ```

3. **Build and test locally:**

   ```bash
   make clean all
   make run
   ```
4. **Check your diffs** - small, self-contained commits are easier to review.
5. **Submit a pull request** and describe:

   * What it changes.
   * Why it’s needed.
   * How you tested it.

---

## 🧩 Code Style

* Follow **K&R C** with 4-space indentation.
* Log through `liblog` instead of `printf` in system components.

---

## 🧪 Testing

* Boot your change in QEMU before submitting.
* Run `make clean all` to ensure full rebuilds work.
* Check that `/sbin/init` launches correctly from both the `atlas.img` and the `atlaslinux-x86_64.iso`.

---

## 📝 Documentation

If you add a new tool, library, or subsystem:

* Add a short markdown file under `docs/`.
* Update the relevant section in `guide.md` or `index.md`.
* Include usage examples or boot arguments if applicable.

---

## 🧹 Commit Messages

Keep them descriptive and imperative:

```
init: add tmpfs mount for /run
liblog: fix race in log rotation
build: use fakeroot for initramfs packing
```

Avoid generic messages like “update files” or “minor fix.”

---

## 💬 Getting Help

Open a draft PR or discussion if:

* You’re unsure about design choices.
* You want review on architecture or code layout before implementing.
* You found a kernel interaction or boot quirk worth documenting.

---

Atlas Linux is a small system with big ambitions.
Every contribution - even documentation or cleanup - helps keep it simple, transparent, and yours.