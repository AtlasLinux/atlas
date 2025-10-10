/* server.c -- Argus minimal compositor with CPU blit compositor (fixed)
 *
 * - Accepts wl_shm buffers from clients
 * - Maintains surface list, supports multiple clients
 * - On commit: moves pending buffer -> current, composites all current buffers
 *   into /tmp/argus_output.ppm, sends wl_buffer.release and wl_callback.done
 *
 * Build: link with -lwayland-server
 */

#define _GNU_SOURCE
#include <wayland-server.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <limits.h>

/* Output framebuffer size (changeable) */
#define OUT_W 1024
#define OUT_H 768

/* Simple singly-linked surface list; head = bottom, tail = top */
struct surface;

static struct surface *surface_head = NULL;

struct shm_pool {
    int fd;
    size_t size;
    struct wl_resource *resource; // wl_shm_pool resource
};

struct shm_buffer {
    struct shm_pool *pool;
    int32_t offset;
    int32_t width, height, stride;
    uint32_t format;
    void *data;   // mmap pointer (pool->fd + offset)
    size_t data_size;
    /* note: do not free/unmap until buffer resource destructor runs */
};

struct surface {
    struct wl_resource *resource; /* wl_surface resource */
    struct wl_resource *pending_buffer;
    int32_t pending_x, pending_y;

    struct wl_resource *current_buffer;

    /* single pending frame callback resource (created by client) */
    struct wl_resource *frame_callback;

    /* assigned position of current_buffer */
    int32_t x, y;

    struct surface *next;
};

/* -------- helpers -------- */

static uint32_t
now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t ms = (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    return (uint32_t)ms;
}

/* Write PPM P6 */
static void
write_output_ppm(const uint8_t *rgb, int w, int h)
{
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "/tmp/argus_output.ppm");
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror("fopen output ppm");
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    size_t wrote = fwrite(rgb, 1, (size_t)w * h * 3, f);
    if (wrote != (size_t)w * h * 3) {
        fprintf(stderr, "partial write %zu\n", wrote);
    }
    fclose(f);
    fprintf(stderr, "Wrote %s\n", path);
}

/* Blit single XRGB8888 shm_buffer into rgb24 dest at (dx,dy).
 * No clipping beyond dest bounds is done conservatively.
 */
static void
blit_shm_to_rgb(const struct shm_buffer *buf, uint8_t *dst_rgb, int dst_w, int dst_h, int dx, int dy)
{
    if (!buf || !buf->data) return;
    for (int y = 0; y < buf->height; ++y) {
        int ty = dy + y;
        if (ty < 0 || ty >= dst_h) continue;
        uint8_t *src_row = (uint8_t*)buf->data + (size_t)y * buf->stride;
        uint8_t *dst_row = dst_rgb + (size_t)ty * dst_w * 3;
        for (int x = 0; x < buf->width; ++x) {
            int tx = dx + x;
            if (tx < 0 || tx >= dst_w) continue;
            /* src XRGB8888 little-endian layout: B G R X */
            uint8_t b = src_row[x*4 + 0];
            uint8_t g = src_row[x*4 + 1];
            uint8_t r = src_row[x*4 + 2];
            uint8_t *p = dst_row + tx * 3;
            p[0] = r;
            p[1] = g;
            p[2] = b;
        }
    }
}

/* Composite all surfaces in list order into an RGB buffer and publish.
 * After compositing: send wl_buffer.release for each used current_buffer,
 * send wl_callback.done for each registered frame callback.
 */
static void
composite_and_present(void)
{
    /* allocate RGB buffer */
    size_t out_size = (size_t)OUT_W * OUT_H * 3;
    uint8_t *out = malloc(out_size);
    if (!out) {
        fprintf(stderr, "no mem for out buffer\n");
        return;
    }
    /* clear to mid-gray */
    memset(out, 0x80, out_size);

    /* draw surfaces from head -> tail (head bottom) */
    struct surface *s = surface_head;
    while (s) {
        if (s->current_buffer) {
            struct shm_buffer *buf = wl_resource_get_user_data(s->current_buffer);
            if (buf) {
                blit_shm_to_rgb(buf, out, OUT_W, OUT_H, s->x, s->y);
            }
        }
        s = s->next;
    }

    /* write file (overwrite each time) */
    write_output_ppm(out, OUT_W, OUT_H);

    /* send release for each used buffer and trigger frame callbacks */
    uint32_t tm = now_ms();
    s = surface_head;
    while (s) {
        if (s->current_buffer) {
            wl_buffer_send_release(s->current_buffer);
        }
        if (s->frame_callback) {
            wl_callback_send_done(s->frame_callback, tm);
            wl_resource_destroy(s->frame_callback);
            s->frame_callback = NULL;
        }
        s = s->next;
    }

    free(out);
}

/* Move surface to tail (top) of list */
static void
surface_bring_to_top(struct surface *surf)
{
    if (!surf || !surface_head) return;
    if (surface_head == surf && surf->next == NULL) return;

    /* if surf is head */
    if (surface_head == surf) {
        surface_head = surf->next;
        struct surface *it = surface_head;
        while (it->next) it = it->next;
        it->next = surf;
        surf->next = NULL;
        return;
    }

    /* find predecessor */
    struct surface *prev = surface_head;
    struct surface *it = surface_head->next;
    while (it && it != surf) {
        prev = it;
        it = it->next;
    }
    if (!it) return; /* not in list */
    prev->next = it->next;

    /* append at tail */
    struct surface *tail = surface_head;
    while (tail->next) tail = tail->next;
    tail->next = it;
    it->next = NULL;
}

/* -------- wl_shm_pool / wl_buffer handling ---------- */

static void
pool_destroy_resource(struct wl_resource *resource)
{
    struct shm_pool *pool = wl_resource_get_user_data(resource);
    if (!pool) return;
    if (pool->fd >= 0) close(pool->fd);
    free(pool);
}

static void
buffer_destroy_resource(struct wl_resource *resource)
{
    struct shm_buffer *buf = wl_resource_get_user_data(resource);
    if (!buf) return;
    if (buf->data) {
        off_t page_offset = buf->offset & ~(sysconf(_SC_PAGE_SIZE) - 1);
        size_t map_size = (size_t)(buf->data_size + (buf->offset - page_offset));
        void *map_start = (uint8_t*)buf->data - (buf->offset - page_offset);
        munmap(map_start, map_size);
    }
    free(buf);
}

static void
buffer_destroy_request(struct wl_client *client, struct wl_resource *resource)
{
    (void) client;
    wl_resource_destroy(resource);
}

static void
pool_create_buffer(struct wl_client *client,
                   struct wl_resource *pool_res,
                   uint32_t id,
                   int32_t offset,
                   int32_t width,
                   int32_t height,
                   int32_t stride,
                   uint32_t format)
{
    (void) client;
    struct shm_pool *pool = wl_resource_get_user_data(pool_res);
    if (!pool) {
        wl_resource_post_error(pool_res, WL_DISPLAY_ERROR_INVALID_OBJECT, "pool userdata missing");
        return;
    }

    if (width <= 0 || height <= 0 || stride <= 0) {
        wl_resource_post_error(pool_res, WL_DISPLAY_ERROR_INVALID_OBJECT, "bad buffer size");
        return;
    }

    uint64_t needed = (uint64_t)offset + (uint64_t)stride * (uint64_t)height;
    if (needed > pool->size) {
        wl_resource_post_error(pool_res, WL_DISPLAY_ERROR_INVALID_OBJECT, "buffer out of pool bounds");
        return;
    }

    struct shm_buffer *buf = calloc(1, sizeof(*buf));
    if (!buf) {
        wl_resource_post_no_memory(pool_res);
        return;
    }

    buf->pool = pool;
    buf->offset = offset;
    buf->width = width;
    buf->height = height;
    buf->stride = stride;
    buf->format = format;
    buf->data_size = (size_t)stride * (size_t)height;

    off_t page_offset = offset & ~(sysconf(_SC_PAGE_SIZE) - 1);
    off_t diff = offset - page_offset;
    void *map = mmap(NULL, diff + buf->data_size, PROT_READ, MAP_SHARED, pool->fd, page_offset);
    if (map == MAP_FAILED) {
        perror("mmap buffer");
        free(buf);
        wl_resource_post_error(pool_res, WL_DISPLAY_ERROR_INVALID_OBJECT, "mmap failed");
        return;
    }
    buf->data = (uint8_t*)map + diff;

    struct wl_resource *b_res = wl_resource_create(wl_resource_get_client(pool_res), &wl_buffer_interface, wl_resource_get_version(pool_res), id);
    if (!b_res) {
        munmap(map, diff + buf->data_size);
        free(buf);
        wl_resource_post_no_memory(pool_res);
        return;
    }
    /* set implementation so destroy request works and destructor unmaps */
    static const struct wl_buffer_interface buffer_impl = {
        .destroy = buffer_destroy_request
    };
    wl_resource_set_implementation(b_res, &buffer_impl, buf, buffer_destroy_resource);

    fprintf(stderr, "Created shm buffer id=%u size=%dx%d stride=%d format=%u\n", id, width, height, stride, format);
}

static void
pool_destroy_request(struct wl_client *client, struct wl_resource *resource)
{
    (void) client;
    wl_resource_destroy(resource);
}

static void
shm_create_pool(struct wl_client *client, struct wl_resource *shm_res, uint32_t id, int fd, int32_t size)
{
    (void) shm_res;
    struct shm_pool *pool = calloc(1, sizeof(*pool));
    if (!pool) {
        close(fd);
        wl_client_post_no_memory(client);
        return;
    }
    pool->fd = fd;
    pool->size = size;

    struct wl_resource *pool_res = wl_resource_create(client, &wl_shm_pool_interface, wl_resource_get_version(shm_res), id);
    if (!pool_res) {
        close(fd);
        free(pool);
        wl_client_post_no_memory(client);
        return;
    }
    pool->resource = pool_res;
    static const struct wl_shm_pool_interface pool_impl_local = {
        .create_buffer = pool_create_buffer,
        .destroy = pool_destroy_request
    };
    wl_resource_set_implementation(pool_res, &pool_impl_local, pool, pool_destroy_resource);
    fprintf(stderr, "shm_pool created fd=%d size=%d (id=%u)\n", fd, size, id);
}

static const struct wl_shm_interface shm_impl = {
    .create_pool = shm_create_pool
};

static void
shm_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
    (void) data; (void) version;
    struct wl_resource *res = wl_resource_create(client, &wl_shm_interface, 1, id);
    wl_resource_set_implementation(res, &shm_impl, NULL, NULL);
    fprintf(stderr, "Client bound wl_shm\n");
}

/* -------- wl_compositor + wl_surface minimal handling ---------- */

static void
surface_attach(struct wl_client *client, struct wl_resource *surface_res,
               struct wl_resource *buffer_res, int32_t x, int32_t y)
{
    (void) client;
    struct surface *s = wl_resource_get_user_data(surface_res);
    if (!s) return;
    s->pending_buffer = buffer_res;
    s->pending_x = x;
    s->pending_y = y;
    fprintf(stderr, "surface attach (buffer_res=%p) at %d,%d\n", (void*)buffer_res, x, y);
}

static void
surface_damage(struct wl_client *client, struct wl_resource *surface_res, int32_t x, int32_t y, int32_t w, int32_t h)
{
    (void) client; (void) surface_res; (void) x; (void) y; (void) w; (void) h;
    /* ignore damage for this simple compositor */
}

static void
surface_frame_request(struct wl_client *client, struct wl_resource *surface_res, uint32_t callback_id)
{
    struct surface *s = wl_resource_get_user_data(surface_res);
    if (!s) return;
    /* create a wl_callback resource to satisfy client; we'll send done after present */
    struct wl_resource *cb = wl_resource_create(client, &wl_callback_interface, wl_resource_get_version(surface_res), callback_id);
    if (!cb) {
        wl_client_post_no_memory(client);
        return;
    }
    /* no per-callback implementation necessary; we will destroy it after sending done */
    s->frame_callback = cb;
    fprintf(stderr, "surface frame callback registered (id=%u)\n", callback_id);
}

static void
surface_commit(struct wl_client *client, struct wl_resource *surface_res)
{
    (void) client;
    struct surface *s = wl_resource_get_user_data(surface_res);
    if (!s) return;

    /* move pending buffer to current */
    if (s->pending_buffer) {
        s->current_buffer = s->pending_buffer;
        s->x = s->pending_x;
        s->y = s->pending_y;
        s->pending_buffer = NULL;
    } else {
        /* no buffer attached on commit */
        fprintf(stderr, "commit with no pending buffer\n");
    }

    /* bring surface to top */
    surface_bring_to_top(s);

    /* composite all surfaces and present */
    composite_and_present();
}

static void
surface_destroy_resource(struct wl_resource *resource)
{
    struct surface *s = wl_resource_get_user_data(resource);
    if (!s) return;
    /* remove from list */
    if (surface_head == s) {
        surface_head = s->next;
    } else {
        struct surface *it = surface_head;
        while (it && it->next != s) it = it->next;
        if (it && it->next == s) it->next = s->next;
    }
    /* if there's a frame callback outstanding, destroy it */
    if (s->frame_callback) {
        wl_resource_destroy(s->frame_callback);
        s->frame_callback = NULL;
    }
    free(s);
}

static const struct wl_surface_interface surface_impl = {
    .attach = surface_attach,
    .damage = surface_damage,
    .frame = surface_frame_request,
    .commit = surface_commit
};

static void
compositor_create_surface(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
    (void) resource;
    struct wl_resource *surf = wl_resource_create(client, &wl_surface_interface, wl_resource_get_version(resource), id);
    if (!surf) {
        wl_client_post_no_memory(client);
        return;
    }
    struct surface *s = calloc(1, sizeof(*s));
    if (!s) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(surf, &surface_impl, s, surface_destroy_resource);

    /* append to list tail (new surfaces are topmost) */
    if (!surface_head) {
        surface_head = s;
        s->next = NULL;
    } else {
        struct surface *it = surface_head;
        while (it->next) it = it->next;
        it->next = s;
        s->next = NULL;
    }

    fprintf(stderr, "Created surface id=%u\n", id);
}

static const struct wl_compositor_interface compositor_impl = {
    .create_surface = compositor_create_surface,
    .create_region = NULL
};

static void
compositor_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
    (void) data; (void) version;
    struct wl_resource *res = wl_resource_create(client, &wl_compositor_interface, 1, id);
    wl_resource_set_implementation(res, &compositor_impl, NULL, NULL);
    fprintf(stderr, "Client bound wl_compositor\n");
}

/* -------- server bootstrap ---------- */

int main(int argc, char **argv)
{
    (void) argc; (void) argv;
    struct wl_display *display = wl_display_create();
    if (!display) {
        fprintf(stderr, "wl_display_create failed\n");
        return 1;
    }

    if (wl_display_add_socket(display, "argus") != 0) {
        fprintf(stderr, "wl_display_add_socket failed\n");
        wl_display_destroy(display);
        return 1;
    }
    setenv("WAYLAND_DISPLAY", "argus", 1);

    wl_global_create(display, &wl_compositor_interface, 1, NULL, compositor_bind);
    wl_global_create(display, &wl_shm_interface, 1, NULL, shm_bind);

    fprintf(stderr, "Argus server started on WAYLAND_DISPLAY=argus\n");

    wl_display_run(display);

    wl_display_destroy(display);
    return 0;
}
