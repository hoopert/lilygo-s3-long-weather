/*******************************************************************************
 * Size: 11 px
 * Bpp: 4
 * Opts: --font /tmp/tmp.LTx70OPR55/MaterialSymbolsRounded.ttf --range 0xE55D,0xE5D5 --size 11 --bpp 4 --no-compress --format lvgl --lv-include lvgl.h --output /home/user/lilygo-s3-long-weather/firmware/src/ui/fonts/icons_xs.c
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef ICONS_XS
#define ICONS_XS 1
#endif

#if ICONS_XS

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+E55D "" */
    0x0, 0x0, 0x40, 0x0, 0x0, 0x0, 0x3f, 0x30,
    0x0, 0x0, 0xa, 0xaa, 0x0, 0x0, 0x1, 0xd0,
    0xd1, 0x0, 0x0, 0x87, 0x7, 0x80, 0x0, 0xe,
    0x0, 0xe, 0x0, 0x6, 0x95, 0xa5, 0x96, 0x0,
    0xdf, 0xa5, 0xaf, 0xd0, 0x7, 0x10, 0x0, 0x17,
    0x0,

    /* U+E5D5 "" */
    0x0, 0x0, 0x10, 0x0, 0x0, 0x9e, 0xde, 0x6e,
    0x9, 0xa0, 0x1, 0xcf, 0xe, 0x0, 0x1e, 0xff,
    0x2c, 0x0, 0x0, 0x0, 0xe, 0x0, 0x0, 0x5,
    0x8, 0xa0, 0x0, 0xa8, 0x0, 0x8e, 0xde, 0x80,
    0x0, 0x0, 0x10, 0x0
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 176, .box_w = 9, .box_h = 9, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 41, .adv_w = 176, .box_w = 8, .box_h = 9, .ofs_x = 1, .ofs_y = 1}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

static const uint16_t unicode_list_0[] = {
    0x0, 0x78
};

/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 58717, .range_length = 121, .glyph_id_start = 1,
        .unicode_list = unicode_list_0, .glyph_id_ofs_list = NULL, .list_length = 2, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
    }
};



/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LV_VERSION_CHECK(8, 0, 0)
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 1,
    .bpp = 4,
    .kern_classes = 0,
    .bitmap_format = 0,
#if LV_VERSION_CHECK(8, 0, 0)
    .cache = &cache
#endif
};


/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LV_VERSION_CHECK(8, 0, 0)
const lv_font_t icons_xs = {
#else
lv_font_t icons_xs = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 9,          /*The maximum line height required by the font*/
    .base_line = -1,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = 0,
    .underline_thickness = 0,
#endif
    .dsc = &font_dsc           /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
};



#endif /*#if ICONS_XS*/

