CC         := gcc
CFLAGS     := -std=c99 -Wall -Wextra -O2 -static -D_POSIX_C_SOURCE=200112L

SRC_DIR     := src
BUILD_DIR   := build

IMAGE       := atlas.img
IMAGE_SIZE  := 64    # size in MB
MOUNT_POINT := mnt
ROOTFS      := $(BUILD_DIR)

# find all subdirectories of src that have a Makefile
SUBPROJECTS := $(shell find $(SRC_DIR) -type f -name Makefile)

# derive the built binary path for each subproject
BUILT_BINS := $(patsubst %/Makefile,%/build/%,$(SUBPROJECTS))

.PHONY: all img move run clean subprojects

all: clean img

# run each subproject Makefile
subprojects: $(SUBPROJECTS)
	@for mf in $(SUBPROJECTS); do \
		dir=$$(dirname $$mf); \
		echo "==> Building $$dir"; \
		$(MAKE) -C $$dir CC="$(CC)" CFLAGS="$(CFLAGS)"; \
	done

# disk image build
img: subprojects
	@echo "==> Rebuilding $(IMAGE) ($(IMAGE_SIZE)MB))"
	@dd if=/dev/zero of=$(IMAGE) bs=1M count=$(IMAGE_SIZE) status=none
	@mkfs.ext4 -F $(IMAGE)
	@echo "==> Installing full rootfs into $(IMAGE)"
	sudo mount -o loop $(IMAGE) $(MOUNT_POINT)
	@mkdir -p $(MOUNT_POINT) $(ROOTFS)
	sudo cp -a $(ROOTFS)/* $(MOUNT_POINT)/

	# populate minimal /dev
	sudo mkdir -p $(MOUNT_POINT)/dev
	sudo mknod -m 600 $(MOUNT_POINT)/dev/console c 5 1
	sudo mknod -m 666 $(MOUNT_POINT)/dev/tty c 5 0
	sudo mknod -m 620 $(MOUNT_POINT)/dev/tty1 c 4 1

	# copy built executables from each subproject
	@for mf in $(SUBPROJECTS); do \
		dir=$$(dirname $$mf); \
		name=$$(basename $$dir); \
		exec_path="$$dir/build/$$name"; \
		dest_dir=$$(dirname $${dir#$(SRC_DIR)/}); \
		echo "==> Installing $$exec_path to $(MOUNT_POINT)/$$dest_dir/$$name"; \
		sudo mkdir -p "$(MOUNT_POINT)/$$dest_dir"; \
		sudo cp "$$exec_path" "$(MOUNT_POINT)/$$dest_dir/$$name"; \
	done

	sudo umount $(MOUNT_POINT)
	@echo "==> $(IMAGE) rebuilt."

move: subprojects
	@echo "==> Mounting $(IMAGE)"
	sudo mount -o loop $(IMAGE) $(MOUNT_POINT)
	@echo "==> Copying updated tools"
	@for mf in $(SUBPROJECTS); do \
		dir=$$(dirname $$mf); \
		name=$$(basename $$dir); \
		exec_path="$$dir/build/$$name"; \
		dest_dir=$$(dirname $${dir#$(SRC_DIR)/}); \
		sudo mkdir -p "$(MOUNT_POINT)/$$dest_dir"; \
		sudo cp "$$exec_path" "$(MOUNT_POINT)/$$dest_dir/$$name"; \
	done
	sudo umount $(MOUNT_POINT)
	@echo "==> Binaries updated."

run: move
	qemu-system-x86_64 \
		-kernel kernel/bzImage \
		-append "root=/dev/vda rw console=tty1" \
		-drive file=$(IMAGE),if=virtio,format=raw

clean:
	rm -rf $(BUILD_DIR)
	@for mf in $(SUBPROJECTS); do \
		dir=$$(dirname $$mf); \
		$(MAKE) -C $$dir clean || true; \
	done
