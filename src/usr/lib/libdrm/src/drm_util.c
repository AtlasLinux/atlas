/* drm_util.c */
#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "drm_util.h"
#include "drm/drm.h"
#include "drm/drm_mode.h"

static int xioctl(int fd, unsigned long req, void *arg) {
    int r;
    do r = ioctl(fd, req, arg);
    while (r == -1 && errno == EINTR);
    return r;
}

int drm_open(const char *path) {
    return open(path, O_RDWR | O_CLOEXEC);
}

int drm_get_resources(int fd, struct drm_mode_card_res *res) {
    memset(res, 0, sizeof(*res));
    return xioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, res);
}

int drm_get_connector(int fd, uint32_t conn_id, struct drm_mode_get_connector *conn) {
    memset(conn, 0, sizeof(*conn));
    conn->connector_id = conn_id;
    return xioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, conn);
}

int drm_get_encoder(int fd, uint32_t enc_id, struct drm_mode_get_encoder *enc) {
    memset(enc, 0, sizeof(*enc));
    enc->encoder_id = enc_id;
    return xioctl(fd, DRM_IOCTL_MODE_GETENCODER, enc);
}

int drm_get_crtc(int fd, uint32_t crtc_id, struct drm_mode_crtc *crtc) {
    memset(crtc, 0, sizeof(*crtc));
    crtc->crtc_id = crtc_id;
    return xioctl(fd, DRM_IOCTL_MODE_GETCRTC, crtc);
}

int drm_create_dumb(int fd, uint32_t w, uint32_t h, uint32_t bpp,
                    struct drm_mode_create_dumb *out) {
    memset(out, 0, sizeof(*out));
    out->width = w;
    out->height = h;
    out->bpp = bpp;
    return xioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, out);
}

int drm_destroy_dumb(int fd, uint32_t handle) {
    struct drm_mode_destroy_dumb dreq = { .handle = handle };
    return xioctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
}

int drm_map_dumb(int fd, uint32_t handle, off_t *offset) {
    struct drm_mode_map_dumb mreq = { .handle = handle };
    int ret = xioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq);
    if (ret == 0 && offset) *offset = mreq.offset;
    return ret;
}

int drm_add_fb(int fd, uint32_t w, uint32_t h, uint32_t pitch, uint32_t bpp,
               uint32_t depth, uint32_t handle, uint32_t *fb_id) {
    struct drm_mode_fb_cmd fbc = {0};
    fbc.width  = w;
    fbc.height = h;
    fbc.pitch  = pitch;
    fbc.bpp    = bpp;
    fbc.depth  = depth;
    fbc.handle = handle;
    int ret = xioctl(fd, DRM_IOCTL_MODE_ADDFB, &fbc);
    if (ret == 0 && fb_id) *fb_id = fbc.fb_id;
    return ret;
}

int drm_set_crtc(int fd, uint32_t crtc_id, uint32_t fb_id, int x, int y,
                 const uint32_t *connectors, int count,
                 const struct drm_mode_modeinfo *mode) {
    struct drm_mode_crtc s = {0};
    s.crtc_id = crtc_id;
    s.fb_id = fb_id;
    s.x = x;
    s.y = y;
    s.set_connectors_ptr = (uintptr_t)connectors;
    s.count_connectors = count;
    if (mode) s.mode = *mode;
    s.mode_valid = (mode != NULL);
    return xioctl(fd, DRM_IOCTL_MODE_SETCRTC, &s);
}
