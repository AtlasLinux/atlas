SRC_DIR     := src
BUILD_DIR   := build
ISO_DIR		:= iso

IMAGE       := atlas.img
ISO 		:= atlas.iso
IMAGE_SIZE  := 64    # size in MB
MOUNT_POINT := mnt

SUBPROJECTS := $(shell find $(SRC_DIR) -type f -name Makefile)

.PHONY: all img run clean subprojects crun iso build

all: img

subprojects: $(SUBPROJECTS)
	@for mf in $(SUBPROJECTS); do \
		dir=$$(dirname $$mf); \
		echo "==> Building $$dir"; \
		$(MAKE) -C $$dir; \
	done

build: subprojects
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

iso: build
	@mkdir -p $(ISO_DIR)/boot/grub
	@sudo ln $(BUILD_DIR)/sbin/init $(BUILD_DIR)/init
	@cd $(BUILD_DIR) && find . -print0 | cpio --null -ov --format=newc | gzip -9 > ../$(ISO_DIR)/boot/initramfs.cpio.gz
	@sudo cp kernel/bzImage $(ISO_DIR)/boot
	@cp grub.cfg $(ISO_DIR)/boot/grub
	@grub-mkrescue -o $(ISO) $(ISO_DIR)

img: build
	@sudo umount $(MOUNT_POINT) 2>/dev/null || true
	@echo "==> Rebuilding $(IMAGE) ($(IMAGE_SIZE)MB))"
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
		-kernel kernel/bzImage \
		-append "root=/dev/vda rw console=tty1 vga=0x317" \
		-netdev user,id=net0 \
		-device e1000,netdev=net0 \
		-m 8096 \
		-drive file=$(IMAGE),if=virtio,format=raw

run-iso: iso
	qemu-system-x86_64 \
		-cdrom $(ISO) \
		-boot d \
 		-netdev user,id=net0 \
 		-device e1000,netdev=net0 \
		-m 8096

crun: clean run

clean:
	sudo rm -rf $(BUILD_DIR)
	sudo rm -rf $(ISO_DIR)
	sudo rm -f $(ISO)
	rm -rf $(IMAGE)
	@for mf in $(SUBPROJECTS); do \
		dir=$$(dirname $$mf); \
		$(MAKE) -C $$dir clean || true; \
	done