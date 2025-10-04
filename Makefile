SRC_DIR     := 	src
BUILD_DIR   := 	build
ISO_DIR		:= 	iso

IMAGE       := 	atlas.img
ISO 		:= 	atlaslinux-x86_64.iso
IMAGE_SIZE  := 	64   # size in MB
MOUNT_POINT := 	mnt

SUBPROJECTS := 	$(shell find $(SRC_DIR) -type f -name Makefile | sort -r)

KERNEL_TREE := 	linux
KERNEL_IMAGE:= 	$(KERNEL_TREE)/bzImage
KERNEL_VER  := 	$(shell strings $(KERNEL_IMAGE) | grep "atlas" -m 1 | sed 's/ .*//')
MODULES     ?= 	e1000 \
				virtio_dma_buf virtio-gpu
DEST_ROOT   := 	$(SRC_DIR)/usr/lib/modules/$(KERNEL_VER)

QEMU_ARGS 	?= \
		-device ich9-intel-hda \
		-device hda-duplex \
		-netdev user,id=net0 \
		-device e1000,netdev=net0 \
		-device virtio-vga \
		-m 8096 \
		-drive if=pflash,format=raw,readonly=on,file=x64/OVMF_CODE.4m.fd \
		-drive if=pflash,format=raw,file=x64/OVMF_VARS.4m.fd

.PHONY: all img run clean install crun iso build crun-iso kernel modules

all: img

modules:
	@mkdir -p $(BUILD_DIR)
	@echo "Installing modules: $(MODULES)"
	@for m in $(MODULES); do \
		path=$$(find $(KERNEL_TREE) -type f -name "$$m.ko" | head -n1); \
		if [ -z "$$path" ]; then \
			echo "Error: module $$m.ko not found under $(KERNEL_TREE)" >&2; \
			exit 1; \
		fi; \
		rel=$${path#$(KERNEL_TREE)/}; \
		dest=$(DEST_ROOT)/$$rel; \
		sudo mkdir -p $$(dirname $$dest); \
		sudo cp -v $$path $$dest; \
	done
	@sudo cp $(KERNEL_TREE)/modules.builtin  $(DEST_ROOT)
	@sudo cp $(KERNEL_TREE)/modules.builtin.modinfo  $(DEST_ROOT)
	@sudo cp $(KERNEL_TREE)/modules.order  $(DEST_ROOT)
	@sudo depmod -b $(SRC_DIR)/usr $(KERNEL_VER)

kernel:
	@cd linux; \
	export INSTALL_MOD_PATH=$(abspath src/usr); \
	cp ../kernel.conf .config; \
	$(MAKE) -j$(shell nproc) olddefconfig; \
	$(MAKE) -j$(shell nproc) all;
	@cp $(KERNEL_TREE)/arch/x86/boot/bzImage $(KERNEL_IMAGE)

build: $(SUBPROJECTS)
	@set -e; \
	for mf in $(SUBPROJECTS); do \
		dir=$$(dirname $$mf); \
		echo "==> Building $$dir"; \
		$(MAKE) -C $$dir --no-print-directory -s; \
	done

install: build
	@for mf in $(SUBPROJECTS); do \
		dir=$$(dirname $$mf); \
		rel=$${dir#$(SRC_DIR)/}; \
		parent_dir=$$(dirname "$$rel"); \
		find "$$dir/build" \( -type f -executable -o -name "*.so*" \) | while read exec_path; do \
			file_name=$$(basename "$$exec_path"); \
			if [ "$$parent_dir" = "." ]; then \
				dest_path="$(BUILD_DIR)/$$file_name"; \
			else \
				dest_path="$(BUILD_DIR)/$$parent_dir/$$file_name"; \
			fi; \
			if [ ! -f "$$dest_path" ] || [ "$$exec_path" -nt "$$dest_path" ]; then \
				echo "==> Installing $$exec_path to $$dest_path"; \
				sudo mkdir -p "$$(dirname "$$dest_path")"; \
				sudo cp "$$exec_path" "$$dest_path"; \
			fi; \
		done; \
	done

	@echo "==> Copying plain files without Makefile"
	@find $(SRC_DIR) \
		-type d -name build -prune -o \
		-type d -exec test -f "{}/Makefile" \; -prune -o \
		-type f ! -name Makefile -print | while read f; do \
			rel=$${f#$(SRC_DIR)/}; \
			dest="$(BUILD_DIR)/$$rel"; \
			if [ ! -f "$$dest" ] || [ "$$f" -nt "$$dest" ]; then \
				echo "==> Installing $$f to $$dest"; \
				sudo mkdir -p "$$(dirname "$$dest")"; \
				sudo cp "$$f" "$$dest"; \
			fi; \
	done

iso: install
	@mkdir -p $(ISO_DIR)/boot/grub
	@sudo rm -f $(BUILD_DIR)/init
	@sudo ln $(BUILD_DIR)/sbin/init $(BUILD_DIR)/init
	@cd $(BUILD_DIR) && find . -print0 | cpio --null -ov --format=newc | gzip -9 > ../$(ISO_DIR)/boot/initramfs.cpio.gz
	@sudo cp $(KERNEL_IMAGE) $(ISO_DIR)/boot
	@cp grub.cfg $(ISO_DIR)/boot/grub
	@grub-mkrescue -o $(ISO) $(ISO_DIR)

img: install
	@sudo umount $(MOUNT_POINT) 2>/dev/null || true
	@echo "==> Rebuilding $(IMAGE) ($(IMAGE_SIZE)MB))"
	@rm -f $(IMAGE)
	@if [ ! -f $(IMAGE) ]; then \
		dd if=/dev/zero of=$(IMAGE) bs=1M count=$(IMAGE_SIZE) status=none; \
		mkfs.ext4 -F $(IMAGE); \
	fi
	@echo "==> Installing full rootfs into $(IMAGE)"
	@mkdir -p $(MOUNT_POINT)
	sudo mount -o loop $(IMAGE) $(MOUNT_POINT)

	sudo cp -r $(BUILD_DIR)/* $(MOUNT_POINT)

	sudo umount $(MOUNT_POINT)
	@echo "==> $(IMAGE) rebuilt."

run: img
	qemu-system-x86_64 \
		$(QEMU_ARGS) \
		-kernel $(KERNEL_IMAGE) \
		-append "root=/dev/vda rw console=tty1" \
		-drive file=$(IMAGE),if=virtio,format=raw \

run-iso: iso
	qemu-system-x86_64 \
		-cdrom $(ISO) \
		$(QEMU_ARGS)

crun: clean run
crun-iso: clean run-iso

clean:
	sudo rm -rf $(BUILD_DIR)
	sudo rm -rf $(ISO_DIR)
	sudo rm -f $(ISO)
	rm -rf $(IMAGE)
	@for mf in $(SUBPROJECTS); do \
		dir=$$(dirname $$mf); \
		$(MAKE) -C $$dir --no-print-directory -s clean || true; \
	done
