/* This is dvipdfmx, an eXtended version of dvipdfm by Mark A. Wicks.

    Copyright (C) 2002-2015 by Jin-Hwan Cho and Shunsaku Hirata,
    the dvipdfmx project team.

    Copyright (C) 1998, 1999 by Mark A. Wicks <mwicks@kettering.edu>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
*/

#ifndef _PDFDEV_H_
#define _PDFDEV_H_

#include "numbers.h"
#include "pdfobj.h"
#include "pdfcolor.h"

typedef int spt_t;

typedef struct pdf_tmatrix
{
  double a, b, c, d, e, f;
} pdf_tmatrix;

typedef struct pdf_rect
{
  double llx, lly, urx, ury;
} pdf_rect;

typedef struct pdf_coord
{
  double x, y;
} pdf_coord;

/* The name transform_info is misleading.
 * I'll put this here for a moment...
 */
typedef struct
{
  /* Physical dimensions
   *
   * If those values are given, images will be scaled
   * and/or shifted to fit within a box described by
   * those values.
   */
  double      width;
  double      height;
  double      depth;

  pdf_tmatrix matrix; /* transform matrix */
  pdf_rect    bbox;   /* user_bbox */

  int         flags;
} transform_info;
#define INFO_HAS_USER_BBOX (1 << 0)
#define INFO_HAS_WIDTH     (1 << 1)
#define INFO_HAS_HEIGHT    (1 << 2)
#define INFO_DO_CLIP       (1 << 3)
#define INFO_DO_HIDE       (1 << 4)
extern void   transform_info_clear (transform_info *info);

#include "dpxutil.h"
#include "pdfdoc.h"

typedef struct pdf_dev pdf_dev;

extern dpx_stack *gs_stack;

extern void   pdf_dev_set_verbose (void);

/* Not in spt_t. */
extern int pdf_dev_sprint_matrix (pdf_doc *p, char *buf, const pdf_tmatrix *M);
extern int pdf_dev_sprint_rect   (pdf_doc *p, char *buf, const pdf_rect    *r);
extern int pdf_dev_sprint_coord  (pdf_doc *p, char *buf, const pdf_coord   *c);
extern int pdf_dev_sprint_length (pdf_doc *p, char *buf, double value);

extern int pdf_sprint_number (char *buf, double value);

/* unit_conv: multiplier for input unit (spt_t) to bp conversion.
 * precision: How many fractional digits preserved in output (not real
 *            accuracy control).
 * is_bw:     Ignore color related special instructions.
 */
extern pdf_dev *pdf_init_device   (pdf_doc *pdf,
                                   double unit_conv, int precision, int is_bw);
extern void     pdf_close_device  (pdf_dev *p);

/* returns 1.0/unit_conv */
extern double   pdf_dev_unit_dviunit  (pdf_doc *p);

/* Draw texts and rules:
 *
 * xpos, ypos, width, and height are all fixed-point numbers
 * converted to big-points by multiplying unit_conv (dvi2pts).
 * They must be position in the user space.
 *
 * ctype:
 *   0 - input string is in multi-byte encoding.
 *   1 - input string is in 8-bit encoding.
 *   2 - input string is in 16-bit encoding.
 */
extern void   pdf_dev_set_string (pdf_doc *p, spt_t xpos, spt_t ypos,
                                  const void *instr_ptr, int instr_len,
                                  spt_t text_width,
                                  int   font_id, int ctype);
extern void   pdf_dev_set_rule   (pdf_doc *p, spt_t xpos, spt_t ypos,
                                  spt_t width, spt_t height);

/* Place XObject */
extern int    pdf_dev_put_image  (pdf_doc *pdf, int xobj_id,
                                  transform_info *ti,
                                  double ref_x, double ref_y,
                                  pdf_rect *rect); /* optional, ret. value */

/* The design_size and ptsize required by PK font support...
 */
extern int    pdf_dev_locate_font (pdf_doc *p,
                                   const char *font_name, spt_t ptsize);

/* Always returns 1.0, please rename this. */
extern double pdf_dev_scale      (pdf_doc *p);

/* Access text state parameters. */
/* ps: special support want this (pTeX). */
extern int    pdf_dev_get_font_wmode  (pdf_doc *p, int font_id);

/* Text composition (direction) mode
 * This affects only when auto_rotate is enabled.
 */
extern int    pdf_dev_get_dirmode     (pdf_doc *p);
extern void   pdf_dev_set_dirmode     (pdf_doc *p, int dimode);

/* Set rect to rectangle in device space.
 * Unit conversion spt_t to bp and transformation applied within it.
 */
extern void   pdf_dev_set_rect   (pdf_doc *p, pdf_rect *rect,
                                  spt_t x_pos, spt_t y_pos,
                                  spt_t width, spt_t height, spt_t depth);

/* Accessor to various device parameters.
 */
#define PDF_DEV_PARAM_AUTOROTATE  1
#define PDF_DEV_PARAM_COLORMODE   2

extern int    pdf_dev_get_param (pdf_doc *p, int param_type);
extern void   pdf_dev_set_param (pdf_doc *p, int param_type, int value);

/* Text composition mode is ignored (always same as font's
 * writing mode) and glyph rotation is not enabled if
 * auto_rotate is unset.
 */
#define pdf_dev_set_autorotate(p,v) \
  pdf_dev_set_param((p), PDF_DEV_PARAM_AUTOROTATE, (v))

/*
 * For pdf_doc, pdf_draw and others.
 */

/* Force reselecting font and color:
 * XFrom (content grabbing) and Metapost support want them.
 */
extern void   pdf_dev_reset_fonts (pdf_doc *p, int newpage);
extern void   pdf_dev_reset_color (pdf_doc *p, int force);

/* Initialization of transformation matrix with M and others.
 * They are called within pdf_doc_begin_page() and pdf_doc_end_page().
 */
extern void   pdf_dev_bop (pdf_doc *p, const pdf_tmatrix *M);
extern void   pdf_dev_eop (pdf_doc *p);

/* Text is normal and line art is not normal in dvipdfmx. So we don't have
 * begin_text (BT in PDF) and end_text (ET), but instead we have graphics_mode()
 * to terminate text section. pdf_dev_flushpath() and others call this.
 */
extern void   pdf_dev_graphics_mode (pdf_doc *p);

#endif /* _PDFDEV_H_ */
