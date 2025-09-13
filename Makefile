CC         := gcc
CFLAGS     := -std=c99 -Wall -Wextra -O2 -static -D_POSIX_C_SOURCE=200112L

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

    # populate minimal /dev
	sudo mkdir -p $(MOUNT_POINT)/dev
	sudo mknod -m 600 $(MOUNT_POINT)/dev/console c 5 1
	sudo mknod -m 666 $(MOUNT_POINT)/dev/tty c 5 0
	sudo mknod -m 620 $(MOUNT_POINT)/dev/tty1 c 4 1

    # copy all executable files from each subproject
	@for mf in $(SUBPROJECTS); do \
			dir=$$(dirname $$mf); \
			# remove src/ prefix \
			rel=$${dir#$(SRC_DIR)/}; \
			path=$$(dirname $$rel); \
			# find all executables in build/ \
			find "$$dir/build" -type f -executable | while read exec_path; do \
				# strip build/ prefix to get relative path \
				rel_exec=$${exec_path#$$dir/build/}; \
				if [ "$$path" = "." ]; then \
					dest_path="$(MOUNT_POINT)/$$rel_exec"; \
				else \
					dest_path="$(MOUNT_POINT)/$$path/$$rel_exec"; \
				fi; \
				echo "==> Installing $$exec_path to $$dest_path"; \
				sudo mkdir -p $$(dirname "$$dest_path"); \
				sudo cp "$$exec_path" "$$dest_path"; \
			done; \
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
