SRC_DIR     := src
BUILD_DIR   := build

IMAGE       := atlas.img
IMAGE_SIZE  := 64    # size in MB
MOUNT_POINT := mnt

# find all subdirectories of src that have a Makefile
SUBPROJECTS := $(shell find $(SRC_DIR) -type f -name Makefile)

# derive the built binary path for each subproject
BUILT_BINS := $(patsubst %/Makefile,%/build/%,$(SUBPROJECTS))

.PHONY: all img run clean subprojects

all: clean img

# run each subproject Makefile
subprojects: $(SUBPROJECTS)
	@for mf in $(SUBPROJECTS); do \
		dir=$$(dirname $$mf); \
		echo "==> Building $$dir"; \
		$(MAKE) -C $$dir; \
	done

# disk image build
img: subprojects
	@echo "==> Rebuilding $(IMAGE) ($(IMAGE_SIZE)MB))"
	@dd if=/dev/zero of=$(IMAGE) bs=1M count=$(IMAGE_SIZE) status=none
	@mkfs.ext4 -F $(IMAGE)
	@echo "==> Installing full rootfs into $(IMAGE)"
	@mkdir -p $(MOUNT_POINT)
	sudo mount -o loop $(IMAGE) $(MOUNT_POINT)

    # copy all executable files from each subproject
	@for mf in $(SUBPROJECTS); do \
		dir=$$(dirname $$mf); \
		rel=$${dir#$(SRC_DIR)/}; \
		parent_dir=$$(dirname "$$rel"); \
		find "$$dir/build" -type f -executable | while read exec_path; do \
			file_name=$$(basename "$$exec_path"); \
			if [ "$$parent_dir" = "." ]; then \
				dest_path="$(MOUNT_POINT)/$$file_name"; \
			else \
				dest_path="$(MOUNT_POINT)/$$parent_dir/$$file_name"; \
			fi; \
			echo "==> Installing $$exec_path to $$dest_path"; \
			sudo mkdir -p "$$(dirname "$$dest_path")"; \
			sudo cp "$$exec_path" "$$dest_path"; \
		done; \
	done

    # copy plain files, but skip directories that have a Makefile
	@echo "==> Copying plain files without Makefile"
	@find $(SRC_DIR) \
		-type d -name build -prune -o \
		-type d -exec test -f "{}/Makefile" \; -prune -o \
		-type f ! -name Makefile -print | while read f; do \
		rel=$${f#$(SRC_DIR)/}; \
		dest="$(MOUNT_POINT)/$$rel"; \
		echo "==> Installing $$f to $$dest"; \
		sudo mkdir -p "$$(dirname "$$dest")"; \
		sudo cp "$$f" "$$dest"; \
	done

	sudo umount $(MOUNT_POINT)
	@echo "==> $(IMAGE) rebuilt."

run: img
	qemu-system-x86_64 \
		-kernel kernel/bzImage \
		-append "root=/dev/vda rw console=tty1" \
		-netdev user,id=net0 \
		-device e1000,netdev=net0 \
		-drive file=$(IMAGE),if=virtio,format=raw

crun: clean run

clean:
	rm -rf $(BUILD_DIR)
	@for mf in $(SUBPROJECTS); do \
		dir=$$(dirname $$mf); \
		$(MAKE) -C $$dir clean || true; \
	done
