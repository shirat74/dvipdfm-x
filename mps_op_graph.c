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

struct mp_font
{
  char   *font_name;
  int     font_id;
  int     tfm_id;     /* Used for text width calculation */
  int     subfont_id;
  double  pt_size;
};

dpx_stack font_stack = {0, NULL, NULL};

#define FONT_DEFINED(f) ((f) && (f)->font_name && ((f)->font_id >= 0))

#if 1
static int
getinterval_number_value (mpsi *p, double *values, int at, int n)
{
  int error = 0, i;

  if (dpx_stack_depth(&p->stack.operand) < n + at)
    return -1;
  for (i = 0; !error && i < n; i++) {
    pst_obj *obj = dpx_stack_at(&p->stack.operand, i + at);
    if (!PST_NUMBERTYPE(obj)) {
      error = -1;
      break;
    }
    values[n-i-1] = pst_getRV(obj);
  }

  return 0;
}

static int
clean_stack (mpsi *p, int n)
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

static int
check_array_matrix_value (pst_obj *obj)
{
  int        i;
  pst_array *data;

  if (!PST_ARRAYTYPE(obj))
    return -1; /* typecheck */
  if (pst_length_of(obj) < 6)
    return -1; /* rangecheck */
  
  data = obj->data;
  for (i = obj->comp.off; i < obj->comp.off + 6; i++) {
    if (!PST_NUMBERTYPE(data->values[i]))
      return -1; /* typecheck */
  }

  return 0;
}

static void
matrix_to_array (pst_obj *obj, const pdf_tmatrix *M)
{
  pst_array *data;
  int        i, n;

  ASSERT(PST_ARRAYTYPE(obj));
  ASSERT(pst_length_of(obj) >= 6);

  data = obj->data;
  n    = obj->comp.off;
  for (i = n; i < n + 6; i++) {
    if (data->values[i])
      pst_release_obj(data->values[i]);
  }
  data->values[n]   = pst_new_real(M->a);
  data->values[n+1] = pst_new_real(M->b);
  data->values[n+2] = pst_new_real(M->c);
  data->values[n+3] = pst_new_real(M->d);
  data->values[n+4] = pst_new_real(M->e);
  data->values[n+5] = pst_new_real(M->f);
  obj->comp.size    = 6;
}

static void
array_to_matrix (pdf_tmatrix *M, pst_obj *obj)
{
  pst_array *data;
  int        n;

  ASSERT(PST_ARRAYTYPE(obj));
  ASSERT(pst_length_of(obj) >= 6);

  data = obj->data;
  n    = obj->comp.off;
  M->a = pst_getRV(data->values[n]);
  M->b = pst_getRV(data->values[n+1]);
  M->c = pst_getRV(data->values[n+2]);
  M->d = pst_getRV(data->values[n+3]);
  M->e = pst_getRV(data->values[n+4]);
  M->f = pst_getRV(data->values[n+5]);
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

static int mps_op__matrix (mpsi *p)
{
  dpx_stack *stk = &p->stack.operand;
  pst_obj   *obj;
  pst_array *data;
  int        i;

  obj  = pst_new_array(6);  
  data = obj->data;
  for (i = 0; i < 6; i++) {
    pst_obj *val = data->values[i];
    if (val)
      pst_release_obj(val);
  }
  data->values[0] = pst_new_real(1.0);
  data->values[1] = pst_new_real(0.0);
  data->values[2] = pst_new_real(0.0);
  data->values[3] = pst_new_real(1.0);
  data->values[4] = pst_new_real(0.0);
  data->values[5] = pst_new_real(0.0);
  
  dpx_stack_push(stk, obj);

  return 0;
}

static int mps_op__currentmatrix (mpsi *p)
{
  dpx_stack   *stk = &p->stack.operand;
  pst_obj     *obj;
  pdf_tmatrix  M;

  if (dpx_stack_depth(stk) < 1)
    return -1; /* stackunderflow */

  obj = dpx_stack_top(stk);
  if (!PST_ARRAYTYPE(obj))
    return -1; /* typecheck */
  if (pst_length_of(obj) < 6)
    return -1; /* rangecheck */

  obj  = dpx_stack_pop(stk);
  pdf_dev_currentmatrix(&M);
  matrix_to_array(obj, &M);
  dpx_stack_push(stk, obj);

  return 0;
}

static int mps_op__setmatrix (mpsi *p)
{
  int          error = 0;
  dpx_stack   *stk   = &p->stack.operand;
  pst_obj     *obj;
  pdf_tmatrix  M, N;

  if (dpx_stack_depth(stk) < 1)
    error = -1; /* stackunderflow */

  obj = dpx_stack_top(&p->stack.operand);
  if (!PST_ARRAYTYPE(obj))
    return -1; /* typecheck */
  if (pst_length_of(obj) < 6)
    return -1; /* rangecheck */
  error = check_array_matrix_value(obj);
  if (error)
    return error;
  array_to_matrix(&N, obj);
  /* NYI: should implement pdf_dev_setmatrix */
  pdf_dev_currentmatrix(&M);
  pdf_invertmatrix(&M);
  pdf_dev_concat(&M);
  pdf_dev_concat(&N);
  clean_stack(p, 1);

  return error;
}  

static int mps_op__concatmatrix (mpsi *p)
{
  int          error       = 0;
  dpx_stack   *stk         = &p->stack.operand;
  pdf_tmatrix  M, N;
  pst_obj     *obj1, *obj2, *obj3;

  if (dpx_stack_depth(stk) < 3)
    return -1;
  
  obj1 = dpx_stack_at(stk, 0);
  obj2 = dpx_stack_at(stk, 1);
  obj3 = dpx_stack_at(stk, 2);
  if (!PST_ARRAYTYPE(obj1))
    return -1; /* typecheck */
  error = check_array_matrix_value(obj2);
  if (error)
    return error;
  error = check_array_matrix_value(obj3);
  if (error)
    return error;

  array_to_matrix(&N, obj2);
  array_to_matrix(&M, obj3);  
  pdf_concatmatrix(&M, &N);
  
  obj1 = dpx_stack_pop(stk);
  matrix_to_array(obj1, &M);

  clean_stack(p, 2);

  dpx_stack_push(stk, obj1);

  return error;
}

static int mps_op__scale (mpsi *p)
{
  int          error       = 0;
  dpx_stack   *stk         = &p->stack.operand;
  pdf_tmatrix  matrix      = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
  int          have_matrix = 0;
  pst_obj     *obj;
  double       values[6] = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
 
  if (dpx_stack_depth(stk) < 2)
    return -1;
  obj = dpx_stack_top(stk);
  if (PST_ARRAYTYPE(obj)) {
    have_matrix = 1;
    error = getinterval_number_value(p, values, 1, 2);
  } else {
    have_matrix = 0;   
    error = getinterval_number_value(p, values, 0, 2);
  }   
  if (error)
    return error;
  
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
  if (!have_matrix) {
    error = pdf_dev_concat(&matrix);
    if (error)
      return error;
  }
  
  if (have_matrix)
    obj = dpx_stack_pop(stk);
  clean_stack(p, 2);
  if (have_matrix) {
    matrix_to_array(obj, &matrix);
    dpx_stack_push(stk, obj);
  }

  return error;
}

static int mps_op__rotate (mpsi *p)
{
  int          error       = 0;
  dpx_stack   *stk         = &p->stack.operand;
  pdf_tmatrix  matrix      = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
  int          have_matrix = 0;
  pst_obj     *obj;
  double       values[6]   = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
 
  if (dpx_stack_depth(stk) < 1)
    return -1;
  obj = dpx_stack_top(stk);
  if (PST_ARRAYTYPE(obj)) {
    have_matrix = 1;
    error = getinterval_number_value(p, values, 1, 1);
  } else {
    have_matrix = 0;   
    error = getinterval_number_value(p, values, 0, 1);
  }   
  if (error)
    return error;

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

  if (!have_matrix) {
    error = pdf_dev_concat(&matrix);
    if (error)
      return error;
  }
  
  if (have_matrix)
    obj = dpx_stack_pop(stk);
  clean_stack(p, 1);
  if (have_matrix) {
    matrix_to_array(obj, &matrix);
    dpx_stack_push(stk, obj);
  }

  return error;
}

static int mps_op__translate (mpsi *p)
{
  int          error       = 0;
  dpx_stack   *stk         = &p->stack.operand;
  pdf_tmatrix  matrix      = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
  int          have_matrix = 0;
  pst_obj     *obj;
  double       values[6]   = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
 
  if (dpx_stack_depth(stk) < 2)
    return -1;
  obj = dpx_stack_top(stk);
  if (PST_ARRAYTYPE(obj)) {
    have_matrix = 1;
    error = getinterval_number_value(p, values, 1, 2);
  } else {
    have_matrix = 0;   
    error = getinterval_number_value(p, values, 0, 2);
  }   
  if (error)
    return error;

  pdf_setmatrix(&matrix, 1.0, 0.0, 0.0, 1.0, values[0], values[1]);

  if (!have_matrix) {
    error = pdf_dev_concat(&matrix);
    if (error)
      return error;
  }
  
  if (have_matrix)
    obj = dpx_stack_pop(stk);
  clean_stack(p, 2);
  if (have_matrix) {
    matrix_to_array(obj, &matrix);
    dpx_stack_push(stk, obj);
  }

  return error;
}

static int mps_op__show (mpsi *p)
{
  int             error = 0;
  dpx_stack      *stk   = &p->stack.operand;
  pst_obj        *str;
  spt_t           x, y;
  pdf_coord       cp;
  int             font_id, tfm_id, sfd_id;
  double          font_scale, text_width;
  unsigned char  *strptr;
  int             length;
  struct mp_font *font;

  if (dpx_stack_depth(stk) < 1)
    return -1; /* stackunderflow */
  str = dpx_stack_top(stk);
  if (!PST_STRINGTYPE(str))
    return -1; /* typecheck */

  pdf_dev_currentpoint(&cp);
  x = cp.x * dev_unit_dviunit();
  y = cp.y * dev_unit_dviunit();
  {
    pst_string *data = str->data;

    strptr = data->value + str->comp.off;
  }
  length = str->comp.size;

  font = dpx_stack_top(&font_stack);
  if (!font)
    return -1;
  font_id    = font->font_id;
  tfm_id     = font->tfm_id;
  sfd_id     = font->subfont_id;
  font_scale = font->pt_size;

  text_width = 0.0;
  if (sfd_id >= 0) {
    unsigned short  uch;
    unsigned char  *ustr;
    int      i;

    ustr = NEW(length * 2, unsigned char);
    for (i = 0; i < length; i++) {
      uch = lookup_sfd_record(sfd_id, strptr[i]);
      ustr[2*i  ] = uch >> 8;
      ustr[2*i+1] = uch & 0xff;
      if (tfm_id >= 0) {
        text_width += tfm_get_width(tfm_id, strptr[i]);
      }
    }
    text_width *= font_scale;

    pdf_dev_set_string(x, y, ustr, length * 2,
                       (spt_t)(text_width*dev_unit_dviunit()),
                       font_id, 0);
    RELEASE(ustr);
  } else {
#define FWBASE ((double) (1<<20))
    if (tfm_id >= 0) {
      text_width = (double) tfm_string_width(tfm_id, strptr, length)/FWBASE;
      text_width *= font_scale;
    }
    pdf_dev_set_string(x, y, strptr, length,
                       (spt_t)(text_width*dev_unit_dviunit()),
                       font_id, 0);
  }

  if (pdf_dev_get_font_wmode(font_id)) {
    pdf_dev_rmoveto(0.0, -text_width);
  } else {
    pdf_dev_rmoveto(text_width, 0.0);
  }
  graphics_mode();
  clean_stack(p, 1);

  return error;
}

static int mps_op__stringwidth (mpsi *p)
{
  int             error = 0;
  dpx_stack      *stk   = &p->stack.operand;
  pst_obj        *str;
  int             font_id, tfm_id, sfd_id;
  double          font_scale, text_width;
  unsigned char  *strptr;
  int             length;
  struct mp_font *font;

  if (dpx_stack_depth(stk) < 1)
    return -1; /* stackunderflow */
  str = dpx_stack_top(stk);
  if (!PST_STRINGTYPE(str))
    return -1; /* typecheck */

  {
    pst_string *data = str->data;

    strptr = data->value + str->comp.off;
  }
  length = str->comp.size;

  font = dpx_stack_top(&font_stack);
  if (!font)
    return -1;
  font_id    = font->font_id;
  tfm_id     = font->tfm_id;
  sfd_id     = font->subfont_id;
  font_scale = font->pt_size;

  text_width = 0.0;
  if (sfd_id >= 0) {
    int i;

    for (i = 0; i < length; i++) {
      text_width += tfm_get_width(tfm_id, strptr[i]);
    }
    text_width *= font_scale;
  } else if (tfm_id >= 0) {
#define FWBASE ((double) (1<<20))
    text_width = (double) tfm_string_width(tfm_id, strptr, length)/FWBASE;
    text_width *= font_scale;
  }

  clean_stack(p, 1);
  if (pdf_dev_get_font_wmode(font_id)) {
    dpx_stack_push(stk, pst_new_real(0.0));
    dpx_stack_push(stk, pst_new_real(text_width));
  } else {
    dpx_stack_push(stk, pst_new_real(text_width));
    dpx_stack_push(stk, pst_new_real(0.0));
  }

  return error;
}

/* NYI */
static int mps_op__makefont (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj;

  if (dpx_stack_depth(stk) < 2)
    return -1;
  
  obj = dpx_stack_pop(stk);
  pst_release_obj(obj);

  return error;
}

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
    clean_stack(p, 5);
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
      copy = pst_copy_obj(proc[3-i]);
      copy->attr.is_exec = 0; /* cvlit */
      dpx_stack_push(&p->stack.exec, copy);
    }
  }

  {
    pst_obj *obj = NULL;
    int      i;

    switch (op) {
    case 'm':
      obj = proc[0];
      break;
    case 'l':
      obj = proc[1];
      break;
    case 'c':
      obj = proc[3];
      break;
    case 'h':
      obj = proc[3];
      break;
    }
  
    for (i = 0; i < num_coords; i++) {
      dpx_stack_push(&p->stack.operand, pst_new_real(pt[i].x));
      dpx_stack_push(&p->stack.operand, pst_new_real(pt[i].y));
    }
    dpx_stack_push(&p->stack.exec, pst_copy_obj(obj));
  }

  pst_release_obj(path);
  pst_release_obj(proc[0]);
  pst_release_obj(proc[1]);
  pst_release_obj(proc[2]);
  pst_release_obj(proc[3]);

  return error;
}

struct path {
  int      index;
  pst_obj *path;
};

static int add_path (pdf_coord *pt, int op, pdf_coord cp, void *dp)
{
  struct path *p = (struct path *) dp;
  pst_array   *vals, *data;
  int          i, n, n_pts;

  data = p->path->data;
  n    = p->index;
  if (n >= pst_length_of(p->path))
    return -1;

  switch (op) {
  case 'm': case 'l':
    n_pts = 1;
    break;
  case 'c':
    n_pts = 3;
    break;
  default:
    n_pts = 0;
  }
  vals = NEW(1, pst_array);
  vals->link   = 0;
  vals->size   = 2 * n_pts;
  vals->values = NEW(2 * n_pts, pst_obj *);
  for (i = 0; i < n_pts; i++) {
    vals->values[2*i]   = pst_new_real(pt[i].x);
    vals->values[2*i+1] = pst_new_real(pt[i].y);
  }
  data->values[2*n]   = pst_new_obj(PST_TYPE_ARRAY, vals);
  data->values[2*n+1] = pst_new_integer(op);
  p->index++;

  return 0;
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

  num_paths = pdf_dev_path_length();
  if (num_paths > 0) {
    pst_array   *data;
    struct path  pa;
    
    data = NEW(1, pst_array);
    data->link   = 0;
    data->size   = 2 * num_paths;
    data->values = NEW(2 * num_paths, pst_obj *);
    path         = pst_new_obj(PST_TYPE_ARRAY, data);
    pa.index     = 0;
    pa.path      = path;
    error = pdf_dev_pathforall(add_path, &pa);
    if (!error) {
      pst_obj *copy, *cvx, *this;
      int      i;

      this = mps_search_systemdict(p, "%pathforall_loop");
      dpx_stack_push(&p->stack.exec, pst_copy_obj(this));
      dpx_stack_push(&p->stack.exec, pst_copy_obj(path));
      cvx  = mps_search_systemdict(p, "cvx");
      for (i = 0; i < 4; i++) {
        dpx_stack_push(&p->stack.exec, pst_copy_obj(cvx));
        copy = pst_copy_obj(proc[3-i]);
        copy->attr.is_exec = 0; /* cvlit */
        dpx_stack_push(&p->stack.exec, copy);
      }
    }
    pst_release_obj(path);
  }

  if (!error)
    clean_stack(p, 4);

  return error;
}

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

void
mps_set_currentfont (mpsi *p, const char *font_name, int font_id, int tfm_id, int sfd_id, double pt_size)
{
  struct mp_font *font;

  font = dpx_stack_top(&font_stack);
  if (!font) {
    font = NEW(1, struct mp_font);
    font->font_name = NULL;
    dpx_stack_push(&font_stack, font);
  }

  clear_mp_font_struct(font);
  font->font_name = NEW(strlen(font_name)+1, char);
  strcpy(font->font_name, font_name);
  font->font_id    = font_id;
  font->tfm_id     = tfm_id;
  font->subfont_id = sfd_id;
  font->pt_size    = pt_size;
}

static int
mp_setfont (const char *font_name, double pt_size)
{
  const char     *name = font_name;
  struct mp_font *font;
  int             subfont_id = -1;
  fontmap_rec    *mrec;

  /* NYI */
  if (!strcmp(font_name, "PSTricksDotFont")) {
    font_name = "ZapfDingbats";
  }

  font = dpx_stack_top(&font_stack);
  if (!font) {
    font = NEW(1, struct mp_font);
    font->font_name = NULL;
    dpx_stack_push(&font_stack, font);
  }

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
  font->font_id    = pdf_dev_locate_font(name, (spt_t) (pt_size * dev_unit_dviunit()));

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

  current = dpx_stack_top(&font_stack);
  next    = NEW(1, struct mp_font);
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
  dpx_stack_push(&font_stack, next);
}

static void
restore_font (void)
{
  struct mp_font *current;

  current = dpx_stack_pop(&font_stack);
  if (current) {
    clear_mp_font_struct(current);
    RELEASE(current);
  }
}

static void
clear_fonts (void)
{
  struct mp_font *font;
  while ((font = dpx_stack_pop(&font_stack)) != NULL) {
    clear_mp_font_struct(font);
    RELEASE(font);
  }
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
#define EOFILL  46

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
  {"eofill",       EOFILL},
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

#define NUM_PS_OPERATORS_G  (sizeof(ps_operators)/sizeof(ps_operators[0]))

static int
get_opcode (const char *token)
{
  int   i;

  for (i = 0; i < NUM_PS_OPERATORS_G; i++) {
    if (!strcmp(token, ps_operators[i].token)) {
      return ps_operators[i].opcode;
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
    error = getinterval_number_value(p, values, 0, 1);
    if (!error)
      error = pdf_dev_setflat(values[0]);
    if (!error)
      clean_stack(p, 1);
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

    /* Path construction */
  case MOVETO:
    error = getinterval_number_value(p, values, 0, 2);
    if (!error)
      error = pdf_dev_moveto(values[0], values[1]);
    if (!error)
      error = clean_stack(p, 2);
    break;
  case RMOVETO:
    error = getinterval_number_value(p, values, 0, 2);
    if (!error)
      error = pdf_dev_rmoveto(values[0], values[1]);
    if (!error)
      error = clean_stack(p, 2);
    break;
  case LINETO:
    error = getinterval_number_value(p, values, 0, 2);
    if (!error)
      error = pdf_dev_lineto(values[0], values[1]);
    if (!error)
      error = clean_stack(p, 2);
    break;
  case RLINETO:
    error = getinterval_number_value(p, values, 0, 2);
    if (!error)
      error = pdf_dev_rlineto(values[0], values[1]);
    if (!error)
      error = clean_stack(p, 2);
    break;
  case CURVETO:
    error = getinterval_number_value(p, values, 0, 6);
    if (!error)
      error = pdf_dev_curveto(values[0], values[1], values[2], values[3], values[4], values[5]);
    if (!error)
      error = clean_stack(p, 6);
    break;
  case RCURVETO:
    error = getinterval_number_value(p, values, 0, 6);
    if (!error)
      error = pdf_dev_rcurveto(values[0], values[1], values[2], values[3], values[4], values[5]);
    if (!error)
      error = clean_stack(p, 6);
    break;
  case CLOSEPATH:
    error = pdf_dev_closepath();
    break;
  case ARC:
    error = getinterval_number_value(p, values, 0, 5);
    if (!error)
      error = pdf_dev_arc(values[0], values[1], values[2], values[3], values[4]);
    if (!error)
      error = clean_stack(p, 5);
    break;
  case ARCN:
    error = getinterval_number_value(p, values, 0, 5);
    if (!error)
      error = pdf_dev_arcn(values[0], values[1], values[2], values[3], values[4]);
    if (!error)
      error = clean_stack(p, 5);
    break;
    
  case NEWPATH:
    pdf_dev_newpath();
    break;
  case STROKE:
    /* fill rule not supported yet */
    pdf_dev_flushpath('S', PDF_FILL_RULE_NONZERO);
    break;
  case FILL:
    pdf_dev_flushpath('f', PDF_FILL_RULE_NONZERO);
    break;
  case EOFILL:
    pdf_dev_flushpath('f', PDF_FILL_RULE_EVENODD);
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
      clean_stack(p, 1); /* not a number */
    break;

    /* Positive angle means clock-wise direction in graphicx-dvips??? */
  case SETDASH:
    error = getinterval_number_value(p, values, 0, 1);
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
      clean_stack(p, 2);
    break;
  case SETLINECAP:
    error = getinterval_number_value(p, values, 0, 1);
    if (!error)
      error = pdf_dev_setlinecap((int)values[0]);
    if (!error)
      clean_stack(p, 1);
    break;
  case SETLINEJOIN:
    error = getinterval_number_value(p, values, 0, 1);
    if (!error)
      error = pdf_dev_setlinejoin((int)values[0]);
    if (!error)
      clean_stack(p, 1);
    break;
  case SETLINEWIDTH:
    error = getinterval_number_value(p, values, 0, 1);
    if (!error)
      error = pdf_dev_setlinewidth(values[0]);
    if (!error)
      clean_stack(p, 1);
    break;
  case SETMITERLIMIT:
    error = getinterval_number_value(p, values, 0, 1);
    if (!error)
      error = pdf_dev_setmiterlimit(values[0]);
    if (!error)
      clean_stack(p, 1);
    break;

  case SETCMYKCOLOR:
    error = getinterval_number_value(p, values, 0, 4);
    /* Not handled properly */
    if (!error) {
      pdf_color_cmykcolor(&color, values[0], values[1], values[2], values[3]);
      pdf_dev_set_strokingcolor(&color);
      pdf_dev_set_nonstrokingcolor(&color);
    }
    if (!error)
      clean_stack(p, 4);
    break;
  case SETGRAY:
    /* Not handled properly */
    error = getinterval_number_value(p, values, 0, 1);
    if (!error) {
      pdf_color_graycolor(&color, values[0]);
      pdf_dev_set_strokingcolor(&color);
      pdf_dev_set_nonstrokingcolor(&color);
    }
    if (!error)
      clean_stack(p, 1);
    break;
  case SETRGBCOLOR:
    error = getinterval_number_value(p, values, 0, 3);
    if (!error) {
      pdf_color_rgbcolor(&color, values[0], values[1], values[2]);
      pdf_dev_set_strokingcolor(&color);
      pdf_dev_set_nonstrokingcolor(&color);
    }
    if (!error)
      clean_stack(p, 3);
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
        pdf_setmatrix(&matrix, values[0], values[1], values[2], values[3], values[4], values[5]);
        has_matrix = 1;
      }
      if (has_matrix) {
        error = getinterval_number_value(p, values, 1, 2);
      } else {
        error = getinterval_number_value(p, values, 0, 2);
      }
      if (error)
        break;
      cp.y = values[1];
      cp.x = values[0];

      if (!has_matrix) {
      	ps_dev_CTM(&matrix); /* Here, we need real PostScript CTM */
      }
      pdf_dev_dtransform(&cp, &matrix);
      clean_stack(p, has_matrix ? 3 : 2); 
      mps_push_stack(p, pst_new_real(cp.x));
      mps_push_stack(p, pst_new_real(cp.y));
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
        pdf_setmatrix(&matrix, values[0], values[1], values[2], values[3], values[4], values[5]);
        has_matrix = 1;
      }
      if (has_matrix) {
        error = getinterval_number_value(p, values, 1, 2);
      } else {
        error = getinterval_number_value(p, values, 0, 2);
      }
      if (error)
        break;
      cp.y = values[1];
      cp.x = values[0];

      if (!has_matrix) {
      	ps_dev_CTM(&matrix); /* Here, we need real PostScript CTM */
      }
      pdf_dev_idtransform(&cp, &matrix);
      clean_stack(p, has_matrix ? 3 : 2);
      mps_push_stack(p, pst_new_real(cp.x));
      mps_push_stack(p, pst_new_real(cp.y));
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
        error = getinterval_number_value(p, values, 1, 2);
      } else {
        error = getinterval_number_value(p, values, 0, 2);
      }
      if (error)
        break;
      cp.y = values[1];
      cp.x = values[0];

      if (!has_matrix) {
      	ps_dev_CTM(&matrix); /* Here, we need real PostScript CTM */
      }
      pdf_dev_transform(&cp, &matrix);
      clean_stack(p, has_matrix ? 3 : 2);
      mps_push_stack(p, pst_new_real(cp.x));
      mps_push_stack(p, pst_new_real(cp.y));
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
        error = getinterval_number_value(p, values, 1, 2);
      } else {
        error = getinterval_number_value(p, values, 0, 2);
      }
      if (error)
        break;
      cp.y = values[1];
      cp.x = values[0];

      if (!has_matrix) {
      	ps_dev_CTM(&matrix); /* Here, we need real PostScript CTM */
      }
      pdf_dev_itransform(&cp, &matrix);
      clean_stack(p, has_matrix ? 3 : 2);
      mps_push_stack(p, pst_new_real(cp.x));
      mps_push_stack(p, pst_new_real(cp.y));
    }
    break;

#if 1
  case FINDFONT:
    {
      dpx_stack *stk = &p->stack.operand;
      pst_obj   *obj, *dict;
      char      *fontname;
      
      if (dpx_stack_depth(stk) < 1)
        return -1;
    
      obj = dpx_stack_top(stk);
      if (!PST_NAMETYPE(obj) && !PST_STRINGTYPE(obj))
        return -1;
      fontname = (char *) pst_getSV(obj);
      WARN("findfont: %s", fontname);
      RELEASE(fontname);

      obj  = dpx_stack_pop(stk);
      dict = pst_new_dict(-1);
      {
        pst_dict *data = dict->data;

        ht_insert_table(data->values, "FontName", strlen("FontName"), obj);
      }
      dpx_stack_push(stk, dict);
    }
    break;
  case SETFONT:
    {
      dpx_stack *stk = &p->stack.operand;
      pst_obj   *dict, *name, *num;
      pst_dict  *data;
      char      *fontname;
      double     fontscale = 1.0;

      if (dpx_stack_depth(stk) < 1)
        return -1;
      dict = dpx_stack_top(stk);
      if (!PST_DICTTYPE(dict))
        return -1;
      data = dict->data;
      name = ht_lookup_table(data->values, "FontName", strlen("FontName"));
      if (!name || !PST_NAMETYPE(name))
        return -1;
      fontname = (char *) pst_getSV(name);
      num = ht_lookup_table(data->values, "FontScale", strlen("FontScale"));
      if (num && PST_NUMBERTYPE(num))
        fontscale = pst_getRV(num);
      error = mp_setfont(fontname, fontscale);
      RELEASE(fontname);
      if (!error) {
        dict = dpx_stack_pop(stk);
        pst_release_obj(dict);
      }
    }
    break;
  case SCALEFONT:
    {
      dpx_stack *stk = &p->stack.operand;
      pst_obj   *dict, *num;
      pst_dict  *data;
      double     fontscale = 1.0;

      if (dpx_stack_depth(stk) < 2)
        return -1;
      num  = dpx_stack_at(stk, 0);
      dict = dpx_stack_at(stk, 1);
      if (!PST_NUMBERTYPE(num) || !PST_DICTTYPE(dict))
        return -1;
      data = dict->data;
      num  = dpx_stack_pop(stk);
      fontscale = pst_getRV(num);
      pst_release_obj(num);

      num  = ht_lookup_table(data->values, "FontScale", strlen("FontScale"));
      fontscale *= pst_getRV(num);
      ht_insert_table(data->values, "FontScale", strlen("FontScale"), pst_new_real(fontscale));
    }
    break;
  case CURRENTFONT:
    {
      dpx_stack      *stk = &p->stack.operand;
      struct mp_font *font = dpx_stack_top(&font_stack);
      pst_obj        *obj;
      pst_dict       *data;

      obj  = pst_new_dict(-1);
      data = obj->data;
      ht_insert_table(data->values, "FontName",  strlen("FontName"),  pst_new_name(font->font_name, 0));
      ht_insert_table(data->values, "FontScale", strlen("FontScale"), pst_new_real(font->pt_size));
      ht_insert_table(data->values, "FontType",  strlen("FontType"),  pst_new_integer(1));
      ht_insert_table(data->values, "FMapType",  strlen("FMapType"),  pst_new_integer(0));
      dpx_stack_push(stk, obj);
    }
    break;
#endif

  default:
    ERROR("Unknown operator: %s", token);
    break;
  }

  return error;
}

static int mps_op__setcachedevice (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  double     values[6];
  int        i;

  if (dpx_stack_depth(stk) < 6)
    return -1; /* stackunderflow */
  error = getinterval_number_value(p, values, 0, 6);
  if (error)
    return error;
  for (i = 0; i < 6; i++) {
    char buf[256];
    int  len;

    len = sprintf(buf, "%g ", values[i]);
    pdf_doc_add_page_content(buf, len);
  }
  pdf_doc_add_page_content("d1 ", strlen("d1 "));
  clean_stack(p, 6);

  return error;
}

static pdf_obj *
convert_charproc (mpsi *p, const char *glyph, pdf_obj *resource)
{
  pdf_obj *content;
  char    *str, *ptr, *endptr;

  content  = pdf_new_stream(STREAM_COMPRESS);

  pdf_dev_push_gstate();
  pdf_doc_begin_capture(content, resource);
  /* stack: --fontdict-- */
  str      = NEW(strlen("dup begin dup / BuildGlyph end")+strlen(glyph)+1, char);
  sprintf(str, "dup begin dup /%s BuildGlyph end", glyph);
  ptr      = str;
  endptr   = str + strlen(str);
  error    = mps_exec_inline(p, &ptr, endptr, 0.0, 0.0); 
  RELEASE(str);
  pdf_doc_end_capture();
  pdf_dev_pop_gstate();

  if (error) {
    pdf_release_obj(content);
    return NULL;
  }

  return content;
}

static int
create_type3_resource (pst_obj *font)
{
  pst_dict  *data;
  pst_obj   *enc;
  pst_array *enc_data;
  pdf_obj   *fontdict, *charproc, *resource;
  pdf_obj   *encoding, *widths;

  ASSERT(PST_DICTTYPE(font));

  data = dict->data;
  enc  = ht_lookup_table(data->values, "Encoding", strlen("Encoding"));
  if (!enc)
    return -1; /* undefined */
  if (!PST_ARRAYTYPE(enc))
    return -1; /* typecheck */
  enc_data = enc->data;
  charproc = pdf_new_dict();
  resource = pdf_new_dict();
  for (i = enc->comp.off; i < enc->comp.off + enc->comp.size; i++) {
    pst_obj *gname = enc_data->values[i];
    char    *glyph;

    if (PST_NULLTYPE(gname))
      continue;
    if (!PST_NAMETYPE(gname) && !PST_STRINGTYPE(gname))
      return -1; /* typecheck */
    glyph = (char *) pst_getSV(gname);
    if (strcmp(glyph, ".notdef")) {
      content = convert_charproc(p, glyph, resource);
      if (content) {
        pdf_add_dict(charproc, pdf_new_name(glyph), pdf_ref_obj(content));
        pdf_release_obj(content);
      }
    }
    RELEASE(glyph);
  }
  content = convert_charproc(p, ".notdef", resource);
  if (content) {
    pdf_add_dict(charproc, pdf_new_name(".notdef"), pdf_ref_obj(content));
    pdf_release_obj(content);
  }
  
  return 0;
}

static int mps_op__definefont (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *fontdict, *name, *dict, *enc;
  pst_dict  *data;
  pst_array *enc_data;
  int        i;
  pdf_obj   *content, *charproc, *resource;

  if (dpx_stack_depth(stk) < 2)
    return -1; /* stackunderflow */
  name = dpx_stack_at(stk, 1);
  if (!PST_NAMETYPE(name) && !PST_STRINGTYPE(name))
    return -1; /* typecheck */
  dict = dpx_stack_at(stk, 0);
  if (!PST_DICTTYPE(dict))
    return -1; /* typecheck */

  
  fontdict = pst_copy_obj(dict);
  clean_stack(p, 2);

  pdf_ref_obj(charproc);
  pdf_release_obj(charproc);

  dpx_stack_push(stk, fontdict);

  return error;
}

static int mps_op__graphic (mpsi *p)
{
  return do_operator(p, mps_current_operator(p), 0, 0);
}

#define NUM_PS_OPERATORS  (sizeof(operators)/sizeof(operators[0]))

static pst_operator operators[] = {
  {"%pathforall_loop", mps_op__p_pathforall_loop},
  {"pathforall",       mps_op__pathforall},
  {"show",             mps_op__show},
  {"stringwidth",      mps_op__stringwidth},
  {"matrix",           mps_op__matrix},
  {"setmatrix",        mps_op__setmatrix},
  {"currentmatrix",    mps_op__currentmatrix},
  {"concatmatrix",     mps_op__concatmatrix},
  {"scale",            mps_op__scale},
  {"rotate",           mps_op__rotate},
  {"translate",        mps_op__translate},

  {"makefont",         mps_op__makefont},
  {"definefont",       mps_op__definefont},
  {"setcachedevice",   mps_op__setcachedevice},
};

int mps_op_graph_load (mpsi *p)
{
  int   i;

  for (i = 0; i < NUM_PS_OPERATORS_G; i++) {
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

  for (i = 0; i < NUM_PS_OPERATORS; i++) {
    pst_obj  *obj;

    obj  = pst_new_obj(PST_TYPE_OPERATOR, &operators[i]);
    obj->attr.is_exec = 1;
    mps_add_systemdict(p, obj);
  }

  return 0;
}
