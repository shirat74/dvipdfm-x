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

#include "mpost.h"

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

static pst_obj *
mps_search_systemdict (mpsi *p, const char *key)
{
  pst_obj  *obj;
  pst_dict *dict;

  dict = p->systemdict->data;
  obj  = ht_lookup_table(dict->values, key, strlen(key));

  return obj;
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

static int typecheck_integers (dpx_stack *stk, int n)
{
  while (n-- > 0) {
    pst_obj *obj = dpx_stack_at(stk, n);
    if (!PST_INTEGERTYPE(obj))
      return -1;
  }
  return 0;
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
  if (!PST_INTEGERTYPE(obj) && !PST_BOOLEANTYPE(obj))
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
    pst_obj  *cvx, *this;

    this = mps_search_systemdict(p, "for");
    dpx_stack_push(&p->stack.exec, pst_copy_obj(this));
    cvx  = mps_search_systemdict(p, "cvx");
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
    pst_obj  *cvx, *this;

    this = mps_search_systemdict(p, "repeat");
    dpx_stack_push(&p->stack.exec, pst_copy_obj(this));
    cvx  = mps_search_systemdict(p, "cvx");
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
    pst_obj *cvx, *this;

    this = mps_search_systemdict(p, "loop");
    dpx_stack_push(&p->stack.exec, pst_copy_obj(this));
    cvx  = mps_search_systemdict(p, "cvx");
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
      if (!strcmp(op->name, "cshow") || !strcmp(op->name, "kshow") ||
          !strcmp(op->name, "pathforall") || !strcmp(op->name, "filenameforall") ||
          !strcmp(op->name, "resourceforall") || 
          !strcmp(op->name, "for") || !strcmp(op->name, "repeat") ||
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

static int mps_op__countexecstack (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  int        n;

  n = dpx_stack_depth(&p->stack.exec);
  dpx_stack_push(stk, pst_new_integer(n));

  return error;
}

static int mps_op__execstack (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj;
  int        n;

  if (dpx_stack_depth(stk) < 1)
    return -1;
  obj = dpx_stack_top(stk);
  if (!PST_ARRAYTYPE(obj))
    return -1;
  if (pst_length_of(obj) < dpx_stack_depth(&p->stack.exec))
    return -1; /* rangecheck */

  obj = dpx_stack_pop(stk);
  n   = dpx_stack_depth(&p->stack.exec);

  obj->comp.size = n;
  
  /* FIXME */
  {
    pst_array *data = obj->data;
    
    while (n-- > 0) {
      int      m;
      pst_obj *elem;

      m    = obj->comp.off + n;
      elem = dpx_stack_at(&p->stack.exec, m);
      if (data->values[m])
        pst_release_obj(data->values[m]);
      data->values[m] = pst_copy_obj(elem);
    }
  }
  dpx_stack_push(stk, obj);

  return error;
}

/* NYI */
static int mps_op__quit (mpsi *p)
{
  WARN("mps: \"quit\" called.");

  return 0;
}

static int mps_op__start (mpsi *p)
{
  return 0;
}

static int mps_op__type (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj, *name = NULL;

  if (dpx_stack_depth(stk) < 1)
    return -1;

  obj = dpx_stack_top(stk);
  switch(obj->type) {
  case PST_TYPE_ARRAY:
    name = pst_new_name("arraytype", 0);
    break;
  case PST_TYPE_BOOLEAN:
    name = pst_new_name("booleantype", 0);
    break;
  case PST_TYPE_DICT:
    name = pst_new_name("dicttype", 0);
    break;
  case PST_TYPE_INTEGER:
    name = pst_new_name("integertype", 0);
    break;
  case PST_TYPE_MARK:
    name = pst_new_name("marktype", 0);
    break;
  case PST_TYPE_NAME:
    name = pst_new_name("nametype", 0);
    break;
  case PST_TYPE_NULL:
    name = pst_new_name("nulltype", 0);
    break;
  case PST_TYPE_OPERATOR:
    name = pst_new_name("operatortype", 0);
    break;
  case PST_TYPE_REAL:
    name = pst_new_name("realtype", 0);
    break;
  case PST_TYPE_STRING:
    name = pst_new_name("stringtype", 0);
    break;
  default:
    error = -1;
  }

  if (!error) {
    obj = dpx_stack_pop(stk);
    pst_release_obj(obj);
  }
  if (name)
    dpx_stack_push(stk, name);

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

static int mps_op__xcheck (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj;

  if (dpx_stack_depth(stk) < 1)
    return -1;

  obj = dpx_stack_pop(stk);
  dpx_stack_push(stk, pst_new_boolean(obj->attr.is_exec));
  pst_release_obj(obj);

  return error;
}

static int mps_op__executeonly (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj;

  if (dpx_stack_depth(stk) < 1)
    return -1;

  obj = dpx_stack_pop(stk);
  if (PST_DICTTYPE(obj)) {
    pst_dict *data = obj->data;
    data->access = acc_executeonly;
  } else {
    obj->attr.access = acc_executeonly;
  }
  dpx_stack_push(stk, obj);

  return error;
}

static int mps_op__noaccess (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj;

  if (dpx_stack_depth(stk) < 1)
    return -1;

  obj = dpx_stack_pop(stk);
  if (PST_DICTTYPE(obj)) {
    pst_dict *data = obj->data;
    data->access = acc_none;
  } else {
    obj->attr.access = acc_none;
  }
  dpx_stack_push(stk, obj);

  return error;
}

static int mps_op__readonly (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj;

  if (dpx_stack_depth(stk) < 1)
    return -1;

  obj = dpx_stack_pop(stk);
  if (PST_DICTTYPE(obj)) {
    pst_dict *data = obj->data;
    data->access = acc_readonly;
  } else {
    obj->attr.access = acc_readonly;
  }
  dpx_stack_push(stk, obj);

  return error;
}

static int mps_op__rcheck (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj;
  int        r = 1;

  if (dpx_stack_depth(stk) < 1)
    return -1;

  obj = dpx_stack_pop(stk);
  if (PST_DICTTYPE(obj)) {
    pst_dict *data = obj->data;
    r = data->access <= acc_readonly ? 1 : 0;
  } else {
    r = obj->attr.access <= acc_readonly ? 1 : 0;
  }
  dpx_stack_push(stk, pst_new_boolean(r));
  pst_release_obj(obj);

  return error;
}

static int mps_op__wcheck (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj;
  int        r = 1;

  if (dpx_stack_depth(stk) < 1)
    return -1;

  obj = dpx_stack_pop(stk);
  if (PST_DICTTYPE(obj)) {
    pst_dict *data = obj->data;
    r = data->access == acc_unlimited ? 1 : 0;
  } else {
    r = obj->attr.access == acc_unlimited ? 1 : 0;
  }
  dpx_stack_push(stk, pst_new_boolean(r));
  pst_release_obj(obj);

  return error;
}

static int mps_op__cvi (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj;
  int        val;

  if (dpx_stack_depth(stk) < 1)
    return -1;
  obj = dpx_stack_top(stk);
  if (!PST_NUMBERTYPE(obj) && !PST_STRINGTYPE(obj))
    return -1; /* typecheck */

  obj = dpx_stack_pop(stk);
  val = pst_getIV(obj);
  pst_release_obj(obj);

  dpx_stack_push(stk, pst_new_integer(val));

  return error;
}

static int mps_op__cvn (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj;
  char      *val;
  int        exe;

  if (dpx_stack_depth(stk) < 1)
    return -1;
  obj = dpx_stack_top(stk);
  if (!PST_STRINGTYPE(obj))
    return -1; /* typecheck */

  obj = dpx_stack_pop(stk);
  val = (char *) pst_getSV(obj); /* Need to check */
  exe = obj->attr.is_exec;
  pst_release_obj(obj);

  dpx_stack_push(stk, pst_new_name(val, exe));

  return error;
}

static int mps_op__cvr (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj;
  double     val;

  if (dpx_stack_depth(stk) < 1)
    return -1;
  obj = dpx_stack_top(stk);
  if (!PST_NUMBERTYPE(obj) && !PST_STRINGTYPE(obj))
    return -1; /* typecheck */

  val = pst_getRV(obj);
  pst_release_obj(obj);

  dpx_stack_push(stk, pst_new_real(val));

  return error;
}

#if 0
/* NYI */
static int mps_op__cvr (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *num, *rdx, *str;

  if (dpx_stack_depth(stk) < 3)
    return -1;
  str = dpx_stack_at(stk, 0);
  rdx = dpx_stack_at(stk, 1);
  num = dpx_stack_at(stk, 2);
  if (!PST_NUMBERTYPE(num) || !PST_INTEGERTYPE(rdx) || !PST_STRINGTYPE(str))
    return -1; /* typecheck */


  return error;
}

static int mps_op__cvs (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;

  return error;
}
#endif

#define NUM_PS_OPERATORS  (sizeof(operators)/sizeof(operators[0]))

static pst_operator operators[] = {
  {"pop",            mps_op__pop},
  {"exch",           mps_op__exch},
  {"dup",            mps_op__dup},
  {"copy",           mps_op__copy},
  {"index",          mps_op__index},
  {"roll",           mps_op__roll},
  {"clear",          mps_op__clear},
  {"count",          mps_op__count},
  {"mark",           mps_op__mark},
  {"cleartomark",    mps_op__cleartomark},
  {"counttomark",    mps_op__counttomark},

  {"eq",             mps_op__eq},
  {"ne",             mps_op__ne},
  {"ge",             mps_op__ge},
  {"gt",             mps_op__gt},
  {"le",             mps_op__le},
  {"lt",             mps_op__lt},
  {"and",            mps_op__and},
  {"not",            mps_op__not},
  {"or",             mps_op__or},
  {"xor",            mps_op__xor},
  {"true",           mps_op__true},
  {"false",          mps_op__false},
  {"bitshift",       mps_op__bitshift},

  {"exec",           mps_op__exec},
  {"if",             mps_op__if},
  {"ifelse",         mps_op__ifelse},
  {"for",            mps_op__for},
  {"repeat",         mps_op__repeat},
  {"loop",           mps_op__loop},
  {"exit",           mps_op__exit},
  {"stop",           mps_op__stop},
  {"stopped",        mps_op__stopped},
  {"countexecstack", mps_op__countexecstack},
  {"execstack",      mps_op__execstack},
  {"quit",           mps_op__quit},  /* NYI */
  {"start",          mps_op__start}, /* NYI */

  {"type",           mps_op__type},
  {"cvlit",          mps_op__cvlit},
  {"cvx",            mps_op__cvx},
  {"xcheck",         mps_op__xcheck},
  {"executeonly",    mps_op__executeonly},
  {"noaccess",       mps_op__noaccess},
  {"readonly",       mps_op__readonly},
  {"rcheck",         mps_op__rcheck},
  {"wcheck",         mps_op__wcheck},
  {"cvi",            mps_op__cvi},
  {"cvn",            mps_op__cvn},
  {"cvr",            mps_op__cvr},
#if 0
  {"cvrs",           mps_op__cvrs},
  {"cvs",            mps_op__cvs}
#endif
};


int mps_op_basic_load (mpsi *p)
{
  int   i;

  for (i = 0; i < NUM_PS_OPERATORS; i++) {
    pst_obj  *obj;

    obj  = pst_new_obj(PST_TYPE_OPERATOR, &operators[i]);
    obj->attr.is_exec = 1;
    mps_add_systemdict(p, obj);
  }

  return 0;
}
