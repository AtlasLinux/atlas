/* drm_util.h */
#ifndef DRM_UTIL_H
#define DRM_UTIL_H

#include <stdint.h>
#include <sys/types.h>
#include "drm/drm.h"

int drm_open(const char *path);
int drm_get_resources(int fd, struct drm_mode_card_res *res);
int drm_get_connector(int fd, uint32_t conn_id, struct drm_mode_get_connector *conn);
int drm_get_encoder(int fd, uint32_t enc_id, struct drm_mode_get_encoder *enc);
int drm_get_crtc(int fd, uint32_t crtc_id, struct drm_mode_crtc *crtc);

int drm_create_dumb(int fd, uint32_t w, uint32_t h, uint32_t bpp,
                    struct drm_mode_create_dumb *out);
int drm_destroy_dumb(int fd, uint32_t handle);
int drm_map_dumb(int fd, uint32_t handle, off_t *offset);
int drm_add_fb(int fd, uint32_t w, uint32_t h, uint32_t pitch, uint32_t bpp,
               uint32_t depth, uint32_t handle, uint32_t *fb_id);
int drm_set_crtc(int fd, uint32_t crtc_id, uint32_t fb_id, int x, int y,
                 const uint32_t *connectors, int count,
                 const struct drm_mode_modeinfo *mode);

#endif
