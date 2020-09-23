/* This is dvipdfmx, an eXtended version of dvipdfm by Mark A. Wicks.

    Copyright (C) 2002-2020 by Jin-Hwan Cho and Shunsaku Hirata,
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <string.h>
#include <math.h>

#include "system.h"
#include "mem.h"
#include "error.h"
#include "mfileio.h"
#include "numbers.h"
#include "dpxconf.h"

#include "tfm.h"

#include "pdfobj.h"
#include "pdfparse.h"
#include "pdfdev.h"
#include "pdfdoc.h"

#include "pdfcolor.h"
#include "pdfdraw.h"

#include "fontmap.h"
#include "subfont.h"

#include "pdfximage.h"

#include "mpost.h"
#include "mps_op_graph.h"
#include "dvipdfmx.h"

#include "mps_op_graph.h"

#if 1
static int
get_numbers (mpsi *p, double *values, int n)
{
  int error = 0, i;

  if (dpx_stack_depth(&p->stack.operand) < n)
    return -1;
  for (i = 0; !error && i < n; i++) {
    pst_obj *obj = dpx_stack_at(&p->stack.operand, i);
    if (!PST_NUMBERTYPE(obj)) {
      error = -1;
      break;
    }
    values[n-i-1] = pst_getRV(obj);
  }

  return 0;
}

static int
get_numbers_2 (mpsi *p, double *values, int n)
{
  int error = 0, i;

  if (dpx_stack_depth(&p->stack.operand) < n + 1)
    return -1;
  for (i = 1; !error && i < n + 1; i++) {
    pst_obj *obj = dpx_stack_at(&p->stack.operand, i);
    if (!PST_NUMBERTYPE(obj)) {
      error = -1;
      break;
    }
    values[n-i] = pst_getRV(obj);
  }

  return 0;
}

static int
pop_numbers (mpsi *p, int n)
{
  int i;

  if (dpx_stack_depth(&p->stack.operand) < n)
    return -1;
  for (i = 0; i < n; i++) {
    pst_obj *obj = dpx_stack_pop(&p->stack.operand);
    pst_release_obj(obj);
  }

  return 0;
}

static int
mps_cvr_array (mpsi *p, double *values, int n)
{
  pst_obj   *obj;
  pst_array *array;

  if (dpx_stack_depth(&p->stack.operand) < 1)
    return -1;
  obj = dpx_stack_top(&p->stack.operand);
  if (!PST_ARRAYTYPE(obj)) {
    return -1;
  }
  if (pst_length_of(obj) < n) {
    return -1;
  }
  array = obj->data;
  while (n-- > 0) {
    pst_obj *elem = array->values[obj->comp.off+n];
    if (!PST_NUMBERTYPE(elem)) {
      return -1;
    }
    values[n] = pst_getRV(elem);
  }

  return 0;
}

static const char *
mps_current_operator (mpsi *p)
{
  return p->cur_op;
}

static int
mps_push_stack (mpsi *p, pst_obj *obj)
{
  dpx_stack_push(&p->stack.operand, obj);

  return 0;
}
#endif

static int mps_op__p_pathforall_loop (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *proc[4], *path;
  pdf_coord  pt[4];
  int        op, num_coords = 0;

  if (dpx_stack_depth(stk) < 5)
    return -1;
  path    = dpx_stack_at(stk, 0);
  if (!PST_ARRAYTYPE(path))
    return -1;
  proc[3] = dpx_stack_at(stk, 1);
  proc[2] = dpx_stack_at(stk, 2);
  proc[1] = dpx_stack_at(stk, 3);
  proc[0] = dpx_stack_at(stk, 4);
  if (!PST_ARRAYTYPE(proc[0]) || !proc[0]->attr.is_exec ||
      !PST_ARRAYTYPE(proc[1]) || !proc[1]->attr.is_exec ||
      !PST_ARRAYTYPE(proc[2]) || !proc[2]->attr.is_exec ||
      !PST_ARRAYTYPE(proc[3]) || !proc[3]->attr.is_exec)
    return -1;

  if (pst_length_of(path) < 1) {
    int i;
    
    for (i = 0; i < 5; i++) {
      pst_obj *obj = dpx_stack_pop(stk);
      pst_release_obj(obj);
    }
    return 0;
  } else if (pst_length_of(path) % 2) {
    return -1;
  } else {
    pst_array *data = path->data;
    pst_array *vals;
    pst_obj   *v, *c;
    int        i;

    v = data->values[path->comp.off];
    c = data->values[path->comp.off+1];
    if (!PST_ARRAYTYPE(v) || (pst_length_of(v) % 2) != 0 || !PST_INTEGERTYPE(c))
      return -1;
    vals       = v->data;
    num_coords = pst_length_of(v) / 2;
    for (i = 0; i < num_coords; i++) {
      pst_obj *px, *py;
      
      px = vals->values[v->comp.off+2*i];
      py = vals->values[v->comp.off+2*i+1];
      if (!PST_NUMBERTYPE(px) || !PST_NUMBERTYPE(px))
        return -1;
      pt[i].x = pst_getRV(px);
      pt[i].y = pst_getRV(py);
    }
    op = pst_getIV(c);
    if (op != 'm' && op != 'l' && op != 'c' && op != 'h')
      return -1;
  }

  /* OK so far */
  path = dpx_stack_pop(stk);
  path->comp.off  += 2;
  path->comp.size -= 2;

  proc[3] = dpx_stack_pop(stk);
  proc[2] = dpx_stack_pop(stk);
  proc[1] = dpx_stack_pop(stk);
  proc[0] = dpx_stack_pop(stk);

  {
    pst_obj *copy, *cvx, *this;
    int      i;

    this = mps_search_systemdict(p, "%pathforall_loop");
    dpx_stack_push(&p->stack.exec, pst_copy_obj(this));
    dpx_stack_push(&p->stack.exec, pst_copy_obj(path));
    cvx  = mps_search_systemdict(p, "cvx");
    for (i = 0; i < 4; i++) {
      dpx_stack_push(&p->stack.exec, pst_copy_obj(cvx));
      copy = pst_copy_obj(proc[i]);
      copy->attr.is_exec = 0; /* cvlit */
      dpx_stack_push(&p->stack.exec, copy);
    }
  }

  {
    pst_obj *obj = NULL;
    int      i;

    switch (op) {
    case 'm':
      obj = proc[3];
      break;
    case 'l':
      obj = proc[2];
      break;
    case 'c':
      obj = proc[1];
      break;
    case 'h':
      obj = proc[0];
      break;
    }
  
    for (i = 0; i < num_coords; i++) {
      dpx_stack_push(&p->stack.operand, pst_new_real(pt[i].x));
      dpx_stack_push(&p->stack.operand, pst_new_real(pt[i].y));
    }
    WARN("pathforall_loop: %c", op);
    dpx_stack_push(&p->stack.exec, pst_copy_obj(obj));
  }

  pst_release_obj(path);
  pst_release_obj(proc[0]);
  pst_release_obj(proc[1]);
  pst_release_obj(proc[2]);
  pst_release_obj(proc[3]);

  return error;
}

static int mps_op__pathforall (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *proc[4], *path;
  int        num_paths;

  if (dpx_stack_depth(stk) < 4)
    return -1;
  proc[3] = dpx_stack_at(stk, 0);
  proc[2] = dpx_stack_at(stk, 1);
  proc[1] = dpx_stack_at(stk, 2);
  proc[0] = dpx_stack_at(stk, 3);
  if (!PST_ARRAYTYPE(proc[0]) || !proc[0]->attr.is_exec ||
      !PST_ARRAYTYPE(proc[1]) || !proc[1]->attr.is_exec ||
      !PST_ARRAYTYPE(proc[2]) || !proc[2]->attr.is_exec ||
      !PST_ARRAYTYPE(proc[3]) || !proc[3]->attr.is_exec)
    return -1;

  proc[3] = dpx_stack_pop(stk);
  proc[2] = dpx_stack_pop(stk);
  proc[1] = dpx_stack_pop(stk);
  proc[0] = dpx_stack_pop(stk);

  num_paths = pdf_dev_num_path_elem();
  if (num_paths > 0) {
    int        i;
    pst_array *data;
    
    data = NEW(1, pst_array);
    data->link   = 0;
    data->size   = 2 * num_paths;
    data->values = NEW(2 * num_paths, pst_obj *);
    for (i = 0; i < num_paths; i++) {
      pst_array *vals;
      pdf_coord  pt[4];
      int        r, op = 0;
      int        j, num_coords = 0;

      r = pdf_dev_get_path_elem(i, pt, &op);
      if (r)
        break;
      switch (op) {
      case 'm': case 'l':
        num_coords = 1;
        break;
      case 'c':
        num_coords = 3;
        break;
      default:
        num_coords = 0;
      }
      vals = NEW(1, pst_array);
      vals->link   = 0;
      vals->size   = 2 * num_coords;
      vals->values = NEW(2 * num_coords, pst_obj *);
      for (j = 0; j < num_coords; j++) {
        vals->values[2*j]   = pst_new_real(pt[j].x);
        vals->values[2*j+1] = pst_new_real(pt[j].y);
      }
      data->values[2*i]   = pst_new_obj(PST_TYPE_ARRAY, vals);
      data->values[2*i+1] = pst_new_integer(op);
    }
    path = pst_new_obj(PST_TYPE_ARRAY, data);
    {
      pst_obj *copy, *cvx, *this;

      this = mps_search_systemdict(p, "%pathforall_loop");
      dpx_stack_push(&p->stack.exec, pst_copy_obj(this));
      dpx_stack_push(&p->stack.exec, path);
      cvx  = mps_search_systemdict(p, "cvx");
      for (i = 0; i < 4; i++) {
        dpx_stack_push(&p->stack.exec, pst_copy_obj(cvx));
        copy = pst_copy_obj(proc[i]);
        copy->attr.is_exec = 0; /* cvlit */
        dpx_stack_push(&p->stack.exec, copy);
      }
    }
  }

  pst_release_obj(proc[0]);
  pst_release_obj(proc[1]);
  pst_release_obj(proc[2]);
  pst_release_obj(proc[3]);

  return error;
}

static struct mp_font
{
  char   *font_name;
  int     font_id;
  int     tfm_id;     /* Used for text width calculation */
  int     subfont_id;
  double  pt_size;
} font_stack[PDF_GSAVE_MAX] = {
  {NULL, -1, -1, -1, 0}
};
static int currentfont = 0;

#define CURRENT_FONT() ((currentfont < 0) ? NULL : &font_stack[currentfont])
#define FONT_DEFINED(f) ((f) && (f)->font_name && ((f)->font_id >= 0))

static void
clear_mp_font_struct (struct mp_font *font)
{
  ASSERT(font);

  if (font->font_name)
    RELEASE(font->font_name);
  font->font_name  = NULL;
  font->font_id    = -1;
  font->tfm_id     = -1;
  font->subfont_id = -1;
  font->pt_size    = 0.0;
}

static int
mp_setfont (const char *font_name, double pt_size)
{
  const char     *name = font_name;
  struct mp_font *font;
  int             subfont_id = -1;
  fontmap_rec    *mrec;

  font = CURRENT_FONT();

  mrec = pdf_lookup_fontmap_record(font_name);
  if (mrec && mrec->charmap.sfd_name && mrec->charmap.subfont_id) {
    subfont_id = sfd_load_record(mrec->charmap.sfd_name, mrec->charmap.subfont_id);
  }

  /* See comments in dvi_locate_font() in dvi.c. */
  if (mrec && mrec->map_name) {
    name = mrec->map_name;
  } else {
    name = font_name;
  }

  if (font->font_name)
    RELEASE(font->font_name);
  font->font_name  = NEW(strlen(font_name) + 1, char);
  strcpy(font->font_name, font_name);
  font->subfont_id = subfont_id;
  font->pt_size    = pt_size;
  font->tfm_id     = tfm_open(font_name, 0); /* Need not exist in MP mode */
  font->font_id    = pdf_dev_locate_font(name,
                                         (spt_t) (pt_size * dev_unit_dviunit()));

  if (font->font_id < 0) {
    ERROR("MPOST: No physical font assigned for \"%s\".", font_name);
    return 1;
  }

  return  0;
}

static void
save_font (void)
{
  struct mp_font *current, *next;

  current = &font_stack[currentfont++];
  next    = &font_stack[currentfont  ];
  if (FONT_DEFINED(current)) {
    next->font_name = NEW(strlen(current->font_name)+1, char);
    strcpy(next->font_name, current->font_name);
    next->font_id    = current->font_id;
    next->pt_size    = current->pt_size;
    next->subfont_id = current->subfont_id;
    next->tfm_id     = current->tfm_id;    
  } else {
    next->font_name  = NULL;
    next->font_id    = -1;
    next->pt_size    = 0.0;
    next->subfont_id = -1;
    next->tfm_id     = -1;
  }
}

static void
restore_font (void)
{
  struct mp_font *current;

  current = CURRENT_FONT();
  if (current) {
    clear_mp_font_struct(current);
  }

  currentfont--;
}

static void
clear_fonts (void)
{
  while (currentfont > 0) {
    clear_mp_font_struct(&font_stack[currentfont]);
    currentfont--;
  }
}

static int
is_fontname (const char *token)
{
  fontmap_rec *mrec;

  mrec = pdf_lookup_fontmap_record(token);
  if (mrec)
    return  1;

  return  tfm_exists(token);
}

/* PostScript Operators */

/* Acoid conflict with SET... from <wingdi.h>.  */
#undef SETLINECAP
#undef SETLINEJOIN
#undef SETMITERLIMIT
#undef TRANSFORM

#define NEWPATH		31
#define CLOSEPATH    	32
#define MOVETO		33
#define RMOVETO         34
#define CURVETO   	35
#define RCURVETO        36
#define LINETO		37
#define RLINETO		38
#define ARC             39
#define ARCN            40

#define FILL		41
#define STROKE		42
#define SHOW		43

#define CLIP         	44
#define EOCLIP         	45

#define SHOWPAGE	49

#define GSAVE		50
#define GRESTORE	51

#define CONCAT       	52
#define SCALE		53
#define TRANSLATE	54
#define ROTATE          55

#define SETLINEWIDTH	60
#define SETDASH		61
#define SETLINECAP 	62
#define SETLINEJOIN	63
#define SETMITERLIMIT	64

#define SETGRAY		70
#define SETRGBCOLOR	71
#define SETCMYKCOLOR	72

#define CURRENTPOINT    80
#define IDTRANSFORM	     81
#define DTRANSFORM	     82
#define TRANSFORM        83
#define ITRANSFORM       84
#define CURRENTMATRIX    85
#define MATRIX           86
#define SETMATRIX        87

#define FINDFONT        201
#define SCALEFONT       202
#define SETFONT         203
#define CURRENTFONT     204

#define STRINGWIDTH     210

#define CURRENTFLAT     900
#define SETFLAT         901
#define CLIPPATH        902
#define CURRENTLINEWIDTH 903
#define PATHBBOX         904
#define FLATTENPATH      905

#define DEF             999

#define FSHOW		1001
#define STEXFIG         1002
#define ETEXFIG         1003
#define HLW             1004
#define VLW             1005
#define RD              1006
#define B               1007

static struct operators 
{
  const char *token;
  int         opcode;
} ps_operators[] = {
  {"clip",         CLIP},
  {"eoclip",       EOCLIP},
  {"closepath",    CLOSEPATH},
  {"concat",       CONCAT},

  {"newpath",      NEWPATH},
  {"moveto",       MOVETO},
  {"rmoveto",      RMOVETO},
  {"lineto",       LINETO},
  {"rlineto",      RLINETO},
  {"curveto",      CURVETO},
  {"rcurveto",     RCURVETO},
  {"arc",          ARC},
  {"arcn",         ARCN},

  {"stroke",       STROKE},  
  {"fill",         FILL},
  {"show",         SHOW},
  {"showpage",     SHOWPAGE},

  {"gsave",        GSAVE},
  {"grestore",     GRESTORE},
  {"translate",    TRANSLATE},
  {"rotate",       ROTATE},
  {"scale",        SCALE},

  {"setlinecap",    SETLINECAP},
  {"setlinejoin",   SETLINEJOIN},
  {"setlinewidth",  SETLINEWIDTH},
  {"setmiterlimit", SETMITERLIMIT},
  {"setdash",       SETDASH},

  {"setgray",      SETGRAY},
  {"setrgbcolor",  SETRGBCOLOR},
  {"setcmykcolor", SETCMYKCOLOR},

  {"matrix",        MATRIX},
  {"setmatrix",     SETMATRIX},
  {"currentpoint",  CURRENTPOINT},
  {"currentmatrix", CURRENTMATRIX},
  {"dtransform",    DTRANSFORM},
  {"idtransform",   IDTRANSFORM},
  {"transform",     TRANSFORM},
  {"itransform",    ITRANSFORM},

  {"findfont",     FINDFONT},
  {"scalefont",    SCALEFONT},
  {"setfont",      SETFONT},
  {"currentfont",  CURRENTFONT},

  {"stringwidth",  STRINGWIDTH},

  {"flattenpath",  FLATTENPATH},
#if 1
  /* NYI */
  {"currentflat",      CURRENTFLAT},
  {"setflat",          SETFLAT},
  {"currentlinewidth", CURRENTFLAT},
  {"clippath",         CLIPPATH},
  {"initclip",         CLIPPATH},
  {"pathbbox",         PATHBBOX},
#endif
};

static struct operators mps_operators[] = {
  {"fshow",       FSHOW}, /* exch findfont exch scalefont setfont show */
  {"startTexFig", STEXFIG},
  {"endTexFig",   ETEXFIG},
  {"hlw",         HLW}, /* 0 dtransform exch truncate exch idtransform pop setlinewidth */
  {"vlw",         VLW}, /* 0 exch dtransform truncate idtransform pop setlinewidth pop */
  {"l",           LINETO},
  {"r",           RLINETO},
  {"c",           CURVETO},
  {"m",           MOVETO},
  {"p",           CLOSEPATH},
  {"n",           NEWPATH},
  {"C",           SETCMYKCOLOR},
  {"G",           SETGRAY},
  {"R",           SETRGBCOLOR},
  {"lj",          SETLINEJOIN},
  {"ml",          SETMITERLIMIT},
  {"lc",          SETLINECAP},
  {"S",           STROKE},
  {"F",           FILL},
  {"q",           GSAVE},
  {"Q",           GRESTORE},
  {"s",           SCALE},
  {"t",           CONCAT},
  {"sd",          SETDASH},
  {"rd",          RD}, /* [] 0 setdash */
  {"P",           SHOWPAGE},
  {"B",           B}, /* gsave fill grestore */
  {"W",           CLIP}
};

#define NUM_PS_OPERATORS  (sizeof(ps_operators)/sizeof(ps_operators[0]))
#define NUM_MPS_OPERATORS (sizeof(mps_operators)/sizeof(mps_operators[0]))
static int
get_opcode (const char *token)
{
  int   i;

  for (i = 0; i < NUM_PS_OPERATORS; i++) {
    if (!strcmp(token, ps_operators[i].token)) {
      return ps_operators[i].opcode;
    }
  }

  for (i = 0; i < NUM_MPS_OPERATORS; i++) {
    if (!strcmp(token, mps_operators[i].token)) {
      return mps_operators[i].opcode;
    }
  }

  return -1;
}


/*
 * CTM(Current Transformation Matrix) means the transformation of User Space
 * to Device Space coordinates. Because DVIPDFMx does not know the resolution
 * of Device Space, we assume that the resolution is 1/1000.
 */
#define DEVICE_RESOLUTION 1000
static int
ps_dev_CTM (pdf_tmatrix *M)
{
  pdf_dev_currentmatrix(M);
#if 0
  /* FIXME Don't know why... */
  M->a *= DEVICE_RESOLUTION; M->b *= DEVICE_RESOLUTION;
  M->c *= DEVICE_RESOLUTION; M->d *= DEVICE_RESOLUTION;
  M->e *= DEVICE_RESOLUTION; M->f *= DEVICE_RESOLUTION;
#endif

  return 0;
}

/*
 * Again, the only piece that needs x_user and y_user is
 * that piece dealing with texfig.
 */
static int
do_operator (mpsi *p, const char *token, double x_user, double y_user)
{
  int         error  = 0;
  int         opcode = 0;
  double      values[12];
  pdf_tmatrix matrix;
  pdf_coord   cp;
  pdf_color   color;

  opcode = get_opcode(token);

  switch (opcode) {

#if 1
  /* NYI */
  case CURRENTLINEWIDTH:
    mps_push_stack(p, pst_new_integer(1));
    break;
  case CURRENTFLAT:
    mps_push_stack(p, pst_new_integer(1));
    break;
  case SETFLAT:
    error = get_numbers(p, values, 1);
    if (!error)
      error = pdf_dev_setflat(values[0]);
    if (!error)
      error = pop_numbers(p, 1);
    break;
  case CLIPPATH:
    break;
#endif

  case FLATTENPATH:
    pdf_dev_flattenpath();
    break;

  case PATHBBOX:
    {
      pdf_rect r;

      error = pdf_dev_pathbbox(&r);
      if (!error) {
        dpx_stack_push(&p->stack.operand, pst_new_real(r.llx));
        dpx_stack_push(&p->stack.operand, pst_new_real(r.lly));
        dpx_stack_push(&p->stack.operand, pst_new_real(r.urx));
        dpx_stack_push(&p->stack.operand, pst_new_real(r.ury));
      }
    }
    break;

  case MATRIX:
    {
      pst_obj   *obj;
      pst_array *data;
    
      data = NEW(1, pst_array);
      data->size = 6;
      data->link = 0;
      data->values = NEW(6, pst_obj *);
      data->values[0] = pst_new_real(1.0);
      data->values[1] = pst_new_real(0.0);
      data->values[2] = pst_new_real(0.0);
      data->values[3] = pst_new_real(1.0);
      data->values[4] = pst_new_real(0.0);
      data->values[5] = pst_new_real(0.0);
      obj = pst_new_obj(PST_TYPE_ARRAY, data);
      dpx_stack_push(&p->stack.operand, obj);
    }
    break;
  case CURRENTMATRIX:
    if (dpx_stack_depth(&p->stack.operand) < 1) {
      error = -1;
    } else {
      pst_obj *obj = dpx_stack_top(&p->stack.operand);
      if (!PST_ARRAYTYPE(obj) || pst_length_of(obj) != 6) {
        error = -1;
      } else {
        pst_array   *array;
        pdf_tmatrix  M;
        int          i;

        obj = dpx_stack_pop(&p->stack.operand);
        array = obj->data;
        pdf_dev_currentmatrix(&M);
        for (i = 0; i < 6; i++) {
          if (array->values[i])
            pst_release_obj(array->values[i]);
        }
        array->values[0] = pst_new_real(M.a);
        array->values[1] = pst_new_real(M.b);
        array->values[2] = pst_new_real(M.c);
        array->values[3] = pst_new_real(M.d);
        array->values[4] = pst_new_real(M.e);
        array->values[5] = pst_new_real(M.f);
      }
      dpx_stack_push(&p->stack.operand, obj);
    }
    break;
  case SETMATRIX:
    if (dpx_stack_depth(&p->stack.operand) < 1) {
      error = -1; /* stackunderflow */
    } else {
      pst_obj *obj = dpx_stack_top(&p->stack.operand);
      if (!PST_ARRAYTYPE(obj)) {
        error = -1; /* typecheck */
      } else if (pst_length_of(obj) != 6) {
        error = -1; /* rangecheck */
      } else {
        pst_array *data = obj->data;
        int        i, n;
        double     v[6];

        n = obj->comp.off;
        for (i = 0; i < 6 && !error; i++) {
          pst_obj *elem = data->values[n+i];
          if (!elem || !PST_NUMBERTYPE(elem)) {
            error = -1; /* typecheck */
          } else {
            v[i] = pst_getRV(elem);
          }
        }
        if (!error) {
          /* FIXME */
          pdf_tmatrix M;

          /* NYI: should implement pdf_dev_setmatrix */
          pdf_setmatrix(&matrix, v[0], v[1], v[2], v[3], v[4], v[5]);
          pdf_dev_currentmatrix(&M);
          pdf_invertmatrix(&M);
          pdf_dev_concat(&M);
          pdf_dev_concat(&matrix);
          obj = dpx_stack_pop(&p->stack.operand);
          pst_release_obj(obj);
        }
      }
    }
    break;
  
    /* Path construction */
  case MOVETO:
    error = get_numbers(p, values, 2);
    if (!error)
      error = pdf_dev_moveto(values[0], values[1]);
    if (!error)
      error = pop_numbers(p, 2);
    break;
  case RMOVETO:
    error = get_numbers(p, values, 2);
    if (!error)
      error = pdf_dev_rmoveto(values[0], values[1]);
    if (!error)
      error = pop_numbers(p, 2);
    break;
  case LINETO:
    error = get_numbers(p, values, 2);
    if (!error)
      error = pdf_dev_lineto(values[0], values[1]);
    if (!error)
      error = pop_numbers(p, 2);
    break;
  case RLINETO:
    error = get_numbers(p, values, 2);
    if (!error)
      error = pdf_dev_rlineto(values[0], values[1]);
    if (!error)
      error = pop_numbers(p, 2);
    break;
  case CURVETO:
    error = get_numbers(p, values, 6);
    if (!error)
      error = pdf_dev_curveto(values[0], values[1], values[2], values[3], values[4], values[5]);
    if (!error)
      error = pop_numbers(p, 6);
    break;
  case RCURVETO:
    error = get_numbers(p, values, 6);
    if (!error)
      error = pdf_dev_rcurveto(values[0], values[1], values[2], values[3], values[4], values[5]);
    if (!error)
      error = pop_numbers(p, 6);
    break;
  case CLOSEPATH:
    error = pdf_dev_closepath();
    break;
  case ARC:
    error = get_numbers(p, values, 5);
    if (!error)
      error = pdf_dev_arc(values[0], values[1],
			                    values[2], /* rad */
			                    values[3], values[4]);
    if (!error)
      error = pop_numbers(p, 5);
    break;
  case ARCN:
    error = get_numbers(p, values, 5);
    if (!error)
      error = pdf_dev_arcn(values[0], values[1],
			                     values[2], /* rad */
			                     values[3], values[4]);
    if (!error)
      error = pop_numbers(p, 5);
    break;
    
  case NEWPATH:
    pdf_dev_newpath();
    break;
  case STROKE:
    /* fill rule not supported yet */
    /* pdf_dev_flattenpath(); */
    pdf_dev_flushpath('S', PDF_FILL_RULE_NONZERO);
    break;
  case FILL:
    pdf_dev_flushpath('f', PDF_FILL_RULE_NONZERO);
    break;

  case CLIP:
    error = pdf_dev_clip();
    break;
  case EOCLIP:
    error = pdf_dev_eoclip();
    break;

    /* Graphics state operators: */
  case GSAVE:
    error = pdf_dev_gsave();
    save_font();
    break;
  case GRESTORE:
    error = pdf_dev_grestore();
    restore_font();
    break;

  case CONCAT:
    error = mps_cvr_array(p, values, 6);
    if (error)
      WARN("Missing array before \"concat\".");
    else {
      pdf_setmatrix(&matrix, values[0], values[1], values[2], values[3], values[4], values[5]);
      error = pdf_dev_concat(&matrix);
    }
    if (!error)
      pop_numbers(p, 1); /* not a number */
    break;
  case SCALE:
    error = get_numbers(p, values, 2);
    if (!error) {
      switch (p->compat_mode) {
#ifndef WITHOUT_ASCII_PTEX
      case MP_CMODE_PTEXVERT:
        pdf_setmatrix(&matrix, values[1], 0.0, 0.0, values[0], 0.0, 0.0);
	break;
#endif /* !WITHOUT_ASCII_PTEX */
      default:
        pdf_setmatrix(&matrix, values[0], 0.0, 0.0, values[1], 0.0, 0.0);
        break;
      }
      error = pdf_dev_concat(&matrix);
    }
    if (!error)
      error = pop_numbers(p, 2);
    break;
    /* Positive angle means clock-wise direction in graphicx-dvips??? */
  case ROTATE:
    error = get_numbers(p, values, 1);
    if (!error) {
      values[0] = values[0] * M_PI / 180;

      switch (p->compat_mode) {
      case MP_CMODE_DVIPSK:
      case MP_CMODE_MPOST: /* Really? */
#ifndef WITHOUT_ASCII_PTEX
      case MP_CMODE_PTEXVERT:
#endif /* !WITHOUT_ASCII_PTEX */
        pdf_setmatrix(&matrix, cos(values[0]), -sin(values[0]), sin(values[0]), cos(values[0]), 0.0, 0.0);
        break;
      default:
        pdf_setmatrix(&matrix, cos(values[0]), sin(values[0]), -sin(values[0]), cos(values[0]), 0.0, 0.0);
        break;
      }
      error = pdf_dev_concat(&matrix);
    }
    if (!error)
      error = pop_numbers(p, 1);
    break;
  case TRANSLATE:
    error = get_numbers(p, values, 2);
    if (!error) {
      pdf_setmatrix(&matrix, 1.0, 0.0, 0.0, 1.0, values[0], values[1]);
      error = pdf_dev_concat(&matrix);
    }
    if (!error)
      pop_numbers(p, 2);
    break;

  case SETDASH:
    error = get_numbers(p, values, 1);
    if (!error) {
      pst_obj   *pattern;
      pst_array *data;
      int        i, n;
      double    *dvalues;
      double     offset;

      offset  = values[0];
      pattern = dpx_stack_at(&p->stack.operand, 1);
      if (!PST_ARRAYTYPE(pattern)) {
	      error = -1; /* typecheck */
	      break;
      }
      n       = pst_length_of(pattern);
      data    = pattern->data;
      dvalues = NEW(n, double); 
      for (i = pattern->comp.off; !error && i < pattern->comp.off + pattern->comp.size; i++) {
        pst_obj *dash = data->values[i];
        if (!PST_NUMBERTYPE(dash)) {
          error = -1; /* typecheck */
        } else {
          dvalues[i] = pst_getRV(dash);
        }
      }
      if (!error) {
        error = pdf_dev_setdash(n, dvalues, offset);
      }
      RELEASE(dvalues);
    }
    if (!error)
      error = pop_numbers(p, 2); /* actually not number */
    break;
  case SETLINECAP:
    error = get_numbers(p, values, 1);
    if (!error)
      error = pdf_dev_setlinecap((int)values[0]);
    if (!error)
      error = pop_numbers(p, 1);
    break;
  case SETLINEJOIN:
    error = get_numbers(p, values, 1);
    if (!error)
      error = pdf_dev_setlinejoin((int)values[0]);
    if (!error)
      error = pop_numbers(p, 1);
    break;
  case SETLINEWIDTH:
    error = get_numbers(p, values, 1);
    if (!error)
      error = pdf_dev_setlinewidth(values[0]);
    if (!error)
      error = pop_numbers(p, 1);
    break;
  case SETMITERLIMIT:
    error = get_numbers(p, values, 1);
    if (!error)
      error = pdf_dev_setmiterlimit(values[0]);
    if (!error)
      error = pop_numbers(p, 1);
    break;

  case SETCMYKCOLOR:
    error = get_numbers(p, values, 4);
    /* Not handled properly */
    if (!error) {
      pdf_color_cmykcolor(&color, values[0], values[1], values[2], values[3]);
      pdf_dev_set_strokingcolor(&color);
      pdf_dev_set_nonstrokingcolor(&color);
    }
    if (!error)
      pop_numbers(p, 4);
    break;
  case SETGRAY:
    /* Not handled properly */
    error = get_numbers(p, values, 1);
    if (!error) {
      pdf_color_graycolor(&color, values[0]);
      pdf_dev_set_strokingcolor(&color);
      pdf_dev_set_nonstrokingcolor(&color);
    }
    if (!error)
      pop_numbers(p, 1);
    break;
  case SETRGBCOLOR:
    error = get_numbers(p, values, 3);
    if (!error) {
      pdf_color_rgbcolor(&color, values[0], values[1], values[2]);
      pdf_dev_set_strokingcolor(&color);
      pdf_dev_set_nonstrokingcolor(&color);
    }
    if (!error)
      pop_numbers(p, 3);
    break;

  case SHOWPAGE: /* Let's ignore this for now */
    break;

  case CURRENTPOINT:
    error = pdf_dev_currentpoint(&cp);
    if (!error) {
      mps_push_stack(p, pst_new_real(cp.x));
      mps_push_stack(p, pst_new_real(cp.y));
    }
    break;

  case DTRANSFORM:
    {
      int      has_matrix = 0;
      pst_obj *obj = dpx_stack_top(&p->stack.operand);
      if (PST_ARRAYTYPE(obj)) {
	      error = mps_cvr_array(p, values, 6);
	      if (error)
	        break;
	      pdf_setmatrix(&matrix, values[0], values[1], values[2], values[3],  values[4], values[5]);
	      has_matrix = 1;
      }
      if (has_matrix) {
        error = get_numbers_2(p, values, 2);
      } else {
        error = get_numbers(p, values, 2);
      }
      if (error)
        break;
      cp.y = values[1];
      cp.x = values[0];

      if (!has_matrix) {
      	ps_dev_CTM(&matrix); /* Here, we need real PostScript CTM */
      }
      pdf_dev_dtransform(&cp, &matrix);
      mps_push_stack(p, pst_new_real(cp.x));
      mps_push_stack(p, pst_new_real(cp.y));

      if (!error)
        pop_numbers(p, has_matrix ? 3 : 2);
    }
    break;

  case IDTRANSFORM:
    {
      int      has_matrix = 0;
      pst_obj *obj = dpx_stack_top(&p->stack.operand);
      if (PST_ARRAYTYPE(obj)) {
	      error = mps_cvr_array(p, values, 6);
	      if (error)
	        break;
	      pdf_setmatrix(&matrix, values[0], values[1], values[2], values[3],  values[4], values[5]);
	      has_matrix = 1;
      }
      if (has_matrix) {
        error = get_numbers_2(p, values, 2);
      } else {
        error = get_numbers(p, values, 2);
      }
      if (error)
        break;
      cp.y = values[1];
      cp.x = values[0];

      if (!has_matrix) {
      	ps_dev_CTM(&matrix); /* Here, we need real PostScript CTM */
      }
      pdf_dev_idtransform(&cp, &matrix);
      mps_push_stack(p, pst_new_real(cp.x));
      mps_push_stack(p, pst_new_real(cp.y));
      
      if (!error)
        pop_numbers(p, has_matrix ? 3 : 2);
    }
    break;

  case TRANSFORM:
    {
      int      has_matrix = 0;
      pst_obj *obj = dpx_stack_top(&p->stack.operand);
      if (PST_ARRAYTYPE(obj)) {
	      error = mps_cvr_array(p, values, 6);
	      if (error)
	        break;
	      pdf_setmatrix(&matrix, values[0], values[1], values[2], values[3],  values[4], values[5]);
	      has_matrix = 1;
      }
      if (has_matrix) {
        error = get_numbers_2(p, values, 2);
      } else {
        error = get_numbers(p, values, 2);
      }
      if (error)
        break;
      cp.y = values[1];
      cp.x = values[0];

      if (!has_matrix) {
      	ps_dev_CTM(&matrix); /* Here, we need real PostScript CTM */
      }
      pdf_dev_transform(&cp, &matrix);
      mps_push_stack(p, pst_new_real(cp.x));
      mps_push_stack(p, pst_new_real(cp.y));
      
      if (!error)
        pop_numbers(p, has_matrix ? 3 : 2);
    }
    break;

  case ITRANSFORM:
    {
      int      has_matrix = 0;
      pst_obj *obj = dpx_stack_top(&p->stack.operand);
      if (PST_ARRAYTYPE(obj)) {
	      error = mps_cvr_array(p, values, 6);
	      if (error)
	        break;
	      pdf_setmatrix(&matrix, values[0], values[1], values[2], values[3],  values[4], values[5]);
	      has_matrix = 1;
      }
      if (has_matrix) {
        error = get_numbers_2(p, values, 2);
      } else {
        error = get_numbers(p, values, 2);
      }
      if (error)
        break;
      cp.y = values[1];
      cp.x = values[0];

      if (!has_matrix) {
      	ps_dev_CTM(&matrix); /* Here, we need real PostScript CTM */
      }
      pdf_dev_itransform(&cp, &matrix);
      mps_push_stack(p, pst_new_real(cp.x));
      mps_push_stack(p, pst_new_real(cp.y));
      
      if (!error)
        pop_numbers(p, has_matrix ? 3 : 2);
    }
    break;

#if 1
  case FINDFONT:
    {
      pst_obj *obj = dpx_stack_pop(&p->stack.operand);
      pst_release_obj(obj);
      dpx_stack_push(&p->stack.operand, pst_new_null());
    }
    break;
  case SETFONT:
    {
      pst_obj *obj = dpx_stack_pop(&p->stack.operand);
      pst_release_obj(obj);
    }
    break;
  case SCALEFONT:
    {
      pst_obj *obj1, *obj2;
      obj1 = dpx_stack_pop(&p->stack.operand);
      pst_release_obj(obj1);
      obj2 = dpx_stack_pop(&p->stack.operand);
      pst_release_obj(obj2);
      dpx_stack_push(&p->stack.operand, pst_new_null());
    }
    break;
  case CURRENTFONT:
    {
      dpx_stack_push(&p->stack.operand, pst_new_null());
    }
    break;
  case STRINGWIDTH:
    {
      pst_obj *obj;
      obj = dpx_stack_pop(&p->stack.operand);
      pst_release_obj(obj);
      dpx_stack_push(&p->stack.operand, pst_new_real(10.0));
      dpx_stack_push(&p->stack.operand, pst_new_real(10.0));
    }
    break;
  case SHOW:
    {
      pst_obj *obj;
      obj = dpx_stack_pop(&p->stack.operand);
      pst_release_obj(obj);
    }
    break;

#else
  case FINDFONT:
    error = do_findfont();
    break;
  case SCALEFONT:
    error = do_scalefont();
    break;
  case SETFONT:
    error = do_setfont();
    break;
  case CURRENTFONT:
    error = do_currentfont();
    break;

  case SHOW:
    error = do_show();
    break;

  case STRINGWIDTH:
    error = 1;
    break;
#endif

#if 0
    /* Extensions */
  case FSHOW:
    error = do_mpost_bind_def("exch findfont exch scalefont setfont show", x_user, y_user);
    break;
  case STEXFIG:
  case ETEXFIG:
    error = do_texfig_operator(opcode, x_user, y_user);
    break;
  case HLW:
    error = do_mpost_bind_def("0 dtransform exch truncate exch idtransform pop setlinewidth", x_user, y_user);
    break;
  case VLW:
    error = do_mpost_bind_def("0 exch dtransform truncate idtransform setlinewidth pop", x_user, y_user);
    break;
  case RD:
    error = do_mpost_bind_def("[] 0 setdash", x_user, y_user);
    break;
  case B:
    error = do_mpost_bind_def("gsave fill grestore", x_user, y_user);
    break;
#endif

  default:
#if 0
    if (is_fontname(token)) {
      PUSH(pdf_new_name(token));
    } else {
      WARN("Unknown token \"%s\"", token);
      error = 1;
    }
#endif
    ERROR("Unknown operator: %s", token);
    break;
  }

  return error;
}

static int mps_op__graphic (mpsi *p)
{
  return do_operator(p, mps_current_operator(p), 0, 0);
}

int mps_op_graph_load (mpsi *p)
{
  int   i;

  for (i = 0; i < NUM_PS_OPERATORS; i++) {
    pst_obj      *obj;
    pst_operator *op;

    op = NEW(1, pst_operator);
    op->name = ps_operators[i].token;
    op->action = (mps_op_fn_ptr) mps_op__graphic;
    obj = pst_new_obj(PST_TYPE_OPERATOR, op);
    obj->attr.is_exec = 1;
    mps_add_systemdict(p, obj);
  }

#if 0
  for (i = 0; i < NUM_MPS_OPERATORS; i++) {
    if (!strcmp(token, mps_operators[i].token)) {
      return mps_operators[i].opcode;
    }
  }
#endif

  {
    pst_obj      *obj;
    pst_operator *op;

    op = NEW(1, pst_operator);
    op->name   = "%pathforall_loop";
    op->action = (mps_op_fn_ptr) mps_op__p_pathforall_loop;
    obj = pst_new_obj(PST_TYPE_OPERATOR, op);
    obj->attr.is_exec = 1;
    mps_add_systemdict(p, obj); 
  }

  {
    pst_obj      *obj;
    pst_operator *op;

    op = NEW(1, pst_operator);
    op->name   = "pathforall";
    op->action = (mps_op_fn_ptr) mps_op__pathforall;
    obj = pst_new_obj(PST_TYPE_OPERATOR, op);
    obj->attr.is_exec = 1;
    mps_add_systemdict(p, obj); 
  }

  return 0;
}
