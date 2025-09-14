SRC_DIR     := src
BUILD_DIR   := build

IMAGE       := atlas.img
IMAGE_SIZE  := 64    # size in MB
MOUNT_POINT := mnt

SUBPROJECTS := $(shell find $(SRC_DIR) -type f -name Makefile)

.PHONY: all img run clean subprojects

all: img

subprojects: $(SUBPROJECTS)
	@for mf in $(SUBPROJECTS); do \
		dir=$$(dirname $$mf); \
		echo "==> Building $$dir"; \
		$(MAKE) -C $$dir; \
	done

img: subprojects
	@sudo umount $(MOUNT_POINT) 2>/dev/null || true
	@echo "==> Rebuilding $(IMAGE) ($(IMAGE_SIZE)MB))"
	@if [ ! -f $(IMAGE) ]; then \
		dd if=/dev/zero of=$(IMAGE) bs=1M count=$(IMAGE_SIZE) status=none; \
		mkfs.ext4 -F $(IMAGE); \
	fi
	@echo "==> Installing full rootfs into $(IMAGE)"
	@mkdir -p $(MOUNT_POINT)
	sudo mount -o loop $(IMAGE) $(MOUNT_POINT)

	@for mf in $(SUBPROJECTS); do \
		dir=$$(dirname $$mf); \
		rel=$${dir#$(SRC_DIR)/}; \
		parent_dir=$$(dirname "$$rel"); \
		find "$$dir/build" \( -type f -executable -o -name "*.so*" \) | while read exec_path; do \
			file_name=$$(basename "$$exec_path"); \
			if [ "$$parent_dir" = "." ]; then \
				dest_path="$(MOUNT_POINT)/$$file_name"; \
			else \
				dest_path="$(MOUNT_POINT)/$$parent_dir/$$file_name"; \
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
			dest="$(MOUNT_POINT)/$$rel"; \
			if [ ! -f "$$dest" ] || [ "$$f" -nt "$$dest" ]; then \
				echo "==> Installing $$f to $$dest"; \
				sudo mkdir -p "$$(dirname "$$dest")"; \
				sudo cp "$$f" "$$dest"; \
			fi; \
	done

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

crun: clean run

clean:
	rm -rf $(BUILD_DIR)
	rm -rf $(IMAGE)
	@for mf in $(SUBPROJECTS); do \
		dir=$$(dirname $$mf); \
		$(MAKE) -C $$dir clean || true; \
	done
