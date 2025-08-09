# Top-level Makefile for Atlas Linux (with disk image rootfs and initramfs support)

# Directories
SRC_DIR   := src
BUILD_DIR := build
MOUNT_POINT := mnt

# Kernel locations
KERNEL_SRC := $(SRC_DIR)/linux
BZIMAGE    := $(KERNEL_SRC)/arch/x86/boot/bzImage
OUT_KERNEL := $(BUILD_DIR)/bzImage

# Packages (add new names here — each package lives in src/<name>)
PACKAGES := init

# Parallelism
NPROC ?= $(shell nproc 2>/dev/null || echo 1)

# QEMU defaults (override on command line if you like)
QEMU      ?= qemu-system-x86_64
QEMU_MEM  ?= 8192
QEMU_CPUS ?= $(NPROC)
QEMU_OPTS ?= -m $(QEMU_MEM) -smp $(QEMU_CPUS) -enable-kvm -display gtk -vga virtio

# Image settings
IMAGE       := $(BUILD_DIR)/atlas.img
IMAGE_SIZE  := 64    # in MB

# initramfs paths (optional quick boot)
INITRAMFS_ROOT := $(BUILD_DIR)/initramfs-root
INITRAMFS      := $(BUILD_DIR)/initramfs.cpio.gz

# Default target
.PHONY: all
all: setup kernel packages initramfs img move

### Setup ###
.PHONY: setup
setup:
	@mkdir -p $(BUILD_DIR) $(MOUNT_POINT)

### Kernel build ###
.PHONY: kernel
kernel: setup
	@echo "==> Building Linux kernel (src/linux)"
	$(MAKE) -C $(KERNEL_SRC) -j$(NPROC)
	@echo "==> Installing kernel to $(OUT_KERNEL)"
	@if [ -f "$(BZIMAGE)" ]; then \
	  cp -f "$(BZIMAGE)" "$(OUT_KERNEL)"; \
	  echo "Copied $(BZIMAGE) -> $(OUT_KERNEL)"; \
	else \
	  echo "ERROR: $(BZIMAGE) not found — did kernel build succeed?"; \
	  exit 1; \
	fi

### Packages ###
.PHONY: packages $(PACKAGES)
packages: $(PACKAGES)

$(PACKAGES):
	@echo "==> Building package: $@"
	$(MAKE) -C $(SRC_DIR)/$@ -j$(NPROC)
	@echo "==> Copying $@ executable to $(BUILD_DIR)/"
	@if [ -f "$(SRC_DIR)/$@/build/$@" ]; then \
	  cp -f "$(SRC_DIR)/$@/build/$@" "$(BUILD_DIR)/"; \
	  chmod +x "$(BUILD_DIR)/$@"; \
	  echo "Copied $(SRC_DIR)/$@/build/$@ -> $(BUILD_DIR)/$@"; \
	else \
	  echo "WARNING: expected executable not found at $(SRC_DIR)/$@/build/$@"; \
	fi

### Initramfs creation ###
.PHONY: initramfs
initramfs: packages
	@echo "==> Building initramfs from build/init"
	@rm -rf "$(INITRAMFS_ROOT)"
	@mkdir -p "$(INITRAMFS_ROOT)"/{bin,sbin,etc,proc,sys,dev,tmp}
	@if [ -f "$(BUILD_DIR)/init" ]; then \
	  mv -f "$(BUILD_DIR)/init" "$(INITRAMFS_ROOT)/sbin/init"; \
	  chmod 0755 "$(INITRAMFS_ROOT)/sbin/init"; \
	else \
	  echo "ERROR: $(BUILD_DIR)/init missing — build the init package first"; exit 1; \
	fi
	ln -sf /sbin/init "$(INITRAMFS_ROOT)/init"
	@if [ ! -e "$(INITRAMFS_ROOT)/dev/console" ]; then \
	  if [ "$$(id -u)" -ne 0 ]; then \
	    if command -v sudo >/dev/null 2>&1; then \
	      sudo mknod -m 600 "$(INITRAMFS_ROOT)/dev/console" c 5 1 || true; \
	    else \
	      echo "Warning: cannot create /dev/console (no sudo)"; \
	    fi; \
	  else \
	    mknod -m 600 "$(INITRAMFS_ROOT)/dev/console" c 5 1 || true; \
	  fi; \
	fi
	@(cd "$(INITRAMFS_ROOT)" && find . -print0 | cpio --null -ov --format=newc 2>/dev/null) | gzip -9 > "$(INITRAMFS)"
	@echo "Created $(INITRAMFS) (size: $$(stat -c%s $(INITRAMFS) || echo unknown) bytes)"

### Disk image creation ###
.PHONY: img
img: setup packages
	@echo "==> Creating blank $(IMAGE) of size $(IMAGE_SIZE)MB"
	@dd if=/dev/zero of=$(IMAGE) bs=1M count=$(IMAGE_SIZE) status=none
	@echo "==> Formatting $(IMAGE) as ext4"
	@mkfs.ext4 -F $(IMAGE)
	@echo "==> Mounting $(IMAGE) on $(MOUNT_POINT)"
	sudo mount -o loop $(IMAGE) $(MOUNT_POINT)
	@echo "==> Creating base directory structure in mounted image"
	sudo mkdir -p $(MOUNT_POINT)/{bin,etc,dev,proc,sys,tmp}
	@echo "==> Copying packages into mounted image"
	@for pkg in $(PACKAGES); do \
	  if [ -f "$(BUILD_DIR)/$$pkg" ]; then \
	    sudo cp -f "$(BUILD_DIR)/$$pkg" $(MOUNT_POINT)/bin/; \
	    sudo chmod +x $(MOUNT_POINT)/bin/$$pkg; \
	  else \
	    echo "WARNING: $(BUILD_DIR)/$$pkg not found, skipping copy to image"; \
	  fi \
	done
	@echo "==> Creating minimal /etc/fstab"
	echo "/dev/vda / ext4 defaults 0 1" | sudo tee $(MOUNT_POINT)/etc/fstab > /dev/null
	@echo "==> Creating minimal /etc/inittab"
	echo "::sysinit:/bin/init" | sudo tee $(MOUNT_POINT)/etc/inittab > /dev/null
	@echo "==> Unmounting $(MOUNT_POINT)"
	sudo umount $(MOUNT_POINT)
	@echo "==> Disk image $(IMAGE) ready"


### Move (mount image and copy fresh executables) ###
.PHONY: move
move: img
	@echo "==> Mounting $(IMAGE)"
	sudo mount -o loop $(IMAGE) $(MOUNT_POINT)
	@echo "==> Copying updated executables"
	@for pkg in $(PACKAGES); do \
	  if [ -f "$(BUILD_DIR)/$$pkg" ]; then \
	    sudo cp -f "$(BUILD_DIR)/$$pkg" $(MOUNT_POINT)/bin/; \
	    sudo chmod +x $(MOUNT_POINT)/bin/$$pkg; \
	  fi \
	done
	@echo "==> Unmounting $(MOUNT_POINT)"
	sudo umount $(MOUNT_POINT)

### Run using the disk image ###
.PHONY: run
run: all move
	@echo "==> Running kernel with disk image rootfs"
	@if [ ! -f "$(OUT_KERNEL)" ]; then \
	  echo "ERROR: kernel $(OUT_KERNEL) missing"; exit 1; \
	fi; \
	if [ ! -f "$(IMAGE)" ]; then \
	  echo "ERROR: disk image $(IMAGE) missing"; exit 1; \
	fi; \
	CMD="$(QEMU) $(QEMU_OPTS) -kernel $(OUT_KERNEL) -drive file=$(IMAGE),if=virtio,format=raw -append \"console=ttyS0 console=tty1 root=/dev/ram rw\""; \
	echo "Running: $$CMD"; \
	eval $$CMD

### Clean ###
.PHONY: clean
clean:
	@echo "==> Cleaning kernel and packages"
	-$(MAKE) -C $(KERNEL_SRC) clean || true
	@for pkg in $(PACKAGES); do \
	  $(MAKE) -C $(SRC_DIR)/$$pkg clean || true; \
	  rm -f "$(BUILD_DIR)/$$pkg" || true; \
	done
	@echo "==> Removing other build artifacts"
	@rm -rf "$(BUILD_DIR)/initramfs-root" "$(BUILD_DIR)/initramfs.cpio.gz" "$(BUILD_DIR)/atlas.img" "$(BUILD_DIR)/bzImage" || true
	@sudo umount $(MOUNT_POINT) || true
	@rmdir $(MOUNT_POINT) || true
