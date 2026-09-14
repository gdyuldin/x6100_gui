/**
 * @file drm.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "drm.h"
#if USE_DRM

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <arm_neon.h>

#include <drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

// Queue size
#define MAX_DIRTY_RECTS 32
#define MAX_DIRTY_BUF 800 * 480 * 2

#define DBG_TAG "drm"

#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))

#define print(msg, ...) fprintf(stderr, msg, ##__VA_ARGS__);
#define err(msg, ...) print("error: " msg "\n", ##__VA_ARGS__)
#define info(msg, ...) print(msg "\n", ##__VA_ARGS__)
#define dbg(msg, ...)                                                                                                  \
    {                                                                                                                  \
    }
// #define dbg(msg, ...) print(DBG_TAG ": " msg "\n", ##__VA_ARGS__)

enum {
    PLANE_PRIMARY_ID,
    PLANE_OVERLAY_ID,
    PLANE_LAST,
};

typedef struct {
    lv_area_t  rects[MAX_DIRTY_RECTS];
    uint32_t   rects_cnt;
    lv_color_t buf[MAX_DIRTY_BUF];
    uint32_t   buf_size;
} damaged_areas_t;

static lv_disp_draw_buf_t disp_buf[PLANE_LAST];
static volatile bool      flip_pending[PLANE_LAST];

static damaged_areas_t damaged_areas[PLANE_LAST][2];
static uint8_t         damaged_cur_i[PLANE_LAST];

struct drm_buffer {
    uint32_t          handle;
    uint32_t          pitch;
    uint32_t          offset;
    unsigned long int size;
    void             *map;
    uint32_t          fb_handle;
    uint32_t          fourcc;
};

struct drm_dev {
    int                fd;
    uint32_t           conn_id, enc_id, crtc_id, crtc_idx;
    uint32_t           width, height;
    uint32_t           mmWidth, mmHeight;
    drmModeModeInfo    mode;
    uint32_t           blob_id;
    drmEventContext    drm_event_ctx;
    drmModeCrtc       *saved_crtc;
    drmModeCrtc       *crtc;
    drmModeConnector  *conn;
    uint32_t           count_plane_props;
    uint32_t           count_crtc_props;
    uint32_t           count_conn_props;
    drmModePropertyPtr plane_props[128];
    drmModePropertyPtr crtc_props[128];
    drmModePropertyPtr conn_props[128];

    // primary plane
    uint32_t           primary_plane_id;
    drmModePlane      *primary_plane;
    struct drm_buffer  primary_bufs[2];
    struct drm_buffer *primary_cur_bufs[2];
    drmModePropertyPtr primary_plane_props[128];
    uint32_t           count_primary_plane_props;

    // overlay plane(LVGL)
    uint32_t           overlay_plane_id;
    drmModePlane      *overlay_plane;
    struct drm_buffer  overlay_bufs[2];
    struct drm_buffer *overlay_cur_bufs[2];
    drmModePropertyPtr overlay_plane_props[128];
    uint32_t           count_overlay_plane_props;
} drm_dev;

static uint32_t get_plane_property_id(const char *name) {
    uint32_t i;

    dbg("Find plane property: %s", name);

    for (i = 0; i < drm_dev.count_plane_props; ++i)
        if (!strcmp(drm_dev.plane_props[i]->name, name))
            return drm_dev.plane_props[i]->prop_id;

    dbg("Unknown plane property: %s", name);

    return 0;
}

static uint32_t get_property_id_from_list(drmModePropertyPtr *props, uint32_t count, const char *name) {
    if (!props || !name)
        return 0;

    for (uint32_t i = 0; i < count; i++) {
        if (props[i] && strcmp(props[i]->name, name) == 0)
            return props[i]->prop_id;
    }
    return 0;
}

static uint32_t get_crtc_property_id(const char *name) {
    uint32_t i;

    dbg("Find crtc property: %s", name);

    for (i = 0; i < drm_dev.count_crtc_props; ++i)
        if (!strcmp(drm_dev.crtc_props[i]->name, name))
            return drm_dev.crtc_props[i]->prop_id;

    dbg("Unknown crtc property: %s", name);

    return 0;
}

static uint32_t get_conn_property_id(const char *name) {
    uint32_t i;

    dbg("Find conn property: %s", name);

    for (i = 0; i < drm_dev.count_conn_props; ++i)
        if (!strcmp(drm_dev.conn_props[i]->name, name))
            return drm_dev.conn_props[i]->prop_id;

    dbg("Unknown conn property: %s", name);

    return 0;
}

static void page_flip_handler(int fd, unsigned int sequence, unsigned int tv_sec, unsigned int tv_usec,
                              void *user_data) {
    (void)fd;
    (void)sequence;
    (void)tv_sec;
    (void)tv_usec;
    (void)user_data;
    for (int i = 0; i < PLANE_LAST; i++)
        flip_pending[i] = 0;
}

static int drm_get_plane_props(uint32_t plane_id, drmModePropertyPtr *plane_props, uint32_t *count_plane_props) {
    uint32_t i;

    drmModeObjectPropertiesPtr props = drmModeObjectGetProperties(drm_dev.fd, plane_id, DRM_MODE_OBJECT_PLANE);
    if (!props) {
        err("drmModeObjectGetProperties failed");
        return -1;
    }
    dbg("Found %u plane props", props->count_props);
    *count_plane_props = props->count_props;
    for (i = 0; i < props->count_props; i++) {
        plane_props[i] = drmModeGetProperty(drm_dev.fd, props->props[i]);
        dbg("Added plane prop %u:%s", plane_props[i]->prop_id, plane_props[i]->name);
    }
    drmModeFreeObjectProperties(props);

    return 0;
}

static int drm_get_crtc_props(void) {
    uint32_t i;

    drmModeObjectPropertiesPtr props = drmModeObjectGetProperties(drm_dev.fd, drm_dev.crtc_id, DRM_MODE_OBJECT_CRTC);
    if (!props) {
        err("drmModeObjectGetProperties failed");
        return -1;
    }
    dbg("Found %u crtc props", props->count_props);
    drm_dev.count_crtc_props = props->count_props;
    for (i = 0; i < props->count_props; i++) {
        drm_dev.crtc_props[i] = drmModeGetProperty(drm_dev.fd, props->props[i]);
        dbg("Added crtc prop %u:%s", drm_dev.crtc_props[i]->prop_id, drm_dev.crtc_props[i]->name);
    }
    drmModeFreeObjectProperties(props);

    return 0;
}

static int drm_get_conn_props(void) {
    uint32_t i;

    drmModeObjectPropertiesPtr props =
        drmModeObjectGetProperties(drm_dev.fd, drm_dev.conn_id, DRM_MODE_OBJECT_CONNECTOR);
    if (!props) {
        err("drmModeObjectGetProperties failed");
        return -1;
    }
    dbg("Found %u connector props", props->count_props);
    drm_dev.count_conn_props = props->count_props;
    for (i = 0; i < props->count_props; i++) {
        drm_dev.conn_props[i] = drmModeGetProperty(drm_dev.fd, props->props[i]);
        dbg("Added connector prop %u:%s", drm_dev.conn_props[i]->prop_id, drm_dev.conn_props[i]->name);
    }
    drmModeFreeObjectProperties(props);

    return 0;
}

static int drm_add_plane_property(drmModeAtomicReq *req, uint32_t plane_id, drmModePropertyPtr *props, uint32_t count,
                                  char *name, uint64_t value) {
    int      ret;
    uint32_t prop_id = get_property_id_from_list(props, count, name);

    if (!prop_id) {
        err("Couldn't find plane prop %s", name);
        return -1;
    }

    ret = drmModeAtomicAddProperty(req, plane_id, prop_id, value);
    if (ret < 0) {
        err("drmModeAtomicAddProperty (%s:%" PRIu64 ") failed: %d", name, value, ret);
        return ret;
    }

    return 0;
}

static int drm_add_crtc_property(drmModeAtomicReq *req, const char *name, uint64_t value) {
    int      ret;
    uint32_t prop_id = get_crtc_property_id(name);

    if (!prop_id) {
        err("Couldn't find crtc prop %s", name);
        return -1;
    }

    ret = drmModeAtomicAddProperty(req, drm_dev.crtc_id, get_crtc_property_id(name), value);
    if (ret < 0) {
        err("drmModeAtomicAddProperty (%s:%" PRIu64 ") failed: %d", name, value, ret);
        return ret;
    }

    return 0;
}

static int drm_add_conn_property(drmModeAtomicReq *req, const char *name, uint64_t value) {
    int      ret;
    uint32_t prop_id = get_conn_property_id(name);

    if (!prop_id) {
        err("Couldn't find conn prop %s", name);
        return -1;
    }

    ret = drmModeAtomicAddProperty(req, drm_dev.conn_id, get_conn_property_id(name), value);
    if (ret < 0) {
        err("drmModeAtomicAddProperty (%s:%" PRIu64 ") failed: %d", name, value, ret);
        return ret;
    }

    return 0;
}

static void wait_flip(int plane_id, bool block) {
    if (flip_pending[plane_id]) {
        struct pollfd pfd[1];
        pfd[0].fd     = drm_dev.fd;
        pfd[0].events = POLLIN;

        int timeout = 0;
        if (block) {
            timeout = -1;
        }
        int ret = poll(pfd, 1, timeout);

        if (ret > 0 && (pfd[0].revents & POLLIN)) {
            drmHandleEvent(drm_dev.fd, &drm_dev.drm_event_ctx);
        }
    }
}

static int drm_add_plane_fb(drmModeAtomicReqPtr req, int plane_id) {
    uint32_t            drm_plane_id;
    drmModePropertyPtr *props;
    uint32_t            count;
    struct drm_buffer  *fbuf;

    if (plane_id == PLANE_OVERLAY_ID) {
        drm_plane_id = drm_dev.overlay_plane_id;
        props        = drm_dev.overlay_plane_props;
        count        = drm_dev.count_overlay_plane_props;
        fbuf         = drm_dev.overlay_cur_bufs[0];
    } else {
        drm_plane_id = drm_dev.primary_plane_id;
        props        = drm_dev.primary_plane_props;
        count        = drm_dev.count_primary_plane_props;
        fbuf         = drm_dev.primary_cur_bufs[0];
    }

    return drm_add_plane_property(req, drm_plane_id, props, count, "FB_ID", fbuf->fb_handle);
}

static int drm_modeset(void) {
    drmModeAtomicReqPtr req   = drmModeAtomicAlloc();
    uint32_t            flags = DRM_MODE_ATOMIC_ALLOW_MODESET;
    int                 ret;

    drm_add_conn_property(req, "CRTC_ID", drm_dev.crtc_id);
    drm_add_crtc_property(req, "MODE_ID", drm_dev.blob_id);
    drm_add_crtc_property(req, "ACTIVE", 1);

    for (int p = 0; p < PLANE_LAST; p++) {
        uint32_t            plane_id;
        drmModePropertyPtr *props;
        uint32_t            count;
        uint32_t            fb_handle;

        if (p == PLANE_OVERLAY_ID) {
            plane_id  = drm_dev.overlay_plane_id;
            props     = drm_dev.overlay_plane_props;
            count     = drm_dev.count_overlay_plane_props;
            fb_handle = drm_dev.overlay_bufs[0].fb_handle;
        } else {
            plane_id  = drm_dev.primary_plane_id;
            props     = drm_dev.primary_plane_props;
            count     = drm_dev.count_primary_plane_props;
            fb_handle = drm_dev.primary_bufs[0].fb_handle;
        }

        drm_add_plane_property(req, plane_id, props, count, "CRTC_ID", drm_dev.crtc_id);
        drm_add_plane_property(req, plane_id, props, count, "SRC_X", 0);
        drm_add_plane_property(req, plane_id, props, count, "SRC_Y", 0);
        drm_add_plane_property(req, plane_id, props, count, "SRC_W", drm_dev.width << 16);
        drm_add_plane_property(req, plane_id, props, count, "SRC_H", drm_dev.height << 16);
        drm_add_plane_property(req, plane_id, props, count, "CRTC_X", 0);
        drm_add_plane_property(req, plane_id, props, count, "CRTC_Y", 0);
        drm_add_plane_property(req, plane_id, props, count, "CRTC_W", drm_dev.width);
        drm_add_plane_property(req, plane_id, props, count, "CRTC_H", drm_dev.height);
        drm_add_plane_property(req, plane_id, props, count, "FB_ID", fb_handle);
    }

    ret = drmModeAtomicCommit(drm_dev.fd, req, flags, NULL);
    drmModeAtomicFree(req);
    if (ret)
        err("drm_modeset failed: %s", strerror(errno));

    return ret;
}

static void damaged_apply(struct drm_buffer *fbuf, damaged_areas_t *dirty_queue, bool clear);

void drm_flip(void) {
    for (int p = 0; p < PLANE_LAST; p++) {
        while (flip_pending[p])
            wait_flip(p, true);
    }

    bool any = false;

    for (int p = 0; p < PLANE_LAST; p++) {
        uint8_t ci = damaged_cur_i[p];
        uint8_t pi = (ci + 1) % 2;

        bool has_dirty = (damaged_areas[p][ci].buf_size > 0 || damaged_areas[p][pi].buf_size > 0);
        if (!has_dirty)
            continue;

        struct drm_buffer *fbuf = (p == PLANE_OVERLAY_ID) ? drm_dev.overlay_cur_bufs[1] : drm_dev.primary_cur_bufs[1];

        damaged_apply(fbuf, &damaged_areas[p][pi], true);
        damaged_apply(fbuf, &damaged_areas[p][ci], false);

        damaged_cur_i[p] = pi;

        struct drm_buffer **cur  = (p == PLANE_OVERLAY_ID) ? drm_dev.overlay_cur_bufs : drm_dev.primary_cur_bufs;
        struct drm_buffer  *bufs = (p == PLANE_OVERLAY_ID) ? drm_dev.overlay_bufs : drm_dev.primary_bufs;
        if (!cur[0])
            cur[1] = &bufs[1];
        else
            cur[1] = cur[0];
        cur[0] = fbuf;

        any = true;
    }

    if (!any)
        return;

    drmModeAtomicReqPtr req = drmModeAtomicAlloc();
    for (int p = 0; p < PLANE_LAST; p++) {
        if (drm_add_plane_fb(req, p) < 0) {
            drmModeAtomicFree(req);
            return;
        }
    }

    uint32_t flags = DRM_MODE_PAGE_FLIP_EVENT | DRM_MODE_ATOMIC_NONBLOCK;
    int      ret   = drmModeAtomicCommit(drm_dev.fd, req, flags, NULL);
    drmModeAtomicFree(req);
    if (ret) {
        err("drmModeAtomicCommit failed: %s", strerror(errno));
        return;
    }

    for (int p = 0; p < PLANE_LAST; p++)
        flip_pending[p] = 1;
}

static int find_plane_by_type(uint32_t fourcc, uint32_t type, uint32_t *plane_id, uint32_t crtc_id, uint32_t crtc_idx) {
    drmModePlaneResPtr planes;
    drmModePlanePtr    plane;
    unsigned int       i, j;
    int                ret = 0;

    planes = drmModeGetPlaneResources(drm_dev.fd);
    if (!planes) {
        err("drmModeGetPlaneResources failed");
        return -1;
    }

    for (i = 0; i < planes->count_planes; ++i) {
        plane = drmModeGetPlane(drm_dev.fd, planes->planes[i]);
        if (!plane) {
            err("drmModeGetPlane failed");
            break;
        }

        if (!(plane->possible_crtcs & (1 << crtc_idx))) {
            drmModeFreePlane(plane);
            continue;
        }

        // Проверяем тип plane (primary/overlay)
        uint32_t                   plane_type = 0;
        drmModeObjectPropertiesPtr props =
            drmModeObjectGetProperties(drm_dev.fd, plane->plane_id, DRM_MODE_OBJECT_PLANE);
        if (props) {
            for (uint32_t p = 0; p < props->count_props; p++) {
                drmModePropertyPtr prop = drmModeGetProperty(drm_dev.fd, props->props[p]);
                if (prop && !strcmp(prop->name, "type")) {
                    plane_type = props->prop_values[p];
                    drmModeFreeProperty(prop);
                    break;
                }
                if (prop)
                    drmModeFreeProperty(prop);
            }
            drmModeFreeObjectProperties(props);
        }

        if (plane_type != type) {
            drmModeFreePlane(plane);
            continue;
        }

        // Проверяем формат
        for (j = 0; j < plane->count_formats; ++j) {
            if (plane->formats[j] == fourcc)
                break;
        }

        if (j == plane->count_formats) {
            drmModeFreePlane(plane);
            continue;
        }

        *plane_id = plane->plane_id;
        drmModeFreePlane(plane);
        ret = 0;
        break;
    }

    if (i == planes->count_planes)
        ret = -1;

    drmModeFreePlaneResources(planes);
    return ret;
}

static int drm_find_connector(void) {
    drmModeConnector *conn = NULL;
    drmModeEncoder   *enc  = NULL;
    drmModeRes       *res;
    int               i;

    if ((res = drmModeGetResources(drm_dev.fd)) == NULL) {
        err("drmModeGetResources() failed");
        return -1;
    }

    if (res->count_crtcs <= 0) {
        err("no Crtcs");
        goto free_res;
    }

    /* find all available connectors */
    for (i = 0; i < res->count_connectors; i++) {
        conn = drmModeGetConnector(drm_dev.fd, res->connectors[i]);
        if (!conn)
            continue;

#if DRM_CONNECTOR_ID >= 0
        if (conn->connector_id != DRM_CONNECTOR_ID) {
            drmModeFreeConnector(conn);
            continue;
        }
#endif

        if (conn->connection == DRM_MODE_CONNECTED) {
            dbg("drm: connector %d: connected", conn->connector_id);
        } else if (conn->connection == DRM_MODE_DISCONNECTED) {
            dbg("drm: connector %d: disconnected", conn->connector_id);
        } else if (conn->connection == DRM_MODE_UNKNOWNCONNECTION) {
            dbg("drm: connector %d: unknownconnection", conn->connector_id);
        } else {
            dbg("drm: connector %d: unknown", conn->connector_id);
        }

        if (conn->connection == DRM_MODE_CONNECTED && conn->count_modes > 0)
            break;

        drmModeFreeConnector(conn);
        conn = NULL;
    };

    if (!conn) {
        err("suitable connector not found");
        goto free_res;
    }

    drm_dev.conn_id = conn->connector_id;
    dbg("conn_id: %d", drm_dev.conn_id);
    drm_dev.mmWidth  = conn->mmWidth;
    drm_dev.mmHeight = conn->mmHeight;

    memcpy(&drm_dev.mode, &conn->modes[0], sizeof(drmModeModeInfo));

    if (drmModeCreatePropertyBlob(drm_dev.fd, &drm_dev.mode, sizeof(drm_dev.mode), &drm_dev.blob_id)) {
        err("error creating mode blob");
        goto free_res;
    }

    drm_dev.width  = conn->modes[0].hdisplay;
    drm_dev.height = conn->modes[0].vdisplay;

    for (i = 0; i < res->count_encoders; i++) {
        enc = drmModeGetEncoder(drm_dev.fd, res->encoders[i]);
        if (!enc)
            continue;

        dbg("enc%d enc_id %d conn enc_id %d", i, enc->encoder_id, conn->encoder_id);

        if (enc->encoder_id == conn->encoder_id)
            break;

        drmModeFreeEncoder(enc);
        enc = NULL;
    }

    if (enc) {
        drm_dev.enc_id = enc->encoder_id;
        dbg("enc_id: %d", drm_dev.enc_id);
        drm_dev.crtc_id = enc->crtc_id;
        dbg("crtc_id: %d", drm_dev.crtc_id);
        drmModeFreeEncoder(enc);
    } else {
        /* Encoder hasn't been associated yet, look it up */
        for (i = 0; i < conn->count_encoders; i++) {
            int crtc, crtc_id = -1;

            enc = drmModeGetEncoder(drm_dev.fd, conn->encoders[i]);
            if (!enc)
                continue;

            for (crtc = 0; crtc < res->count_crtcs; crtc++) {
                uint32_t crtc_mask = 1 << crtc;

                crtc_id = res->crtcs[crtc];

                dbg("enc_id %d crtc%d id %d mask %x possible %x", enc->encoder_id, crtc, crtc_id, crtc_mask,
                    enc->possible_crtcs);

                if (enc->possible_crtcs & crtc_mask)
                    break;
            }

            if (crtc_id > 0) {
                drm_dev.enc_id = enc->encoder_id;
                dbg("enc_id: %d", drm_dev.enc_id);
                drm_dev.crtc_id = crtc_id;
                dbg("crtc_id: %d", drm_dev.crtc_id);
                break;
            }

            drmModeFreeEncoder(enc);
            enc = NULL;
        }

        if (!enc) {
            err("suitable encoder not found");
            goto free_res;
        }

        drmModeFreeEncoder(enc);
    }

    drm_dev.crtc_idx = -1;

    for (i = 0; i < res->count_crtcs; ++i) {
        if (drm_dev.crtc_id == res->crtcs[i]) {
            drm_dev.crtc_idx = i;
            break;
        }
    }

    if (drm_dev.crtc_idx == -1) {
        err("drm: CRTC not found");
        goto free_res;
    }

    dbg("crtc_idx: %d", drm_dev.crtc_idx);

    /* Success path: release the objects obtained for the search. */
    drmModeFreeConnector(conn);
    drmModeFreeResources(res);

    return 0;

free_res:
    if (conn) {
        drmModeFreeConnector(conn);
    }
    drmModeFreeResources(res);

    return -1;
}

static int drm_open(const char *path) {
    int      fd, flags;
    uint64_t has_dumb;
    int      ret;

    fd = open(path, O_RDWR);
    if (fd < 0) {
        err("cannot open \"%s\"", path);
        return -1;
    }

    /* set FD_CLOEXEC flag */
    if ((flags = fcntl(fd, F_GETFD)) < 0 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0) {
        err("fcntl FD_CLOEXEC failed");
        goto err;
    }

    /* check capability */
    ret = drmGetCap(fd, DRM_CAP_DUMB_BUFFER, &has_dumb);
    if (ret < 0 || has_dumb == 0) {
        err("drmGetCap DRM_CAP_DUMB_BUFFER failed or \"%s\" doesn't have dumb "
            "buffer",
            path);
        goto err;
    }

    return fd;
err:
    close(fd);
    return -1;
}

static int drm_setup() {
    int         ret;
    const char *device_path = NULL;

    device_path = getenv("DRM_CARD");
    if (!device_path)
        device_path = DRM_CARD;

    drm_dev.fd = drm_open(device_path);
    if (drm_dev.fd < 0)
        return -1;

    ret = drmSetClientCap(drm_dev.fd, DRM_CLIENT_CAP_ATOMIC, 1);
    if (ret) {
        err("No atomic modesetting support: %s", strerror(errno));
        goto err;
    }

    ret = drm_find_connector();
    if (ret) {
        err("available drm devices not found");
        goto err;
    }

    ret = find_plane_by_type(DRM_FORMAT_XRGB8888, DRM_PLANE_TYPE_PRIMARY, &drm_dev.primary_plane_id, drm_dev.crtc_id,
                             drm_dev.crtc_idx);
    if (ret) {
        err("Cannot find primary plane");
        goto err;
    }

    ret = find_plane_by_type(DRM_FORMAT_ARGB8888, DRM_PLANE_TYPE_OVERLAY, &drm_dev.overlay_plane_id, drm_dev.crtc_id,
                             drm_dev.crtc_idx);
    if (ret) {
        err("Cannot find overlay plane");
        goto err;
    }

    drm_dev.primary_plane = drmModeGetPlane(drm_dev.fd, drm_dev.primary_plane_id);
    if (!drm_dev.primary_plane) {
        err("Cannot get primary plane");
        goto err;
    }
    drm_dev.overlay_plane = drmModeGetPlane(drm_dev.fd, drm_dev.overlay_plane_id);
    if (!drm_dev.overlay_plane) {
        err("Cannot get overlay plane");
        goto err;
    }

    drm_dev.crtc = drmModeGetCrtc(drm_dev.fd, drm_dev.crtc_id);
    if (!drm_dev.crtc) {
        err("Cannot get crtc");
        goto err;
    }

    drm_dev.conn = drmModeGetConnector(drm_dev.fd, drm_dev.conn_id);
    if (!drm_dev.conn) {
        err("Cannot get connector");
        goto err;
    }

    ret =
        drm_get_plane_props(drm_dev.primary_plane_id, drm_dev.primary_plane_props, &drm_dev.count_primary_plane_props);
    if (ret) {
        err("Cannot get primary plane props");
        goto err;
    }

    ret =
        drm_get_plane_props(drm_dev.overlay_plane_id, drm_dev.overlay_plane_props, &drm_dev.count_overlay_plane_props);
    if (ret) {
        err("Cannot get overlay plane props");
        goto err;
    }

    ret = drm_get_crtc_props();
    if (ret) {
        err("Cannot get crtc props");
        goto err;
    }

    ret = drm_get_conn_props();
    if (ret) {
        err("Cannot get connector props");
        goto err;
    }

    drm_dev.drm_event_ctx.version           = DRM_EVENT_CONTEXT_VERSION;
    drm_dev.drm_event_ctx.page_flip_handler = page_flip_handler;

    info("drm: Found primary plane_id: %u overlay plane id: %u connector_id: %d crtc_id: %d", drm_dev.primary_plane_id,
         drm_dev.overlay_plane_id, drm_dev.conn_id, drm_dev.crtc_id);

    info("drm: %dx%d (%dmm X% dmm)", drm_dev.width, drm_dev.height, drm_dev.mmWidth, drm_dev.mmHeight);

    return 0;

err:
    close(drm_dev.fd);
    return -1;
}

static int drm_allocate_dumb(struct drm_buffer *buf) {
    struct drm_mode_create_dumb creq;
    struct drm_mode_map_dumb    mreq;
    uint32_t                    handles[4] = {0}, pitches[4] = {0}, offsets[4] = {0};
    int                         ret;

    /* create dumb buffer */
    memset(&creq, 0, sizeof(creq));
    creq.width  = drm_dev.width;
    creq.height = drm_dev.height;
    creq.bpp    = LV_COLOR_DEPTH;
    ret         = drmIoctl(drm_dev.fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq);
    if (ret < 0) {
        err("DRM_IOCTL_MODE_CREATE_DUMB fail");
        return -1;
    }

    buf->handle = creq.handle;
    buf->pitch  = creq.pitch;
    dbg("pitch %d", buf->pitch);
    buf->size = creq.size;
    dbg("size %d", buf->size);

    /* prepare buffer for memory mapping */
    memset(&mreq, 0, sizeof(mreq));
    mreq.handle = creq.handle;
    ret         = drmIoctl(drm_dev.fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq);
    if (ret) {
        err("DRM_IOCTL_MODE_MAP_DUMB fail");
        return -1;
    }

    buf->offset = mreq.offset;

    /* perform actual memory mapping */
    buf->map = mmap(0, creq.size, PROT_READ | PROT_WRITE, MAP_SHARED, drm_dev.fd, mreq.offset);
    if (buf->map == MAP_FAILED) {
        err("mmap fail");
        return -1;
    }

    /* clear the framebuffer to 0 (= full transparency in ARGB8888) */
    memset(buf->map, 0, creq.size);

    /* create framebuffer object for the dumb-buffer */
    handles[0] = creq.handle;
    pitches[0] = creq.pitch;
    offsets[0] = 0;
    ret        = drmModeAddFB2(drm_dev.fd, drm_dev.width, drm_dev.height, buf->fourcc, handles, pitches, offsets,
                               &buf->fb_handle, 0);
    if (ret) {
        err("drmModeAddFB fail");
        return -1;
    }

    return 0;
}

static void drm_init_buf(struct drm_buffer *buf, uint32_t fourcc) {
    buf->fourcc = fourcc;
}

static int drm_setup_buffers(void) {
    int ret;

    drm_init_buf(&drm_dev.primary_bufs[0], DRM_FORMAT_XRGB8888);
    drm_init_buf(&drm_dev.primary_bufs[1], DRM_FORMAT_XRGB8888);
    drm_init_buf(&drm_dev.overlay_bufs[0], DRM_FORMAT_ARGB8888);
    drm_init_buf(&drm_dev.overlay_bufs[1], DRM_FORMAT_ARGB8888);

    /* Allocate DUMB buffers */
    ret = drm_allocate_dumb(&drm_dev.primary_bufs[0]);
    if (ret)
        return ret;
    ret = drm_allocate_dumb(&drm_dev.overlay_bufs[0]);
    if (ret)
        return ret;

    ret = drm_allocate_dumb(&drm_dev.primary_bufs[1]);
    if (ret)
        return ret;
    ret = drm_allocate_dumb(&drm_dev.overlay_bufs[1]);
    if (ret)
        return ret;

    /* Set buffering handling */
    drm_dev.primary_cur_bufs[0] = &drm_dev.primary_bufs[0];
    drm_dev.primary_cur_bufs[1] = &drm_dev.primary_bufs[1];
    drm_dev.overlay_cur_bufs[0] = &drm_dev.overlay_bufs[0];
    drm_dev.overlay_cur_bufs[1] = &drm_dev.overlay_bufs[1];

    return 0;
}

static inline void buf_write(struct drm_buffer *fbuf, const lv_area_t *area, lv_color_t *color_p) {
    int        i, y;
    lv_coord_t w = (area->x2 - area->x1 + 1);
    for (y = 0, i = area->y1; i <= area->y2; ++i, ++y) {
        memcpy((uint8_t *)fbuf->map + (area->x1 * (LV_COLOR_SIZE / 8)) + (fbuf->pitch * i),
               (uint8_t *)color_p + (w * (LV_COLOR_SIZE / 8) * y), w * (LV_COLOR_SIZE / 8));
    }
}

static void damaged_push(const lv_area_t *area, const lv_color_t *color, damaged_areas_t *dirty_queue) {
    if (dirty_queue->rects_cnt >= MAX_DIRTY_RECTS) {
        err("dirty areas is full");
        return;
    }
    uint32_t area_size = lv_area_get_size(area);
    if (dirty_queue->buf_size + area_size > MAX_DIRTY_BUF) {
        err("dirty buf is full");
        return;
    }
    dirty_queue->rects[dirty_queue->rects_cnt++] = *area;
    memcpy(dirty_queue->buf + dirty_queue->buf_size, color, area_size * sizeof(lv_color_t));
    dirty_queue->buf_size += area_size;
}

static void damaged_apply(struct drm_buffer *fbuf, damaged_areas_t *dirty_queue, bool clear) {
    lv_color_t *color_p = dirty_queue->buf;
    for (size_t i = 0; i < dirty_queue->rects_cnt; i++) {
        lv_area_t *area = &dirty_queue->rects[i];
        buf_write(fbuf, area, color_p);
        color_p += lv_area_get_size(area);
    }
    if (clear) {
        dirty_queue->buf_size  = 0;
        dirty_queue->rects_cnt = 0;
    }
}

void drm_flush_primary(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
    damaged_push(area, color_p, &damaged_areas[PLANE_PRIMARY_ID][damaged_cur_i[PLANE_PRIMARY_ID]]);
    lv_disp_flush_ready(disp_drv);
}

void drm_flush_overlay(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
    damaged_push(area, color_p, &damaged_areas[PLANE_OVERLAY_ID][damaged_cur_i[PLANE_OVERLAY_ID]]);
    lv_disp_flush_ready(disp_drv);
}

bool drm_primary_begin_direct(drm_direct_ctx_t *ctx, uint32_t pixels_needed) {
    damaged_areas_t *q = &damaged_areas[PLANE_PRIMARY_ID][damaged_cur_i[PLANE_PRIMARY_ID]];
    if (q->buf_size + pixels_needed > MAX_DIRTY_BUF) {
        return false;
    }
    ctx->buf        = q->buf + q->buf_size;
    ctx->max_pixels = MAX_DIRTY_BUF - q->buf_size;
    return true;
}

void drm_primary_end_direct(const lv_area_t *area) {
    damaged_areas_t *q         = &damaged_areas[PLANE_PRIMARY_ID][damaged_cur_i[PLANE_PRIMARY_ID]];
    uint32_t         area_size = lv_area_get_size(area);
    if (q->buf_size + area_size > MAX_DIRTY_BUF) {
        err("direct rendered area is too big, skip");
        return;
    }
    q->rects[q->rects_cnt++] = *area;
    q->buf_size += area_size;
}

static void neon_blend_argb8888(uint32_t *restrict dst, const uint32_t *restrict bottom, const uint32_t *restrict top, size_t num_pixels) {
    size_t i = 0;

    for (; i + 7 < num_pixels; i += 8) {
        uint8x8x4_t top_channels = vld4_u8((const uint8_t*)&top[i]);
        uint8x8x4_t bottom_channels = vld4_u8((const uint8_t*)&bottom[i]);

        uint8x8_t alpha = top_channels.val[3];
        uint8x8_t inv_alpha = vsub_u8(vdup_n_u8(255), alpha);
        uint16x8_t round_const = vdupq_n_u16(128);

        uint16x8_t r_blend = vmlal_u8(round_const, top_channels.val[2], alpha);
        r_blend = vmlal_u8(r_blend, bottom_channels.val[2], inv_alpha);
        uint8x8_t r_res = vshrn_n_u16(r_blend, 8);

        uint16x8_t g_blend = vmlal_u8(round_const, top_channels.val[1], alpha);
        g_blend = vmlal_u8(g_blend, bottom_channels.val[1], inv_alpha);
        uint8x8_t g_res = vshrn_n_u16(g_blend, 8);

        uint16x8_t b_blend = vmlal_u8(round_const, top_channels.val[0], alpha);
        b_blend = vmlal_u8(b_blend, bottom_channels.val[0], inv_alpha);
        uint8x8_t b_res = vshrn_n_u16(b_blend, 8);

        uint8x8x4_t res_channels;
        res_channels.val[0] = b_res;
        res_channels.val[1] = g_res;
        res_channels.val[2] = r_res;
        res_channels.val[3] = vdup_n_u8(255);

        vst4_u8((uint8_t*)&dst[i], res_channels);
    }

    for (; i < num_pixels; ++i) {
        uint32_t top_px = top[i];
        uint8_t alpha = (top_px >> 24) & 0xFF;

        if (alpha == 255) {
            dst[i] = top_px | 0xFF000000;
        } else if (alpha > 0) {
            uint32_t bottom_px = bottom[i];
            uint8_t inv_alpha = 255 - alpha;

            uint8_t r = (((top_px >> 16) & 0xFF) * alpha + ((bottom_px >> 16) & 0xFF) * inv_alpha + 128) >> 8;
            uint8_t g = (((top_px >>  8) & 0xFF) * alpha + ((bottom_px >>  8) & 0xFF) * inv_alpha + 128) >> 8;
            uint8_t b = (((top_px      ) & 0xFF) * alpha + ((bottom_px      ) & 0xFF) * inv_alpha + 128) >> 8;

            dst[i] = (255 << 24) | (r << 16) | (g << 8) | b;
        }
    }
}

void drm_take_screenshot(uint8_t *buf) {
    neon_blend_argb8888(
        (uint32_t *)buf,
        drm_dev.primary_cur_bufs[0]->map,
        drm_dev.overlay_cur_bufs[0]->map,
        drm_dev.width * drm_dev.height);
}

void drm_get_sizes(lv_coord_t *width, lv_coord_t *height, uint32_t *dpi) {
    if (width)
        *width = drm_dev.width;

    if (height)
        *height = drm_dev.height;

    if (dpi && drm_dev.mmWidth)
        *dpi = DIV_ROUND_UP(drm_dev.width * 25400, drm_dev.mmWidth * 1000);
}

void drm_init(lv_disp_drv_t *disp_drv_primary, lv_disp_drv_t *disp_drv_overlay) {
    int ret;

    ret = drm_setup();
    if (ret) {
        close(drm_dev.fd);
        drm_dev.fd = -1;
        return;
    }

    ret = drm_setup_buffers();
    if (ret) {
        err("DRM buffer allocation failed");
        close(drm_dev.fd);
        drm_dev.fd = -1;
        return;
    }

    ret = drm_modeset();
    if (ret) {
        err("DRM modeset failed");
        close(drm_dev.fd);
        drm_dev.fd = -1;
        return;
    }

    uint8_t disp_id;
    /* Init primary display (primary plane) */
    disp_id                 = PLANE_PRIMARY_ID;
    lv_color_t *buf_primary = calloc(sizeof(lv_color_t), drm_dev.width * drm_dev.height);

    lv_disp_draw_buf_init(&disp_buf[disp_id], buf_primary, NULL, drm_dev.width * drm_dev.height);
    lv_disp_drv_init(disp_drv_primary);

    disp_drv_primary->draw_buf = &disp_buf[disp_id];
    disp_drv_primary->flush_cb = drm_flush_primary;
    disp_drv_primary->hor_res  = drm_dev.width;
    disp_drv_primary->ver_res  = drm_dev.height;

    /* Init overlay display (primary plane) */
    disp_id                 = PLANE_OVERLAY_ID;
    lv_color_t *buf_overlay = calloc(sizeof(lv_color_t), drm_dev.width * drm_dev.height);

    lv_disp_draw_buf_init(&disp_buf[disp_id], buf_overlay, NULL, drm_dev.width * drm_dev.height);
    lv_disp_drv_init(disp_drv_overlay);

    disp_drv_overlay->draw_buf = &disp_buf[disp_id];
    disp_drv_overlay->flush_cb = drm_flush_overlay;
    disp_drv_overlay->hor_res  = drm_dev.width;
    disp_drv_overlay->ver_res  = drm_dev.height;

    for (size_t i = 0; i < PLANE_LAST; i++) {
        flip_pending[i]               = 0;
        damaged_cur_i[i]              = 0;
        damaged_areas[i][0].buf_size  = 0;
        damaged_areas[i][1].buf_size  = 0;
        damaged_areas[i][0].rects_cnt = 0;
        damaged_areas[i][1].rects_cnt = 0;
    }

    info("DRM subsystem and buffer mapped successfully");
}

void drm_exit(void) {
    // Add free all 4 buffers and mutes?
    close(drm_dev.fd);
    drm_dev.fd = -1;
}

#endif
