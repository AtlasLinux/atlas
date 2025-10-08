// argus_client.c
// Minimal wl_shm client: connect, bind wl_shm/wl_compositor, create shm buffer, draw, attach & commit.

#define _GNU_SOURCE
#include <wayland-client.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif
#ifndef MFD_ALLOW_SEALING
#define MFD_ALLOW_SEALING 0x0002U
#endif

static int
my_memfd_create(const char *name, unsigned int flags)
{
#ifdef SYS_memfd_create
    return syscall(SYS_memfd_create, name, flags);
#else
    (void)name; (void)flags;
    errno = ENOSYS;
    return -1;
#endif
}

struct client_state {
    struct wl_display *display;
    struct wl_shm *shm;
    struct wl_compositor *compositor;
    struct wl_surface *surface;
    struct wl_buffer *buffer;
    void *data;
    int fd;
    int width, height, stride;
};

static void registry_add(void *data, struct wl_registry *registry, uint32_t id,
                         const char *interface, uint32_t version)
{
    struct client_state *st = data;
    if (strcmp(interface, wl_shm_interface.name) == 0) {
        st->shm = wl_registry_bind(registry, id, &wl_shm_interface, 1);
    } else if (strcmp(interface, wl_compositor_interface.name) == 0) {
        st->compositor = wl_registry_bind(registry, id, &wl_compositor_interface, 1);
    }
}

static void registry_remove(void *data, struct wl_registry *registry, uint32_t id)
{ (void)data; (void)registry; (void)id; }

static const struct wl_registry_listener registry_listener = {
    .global = registry_add,
    .global_remove = registry_remove
};

int main(int argc, char **argv)
{
    (void) argc; (void) argv;
    struct client_state st = {0};
    st.width = 320;
    st.height = 200;
    st.stride = st.width * 4;

    st.display = wl_display_connect("argus"); // connect to socket "argus"
    if (!st.display) {
        fprintf(stderr, "Failed to connect to display 'argus'\n");
        return 1;
    }

    struct wl_registry *reg = wl_display_get_registry(st.display);
    wl_registry_add_listener(reg, &registry_listener, &st);
    wl_display_roundtrip(st.display); // get globals

    if (!st.shm || !st.compositor) {
        fprintf(stderr, "server did not advertise wl_shm or wl_compositor\n");
        return 1;
    }

    st.surface = wl_compositor_create_surface(st.compositor);

    // create anon shm fd
    int fd = my_memfd_create("argus-client", MFD_CLOEXEC);
    if (fd < 0) {
        // fallback to shm_open
        static char namebuf[64];
        snprintf(namebuf, sizeof(namebuf), "/argus-client-%d", (int)getpid());
        fd = shm_open(namebuf, O_CREAT | O_RDWR, 0600);
        if (fd < 0) {
            perror("shm_open");
            return 1;
        }
        shm_unlink(namebuf);
    }
    st.fd = fd;
    size_t size = st.stride * st.height;
    if (ftruncate(fd, size) < 0) {
        perror("ftruncate");
        close(fd);
        return 1;
    }
    void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return 1;
    }
    st.data = data;

    // Fill with a test pattern (gradient)
    for (int y = 0; y < st.height; ++y) {
        uint32_t *row = (uint32_t*)((uint8_t*)data + y * st.stride);
        for (int x = 0; x < st.width; ++x) {
            uint8_t r = (x * 255) / st.width;
            uint8_t g = (y * 255) / st.height;
            uint8_t b = 0x80;
            // XRGB8888 little-endian -> bytes: B G R X (actually X is high byte)
            row[x] = (r << 16) | (g << 8) | (b << 0);
        }
    }

    struct wl_shm_pool *pool = wl_shm_create_pool(st.shm, fd, size);
    st.buffer = wl_shm_pool_create_buffer(pool, 0, st.width, st.height, st.stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);

    wl_surface_attach(st.surface, st.buffer, 0, 0);
    wl_surface_commit(st.surface);
    wl_display_flush(st.display);

    // Wait for a short while so server has time to process.
    // In a real client you'd run the event loop and react.
    sleep(1);

    // cleanup
    wl_buffer_destroy(st.buffer);
    wl_surface_destroy(st.surface);
    if (st.data) munmap(st.data, size);
    if (st.fd >= 0) close(st.fd);
    wl_display_disconnect(st.display);
    return 0;
}
