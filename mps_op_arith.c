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

#define NUM_PS_OPERATORS  (sizeof(operators)/sizeof(operators[0]))
#include "dpxutil.h"
#include "pst.h"


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

static int mps_op__rand (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  int        n;

  n = (int) rand();
  dpx_stack_push(stk, pst_new_integer(n));

  return error;
}

static int mps_op__srand (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj;
  int        n;

  if (dpx_stack_depth(stk) < 1)
    return -1;
  error = typecheck_integers(stk, 1);
  if (error < 0)
    return error;

  obj = dpx_stack_pop(stk);
  n   = pst_getIV(obj);
  pst_release_obj(obj);

  srand(n);
  p->rand_seed = n;

  return error;
}

static int mps_op__rrand (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;

  dpx_stack_push(stk, pst_new_integer(p->rand_seed));

  return error;
}

#define NUM_PS_OPERATORS  (sizeof(operators)/sizeof(operators[0]))

static pst_operator operators[] = {
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
  {"rand",         mps_op__rand},
  {"srand",        mps_op__srand},
  {"rrand",        mps_op__rrand},
};

int mps_op_arith_load (mpsi *p)
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
