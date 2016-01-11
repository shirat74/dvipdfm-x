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

#ifndef _PDF_DRAW_H_
#define _PDF_DRAW_H_

#include "pdfcolor.h"
#include "pdfdev.h"

#define  PDF_DASH_SIZE_MAX  16
#define  PDF_GSAVE_MAX      256

extern void  pdf_dev_init_gstates  (void);
extern void  pdf_dev_clear_gstates (void);

#define pdf_copymatrix(m,n) do {\
  (m)->a = (n)->a; (m)->b = (n)->b;\
  (m)->c = (n)->c; (m)->d = (n)->d;\
  (m)->e = (n)->e; (m)->f = (n)->f;\
} while (0)

#define pdf_setmatrix(m,p,q,r,s,t,u) do {\
  (m)->a = (p); (m)->b = (q);\
  (m)->c = (r); (m)->d = (s);\
  (m)->e = (t); (m)->f = (u);\
} while (0)

/* m -> n x m */
#define pdf_concatmatrix(m,n) do {\
  double _tmp_a, _tmp_b, _tmp_c, _tmp_d; \
  _tmp_a = (m)->a; _tmp_b = (m)->b; \
  _tmp_c = (m)->c; _tmp_d = (m)->d; \
  (m)->a  = ((n)->a) * _tmp_a + ((n)->b) * _tmp_c; \
  (m)->b  = ((n)->a) * _tmp_b + ((n)->b) * _tmp_d; \
  (m)->c  = ((n)->c) * _tmp_a + ((n)->d) * _tmp_c; \
  (m)->d  = ((n)->c) * _tmp_b + ((n)->d) * _tmp_d; \
  (m)->e += ((n)->e) * _tmp_a + ((n)->f) * _tmp_c; \
  (m)->f += ((n)->e) * _tmp_b + ((n)->f) * _tmp_d; \
} while (0)

typedef struct pdf_path_ pdf_path;

extern int    pdf_dev_currentmatrix (pdf_doc *p, pdf_tmatrix *M);
extern int    pdf_dev_currentpoint  (pdf_doc *p, pdf_coord *cp);

extern int    pdf_dev_setlinewidth  (pdf_doc *p, double  width);
extern int    pdf_dev_setmiterlimit (pdf_doc *p, double  mlimit);
extern int    pdf_dev_setlinecap    (pdf_doc *p, int     style);
extern int    pdf_dev_setlinejoin   (pdf_doc *p, int     style);
extern int    pdf_dev_setdash       (pdf_doc *p, int     count,
                                     double *pattern,
                                     double  offset);
#if 0
extern int    pdf_dev_setflat       (pdf_doc *p, int     flatness);
#endif

/* Path Construction */
extern int    pdf_dev_moveto        (pdf_doc *p, double x , double y);
extern int    pdf_dev_rmoveto       (pdf_doc *p, double x , double y);
extern int    pdf_dev_closepath     (pdf_doc *p);

extern int    pdf_dev_lineto        (pdf_doc *p, double x0 , double y0);
extern int    pdf_dev_rlineto       (pdf_doc *p, double x0 , double y0);
extern int    pdf_dev_curveto       (pdf_doc *p, double x0 , double y0,
                                     double x1 , double y1,
                                     double x2 , double y2);
extern int    pdf_dev_vcurveto      (pdf_doc *p, double x0 , double y0,
                                     double x1 , double y1);
extern int    pdf_dev_ycurveto      (pdf_doc *p, double x0 , double y0,
                                     double x1 , double y1);
extern int    pdf_dev_rcurveto      (pdf_doc *p, double x0 , double y0,
                                     double x1 , double y1,
                                     double x2 , double y2);
extern int    pdf_dev_arc           (pdf_doc *p,
                                     double c_x, double c_y, double r,
                                     double a_0, double a_1);
extern int    pdf_dev_arcn          (pdf_doc *p,
                                     double c_x, double c_y, double r,
                                     double a_0, double a_1);

#define PDF_FILL_RULE_NONZERO 0
#define PDF_FILL_RULE_EVENODD 1

extern int    pdf_dev_newpath       (pdf_doc *p);

/* Path Painting */
extern int    pdf_dev_clip          (pdf_doc *p);
extern int    pdf_dev_eoclip        (pdf_doc *p);

#if 0
extern int    pdf_dev_rectstroke    (double x, double y,
                                     double w, double h,
                                     const pdf_tmatrix *M  /* optional */
                                    );
#endif

extern int    pdf_dev_rectfill      (pdf_doc *p,
                                     double x, double y, double w, double h);
extern int    pdf_dev_rectclip      (pdf_doc *p,
                                     double x, double y, double w, double h);
extern int    pdf_dev_rectadd       (pdf_doc *p,
                                     double x, double y, double w, double h);

extern int    pdf_dev_flushpath     (pdf_doc *p, char p_op, int fill_rule);

extern int    pdf_dev_concat        (pdf_doc *p, const pdf_tmatrix *M);
/* NULL pointer of M mean apply current transformation */
extern void   pdf_dev_dtransform    (pdf_doc   *p,
                                     pdf_coord *c, const pdf_tmatrix *M);
extern void   pdf_dev_idtransform   (pdf_doc   *p,
                                     pdf_coord *c, const pdf_tmatrix *M);
extern void   pdf_dev_transform     (pdf_doc   *p,
                                     pdf_coord *c, const pdf_tmatrix *M);
extern void   pdf_dev_itransform    (pdf_doc   *p,
                                     pdf_coord *c, const pdf_tmatrix *M);

extern int    pdf_dev_gsave         (pdf_doc *p);
extern int    pdf_dev_grestore      (pdf_doc *p);

/* Requires from mpost.c because new MetaPost graphics must initialize
 * the current gstate. */
extern int    pdf_dev_push_gstate (pdf_doc *p);
extern int    pdf_dev_pop_gstate (pdf_doc *p);


/* extension */
extern int    pdf_dev_arcx          (pdf_doc *p,
                                     double c_x, double c_y,
                                     double r_x, double r_y,
                                     double a_0, double a_1,
                                     int    a_d, /* arc direction   */
                                     double xar  /* x-axis-rotation */
                                    );
extern int    pdf_dev_bspline       (pdf_doc *p,
                                     double x0, double y0,
                                     double x1, double y1,
                                     double x2, double y2);


extern void   pdf_invertmatrix      (pdf_tmatrix *M);

/* The depth here is the depth of q/Q nesting.
 * We must remember current depth of nesting when starting a page or xform,
 * and must recover until that depth at the end of page/xform.
 */
extern int    pdf_dev_current_depth (pdf_doc *p);
extern void   pdf_dev_grestore_to   (pdf_doc *p, int depth);

#if 0
extern int    pdf_dev_currentcolor  (pdf_color *color, int is_fill);
#endif

extern void   pdf_dev_set_color     (pdf_doc *p,
                                     const pdf_color *color,
                                     char mask, int force);
#define pdf_dev_set_strokingcolor(p,c)    pdf_dev_set_color(p, c,    0, 0);
#define pdf_dev_set_nonstrokingcolor(p,c) pdf_dev_set_color(p, c, 0x20, 0);

#endif /* _PDF_DRAW_H_ */
