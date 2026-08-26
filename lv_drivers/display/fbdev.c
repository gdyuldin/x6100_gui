/**
 * @file fbdev.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "fbdev.h"
#if USE_FBDEV || USE_BSD_FBDEV

#include <stdlib.h>
#include <unistd.h>
#include <stddef.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <semaphore.h>
#include <arm_neon.h>

#if USE_BSD_FBDEV
#include <sys/fcntl.h>
#include <sys/time.h>
#include <sys/consio.h>
#include <sys/fbio.h>
#else  /* USE_BSD_FBDEV */
#include <linux/fb.h>
#endif /* USE_BSD_FBDEV */

/*********************
 *      DEFINES
 *********************/
#ifndef FBDEV_PATH
#define FBDEV_PATH  "/dev/fb0"
#endif

#ifndef DIV_ROUND_UP
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#endif

#define QUEUE_SIZE 3

/**********************
 *      TYPEDEFS
 **********************/

 typedef struct {
    const lv_color_t *src_buf;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool ready_to_rotate;
} rotation_worker_t;

static rotation_worker_t worker = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER,
    .ready_to_rotate = false
};

/**********************
 *      STRUCTURES
 **********************/

struct bsd_fb_var_info{
    uint32_t xoffset;
    uint32_t yoffset;
    uint32_t xres;
    uint32_t yres;
    int bits_per_pixel;
 };

struct bsd_fb_fix_info{
    long int line_length;
    long int smem_len;
};

struct draw_task {
    lv_area_t area;
    lv_color_t color_p[800*480];
    // lv_color_t *color_p;
};

struct draw_queue {
    struct draw_task tasks[QUEUE_SIZE];
    uint8_t head;
    uint8_t tail;
    pthread_mutex_t mut;
    pthread_cond_t cond;
};

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void *rotation_thread_fbdev(void *arg);

/**********************
 *  STATIC VARIABLES
 **********************/
#if USE_BSD_FBDEV
static struct bsd_fb_var_info vinfo;
static struct bsd_fb_fix_info finfo;
#else
static struct fb_var_screeninfo vinfo;
static struct fb_fix_screeninfo finfo;
#endif /* USE_BSD_FBDEV */
static char *fbp = 0;
static long int screensize = 0;
static int fbfd = 0;
static lv_disp_draw_buf_t disp_buf;


static pthread_t thread;
static struct draw_queue queue = {.head=0, .tail=0,  .mut=PTHREAD_MUTEX_INITIALIZER, .cond=PTHREAD_COND_INITIALIZER};

/**********************
 *      MACROS
 **********************/

#if USE_BSD_FBDEV
#define FBIOBLANK FBIO_BLANK
#endif /* USE_BSD_FBDEV */

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void fbdev_init(lv_disp_drv_t *disp_drv)
{
    // Open the file for reading and writing
    fbfd = open(FBDEV_PATH, O_RDWR);
    if(fbfd == -1) {
        perror("Error: cannot open framebuffer device");
        return;
    }
    LV_LOG_INFO("The framebuffer device was opened successfully");

    // Make sure that the display is on.
    if (ioctl(fbfd, FBIOBLANK, FB_BLANK_UNBLANK) != 0) {
        perror("ioctl(FBIOBLANK)");
        return;
    }

#if USE_BSD_FBDEV
    struct fbtype fb;
    unsigned line_length;

    //Get fb type
    if (ioctl(fbfd, FBIOGTYPE, &fb) != 0) {
        perror("ioctl(FBIOGTYPE)");
        return;
    }

    //Get screen width
    if (ioctl(fbfd, FBIO_GETLINEWIDTH, &line_length) != 0) {
        perror("ioctl(FBIO_GETLINEWIDTH)");
        return;
    }

    vinfo.xres = (unsigned) fb.fb_width;
    vinfo.yres = (unsigned) fb.fb_height;
    vinfo.bits_per_pixel = fb.fb_depth;
    vinfo.xoffset = 0;
    vinfo.yoffset = 0;
    finfo.line_length = line_length;
    finfo.smem_len = finfo.line_length * vinfo.yres;
#else /* USE_BSD_FBDEV */

    // Get fixed screen information
    if(ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo) == -1) {
        perror("Error reading fixed information");
        return;
    }

    // Get variable screen information
    if(ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo) == -1) {
        perror("Error reading variable information");
        return;
    }
#endif /* USE_BSD_FBDEV */

    LV_LOG_INFO("%dx%d, %dbpp", vinfo.xres, vinfo.yres, vinfo.bits_per_pixel);

    // Figure out the size of the screen in bytes
    screensize =  finfo.smem_len; //finfo.line_length * vinfo.yres;

    // Map the device to memory
    fbp = (char *)mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
    if((intptr_t)fbp == -1) {
        perror("Error: failed to map framebuffer device to memory");
        return;
    }

    // Don't initialise the memory to retain what's currently displayed / avoid clearing the screen.
    // This is important for applications that only draw to a subsection of the full framebuffer.

    LV_LOG_INFO("The framebuffer device was mapped to memory successfully");

    lv_color_t *buf1 = calloc(sizeof(lv_color_t), vinfo.xres * vinfo.yres);
    lv_color_t *buf2 = calloc(sizeof(lv_color_t), vinfo.xres * vinfo.yres);

    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, vinfo.xres * vinfo.yres);
    lv_disp_drv_init(disp_drv);

    disp_drv->draw_buf = &disp_buf;
    disp_drv->flush_cb = fbdev_flush;
    disp_drv->hor_res  = vinfo.yres;
    disp_drv->ver_res  = vinfo.xres;

    pthread_create(&thread, NULL, rotation_thread_fbdev, NULL);

}

void fbdev_exit(void)
{
    munmap(fbp, screensize);
    close(fbfd);
}

/**
 * Flush a buffer to the marked area
 * @param drv pointer to driver where this function belongs
 * @param area an area where to copy `color_p`
 * @param color_p an array of pixels to copy to the `area` part of the screen
 */
void fbdev_flush(lv_disp_drv_t * drv, const lv_area_t * area, lv_color_t * color_p)
{
    if(fbp == NULL ||
            area->x2 < 0 ||
            area->y2 < 0 ||
            area->x1 > (int32_t)vinfo.yres - 1 ||
            area->y1 > (int32_t)vinfo.xres - 1) {
        lv_disp_flush_ready(drv);
        return;
    }

    // Check if LVGL has finished rendering the ENTIRE screen (for frame stability)
    if (lv_disp_flush_is_last(drv)) {
        drv->hor_res;
        drv->ver_res;
        pthread_mutex_lock(&worker.mutex);

        worker.src_buf = color_p; // Pass a pointer to the filled buffer
        worker.ready_to_rotate = true;

        pthread_cond_signal(&worker.cond);
        pthread_mutex_unlock(&worker.mutex);
    }

    // Release LVGL to draw the next frame into the second buffer
    lv_disp_flush_ready(drv);
}

void fbdev_get_sizes(uint32_t *width, uint32_t *height, uint32_t *dpi) {
    if (width)
        *width = vinfo.xres;

    if (height)
        *height = vinfo.yres;

    if (dpi && vinfo.height)
        *dpi = DIV_ROUND_UP(vinfo.xres * 254, vinfo.width * 10);
}

void fbdev_set_offset(uint32_t xoffset, uint32_t yoffset) {
    vinfo.xoffset = xoffset;
    vinfo.yoffset = yoffset;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

void neon_rotate_90_ccw_32bpp(const uint32_t *src, uint32_t *dst, int src_w, int src_h) {
    for (int y = 0; y < src_h; y += 4) {
        for (int x = 0; x < src_w; x += 4) {

            // Load a 4x4 pixel block (rows r0, r1, r2, r3)
            uint32x4_t r0 = vld1q_u32(src + (y + 0) * src_w + x);
            uint32x4_t r1 = vld1q_u32(src + (y + 1) * src_w + x);
            uint32x4_t r2 = vld1q_u32(src + (y + 2) * src_w + x);
            uint32x4_t r3 = vld1q_u32(src + (y + 3) * src_w + x);

            // The first stage of transposition (exchange of elements within pairs of rows)
            uint32x4x2_t t0 = vtrnq_u32(r0, r1);
            uint32x4x2_t t1 = vtrnq_u32(r2, r3);

            // Final assembly of columns with correct linear pixel order
            uint32x4_t o0 = vcombine_u32(vget_low_u32(t0.val[0]),  vget_low_u32(t1.val[0]));  // Col 0: {r0_0, r1_0, r2_0, r3_0}
            uint32x4_t o1 = vcombine_u32(vget_low_u32(t0.val[1]),  vget_low_u32(t1.val[1]));  // Col 1: {r0_1, r1_1, r2_1, r3_1}
            uint32x4_t o2 = vcombine_u32(vget_high_u32(t0.val[0]), vget_high_u32(t1.val[0])); // Col 2: {r0_2, r1_2, r2_2, r3_2}
            uint32x4_t o3 = vcombine_u32(vget_high_u32(t0.val[1]), vget_high_u32(t1.val[1])); // Col 3: {r0_3, r1_3, r2_3, r3_3}

            // Calculating coordinates for counterclockwise rotation
            int dest_y = (src_w - 4) - x;

            // Unloading to a rotated framebuffer
            vst1q_u32(dst + (dest_y + 3) * src_h + y, o0);
            vst1q_u32(dst + (dest_y + 2) * src_h + y, o1);
            vst1q_u32(dst + (dest_y + 1) * src_h + y, o2);
            vst1q_u32(dst + (dest_y + 0) * src_h + y, o3);
        }
    }
}


#define SCREEN_WIDTH  800
#define SCREEN_HEIGHT 480

void *rotation_thread_fbdev(void *arg) {
    while (1) {
        pthread_mutex_lock(&worker.mutex);
        while (!worker.ready_to_rotate) {
            pthread_cond_wait(&worker.cond, &worker.mutex);
        }

        const uint32_t *raw_lvgl_pixels = (const uint32_t *)worker.src_buf;
        worker.ready_to_rotate = false;
        pthread_mutex_unlock(&worker.mutex);

        neon_rotate_90_ccw_32bpp(raw_lvgl_pixels, (uint32_t*)fbp, vinfo.yres, vinfo.xres);
    }
    return NULL;
}


#endif
