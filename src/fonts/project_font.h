#ifndef SOUNDBOX_PROJECT_FONT_H
#define SOUNDBOX_PROJECT_FONT_H

#include "lvgl.h"

typedef struct {
    const uint8_t *bitmap;
    const lv_font_fmt_txt_glyph_dsc_t *glyphs;
} soundbox_project_font_data_t;

bool soundbox_project_font_get_glyph(const lv_font_t *font, lv_font_glyph_dsc_t *out,
                                     uint32_t letter, uint32_t next);
const uint8_t *soundbox_project_font_get_bitmap(const lv_font_t *font, uint32_t letter);

#endif
