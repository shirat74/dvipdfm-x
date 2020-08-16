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

#include "pdfximage.h"
#include "pdfcolor.h"
#include "pdfdraw.h"

#include "fontmap.h"
#include "subfont.h"

#include "pdfximage.h"

#include "mpost.h"
#include "dvipdfmx.h"

/*
 * Define the origin as (llx, lly) in order to
 * match the new xetex.def and dvipdfmx.def
 */

static double Xorigin, Yorigin;
static int    translate_origin = 0;

void
mps_set_translate_origin (int v) {
  translate_origin = v;
}

static pst_obj *pst_new_dict (size_t size);

/*
 * In PDF, current path is not a part of graphics state parameter.
 * Hence, current path is not saved by the "q" operator  and is not
 * recovered by the "Q" operator. This means that the following PS
 * code
 *
 *   <path construction> gsave <path painting> grestore ...
 *
 * can't be translated to PDF code
 *
 *   <path construction> q <path painting> Q ...
 *
 * . Only clipping path (which is graphics state parameter in PDF
 * too) is treated in the same way. So, we write clipping path
 * immediately and forget about it but remember current path.
 */



/* Compatibility */
#define MP_CMODE_MPOST    0
#define MP_CMODE_DVIPSK   1
#define MP_CMODE_PTEXVERT 2
static int mp_cmode = MP_CMODE_MPOST;



int
mps_scan_bbox (const char **pp, const char *endptr, pdf_rect *bbox)
{
  char  *number;
  double values[4];
  int    i;

  /* skip_white() skips lines starting '%'... */
  while (*pp < endptr && isspace((unsigned char)**pp))
    (*pp)++;

  /* Scan for bounding box record */
  while (*pp < endptr && **pp == '%') {
    if (*pp + 14 < endptr &&
	!strncmp(*pp, "%%BoundingBox:", 14)) {

      *pp += 14;

      for (i = 0; i < 4; i++) {
	skip_white(pp, endptr);
	number = parse_number(pp, endptr);
	if (!number) {
	  break;
	}
	values[i] = atof(number);
	RELEASE(number);
      }
      if (i < 4) {
	return -1;
      } else {
	/* The new xetex.def and dvipdfmx.def require bbox->llx = bbox->lly = 0.  */
        if (translate_origin) {
          bbox->llx = 0;
          bbox->lly = 0;
          bbox->urx = values[2] - values[0];
          bbox->ury = values[3] - values[1];

          Xorigin = (double)values[0];
          Yorigin = (double)values[1];
        } else {
          bbox->llx = values[0];
          bbox->lly = values[1];
          bbox->urx = values[2];
          bbox->ury = values[3];

          Xorigin = 0.0;
          Yorigin = 0.0;
        }
        return 0;
      }
    }
    pdfparse_skip_line (pp, endptr);
    while (*pp < endptr && isspace((unsigned char)**pp))
      (*pp)++;
  }

  return -1;
}

static void
skip_prolog (const char **start, const char *end)
{
  int   found_prolog = 0;
  const char *save;

  save = *start;
  while (*start < end) {
    if (**start != '%')
      skip_white(start, end);
    if (*start >= end)
      break;
    if (!strncmp(*start, "%%EndProlog", 11)) {
      found_prolog = 1;
      pdfparse_skip_line(start, end);
      break;
    } else if (!strncmp(*start, "%%Page:", 7)) {
      pdfparse_skip_line(start, end);
      break;
    }
    pdfparse_skip_line(start, end);
  }
  if (!found_prolog) {
    *start = save;
  }

  return;
}

#define NUM_PS_OPERATORS  (sizeof(operators)/sizeof(operators[0]))

static void
release_obj (void *obj)
{
  if (obj)
    pst_release_obj(obj);
}

static mpsi mps_intrp;

static pst_obj *mps_search_dict_stack (mpsi *p, const char *name, pst_obj **where);
static pst_obj *mps_search_systemdict (mpsi *p, const char *name);

static int mps_eval__name (mpsi *p, pst_obj *obj)
{
  int   error = 0;
  char *name;

  ASSERT(p);
  ASSERT(obj);
  ASSERT(PST_NAMETYPE(obj));

  name = (char *) pst_getSV(obj);
  if (name) {
    pst_obj *val = mps_search_dict_stack(p, name, NULL);
    if (!val)
      return -1; /* err_undefined */
    if (val->attr.is_exec) {
      dpx_stack_push(&p->stack.exec, pst_copy_obj(val));
    } else {
      dpx_stack_push(&p->stack.operand, pst_copy_obj(val));
    }
    RELEASE(name);
  }
  return error;
}

static int mps_eval__operator (mpsi *p, pst_obj *obj)
{
  int           error = 0;
  pst_operator *op;

  ASSERT(p);
  ASSERT(obj);
  ASSERT(PST_OPERATORTYPE(obj));

  op = obj->data;
  if (op->action) {
    p->cur_op = op->name;
    error = op->action(p);
  } else {
    error = -1;
  }

  return error;
}

static int mps_eval__string (mpsi *p, pst_obj *obj)
{
  int            error = 0;
  pst_obj       *first = NULL, *remain = NULL;
  unsigned char *ptr, *endptr, *save;
  size_t         off;

  ASSERT(p);
  ASSERT(obj);
  ASSERT(PST_STRINGTYPE(obj));

  ptr    = pst_data_ptr(obj);
  save   = ptr;
  endptr = ptr + pst_length_of(obj);
  if (ptr) {
    first = pst_scan_token(&ptr, endptr);
  }
  if (ptr < endptr) {
    pst_string *data = obj->data;

    off = ptr - save;
    remain = NEW(1, pst_obj);
    remain->type = PST_TYPE_STRING;
    remain->data = data;
    data->link++;
    remain->attr.is_exec = 1;
    remain->comp.off  = obj->comp.off  + off;
    remain->comp.size = obj->comp.size - off;
  }
  if (remain) {
    dpx_stack_push(&p->stack.exec, remain);
  }
  if (first) {
    if (PST_ARRAYTYPE(first)) {
      dpx_stack_push(&p->stack.operand, first);
    } else {
      dpx_stack_push(&p->stack.exec, first);
    }
  }

  return error;
}

static int mps_eval__array (mpsi *p, pst_obj *obj)
{
  int        error = 0;
  pst_obj   *first = NULL, *remain = NULL;
  pst_array *data;

  ASSERT(p);
  ASSERT(obj);
  ASSERT(PST_ARRAYTYPE(obj));

  data = obj->data;
  if (data) {
    if (obj->comp.size > 1) {
      remain = NEW(1, pst_obj);
      remain->type = PST_TYPE_ARRAY;
      remain->data = data;
      data->link++;
      remain->attr = obj->attr;
      remain->comp.off  = obj->comp.off + 1;
      remain->comp.size = obj->comp.size - 1;
    }
    if (obj->comp.size > 0) {
      pst_obj *obj1 = data->values[obj->comp.off];
      first = pst_copy_obj(obj1);
    }
  }
  if (remain) {
    dpx_stack_push(&p->stack.exec, remain);
  }
  if (first) {
    if (PST_ARRAYTYPE(first)) {
      dpx_stack_push(&p->stack.operand, first);
    } else {
      dpx_stack_push(&p->stack.exec, first);
    }
  }

  return error;
}

static int
mps_pop_get_numbers (mpsi *p, double *values, int count)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *tmp;

  while (!error && count-- > 0) {
    tmp = dpx_stack_pop(stk);
    if (!tmp) {
      error = -1;
    } else if (!PST_NUMBERTYPE(tmp)) {
      error = -1;
      pst_release_obj(tmp);
    }
    values[count] = pst_getRV(tmp);
    pst_release_obj(tmp);
  }

  return (count + 1);
}

static int
mps_count_to_mark (mpsi *p)
{
  dpx_stack *stk = &p->stack.operand;
  int        i;

  for (i = 0; i < dpx_stack_depth(stk); i++) {
    pst_obj *obj;
    obj = dpx_stack_at(stk, i);
    if (PST_MARKTYPE(obj))
      return i;
  }

  return -1;
}

/* Stack Operation */
static int mps_op__pop (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj;

  if (dpx_stack_depth(stk) < 1)
    return -1;

  obj = dpx_stack_pop(stk);
  if (!obj) {
    error = -1;
  } else {
    pst_release_obj(obj);
  }

  return error;
}

static int mps_op__exch (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj1, *obj2;

  if (dpx_stack_depth(stk) < 2)
    error = -1;

  obj1 = dpx_stack_pop(stk);
  obj2 = dpx_stack_pop(stk);
  dpx_stack_push(stk, obj1);
  dpx_stack_push(stk, obj2);

  return error;
}

static int mps_op__dup (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj, *dup;

  if (dpx_stack_depth(stk) < 1)
    return -1;

  obj = dpx_stack_top(stk);
  if (!obj) {
    error = -1;
  } else {
    dup = pst_copy_obj(obj);
    dpx_stack_push(stk, dup);
  }

  return error;
}

static int mps_op__copy (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj;
  int        n, m;

  if (dpx_stack_depth(stk) < 1)
    return -1;
  obj = dpx_stack_top(stk);
  if (!PST_INTEGERTYPE(obj))
    return -1;

  obj = dpx_stack_pop(stk);
  n   = pst_getIV(obj);
  pst_release_obj(obj);
  if (n < 0)
    return -1;
  
  m = n - 1;
  while (n-- > 0) {
    obj = dpx_stack_at(stk, m); /* stack grows */
    dpx_stack_push(stk, pst_copy_obj(obj));
  }

  return error;
}

static int mps_op__index (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj;
  int        n;

  if (dpx_stack_depth(stk) < 1)
    return -1;
  obj = dpx_stack_top(stk);
  if (!PST_INTEGERTYPE(obj))
    return -1;

  obj = dpx_stack_pop(stk);
  n   = pst_getIV(obj);
  pst_release_obj(obj);
  if (n < 0)
    return -1;

  obj = dpx_stack_at(stk, n);
  dpx_stack_push(stk, pst_copy_obj(obj));

  return error;
}

static int mps_op__roll (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj;
  int        n, j;

  if (dpx_stack_depth(stk) < 1)
    return -1;
  obj = dpx_stack_top(stk);
  if (!PST_INTEGERTYPE(obj))
    return -1;
  obj = dpx_stack_at(stk, 1);
  if (!PST_INTEGERTYPE(obj))
    return -1;

  obj = dpx_stack_pop(stk);
  j   = pst_getIV(obj);
  pst_release_obj(obj);
  obj = dpx_stack_pop(stk);
  n   = pst_getIV(obj);
  pst_release_obj(obj);
  if (n < 0 || n > dpx_stack_depth(stk))
    return -1;

  dpx_stack_roll(stk, n, j);

  return error;
}

static int mps_op__clear (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj;

  while ((obj = dpx_stack_pop(stk)) != NULL) {
    pst_release_obj(obj);
  }

  return error;
}

static int mps_op__count (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj;
  int        n;

  n   = dpx_stack_depth(stk);
  obj = pst_new_integer(n);
  dpx_stack_push(stk, obj);

  return error;
}

static int mps_op__mark (mpsi *p)
{
  dpx_stack *stk = &p->stack.operand;

  dpx_stack_push(stk, pst_new_mark());

  return 0;
}

static int mps_op__cleartomark (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  int        n;

  n = mps_count_to_mark(p);
  while (n-- > 0) {
    pst_obj *obj = dpx_stack_pop(stk);
    pst_release_obj(obj);
  }

  return error;
}

static int mps_op__counttomark (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj;
  int        n;

  n   = mps_count_to_mark(p);
  obj = pst_new_integer(n);
  dpx_stack_push(stk, obj);

  return error;
}

/* Arithmetic Operator */
static int typecheck_numbers (dpx_stack *stk, int n)
{
  while (n-- > 0) {
    pst_obj *obj = dpx_stack_at(stk, n);
    if (!PST_NUMBERTYPE(obj))
      return -1;
  }
  return 0;
}

static int typecheck_integers (dpx_stack *stk, int n)
{
  while (n-- > 0) {
    pst_obj *obj = dpx_stack_at(stk, n);
    if (!PST_INTEGERTYPE(obj))
      return -1;
  }
  return 0;
}

static int mps_op__add (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj1, *obj2;
  int        both_int = 0;

  if (dpx_stack_depth(stk) < 2)
    return -1;
  error = typecheck_numbers(stk, 2);
  if (error < 0)
    return error;

  obj1 = dpx_stack_pop(stk);
  obj2 = dpx_stack_pop(stk);
  both_int = (PST_INTEGERTYPE(obj1) && PST_INTEGERTYPE(obj2)) ? 1 : 0;
  if (both_int) {
    int v1, v2;
    v1 = pst_getIV(obj1);
    v2 = pst_getIV(obj2);
    dpx_stack_push(stk, pst_new_integer(v1 + v2));
  } else {
    double v1, v2;
    v1 = pst_getRV(obj1);
    v2 = pst_getRV(obj2);
    dpx_stack_push(stk, pst_new_real(v1 + v2));
  }
  pst_release_obj(obj1);
  pst_release_obj(obj2);

  return error;
}

static int mps_op__div (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj1, *obj2;
  double     v1, v2;

  if (dpx_stack_depth(stk) < 2)
    return -1;
  error = typecheck_numbers(stk, 2);
  if (error < 0)
    return error;

  obj1 = dpx_stack_pop(stk);
  obj2 = dpx_stack_pop(stk);
  v1 = pst_getRV(obj1);
  v2 = pst_getRV(obj2);
  if (v1 == 0.0) {
    error = -1;
  } else {
    dpx_stack_push(stk, pst_new_real(v2 / v1));
  }
  pst_release_obj(obj1);
  pst_release_obj(obj2);

  return error;
}

static int mps_op__idiv (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj1, *obj2;
  int        v1, v2;

  if (dpx_stack_depth(stk) < 2)
    return -1;
  error = typecheck_integers(stk, 2);
  if (error < 0)
    return error;

  obj1 = dpx_stack_pop(stk);
  obj2 = dpx_stack_pop(stk);
  v1 = pst_getIV(obj1);
  v2 = pst_getIV(obj2);   
  if (v1 == 0) {
    error = -1;
  } else {
    dpx_stack_push(stk, pst_new_integer(v2 / v1));
  }
  pst_release_obj(obj1);
  pst_release_obj(obj2);

  return error;
}

static int mps_op__mod (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj1, *obj2;
  int        v1, v2;

  if (dpx_stack_depth(stk) < 2)
    return -1;
  error = typecheck_integers(stk, 2);
  if (error < 0)
    return error;

  obj1 = dpx_stack_pop(stk);
  obj2 = dpx_stack_pop(stk);
  v1   = pst_getIV(obj1);
  v2   = pst_getIV(obj2);   
  dpx_stack_push(stk, pst_new_integer(v2 % v1));
  pst_release_obj(obj1);
  pst_release_obj(obj2);

  return error;
}

static int mps_op__mul (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj1, *obj2;
  int        both_int = 0;

  if (dpx_stack_depth(stk) < 2)
    return -1;
  error = typecheck_numbers(stk, 2);
  if (error < 0)
    return error;

  obj1 = dpx_stack_pop(stk);
  obj2 = dpx_stack_pop(stk);
  both_int = (PST_INTEGERTYPE(obj1) && PST_INTEGERTYPE(obj2)) ? 1 : 0;
  if (both_int) {
    int v1, v2;
    v1 = pst_getIV(obj1);
    v2 = pst_getIV(obj2);
    dpx_stack_push(stk, pst_new_integer(v1 * v2));
  } else {
    double v1, v2;
    v1 = pst_getRV(obj1);
    v2 = pst_getRV(obj2);
    dpx_stack_push(stk, pst_new_real(v1 * v2));
  }
  pst_release_obj(obj1);
  pst_release_obj(obj2);

  return error;
}

static int mps_op__sub (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj1, *obj2;
  int        both_int = 0;

  if (dpx_stack_depth(stk) < 2)
    return -1;
  error = typecheck_numbers(stk, 2);
  if (error < 0)
    return error;

  obj1 = dpx_stack_pop(stk);
  obj2 = dpx_stack_pop(stk);
  both_int = (PST_INTEGERTYPE(obj1) && PST_INTEGERTYPE(obj2)) ? 1 : 0;
  if (both_int) {
    int v1, v2;
    v1 = pst_getIV(obj1);
    v2 = pst_getIV(obj2);
    dpx_stack_push(stk, pst_new_integer(v2 - v1));
  } else {
    double v1, v2;
    v1 = pst_getRV(obj1);
    v2 = pst_getRV(obj2);
    dpx_stack_push(stk, pst_new_real(v2 - v1));
  }
  pst_release_obj(obj1);
  pst_release_obj(obj2);

  return error;
}

static int mps_op__abs (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj;
  int        is_int = 0;

  if (dpx_stack_depth(stk) < 1)
    return -1;
  error = typecheck_numbers(stk, 1);
  if (error < 0)
    return error;

  obj    = dpx_stack_pop(stk);
  is_int = (PST_INTEGERTYPE(obj)) ? 1 : 0;
  if (is_int) {
    int v;
    v = pst_getIV(obj);
    v = (v < 0) ? -v : v;
    dpx_stack_push(stk, pst_new_integer(v));
  } else {
    double v;
    v = pst_getRV(obj);
    v = (v < 0.0) ? -v : v;
    dpx_stack_push(stk, pst_new_real(v));
  }
  pst_release_obj(obj);

  return error;
}

static int mps_op__neg (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj;
  int        is_int = 0;

  if (dpx_stack_depth(stk) < 1)
    return -1;
  error = typecheck_numbers(stk, 1);
  if (error < 0)
    return error;

  obj = dpx_stack_pop(stk);
  is_int = (PST_INTEGERTYPE(obj)) ? 1 : 0;
  if (is_int) {
    int v;
    v = pst_getIV(obj);
    dpx_stack_push(stk, pst_new_integer(-v));
  } else {
    double v;
    v = pst_getRV(obj);
    dpx_stack_push(stk, pst_new_real(-v));
  }
  pst_release_obj(obj);

  return error;
}

static int mps_op__ceiling (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj;
  double     v;

  if (dpx_stack_depth(stk) < 1)
    return -1;
  error = typecheck_numbers(stk, 1);
  if (error < 0)
    return error;

  obj = dpx_stack_pop(stk);
  v   = pst_getRV(obj);
  dpx_stack_push(stk, pst_new_integer(ceil(v)));
  pst_release_obj(obj);

  return error;
}

static int mps_op__floor (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj;
  double     v;

  if (dpx_stack_depth(stk) < 1)
    return -1;
  error = typecheck_numbers(stk, 1);
  if (error < 0)
    return error;

  obj = dpx_stack_pop(stk);
  v   = pst_getRV(obj);
  dpx_stack_push(stk, pst_new_integer(floor(v)));
  pst_release_obj(obj);

  return error;
}

static int mps_op__round (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj;
  double     v;

  if (dpx_stack_depth(stk) < 1)
    return -1;
  error = typecheck_numbers(stk, 1);
  if (error < 0)
    return error;

  obj = dpx_stack_pop(stk);
  v   = pst_getRV(obj);
  dpx_stack_push(stk, pst_new_integer(round(v)));
  pst_release_obj(obj);

  return error;
}

static int mps_op__truncate (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj;
  double     v;

  if (dpx_stack_depth(stk) < 1)
    return -1;
  error = typecheck_numbers(stk, 1);
  if (error < 0)
    return error;

  obj = dpx_stack_pop(stk);
  v   = pst_getRV(obj);
  dpx_stack_push(stk, pst_new_integer((v > 0) ? floor(v) : ceil(v)));
  pst_release_obj(obj);

  return error;
}

static int mps_op__sqrt (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj;
  double     v;

  if (dpx_stack_depth(stk) < 1)
    return -1;
  error = typecheck_numbers(stk, 1);
  if (error < 0)
    return error;

  obj = dpx_stack_pop(stk);
  v   = pst_getRV(obj);
  if (v < 0.0) {
    error = -1;
  } else {
    dpx_stack_push(stk, pst_new_real(sqrt(v)));
  }
  pst_release_obj(obj);

  return error;
}

static int mps_op__atan (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj1, *obj2;
  double     v1, v2, angle;

  if (dpx_stack_depth(stk) < 2)
    return -1;
  error = typecheck_numbers(stk, 2);
  if (error < 0)
    return error;

  obj1 = dpx_stack_pop(stk);
  obj2 = dpx_stack_pop(stk);
  v1 = pst_getRV(obj1);
  v2 = pst_getRV(obj2);
  if (v1 == 0.0) {
    error = -1;
  } else {
    angle = atan(v2 / v1) * 180.0 / M_PI;
    dpx_stack_push(stk, pst_new_real(angle));
  }
  pst_release_obj(obj1);
  pst_release_obj(obj2);

  return error;
}

static int mps_op__cos (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj;
  double     v;

  if (dpx_stack_depth(stk) < 1)
    return -1;
  error = typecheck_numbers(stk, 1);
  if (error < 0)
    return error;

  obj = dpx_stack_pop(stk);
  v   = pst_getRV(obj);
  v   = v * M_PI / 180.0;
  dpx_stack_push(stk, pst_new_real(cos(v)));
  pst_release_obj(obj);

  return error;
}

static int mps_op__sin (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj;
  double     v;

  if (dpx_stack_depth(stk) < 1)
    return -1;
  error = typecheck_numbers(stk, 1);
  if (error < 0)
    return error;

  obj = dpx_stack_pop(stk);
  v   = pst_getRV(obj);
  v   = v * M_PI / 180.0;
  dpx_stack_push(stk, pst_new_real(sin(v)));
  pst_release_obj(obj);

  return error;
}

static int mps_op__exp (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj1, *obj2;
  double     base, exp;

  if (dpx_stack_depth(stk) < 2)
    return -1;
  error = typecheck_numbers(stk, 2);
  if (error < 0)
    return error;

  obj1 = dpx_stack_pop(stk);
  obj2 = dpx_stack_pop(stk);
  exp  = pst_getRV(obj1);
  base = pst_getRV(obj2);
  dpx_stack_push(stk, pst_new_real(pow(base, exp)));
  pst_release_obj(obj1);
  pst_release_obj(obj2);

  return error;
}

static int mps_op__ln (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj;
  double     v;

  if (dpx_stack_depth(stk) < 1)
    return -1;
  error = typecheck_numbers(stk, 1);
  if (error < 0)
    return error;

  obj = dpx_stack_pop(stk);
  v   = pst_getRV(obj);
  dpx_stack_push(stk, pst_new_real(log(v)));
  pst_release_obj(obj);

  return error;
}

static int mps_op__log (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj;
  double     v;

  if (dpx_stack_depth(stk) < 1)
    return -1;
  error = typecheck_numbers(stk, 1);
  if (error < 0)
    return error;

  obj = dpx_stack_pop(stk);
  v   = pst_getRV(obj);
  dpx_stack_push(stk, pst_new_real(log10(v)));
  pst_release_obj(obj);

  return error;
}

/* NYI */
static int mps_op__rand (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;

  dpx_stack_push(stk, pst_new_integer(0));

  return error;
}

/* NYI */
static int mps_op__srand (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj;

  if (dpx_stack_depth(stk) < 1)
    return -1;
  error = typecheck_integers(stk, 1);
  if (error < 0)
    return error;

  obj = dpx_stack_pop(stk);
  pst_release_obj(obj);

  return error;
}

/* NYI */
static int mps_op__rrand (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;

  dpx_stack_push(stk, pst_new_integer(0));

  return error;
}

static int typecheck_compare_obj_eq (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk = &p->stack.operand;
  pst_obj   *obj1, *obj2;

  obj1 = dpx_stack_at(stk, 0);
  obj2 = dpx_stack_at(stk, 1);
  if (PST_NUMBERTYPE(obj1) && PST_NUMBERTYPE(obj2)) {
    error = 0;
  } else if ((PST_STRINGTYPE(obj1) || PST_NAMETYPE(obj1)) &&
             (PST_STRINGTYPE(obj2) || PST_NAMETYPE(obj2))) {
    error = 0;
  } else if (pst_type_of(obj1) == pst_type_of(obj2)) {
    error = 0;
  } else {
    error = -1; /* typecheck */
  }

  return error;
}


static int compare_obj_eq (mpsi *p)
{
  int         r   = 0;
  dpx_stack  *stk = &p->stack.operand;
  pst_obj    *obj1, *obj2;

  obj1 = dpx_stack_pop(stk);
  obj2 = dpx_stack_pop(stk);
  if (PST_NUMBERTYPE(obj1) && PST_NUMBERTYPE(obj2)) {
    double v1, v2;
    v1 = pst_getRV(obj1);
    v2 = pst_getRV(obj2);
    r = (v2 == v1) ? 1 : 0;
  } else if ((PST_NAMETYPE(obj1) || PST_STRINGTYPE(obj1)) &&
             (PST_NAMETYPE(obj2) || PST_STRINGTYPE(obj2))) {
    size_t         len1, len2;
    unsigned char *v1, *v2;
    int            c;

    len1 = pst_length_of(obj1);
    len2 = pst_length_of(obj2);
    v1   = pst_data_ptr(obj1);
    v2   = pst_data_ptr(obj2);
    if (len1 == len2) {
      c  = memcmp(v2, v1, len1);
    } else {
      c  = len2 - len1;
    }
    r    = (c == 0) ? 1 : 0;
  }
  /* else dict & array: NYI */
  pst_release_obj(obj1);
  pst_release_obj(obj2);

  return r;
}

static int mps_op__eq (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk = &p->stack.operand;
  int        r = 0;

  if (dpx_stack_depth(stk) < 2)
    return -1; /* stackunderflow */
  error = typecheck_compare_obj_eq(p);
  if (error)
    return error;

  r = compare_obj_eq(p);
  dpx_stack_push(stk, pst_new_boolean(r));

  return error;
}

static int mps_op__ne (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk = &p->stack.operand;
  int        r = 0;

  if (dpx_stack_depth(stk) < 2)
    return -1; /* stackunderflow */
  error = typecheck_compare_obj_eq(p);
  if (error)
    return error;

  r = compare_obj_eq(p);
  dpx_stack_push(stk, pst_new_boolean(!r));

  return error;
}

static int typecheck_compare_obj (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk = &p->stack.operand;
  pst_obj   *obj1, *obj2;

  obj1 = dpx_stack_at(stk, 0);
  obj2 = dpx_stack_at(stk, 1);
  if (PST_NUMBERTYPE(obj1) && PST_NUMBERTYPE(obj2)) {
    error = 0;
  } else if (PST_STRINGTYPE(obj1) && PST_STRINGTYPE(obj2)) {
    error = 0;
  } else {
    error = -1; /* typecheck */
  }

  return error;
}

static int compare_obj (mpsi *p, const char *op)
{
  int         r   = 0;
  dpx_stack  *stk = &p->stack.operand;
  pst_obj    *obj1, *obj2;

  obj1 = dpx_stack_pop(stk);
  obj2 = dpx_stack_pop(stk);
  if (PST_NUMBERTYPE(obj1) && PST_NUMBERTYPE(obj2)) {
    double v1, v2;
    v1 = pst_getRV(obj1);
    v2 = pst_getRV(obj2);
    if (!strcmp(op, "ge")) {
      r = (v2 >= v1) ? 1 : 0;
    } else if (!strcmp(op, "gt")) {
      r = (v2 >  v1) ? 1: 0;
    } else if (!strcmp(op, "le")) {
      r = (v2 <= v1) ? 1 : 0;
    } else if (!strcmp(op, "lt")) {
      r = (v2 <  v1) ? 1: 0;
    }
  } else if (PST_STRINGTYPE(obj1) && PST_STRINGTYPE(obj2)) {
    size_t         len1, len2;
    unsigned char *v1, *v2;
    int            c;

    len1 = pst_length_of(obj1);
    len2 = pst_length_of(obj2);
    v1   = pst_data_ptr(obj1);
    v2   = pst_data_ptr(obj2);
    c    = memcmp(v2, v1, len2 < len1 ? len2 : len1);
    c    = (c == 0) ? len2 - len1 : c;
    if (!strcmp(op, "ge")) {
      r = (c >= 0) ? 1 : 0;
    } else if (!strcmp(op, "gt")) {
      r = (c >  0) ? 1: 0;
    } else if (!strcmp(op, "le")) {
      r = (c <= 0) ? 1 : 0;
    } else if (!strcmp(op, "lt")) {
      r = (c <  0) ? 1: 0;
    }
  }
  pst_release_obj(obj1);
  pst_release_obj(obj2);

  return r;
}

static int mps_op__ge (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk = &p->stack.operand;
  int        r = 0;

  if (dpx_stack_depth(stk) < 2)
    return -1; /* stackunderflow */
  error = typecheck_compare_obj(p);
  if (error)
    return error;

  r = compare_obj(p, "ge");
  dpx_stack_push(stk, pst_new_boolean(r));

  return error;
}

static int mps_op__gt (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk = &p->stack.operand;
  int        r = 0;

  if (dpx_stack_depth(stk) < 2)
    return -1; /* stackunderflow */
  error = typecheck_compare_obj(p);
  if (error)
    return error;

  r = compare_obj(p, "gt");
  dpx_stack_push(stk, pst_new_boolean(r));

  return error;
}

static int mps_op__le (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk = &p->stack.operand;
  int        r = 0;

  if (dpx_stack_depth(stk) < 2)
    return -1; /* stackunderflow */
  error = typecheck_compare_obj(p);
  if (error)
    return error;

  r = compare_obj(p, "le");
  dpx_stack_push(stk, pst_new_boolean(r));

  return error;
}

static int mps_op__lt (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk = &p->stack.operand;
  int        r = 0;

  if (dpx_stack_depth(stk) < 2)
    return -1; /* stackunderflow */
  error = typecheck_compare_obj(p);
  if (error)
    return error;

  r = compare_obj(p, "lt");
  dpx_stack_push(stk, pst_new_boolean(r));

  return error;
}

static int mps_op__and (mpsi *p)
{
  int         error = 0;
  dpx_stack  *stk = &p->stack.operand;
  int         both_bool;
  pst_obj    *obj1, *obj2;
  int         v1, v2;

  if (dpx_stack_depth(stk) < 2)
    return -1;
  obj1 = dpx_stack_at(stk, 0);
  obj2 = dpx_stack_at(stk, 1);
  if (!(PST_BOOLEANTYPE(obj1) || PST_INTEGERTYPE(obj1)))
    return -1;
  if (!(PST_BOOLEANTYPE(obj2) || PST_INTEGERTYPE(obj2)))
    return -1;
  if (pst_type_of(obj1) != pst_type_of(obj2))
    return -1;

  obj1 = dpx_stack_pop(stk);
  obj2 = dpx_stack_pop(stk);
  both_bool = (PST_BOOLEANTYPE(obj1) && PST_BOOLEANTYPE(obj2)) ? 1 : 0;
  v1 = pst_getIV(obj1);
  v2 = pst_getIV(obj2);
  dpx_stack_push(stk, both_bool ? pst_new_boolean(v1 & v2) : pst_new_integer(v1 & v2));
  pst_release_obj(obj1);
  pst_release_obj(obj2);

  return error;
}

static int mps_op__or (mpsi *p)
{
  int         error = 0;
  dpx_stack  *stk = &p->stack.operand;
  int         both_bool;
  pst_obj    *obj1, *obj2;
  int         v1, v2;

  if (dpx_stack_depth(stk) < 2)
    return -1;
  obj1 = dpx_stack_at(stk, 0);
  obj2 = dpx_stack_at(stk, 1);
  if (!(PST_BOOLEANTYPE(obj1) || PST_INTEGERTYPE(obj1)))
    return -1;
  if (!(PST_BOOLEANTYPE(obj2) || PST_INTEGERTYPE(obj2)))
    return -1;
  if (pst_type_of(obj1) != pst_type_of(obj2))
    return -1;

  obj1 = dpx_stack_pop(stk);
  obj2 = dpx_stack_pop(stk);
  both_bool = (PST_BOOLEANTYPE(obj1) && PST_BOOLEANTYPE(obj2)) ? 1 : 0;
  v1 = pst_getIV(obj1);
  v2 = pst_getIV(obj2);
  dpx_stack_push(stk, both_bool ? pst_new_boolean(v1 | v2) : pst_new_integer(v1 | v2));
  pst_release_obj(obj1);
  pst_release_obj(obj2);

  return error;
}

static int mps_op__xor (mpsi *p)
{
  int         error = 0;
  dpx_stack  *stk = &p->stack.operand;
  int         both_bool;
  pst_obj    *obj1, *obj2;
  int         v1, v2;

  if (dpx_stack_depth(stk) < 2)
    return -1;
  obj1 = dpx_stack_at(stk, 0);
  obj2 = dpx_stack_at(stk, 1);
  if (!(PST_BOOLEANTYPE(obj1) || PST_INTEGERTYPE(obj1)))
    return -1;
  if (!(PST_BOOLEANTYPE(obj2) || PST_INTEGERTYPE(obj2)))
    return -1;
  if (pst_type_of(obj1) != pst_type_of(obj2))
    return -1;

  obj1 = dpx_stack_pop(stk);
  obj2 = dpx_stack_pop(stk);
  both_bool = (PST_BOOLEANTYPE(obj1) && PST_BOOLEANTYPE(obj2)) ? 1 : 0;
  v1 = pst_getIV(obj1);
  v2 = pst_getIV(obj2);
  dpx_stack_push(stk, both_bool ? pst_new_boolean(v1 ^ v2) : pst_new_integer(v1 ^ v2));
  pst_release_obj(obj1);
  pst_release_obj(obj2);

  return error;
}

static int mps_op__true (mpsi *p)
{
  int         error = 0;
  dpx_stack  *stk   = &p->stack.operand;

  dpx_stack_push(stk, pst_new_boolean(1));

  return error;
}

static int mps_op__false (mpsi *p)
{
  int         error = 0;
  dpx_stack  *stk   = &p->stack.operand;

  dpx_stack_push(stk, pst_new_boolean(0));

  return error;
}

static int mps_op__not (mpsi *p)
{
  int         error = 0;
  dpx_stack  *stk   = &p->stack.operand;
  pst_obj    *obj;
  int         is_int, v;

  if (dpx_stack_depth(stk) < 1)
    return -1;
  obj = dpx_stack_top(stk);
  if (!PST_INTEGERTYPE(obj) || !PST_BOOLEANTYPE(obj))
    return -1;
  
  obj    = dpx_stack_pop(stk);
  is_int = PST_INTEGERTYPE(obj) ? 1 : 0;
  v      = pst_getIV(obj) ? 0 : 1;
  dpx_stack_push(stk, is_int ? pst_new_integer(v) : pst_new_boolean(v));

  return error;
}

static int mps_op__bitshift (mpsi *p)
{
  int         error = 0;
  dpx_stack  *stk   = &p->stack.operand;
  pst_obj    *obj1, *obj2;
  int         v, shift;

  if (dpx_stack_depth(stk) < 2)
    return -1;
  error = typecheck_integers(stk, 2);
  if (error)
      return error;

  obj1  = dpx_stack_pop(stk);
  obj2  = dpx_stack_pop(stk);
  
  shift = pst_getIV(obj1);
  pst_release_obj(obj1);
  v     = pst_getIV(obj2);
  pst_release_obj(obj2);
  dpx_stack_push(stk, pst_new_integer(shift > 0 ? v << shift : v >> -shift));

  return error;
}

static int mps_op__exec (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj;

  if (dpx_stack_depth(stk) < 1)
    return -1;
  
  obj = dpx_stack_pop(stk);
  dpx_stack_push(&p->stack.exec, obj);

  return error;
}

static int mps_op__if (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj1, *obj2;

  if (dpx_stack_depth(stk) < 2)
    return -1;
  obj1 = dpx_stack_at(stk, 0);
  obj2 = dpx_stack_at(stk, 1);
  if (!PST_BOOLEANTYPE(obj2) || !PST_ARRAYTYPE(obj1) || !obj1->attr.is_exec)
    return -1;

  obj1 = dpx_stack_pop(stk);
  obj2 = dpx_stack_pop(stk);
  if (pst_getIV(obj2)) {
    dpx_stack_push(&p->stack.exec, obj1);
  } else {
    pst_release_obj(obj1);
  }
  pst_release_obj(obj2);

  return error;
}

static int mps_op__ifelse (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj1, *obj2, *obj3;

  if (dpx_stack_depth(stk) < 3)
    return -1;
  obj1 = dpx_stack_at(stk, 0);
  obj2 = dpx_stack_at(stk, 1);
  obj3 = dpx_stack_at(stk, 2);
  if (!PST_BOOLEANTYPE(obj3) ||
      !PST_ARRAYTYPE(obj2) || !obj2->attr.is_exec ||
      !PST_ARRAYTYPE(obj1) || !obj1->attr.is_exec)
    return -1;

  obj1 = dpx_stack_pop(stk);
  obj2 = dpx_stack_pop(stk);
  obj3 = dpx_stack_pop(stk);
  if (pst_getIV(obj3)) {
    dpx_stack_push(&p->stack.exec, obj2);
    pst_release_obj(obj1);
  } else {
    dpx_stack_push(&p->stack.exec, obj1);
    pst_release_obj(obj2);
  }
  pst_release_obj(obj3);

  return error;
}

static int mps_op__for (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *proc, *copy, *init, *incr, *limit, *init2;
  int        is_int;

  if (dpx_stack_depth(stk) < 4)
    return -1;
  proc  = dpx_stack_at(stk, 0);
  limit = dpx_stack_at(stk, 1);
  incr  = dpx_stack_at(stk, 2);
  init  = dpx_stack_at(stk, 3);
  if (!PST_ARRAYTYPE(proc) || !proc->attr.is_exec ||
      !PST_NUMBERTYPE(limit) || !PST_NUMBERTYPE(incr) ||
      !PST_NUMBERTYPE(init))
    return -1;

  proc  = dpx_stack_pop(stk);
  limit = dpx_stack_pop(stk);
  incr  = dpx_stack_pop(stk);
  init  = dpx_stack_pop(stk);
  if (pst_getRV(init) > pst_getRV(limit)) {
    pst_release_obj(proc);
    pst_release_obj(limit);
    pst_release_obj(incr);
    pst_release_obj(init);
    return 0;
  }
  if (PST_INTEGERTYPE(init) && PST_INTEGERTYPE(incr)) {
    is_int = 1;
  } else {
    is_int = 0;
  }
  copy = pst_copy_obj(proc);
  copy->attr.is_exec = 0; /* cvlit */
  {
    pst_obj  *cvx, *op_for;

    op_for = mps_search_systemdict(p, "for");
    dpx_stack_push(&p->stack.exec, pst_copy_obj(op_for));
    cvx    = mps_search_systemdict(p, "cvx");
    dpx_stack_push(&p->stack.exec, pst_copy_obj(cvx));
  }
  dpx_stack_push(&p->stack.exec, copy);
  dpx_stack_push(&p->stack.exec, limit);
  dpx_stack_push(&p->stack.exec, incr);
  if (is_int) {
    int n = pst_getIV(init);
    int j = pst_getIV(incr);
    init2 = pst_new_integer(n + j);
  } else {
    double n = pst_getRV(init);
    double j = pst_getRV(init);
    init2 = pst_new_real(n + j);
  }
  dpx_stack_push(&p->stack.exec, init2);
  dpx_stack_push(&p->stack.exec, proc);
  dpx_stack_push(&p->stack.operand, init);

  return error;
}

static int mps_op__repeat (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *num, *proc, *copy;
  int        n;

  if (dpx_stack_depth(stk) < 2)
    return -1;
  proc = dpx_stack_at(stk, 0);
  num  = dpx_stack_at(stk, 1);
  if (!PST_ARRAYTYPE(proc) || !proc->attr.is_exec ||
      !PST_INTEGERTYPE(num))
    return -1;

  proc = dpx_stack_pop(stk);
  num  = dpx_stack_pop(stk);
  n    = pst_getIV(num);
  pst_release_obj(num);
  if (n < 1) {
    pst_release_obj(proc);
    return 0;
  }
  {
    pst_obj  *cvx, *op_rep;

    op_rep = mps_search_systemdict(p, "repeat");
    dpx_stack_push(&p->stack.exec, pst_copy_obj(op_rep));
    cvx    = mps_search_systemdict(p, "cvx");
    dpx_stack_push(&p->stack.exec, pst_copy_obj(cvx));
  }
  copy = pst_copy_obj(proc);
  copy->attr.is_exec = 0; /* cvlit */
  dpx_stack_push(&p->stack.exec, copy);
  dpx_stack_push(&p->stack.exec, pst_new_integer(n - 1));
  dpx_stack_push(&p->stack.exec, proc);

  return error;
}

static int mps_op__loop (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *proc, *copy;

  if (dpx_stack_depth(stk) < 1)
    return -1;
  proc = dpx_stack_at(stk, 0);
  if (!PST_ARRAYTYPE(proc) || !proc->attr.is_exec)
    return -1;

  proc  = dpx_stack_pop(stk);
  {
    pst_obj *cvx, *op_loop;

    op_loop = mps_search_systemdict(p, "loop");
    dpx_stack_push(&p->stack.exec, pst_copy_obj(op_loop));
    cvx     = mps_search_systemdict(p, "cvx");
    dpx_stack_push(&p->stack.exec, pst_copy_obj(cvx));
  }
  copy = pst_copy_obj(proc);
  copy->attr.is_exec = 0; /* cvlit */
  dpx_stack_push(&p->stack.exec, copy);
  dpx_stack_push(&p->stack.exec, proc);

  return error;
}

static int mps_op__exit (mpsi *p)
{
  pst_obj *obj;

  while ((obj = dpx_stack_pop(&p->stack.exec)) != NULL) {
    if (PST_OPERATORTYPE(obj)) {
      pst_operator *op = obj->data;
      if (!strcmp(op->name, "for") || !strcmp(op->name, "repeat") ||
          !strcmp(op->name, "loop") || !strcmp(op->name, "forall")) {
        pst_release_obj(obj);
        break;
      }
    }
    pst_release_obj(obj);
  }

  return 0;
}

static int mps_op__stop (mpsi *p)
{
  int      error = 0;
  pst_obj *obj   = NULL;

  while ((obj = dpx_stack_pop(&p->stack.exec)) != NULL) {
    if (PST_BOOLEANTYPE(obj) && pst_getIV(obj) == 0) {
      break;
    }
    pst_release_obj(obj);
  }
  if (obj) {
    /* boolean false found */
    pst_release_obj(obj);
    dpx_stack_push(&p->stack.operand, pst_new_boolean(1));
  } else {
    error = -1;
  }

  return error;
}

static int mps_op__stopped (mpsi *p)
{
  dpx_stack *stk = &p->stack.operand;
  pst_obj   *obj;

  if (dpx_stack_depth(stk) < 1)
    return -1; /* stackunderflow */

  /* if not "stop" then boolean "false" is evaluated */
  dpx_stack_push(&p->stack.exec, pst_new_boolean(0));
  obj = dpx_stack_pop(&p->stack.operand);
  dpx_stack_push(&p->stack.exec, obj);

  return 0;
}

static int mps_op__dict (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *num, *dict;
  pst_dict  *data;
  int        n;

  if (dpx_stack_depth(stk) < 1)
    return -1;
  num = dpx_stack_top(stk);
  if (!PST_INTEGERTYPE(num))
    return -1;
  
  num = dpx_stack_pop(stk);
  n   = pst_getIV(num);
  pst_release_obj(num);

  data = NEW(1, pst_dict);
  data->link = 0;
  data->values = NEW(1, struct ht_table);
  ht_init_table(data->values, release_obj);
  data->size   = n; /* max size */

  dict = pst_new_obj(PST_TYPE_DICT, data);
  dpx_stack_push(stk, dict);

  return error;
}

static int mps_op__dict_to_mark (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  int        count;
  pst_obj   *obj, *mark;
  pst_dict  *dict;

  count = mps_count_to_mark(p);
  if (count < 0)
    return -1; /* stackunderflow */
  if ((count % 2) != 0)
    return -1; /* rangecheck */

  count /= 2;
  obj  = pst_new_dict(count);
  dict = obj->data;
  while (count-- > 0) {
    pst_obj *key, *value;

    value = dpx_stack_pop(stk);
    key   = dpx_stack_pop(stk);
    switch (key->type) {
    case PST_TYPE_NAME: case PST_TYPE_STRING:
    case PST_TYPE_REAL: case PST_TYPE_INTEGER:
      {
        char *str = (char *) pst_getSV(key);

        ht_insert_table(dict->values, str, strlen(str), value);
        RELEASE(str);
      }
      break;
    case PST_TYPE_NULL:
      pst_release_obj(value);
      error = -1; /* typecheck */
      break;
    default: /* NYI */
      pst_release_obj(value);
      error = -1;
    }
    pst_release_obj(key);
  }
  mark = dpx_stack_pop(stk); /* mark */
  pst_release_obj(mark);

  dpx_stack_push(stk, obj);

  return error;
}


/* NYI: length maxlength */

static int mps_op__begin (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *dict;

  if (dpx_stack_depth(stk) < 1)
    return -1;
  dict = dpx_stack_top(stk);
  if (!PST_DICTTYPE(dict))
    return -1;
  
  dict = dpx_stack_pop(stk);
  dpx_stack_push(&p->stack.dict, dict);

  return error;
}

static int mps_op__end (mpsi *p)
{
  pst_obj *dict;

  if (dpx_stack_depth(&p->stack.dict) < 1)
    return -1;
  dict = dpx_stack_pop(&p->stack.dict);
  pst_release_obj(dict);

  return 0;
}

static int mps_op__def (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *key, *value;
  pst_obj   *dict;
  char      *str;

  if (dpx_stack_depth(stk) < 2)
    return -1;
  if (dpx_stack_depth(&p->stack.dict) < 1)
    return -1;

  /* NYI: max_size */
  value = dpx_stack_pop(stk);
  key   = dpx_stack_pop(stk);
  /* FIXME: any object other than null allowed for key */
  if (PST_NAMETYPE(key)) {
    str   = (char *) pst_getSV(key);
    dict  = dpx_stack_top(&p->stack.dict);
    if (dict) {
      pst_dict *data = dict->data;
      ht_insert_table(data->values, str, strlen(str), value);
    }
    RELEASE(str);
  } else {
    error = -1;
  }
  pst_release_obj(key);

  return error;
}

static int mps_op__load (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj;
  char      *key;

  if (dpx_stack_depth(stk) < 1)
    return -1;
  obj = dpx_stack_top(stk);
  if (!PST_NAMETYPE(obj))
    return -1;
  
  obj = dpx_stack_pop(stk);
  key = (char *) pst_getSV(obj);
  pst_release_obj(obj);

  obj = mps_search_dict_stack(p, key, NULL);
  RELEASE(key);
  if (obj) {
    dpx_stack_push(stk, pst_copy_obj(obj));
  } else {
    error = -1;
  }

  return error;
}

static int mps_op__known (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj, *dict;
  char      *key;

  if (dpx_stack_depth(stk) < 2)
    return -1;
  obj = dpx_stack_top(stk);
  if (!PST_NAMETYPE(obj))
    return -1;
  obj = dpx_stack_at(stk, 1);
  if (!PST_DICTTYPE(obj))
    return -1;

  obj = dpx_stack_pop(stk);
  key = (char *) pst_getSV(obj);
  pst_release_obj(obj);

  dict = dpx_stack_pop(stk);
  {
    pst_dict *data = dict->data;
    obj = ht_lookup_table(data->values, key, strlen(key));
  }
  pst_release_obj(dict);
  RELEASE(key);
  
  dpx_stack_push(stk, pst_new_boolean(obj ? 1 : 0));

  return error;
}

static int mps_op__where (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj, *dict = NULL;
  char      *key;

  if (dpx_stack_depth(stk) < 1)
    return -1;
  obj = dpx_stack_top(stk);
  if (!PST_NAMETYPE(obj))
    return -1;
  
  obj = dpx_stack_pop(stk);
  key = (char *) pst_getSV(obj);
  pst_release_obj(obj);

  obj = mps_search_dict_stack(p, key, &dict);
  RELEASE(key);

  if (obj && dict) {
    dpx_stack_push(stk, pst_copy_obj(dict));
    dpx_stack_push(stk, pst_new_boolean(1));
  } else {
    dpx_stack_push(stk, pst_new_boolean(0));
  }

  return error;
}

/* NYI: for array and string */
static int mps_op__get (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj, *dict;
  char      *key;

  if (dpx_stack_depth(stk) < 2)
    return -1;
  obj  = dpx_stack_at(stk, 0);
  dict = dpx_stack_at(stk, 1);
  if (!PST_NAMETYPE(obj) || !PST_DICTTYPE(dict))
    return -1;
  
  obj  = dpx_stack_pop(stk);
  key  = (char *) pst_getSV(obj);
  pst_release_obj(obj);
  dict = dpx_stack_pop(stk);

  {
    pst_dict *data = dict->data;

    obj = ht_lookup_table(data->values, key, strlen(key));
  }
  RELEASE(key);

  if (obj) {
    dpx_stack_push(stk, pst_copy_obj(obj));
  } else {
    error = -1; /* err_undefined */
  }

  return error;
}

static int mps_op__cvlit (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj;

  if (dpx_stack_depth(stk) < 1)
    return -1;

  obj = dpx_stack_pop(stk);
  obj->attr.is_exec = 0;
  dpx_stack_push(stk, obj);

  return error;
}

static int mps_op__cvx (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj;

  if (dpx_stack_depth(stk) < 1)
    return -1;

  obj = dpx_stack_pop(stk);
  obj->attr.is_exec = 1;
  dpx_stack_push(stk, obj);

  return error;
}

static int mps_op__array_to_mark (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  int        count;
  pst_obj   *obj, *elem, *mark;
  pst_array *array;

  count = mps_count_to_mark(p);
  if (count < 0)
    return -1;
  array = NEW(1, pst_array);
  array->link   = 0;
  array->size   = count;
  array->values = NEW(count, pst_obj *);
  while (count-- > 0) {
    elem = dpx_stack_pop(stk);
    array->values[count] = elem;
  }
  mark = dpx_stack_pop(stk); /* mark */
  pst_release_obj(mark);

  obj = pst_new_obj(PST_TYPE_ARRAY, array);
  dpx_stack_push(stk, obj);

  return error;
}

static int mps_op__equal (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj;

  if (dpx_stack_depth(stk) < 1)
    return -1;

  obj = dpx_stack_pop(stk);
  if (!obj) {
    error = -1;
  } else {
    char *str = (char *) pst_getSV(obj);
    if (str) {
      WARN(str);
      RELEASE(str);
    } else {
      WARN("--nonstringval--");
    }
    pst_release_obj(obj);
  }

  return error;
}

static int
mps_bind_proc (mpsi *p, pst_obj *proc)
{
  int        error = 0;
  int        i;
  pst_array *data;

  ASSERT(p);
  ASSERT(proc);
  ASSERT(PST_ARRAYTYPE(proc));
  ASSERT(proc->attr.is_exec);

  data = proc->data;
  for (i = proc->comp.off; !error && i < proc->comp.off + proc->comp.size; i++) {
    pst_obj *obj = data->values[i];

    if (!obj)
      continue;
    switch (obj->type) {
    case PST_TYPE_NAME:
      if (obj->attr.is_exec) {
        pst_obj *repl;
        char    *name;

        name = (char *) pst_getSV(obj);
        repl = mps_search_dict_stack(p, name, NULL);
        if (repl && PST_OPERATORTYPE(repl)) {
            data->values[i] = pst_copy_obj(repl);
            pst_release_obj(obj);
        }
      }
      break;
    case PST_TYPE_ARRAY:
      if (obj->attr.is_exec) {
        error = mps_bind_proc(p, obj); /* NYI: working on the same object... really OK? */
      }
      break; 
    }
  }

  return error;
}

static int mps_op__bind (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk = &p->stack.operand;
  pst_obj   *proc;

  if (dpx_stack_depth(stk) < 1)
    return -1;
  proc = dpx_stack_top(stk);
  if (!PST_ARRAYTYPE(proc) || !proc->attr.is_exec)
    return -1;

  proc  = dpx_stack_top(stk);
  error = mps_bind_proc(p, proc);

  return error;
}

static int mps_op__null (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk = &p->stack.operand;

  dpx_stack_push(stk, pst_new_null());

  return error;
}

static int mps_op__noop (mpsi *p)
{
  return 0;
}

static int mps_op__dummy (mpsi *p)
{
  dpx_stack_push(&p->stack.operand, pst_new_null());
  return 0;
}

static pst_operator operators[] = {
  {"pop",          mps_op__pop},
  {"exch",         mps_op__exch},
  {"dup",          mps_op__dup},
  {"copy",         mps_op__copy},
  {"index",        mps_op__index},
  {"roll",         mps_op__roll},
  {"clear",        mps_op__clear},
  {"count",        mps_op__count},
  {"mark",         mps_op__mark},
  {"cleartomark",  mps_op__cleartomark},
  {"counttomark",  mps_op__counttomark},

  {"add",          mps_op__add},
  {"div",          mps_op__div},
  {"idiv",         mps_op__idiv},
  {"mod",          mps_op__mod},
  {"mul",          mps_op__mul},
  {"sub",          mps_op__sub},
  {"abs",          mps_op__abs},
  {"neg",          mps_op__neg},
  {"ceiling",      mps_op__ceiling},
  {"floor",        mps_op__floor},
  {"round",        mps_op__round},
  {"truncate",     mps_op__truncate},
  {"sqrt",         mps_op__sqrt},
  {"atan",         mps_op__atan},
  {"cos",          mps_op__cos},
  {"sin",          mps_op__sin},
  {"exp",          mps_op__exp},
  {"ln",           mps_op__ln},
  {"log",          mps_op__log},
  {"rand",         mps_op__rand},  /* NYI */
  {"srand",        mps_op__srand}, /* NYI */
  {"rrand",        mps_op__rrand}, /* NYI */

  {"eq",           mps_op__eq},
  {"ne",           mps_op__ne},
  {"ge",           mps_op__ge},
  {"gt",           mps_op__gt},
  {"le",           mps_op__le},
  {"lt",           mps_op__lt},
  {"and",          mps_op__and},
  {"not",          mps_op__not},
  {"or",           mps_op__or},
  {"xor",          mps_op__xor},
  {"true",         mps_op__true},
  {"false",        mps_op__false},
  {"bitshift",     mps_op__bitshift},

  {"exec",         mps_op__exec},
  {"if",           mps_op__if},
  {"ifelse",       mps_op__ifelse},
  {"for",          mps_op__for},
  {"repeat",       mps_op__repeat},
  {"loop",         mps_op__loop},
  {"exit",         mps_op__exit},
  {"stop",         mps_op__stop},
  {"stopped",      mps_op__stopped},

  {"dict",         mps_op__dict},
  {"<<",           mps_op__mark},
  {">>",           mps_op__dict_to_mark},
  {"begin",        mps_op__begin},
  {"end",          mps_op__end},
  {"def",          mps_op__def},
  {"load",         mps_op__load},
  {"known",        mps_op__known},
  {"where",        mps_op__where},
  {"get",          mps_op__get},

  {"cvlit",        mps_op__cvlit},
  {"cvx",          mps_op__cvx},

  {"null",         mps_op__null},
  {"bind",         mps_op__bind},
  {"[",            mps_op__mark},
  {"]",            mps_op__array_to_mark},
  {"=",            mps_op__equal},
  {"save",         mps_op__dummy}, /* Not Implemented */
  {"restore",      mps_op__noop} 
};

static pst_obj *
pst_new_dict (size_t size)
{
  pst_obj  *obj;
  pst_dict *dict;

  dict = NEW(1, pst_dict);
  dict->link = 0;
  dict->size = size;
  dict->values = NEW(1, struct ht_table);
  ht_init_table(dict->values, release_obj);

  obj = pst_new_obj(PST_TYPE_DICT, dict);

  return obj;
}

#if 1
#include "mps_op_graph.h"
#endif

static int
mps_init_intrp (mpsi *p)
{
  int       error = 0, i;
  pst_dict *systemdict;

  p->cur_op = NULL;
  dpx_stack_init(&p->stack.operand);

  dpx_stack_init(&p->stack.dict);
  p->systemdict = pst_new_dict(-1);
  p->globaldict = pst_new_dict(-1);
  p->userdict   = pst_new_dict(-1);
  dpx_stack_push(&p->stack.dict, p->systemdict);
  dpx_stack_push(&p->stack.dict, p->globaldict);
  dpx_stack_push(&p->stack.dict, p->userdict);

  systemdict = p->systemdict->data;
  for (i = 0; !error && i < NUM_PS_OPERATORS; i++) {
    pst_obj  *obj;

    obj  = pst_new_obj(PST_TYPE_OPERATOR, &operators[i]);
    obj->attr.is_exec = 1;
    ht_insert_table(systemdict->values, operators[i].name, strlen(operators[i].name), obj);
  }
#if 1
  mps_op_graph_load(p);
#endif

  dpx_stack_init(&p->stack.exec);

  return 0;
}

static int
mps_clean_intrp (mpsi *p)
{
  pst_obj  *obj;

  p->cur_op = NULL;

  while ((obj = dpx_stack_pop(&p->stack.operand)) != NULL) {
    pst_release_obj(obj);
  }
  while ((obj = dpx_stack_pop(&p->stack.dict)) != NULL) {
    pst_release_obj(obj);
  }
  while ((obj = dpx_stack_pop(&p->stack.exec)) != NULL) {
    pst_release_obj(obj);
  }

  return 0;
}

static pst_obj *
mps_search_dict_stack (mpsi *p, const char *key, pst_obj **where)
{
  pst_obj *obj  = NULL;
  pst_obj *dict = NULL;
  int      i, count;

  count = dpx_stack_depth(&p->stack.dict);
  for (i = 0; !obj && i < count; i++) {
    pst_dict *data;

    dict = dpx_stack_at(&p->stack.dict, i);
    data = dict->data;
    obj  = ht_lookup_table(data->values, key, strlen(key));
  }
  if (where)
    *where = dict;

  return obj;
}

static pst_obj *
mps_search_systemdict (mpsi *p, const char *key)
{
  pst_obj  *obj;
  pst_dict *dict;

  dict = p->systemdict->data;
  obj  = ht_lookup_table(dict->values, key, strlen(key));

  return obj;
}

/*
 * The only sections that need to know x_user and y _user are those
 * dealing with texfig.
 */
static int
mps_parse_body (mpsi *p, const char **strptr, const char *endptr)
{
  int error = 0;

  skip_white(strptr, endptr);
  while (*strptr < endptr && !error) {
    pst_obj *obj;

    obj = pst_scan_token((unsigned char **)strptr, (unsigned char *)endptr);
    if (!obj) {
      if (*strptr < endptr) {
        error = -1;
      }
      break;
    }
    if (PST_NAMETYPE(obj) && obj->attr.is_exec) {
      WARN(pst_getSV(obj)); /* DEBUG */
      dpx_stack_push(&p->stack.exec, obj);
    } else {
      dpx_stack_push(&p->stack.operand, obj);
    }

    while ((obj = dpx_stack_pop(&p->stack.exec)) != NULL) {
      if (!obj->attr.is_exec) {
        dpx_stack_push(&p->stack.operand, pst_copy_obj(obj));
        continue;
      }
      switch (obj->type) {
      case PST_TYPE_OPERATOR:
        error = mps_eval__operator(p, obj);
        break;
      case PST_TYPE_NAME:
        error = mps_eval__name(p, obj);
        break;
      case PST_TYPE_ARRAY:
        error = mps_eval__array(p, obj);
        break;
      case PST_TYPE_STRING:
        error = mps_eval__string(p, obj);
        break;
      default:
        dpx_stack_push(&p->stack.operand, pst_copy_obj(obj));
      }
      /* FIXME */
      // pst_release_obj(obj);
    }
    skip_white(strptr, endptr);
  }

  return error;
}

void
mps_eop_cleanup (void)
{
  mpsi *p = &mps_intrp;

  mps_clean_intrp(p);

  return;
}

int
mps_stack_depth (void)
{
  mpsi *p = &mps_intrp;

  return dpx_stack_depth(&p->stack.operand);
}

int
mps_exec_inline (const char **p, const char *endptr, double x_user, double y_user)
{
  int   error = 0;
  int   dirmode, autorotate;

  /* Compatibility for dvipsk. */
  dirmode = pdf_dev_get_dirmode();
  if (dirmode) {
    mp_cmode = MP_CMODE_PTEXVERT;
  } else {
    mp_cmode = MP_CMODE_DVIPSK;
  }

  autorotate = pdf_dev_get_param(PDF_DEV_PARAM_AUTOROTATE);
  pdf_dev_set_param(PDF_DEV_PARAM_AUTOROTATE, 0);
  //pdf_color_push(); /* ... */

  /* Comment in dvipdfm:
   * Remember that x_user and y_user are off by 0.02 %
   */
  pdf_dev_moveto(x_user, y_user);
  mps_init_intrp(&mps_intrp);
  error = mps_parse_body(&mps_intrp, p, endptr);
  mps_clean_intrp(&mps_intrp);

  //pdf_color_pop(); /* ... */
  pdf_dev_set_param(PDF_DEV_PARAM_AUTOROTATE, autorotate);
  pdf_dev_set_dirmode(dirmode);

  return error;
}

/* mp inclusion is a bit of a hack.  The routine
 * starts a form at the lower left corner of
 * the page and then calls begin_form_xobj telling
 * it to record the image drawn there and bundle it
 * up in an xojbect.  This allows us to use the coordinates
 * in the MP file directly.  This appears to be the
 * easiest way to be able to use the pdf_dev_set_string()
 * command (with its scaled and extended fonts) without
 * getting all confused about the coordinate system.
 * After the xobject is created, the whole thing can
 * be scaled any way the user wants
 */
 
/* Should implement save and restore. */
int
mps_include_page (const char *ident, FILE *fp)
{
  int        form_id;
  xform_info info;
  int        st_depth, gs_depth;
  char      *buffer;
  const char *p, *endptr;
  int        length, nb_read;
  int        dirmode, autorotate, error;

  rewind(fp);

  length = file_size(fp);
  if (length < 1) {
    WARN("Can't read any byte in the MPS file.");
    return -1;
  }

  buffer = NEW(length + 1, char);
  buffer[length] = '\0';
  p      = buffer;
  endptr = p + length;

  while (length > 0) {
    nb_read = fread(buffer, sizeof(char), length, fp);
    if (nb_read < 0) {
      RELEASE(buffer);
      WARN("Reading file failed...");
      return -1;
    }
    length -= nb_read;
  }

  error = mps_scan_bbox(&p, endptr, &(info.bbox));
  if (error) {
    WARN("Error occured while scanning MetaPost file headers: Could not find BoundingBox.");
    RELEASE(buffer);
    return -1;
  }
  /* NYI: for mps need to skip prolog? */
  // skip_prolog(&p, endptr);

  dirmode    = pdf_dev_get_dirmode();
  autorotate = pdf_dev_get_param(PDF_DEV_PARAM_AUTOROTATE);
  pdf_dev_set_param(PDF_DEV_PARAM_AUTOROTATE, 0);
  //pdf_color_push();

  form_id  = pdf_doc_begin_grabbing(ident, Xorigin, Yorigin, &(info.bbox));

  mp_cmode = MP_CMODE_MPOST;
  gs_depth = pdf_dev_current_depth();
  // Not Implemented Yet st_depth = mps_stack_depth();
  /* At this point the gstate must be initialized, since it starts a new
   * XObject. Note that it increase gs_depth by 1. */
  pdf_dev_push_gstate();

  mps_init_intrp(&mps_intrp);
  error = mps_parse_body(&mps_intrp, &p, endptr);
  RELEASE(buffer);
  mps_clean_intrp(&mps_intrp);

  if (error) {
    WARN("Errors occured while interpreting MPS file.");
    /* WARN("Leaving garbage in output PDF file."); */
    form_id = -1;
  }

  /* It's time to pop the new gstate above. */
  pdf_dev_pop_gstate();
  // Not Implemented Yet mps_stack_clear_to (st_depth);
  pdf_dev_grestore_to(gs_depth);

  pdf_doc_end_grabbing(NULL);

  //pdf_color_pop();
  pdf_dev_set_param(PDF_DEV_PARAM_AUTOROTATE, autorotate);
  pdf_dev_set_dirmode(dirmode);

  return form_id;
}

int
mps_do_page (FILE *image_file)
{
  int       error = 0;
  pdf_rect  bbox;
  char     *buffer;
  const char *start, *end;
  int       size;
  int       dir_mode;

  rewind(image_file);
  if ((size = file_size(image_file)) == 0) {
    WARN("Can't read any byte in the MPS file.");
    return -1;
  }

  buffer = NEW(size+1, char);
  fread(buffer, sizeof(char), size, image_file);
  buffer[size] = 0;
  start = buffer;
  end   = buffer + size;

  error = mps_scan_bbox(&start, end, &bbox);
  if (error) {
    WARN("Error occured while scanning MetaPost file headers: Could not find BoundingBox.");
    RELEASE(buffer);
    return -1;
  }

  mp_cmode = MP_CMODE_MPOST;

  pdf_doc_begin_page  (1.0, -Xorigin, -Yorigin); /* scale, xorig, yorig */
  pdf_doc_set_mediabox(pdf_doc_current_page_number(), &bbox);

  dir_mode = pdf_dev_get_dirmode();
  pdf_dev_set_autorotate(0);

  /* NYI: for mps need to skip prolog? */
  // skip_prolog(&start, end);

  mps_init_intrp(&mps_intrp);
  error = mps_parse_body(&mps_intrp, &start, end);
  mps_clean_intrp(&mps_intrp);
  if (error) {
    WARN("Errors occured while interpreting MetaPost file.");
  }

  pdf_dev_set_autorotate(1);
  pdf_dev_set_dirmode(dir_mode);

  pdf_doc_end_page();

  RELEASE(buffer);

  /*
   * The reason why we don't return XObject itself is
   * PDF inclusion may not be made so.
   */
  return (error ? -1 : 0);
}
