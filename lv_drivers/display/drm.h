/**
 * @file drm.h
 *
 */

#ifndef DRM_H
#define DRM_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#ifndef LV_DRV_NO_CONF
#ifdef LV_CONF_INCLUDE_SIMPLE
#include "lv_drv_conf.h"
#else
#include "../../lv_drv_conf.h"
#endif
#endif

#if USE_DRM

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

typedef struct {
    lv_color_t *buf;
    uint32_t    max_pixels;
} drm_direct_ctx_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/
void drm_init(lv_disp_drv_t *disp_drv_primary, lv_disp_drv_t *disp_drv_overlay);
void drm_get_sizes(lv_coord_t *width, lv_coord_t *height, uint32_t *dpi);
void drm_flip(void);
void drm_exit(void);

bool drm_primary_begin_direct(drm_direct_ctx_t *ctx, uint32_t pixels_needed);
void drm_primary_end_direct(const lv_area_t *area);

void drm_take_screenshot(uint8_t *buf);


/**********************
 *      MACROS
 **********************/

#endif  /*USE_DRM*/

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*DRM_H*/
