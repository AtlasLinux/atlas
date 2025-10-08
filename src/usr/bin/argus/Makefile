CC = gcc
CFLAGS = -O2 -g -Wall -Wextra
LIBS_SERVER = -lwayland-server
LIBS_CLIENT = -lwayland-client

all: server client vk_probe

VK_PROBE_OBJS = src/vk_probe.c
vk_probe: $(VK_PROBE_OBJS)
	$(CC) -O2 -g -o build/$@ $^ -lvulkan

server: src/server.c
	@mkdir -p build
	$(CC) $(CFLAGS) -o build/$@ $^ $(LIBS_SERVER)

client: src/client.c
	@mkdir -p build
	$(CC) $(CFLAGS) -o build/$@ $^ $(LIBS_CLIENT)

clean:
	rm -f server client /tmp/argus_commit_*.ppm
