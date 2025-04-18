/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2025 Franco Venturi K4VZ
 */

#include "styles_custom.h"
#include "styles.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#define PATH "A:/dev/shm/"

static FILE * fp = NULL;
static char * line = NULL;

extern const uint32_t wf_palette_legacy[256];
extern const uint32_t wf_palette_gauss[256];

static int styles_custom_begin(const char * path) {
    fp = fopen(path, "r");

    if (fp == NULL) {
        fprintf(stderr, "Unable to open style customization file %s: %s\n", path, strerror(errno));
        return -1;
    }
    return 0;
}

static int styles_custom_iter(const char ** key, const char ** value) {
    size_t len = 0;
    ssize_t read;

    while ((read = getline(&line, &len, fp)) != -1) {
        int startidx, endidx;
        startidx = 0;
        endidx = read;
        while (endidx > startidx && isspace(line[endidx-1]))
            endidx--;
        while (endidx > startidx && isspace(line[startidx]))
            startidx++;
        char * trimmed_line = line + startidx;
        line[endidx] = '\0';
        if (strlen(trimmed_line) == 0 || trimmed_line[0] == '#')
            continue;
        char * sep = strchr(trimmed_line, '=');
        if (sep == NULL) {
            fprintf(stderr, "Invalid line in style customization file %s\n", trimmed_line);
            continue;
        }
        endidx = sep - trimmed_line;
        while (endidx > 0 && isspace(trimmed_line[endidx-1]))
            endidx--;
        char *keyx = trimmed_line;
        trimmed_line[endidx] = '\0';
        if (strlen(keyx) == 0) {
            fprintf(stderr, "Empty key in style customization file - value: %s\n", sep+1);
            continue;
        }
        startidx = 1;
        endidx = strlen(sep);
        while (endidx > startidx && isspace(sep[startidx]))
            startidx++;
        char * valuex = sep + startidx;
        if (strlen(valuex) == 0) {
            fprintf(stderr, "Empty value in style customization file - key: %s\n", keyx);
            continue;
        }
        *key = (const char *) keyx;
        *value = (const char *) valuex;
        return 0;
    }
    return EOF;
}

static void styles_custom_end() {
    free(line);
    fclose(fp);
}

static int set_custom_palette(char * value, const uint32_t ** palette) {
    if (strcasecmp(value, "legacy") == 0) {
        *palette = wf_palette_legacy;
    } else if (strcasecmp(value, "gauss") == 0) {
        *palette = wf_palette_gauss;
    } else if (isdigit(value[0])) {
        /* custom palette */
        uint32_t * custom_palette = malloc(256 * sizeof(uint32_t));
        memset(custom_palette, 0, 256 * sizeof(uint32_t));

        char *v = value;
        for (int i = 0; i < 256; i++) {
            char *tok = strtok(v, ", \t");
            v = NULL;
            if (tok == NULL)
                break;
            uint32_t tmp;
            int n;
            if (sscanf(tok, "%i%n", &tmp, &n) != 1 || n != strlen(tok)) {
                fprintf(stderr, "invalid palette value: %s\n", tok);
                break;
            }
            custom_palette[i] = tmp;
        }
        *palette = custom_palette;
    } else {
        return -1;
    }
    return 0;
}

static int set_custom_bg_color(const char * value, lv_color_t * color, lv_style_t * style) {
    unsigned int tmp;
    int n;
    if (sscanf(value, "%i%n", &tmp, &n) != 1 || n != strlen(value)) {
        return -1;
    }
    if (color != NULL) {
        *color = lv_color_hex(tmp);
        lv_style_set_bg_color(style, *color);
    } else {
        lv_color_t tmpcolor = lv_color_hex(tmp);
        lv_style_set_bg_color(style, tmpcolor);
    }
    return 0;
}

static int set_custom_bg_img_src(const char * value, lv_style_t * style) {
    char * tmp;
    if (value[0] == '/') {
        tmp = (char *) malloc(strlen(value) + 1);
        strcpy(tmp, value);
    } else {
        tmp = (char *) malloc(strlen(PATH) + strlen(value) + 1);
        strcpy(tmp, PATH);
        strcat(tmp, value);
    }
    lv_style_set_bg_img_src(style, tmp);
    return 0;
}

static int set_custom_width(const char * value, lv_style_t * style) {
    unsigned int tmp;
    int n;
    if (sscanf(value, "%d%n", &tmp, &n) != 1 || n != strlen(value)) {
        return -1;
    }
    lv_style_set_width(style, tmp);
    return 0;
}

static int set_custom_height(const char * value, lv_style_t * style) {
    unsigned int tmp;
    int n;
    if (sscanf(value, "%d%n", &tmp, &n) != 1 || n != strlen(value)) {
        return -1;
    }
    lv_style_set_height(style, tmp);
    return 0;
}

static int set_custom_text_font(const char * value, lv_style_t * style) {
    if (0) {
#if LV_FONT_MONTSERRAT_8
    } else if (strcasecmp(value, "montserrat 8") == 0) {
        lv_style_set_text_font(style, &lv_font_montserrat_8);
#endif
#if LV_FONT_MONTSERRAT_10
    } else if (strcasecmp(value, "montserrat 10") == 0) {
        lv_style_set_text_font(style, &lv_font_montserrat_10);
#endif
#if LV_FONT_MONTSERRAT_12
    } else if (strcasecmp(value, "montserrat 12") == 0) {
        lv_style_set_text_font(style, &lv_font_montserrat_12);
#endif
#if LV_FONT_MONTSERRAT_14
    } else if (strcasecmp(value, "montserrat 14") == 0) {
        lv_style_set_text_font(style, &lv_font_montserrat_14);
#endif
#if LV_FONT_MONTSERRAT_16
    } else if (strcasecmp(value, "montserrat 16") == 0) {
        lv_style_set_text_font(style, &lv_font_montserrat_16);
#endif
#if LV_FONT_MONTSERRAT_18
    } else if (strcasecmp(value, "montserrat 18") == 0) {
        lv_style_set_text_font(style, &lv_font_montserrat_18);
#endif
#if LV_FONT_MONTSERRAT_20
    } else if (strcasecmp(value, "montserrat 20") == 0) {
        lv_style_set_text_font(style, &lv_font_montserrat_20);
#endif
#if LV_FONT_MONTSERRAT_22
    } else if (strcasecmp(value, "montserrat 22") == 0) {
        lv_style_set_text_font(style, &lv_font_montserrat_22);
#endif
#if LV_FONT_MONTSERRAT_24
    } else if (strcasecmp(value, "montserrat 24") == 0) {
        lv_style_set_text_font(style, &lv_font_montserrat_24);
#endif
#if LV_FONT_MONTSERRAT_26
    } else if (strcasecmp(value, "montserrat 26") == 0) {
        lv_style_set_text_font(style, &lv_font_montserrat_26);
#endif
#if LV_FONT_MONTSERRAT_28
    } else if (strcasecmp(value, "montserrat 28") == 0) {
        lv_style_set_text_font(style, &lv_font_montserrat_28);
#endif
#if LV_FONT_MONTSERRAT_30
    } else if (strcasecmp(value, "montserrat 30") == 0) {
        lv_style_set_text_font(style, &lv_font_montserrat_30);
#endif
#if LV_FONT_MONTSERRAT_32
    } else if (strcasecmp(value, "montserrat 32") == 0) {
        lv_style_set_text_font(style, &lv_font_montserrat_32);
#endif
#if LV_FONT_MONTSERRAT_34
    } else if (strcasecmp(value, "montserrat 34") == 0) {
        lv_style_set_text_font(style, &lv_font_montserrat_34);
#endif
#if LV_FONT_MONTSERRAT_36
    } else if (strcasecmp(value, "montserrat 36") == 0) {
        lv_style_set_text_font(style, &lv_font_montserrat_36);
#endif
#if LV_FONT_MONTSERRAT_38
    } else if (strcasecmp(value, "montserrat 38") == 0) {
        lv_style_set_text_font(style, &lv_font_montserrat_38);
#endif
#if LV_FONT_MONTSERRAT_40
    } else if (strcasecmp(value, "montserrat 40") == 0) {
        lv_style_set_text_font(style, &lv_font_montserrat_40);
#endif
#if LV_FONT_MONTSERRAT_42
    } else if (strcasecmp(value, "montserrat 42") == 0) {
        lv_style_set_text_font(style, &lv_font_montserrat_42);
#endif
#if LV_FONT_MONTSERRAT_44
    } else if (strcasecmp(value, "montserrat 44") == 0) {
        lv_style_set_text_font(style, &lv_font_montserrat_44);
#endif
#if LV_FONT_MONTSERRAT_46
    } else if (strcasecmp(value, "montserrat 46") == 0) {
        lv_style_set_text_font(style, &lv_font_montserrat_46);
#endif
#if LV_FONT_MONTSERRAT_48
    } else if (strcasecmp(value, "montserrat 48") == 0) {
        lv_style_set_text_font(style, &lv_font_montserrat_48);
#endif
#if LV_FONT_UNSCII_8
    } else if (strcasecmp(value, "unscii 8") == 0) {
        lv_style_set_text_font(style, &lv_font_unscii_8);
#endif
#if LV_FONT_UNSCII_16
    } else if (strcasecmp(value, "unscii 16") == 0) {
        lv_style_set_text_font(style, &lv_font_unscii_16);
#endif
    } else if (strcasecmp(value, "sony 14") == 0) {
        lv_style_set_text_font(style, &sony_14);
    } else if (strcasecmp(value, "sony 16") == 0) {
        lv_style_set_text_font(style, &sony_16);
    } else if (strcasecmp(value, "sony 18") == 0) {
        lv_style_set_text_font(style, &sony_18);
    } else if (strcasecmp(value, "sony 20") == 0) {
        lv_style_set_text_font(style, &sony_20);
    } else if (strcasecmp(value, "sony 22") == 0) {
        lv_style_set_text_font(style, &sony_22);
    } else if (strcasecmp(value, "sony 24") == 0) {
        lv_style_set_text_font(style, &sony_24);
    } else if (strcasecmp(value, "sony 26") == 0) {
        lv_style_set_text_font(style, &sony_26);
    } else if (strcasecmp(value, "sony 28") == 0) {
        lv_style_set_text_font(style, &sony_28);
    } else if (strcasecmp(value, "sony 30") == 0) {
        lv_style_set_text_font(style, &sony_30);
    } else if (strcasecmp(value, "sony 32") == 0) {
        lv_style_set_text_font(style, &sony_32);
    } else if (strcasecmp(value, "sony 34") == 0) {
        lv_style_set_text_font(style, &sony_34);
    } else if (strcasecmp(value, "sony 36") == 0) {
        lv_style_set_text_font(style, &sony_36);
    } else if (strcasecmp(value, "sony 38") == 0) {
        lv_style_set_text_font(style, &sony_38);
    } else if (strcasecmp(value, "sony 40") == 0) {
        lv_style_set_text_font(style, &sony_40);
    } else if (strcasecmp(value, "sony 42") == 0) {
        lv_style_set_text_font(style, &sony_42);
    } else if (strcasecmp(value, "sony 44") == 0) {
        lv_style_set_text_font(style, &sony_44);
    } else if (strcasecmp(value, "sony 60") == 0) {
        lv_style_set_text_font(style, &sony_60);
    } else {
        return -1;
    }
    return 0;
}

void styles_customize_theme() {
    if (styles_custom_begin(STYLES_CUSTOM_FILE) == -1)
        return;
    const char * key;
    const char * value;
    int status;
    while (styles_custom_iter(&key, &value) != EOF) {
        if (strcmp(key, "wf.palette") == 0) {
            status = set_custom_palette((char *) value, &wf_palette);
        } else if (strcmp(key, "bg_color") == 0) {
            status = set_custom_bg_color(value, &bg_color, &background_style);
        } else if (strcmp(key, "btn.bg_img") == 0) {
            status = set_custom_bg_img_src(value, &btn_style);
        } else if (strcmp(key, "msg.bg_img") == 0) {
            status = set_custom_bg_img_src(value, &msg_style);
        } else if (strcmp(key, "clock.bg_img") == 0) {
            status = set_custom_bg_img_src(value, &clock_style);
        } else if (strcmp(key, "clock.width") == 0) {
            status = set_custom_width(value, &clock_style);
        } else if (strcmp(key, "clock.height") == 0) {
            status = set_custom_height(value, &clock_style);
        } else if (strcmp(key, "info.bg_img") == 0) {
            status = set_custom_bg_img_src(value, &info_style);
        } else if (strcmp(key, "info.width") == 0) {
            status = set_custom_width(value, &info_style);
        } else if (strcmp(key, "info.height") == 0) {
            status = set_custom_height(value, &info_style);
        } else if (strcmp(key, "meter.bg_img") == 0) {
            status = set_custom_bg_img_src(value, &meter_style);
        } else if (strcmp(key, "meter.width") == 0) {
            status = set_custom_width(value, &meter_style);
        } else if (strcmp(key, "meter.height") == 0) {
            status = set_custom_height(value, &meter_style);
        } else if (strcmp(key, "panel.bg_img") == 0) {
            status = set_custom_bg_img_src(value, &pannel_style);
        } else if (strcmp(key, "msg_tiny.bg_img") == 0) {
            status = set_custom_bg_img_src(value, &msg_tiny_style);
        } else if (strcmp(key, "dialog.bg_img") == 0) {
            status = set_custom_bg_img_src(value, &dialog_style);
        } else if (strcmp(key, "tx_info.bg_img") == 0) {
            status = set_custom_bg_img_src(value, &tx_info_style);
        } else if (strcmp(key, "tx_info.width") == 0) {
            status = set_custom_width(value, &tx_info_style);
        } else if (strcmp(key, "tx_info.height") == 0) {
            status = set_custom_height(value, &tx_info_style);
        } else if (strcmp(key, "freq.font") == 0) {
            status = set_custom_text_font(value, &freq_style);
        } else if (strcmp(key, "freq_main.font") == 0) {
            status = set_custom_text_font(value, &freq_main_style);
        } else if (strcmp(key, "btn.font") == 0) {
            status = set_custom_text_font(value, &btn_style);
        } else if (strcmp(key, "clock.font") == 0) {
            status = set_custom_text_font(value, &clock_style);
        } else if (strcmp(key, "info.font") == 0) {
            status = set_custom_text_font(value, &info_style);
        } else {
            fprintf(stderr, "Unknown key %s\n", key);
        }
        if (status != 0) {
            fprintf(stderr, "Invalid value for key %s -> %s\n", key, value);
        }
    }
    styles_custom_end();
}
