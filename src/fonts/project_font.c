#include "project_font.h"

bool soundbox_project_font_get_glyph(const lv_font_t *font, lv_font_glyph_dsc_t *out,
                                     uint32_t letter, uint32_t next)
{
    const soundbox_project_font_data_t *data;
    const lv_font_fmt_txt_glyph_dsc_t *glyph;
    (void)next;
    if (!font || !out || !font->dsc || letter < 0x20 || letter > 0x7f) return false;
    data = font->dsc;
    glyph = &data->glyphs[letter - 0x20];
    out->adv_w = glyph->adv_w;
    out->box_w = glyph->box_w;
    out->box_h = glyph->box_h;
    out->ofs_x = glyph->ofs_x;
    out->ofs_y = glyph->ofs_y;
    out->bpp = 4;
    out->is_placeholder = 0;
    return true;
}

const uint8_t *soundbox_project_font_get_bitmap(const lv_font_t *font, uint32_t letter)
{
    const soundbox_project_font_data_t *data;
    if (!font || !font->dsc || letter < 0x20 || letter > 0x7f) return NULL;
    data = font->dsc;
    return data->bitmap + data->glyphs[letter - 0x20].bitmap_index;
}
