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

#include "mps_main.h"
#include "dvipdfmx.h"

int trace_mps = 0;

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

#define NUM_PS_OPERATORS  (sizeof(operators)/sizeof(operators[0]))

static void
release_obj (void *obj)
{
  if (obj)
    pst_release_obj(obj);
}

static void
dump_stack (mpsi *p)
{
  int i, n = dpx_stack_depth(&p->stack.operand);
  for (i = 0; i < n; i++) {
    pst_obj *obj1  = dpx_stack_at(&p->stack.operand, i);
    if (obj1) {
      char    *stack = (char *) pst_getSV(obj1);
      WARN("stack: %s (%d)", stack ? stack : "(null)", obj1->type);
      if (stack)
        RELEASE(stack);
    } else {
      WARN("stack: null???");
    }
  }
}

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
    if (trace_mps > 1)
      WARN("lookup %s ==> %s ", name, val ? pst_getSV(val) : "null");
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
  if (op && op->action) {
    p->cur_op = op->name;
    if (trace_mps > 2) {
      int i, n = dpx_stack_depth(&p->stack.operand);
      WARN("executing: %s... (%d)", op->name, n);
      dump_stack(p);
    }
    error = op->action(p);
    if (trace_mps > 2) {
      int i, n = dpx_stack_depth(&p->stack.operand);
      WARN("finished %s: %s... (%d)", error ? "NG" : "OK", op->name, n);
      dump_stack(p);
    }
  } else {
    error = -1;
  }
  fflush(stderr); /* DEBUG */

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
    data->link++;
    remain = pst_new_obj(PST_TYPE_STRING, data);
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
    if (trace_mps > 5) {
      int i;
      for (i = obj->comp.off; i < obj->comp.off + obj->comp.size; i++) {
        char *str = pst_getSV(data->values[i]);
        WARN("array[%d]: %s", i, str);
        if (str) RELEASE(str);
      }
    }
    if (obj->comp.size > 1) {
      data->link++;
      remain = pst_new_obj(PST_TYPE_ARRAY, data);
      remain->attr = obj->attr;
      remain->comp.off  = obj->comp.off + 1;
      remain->comp.size = obj->comp.size - 1;
    }
    if (obj->comp.size > 0) {
      pst_obj *obj1 = data->values[obj->comp.off];
      if (obj1)
        first = pst_copy_obj(obj1);
    }
  }
  if (remain) {
    if (trace_mps) {
      char *str = (char *) pst_getSV(first);
      WARN("eval__array: push remain => %s", str);
      if (str) RELEASE(str);
    }
    dpx_stack_push(&p->stack.exec, remain);
  }
  if (first) {
    if (trace_mps) {
      char *str = (char *) pst_getSV(first);
      WARN("eval__array: push first => %s", str);
      if (str) RELEASE(str);
    }
    if (PST_ARRAYTYPE(first)) {
      dpx_stack_push(&p->stack.operand, first);
    } else {
      dpx_stack_push(&p->stack.exec, first);
    }
  }

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

/* NYI */
static int mps_op__realtime (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;

  dpx_stack_push(stk, pst_new_integer(0));

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
  {"bind",         mps_op__bind},
  {"null",         mps_op__null},
  {"=",            mps_op__equal},
  {"save",         mps_op__dummy}, /* Not Implemented */
  {"restore",      mps_op__pop},
  {"realtime",     mps_op__realtime}, /* pstricks-add.pro requires this */
  {"setglobal",    mps_op__pop}, /* NYI */
  {"normalscale",  mps_op__noop}, /* NYI */
  {"pdfmark",      mps_op__pop},  /* dummy */
};

pst_obj *
pst_new_dict (size_t size)
{
  pst_obj  *obj;
  pst_dict *dict;

  dict = NEW(1, pst_dict);
  dict->link   = 0;
  dict->access = acc_unlimited;
  dict->size   = size;
  dict->values = NEW(1, struct ht_table);
  ht_init_table(dict->values, release_obj);

  obj = pst_new_obj(PST_TYPE_DICT, dict);

  return obj;
}

#if 1
#include "mps_op_arith.h"
#include "mps_op_basic.h"
#include "mps_op_graph.h"
#include "mps_op_objm.h"
#endif

int
mps_add_systemdict (mpsi *p, pst_obj *obj)
{
  pst_dict *systemdict = p->systemdict->data;
  pst_operator *op = obj->data;

  ht_insert_table(systemdict->values, op->name, strlen(op->name), obj);

  return 0;
}

int
mps_init_intrp (mpsi *p)
{
  int       error = 0, i;
  pst_dict *systemdict;
  pst_dict *statusdict, *errordict;

  p->cur_op = NULL;
  dpx_stack_init(&p->stack.operand);

  dpx_stack_init(&p->stack.dict);
  p->systemdict = pst_new_dict(-1);
  p->globaldict = pst_new_dict(-1);
  p->userdict   = pst_new_dict(-1);
  dpx_stack_push(&p->stack.dict, pst_copy_obj(p->systemdict));
  dpx_stack_push(&p->stack.dict, pst_copy_obj(p->globaldict));
  dpx_stack_push(&p->stack.dict, pst_copy_obj(p->userdict));

  systemdict = p->systemdict->data;  
  statusdict = pst_new_dict(-1);
  ht_insert_table(systemdict->values, "statusdict", strlen("statusdict"), statusdict);
  errordict  = pst_new_dict(-1);
  ht_insert_table(systemdict->values, "errordict", strlen("errordict"), errordict);
  for (i = 0; !error && i < NUM_PS_OPERATORS; i++) {
    pst_obj  *obj;

    obj  = pst_new_obj(PST_TYPE_OPERATOR, &operators[i]);
    obj->attr.is_exec = 1;
    ht_insert_table(systemdict->values, operators[i].name, strlen(operators[i].name), obj);
  }
#if 1
  mps_op_arith_load(p);
  mps_op_basic_load(p);
  mps_op_graph_load(p);
  mps_op_objm_load (p);
#endif

  dpx_stack_init(&p->stack.exec);

  p->rand_seed = 0;

  return 0;
}

int
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

  pst_release_obj(p->systemdict);
  pst_release_obj(p->globaldict);
  pst_release_obj(p->userdict);

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
  if (where) {
    *where = obj ? dict : NULL;
  }

  return obj;
}

#if 0
static pst_obj *
mps_search_systemdict (mpsi *p, const char *key)
{
  pst_obj  *obj;
  pst_dict *dict;

  dict = p->systemdict->data;
  obj  = ht_lookup_table(dict->values, key, strlen(key));

  return obj;
}
#endif

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
    if (trace_mps) {
      char *str = (char *) pst_getSV(obj);
      WARN("scan: %s", str); /* DEBUG */
      if (str) RELEASE(str);
    }
    if (PST_NAMETYPE(obj) && obj->attr.is_exec) {
      dpx_stack_push(&p->stack.exec, obj);
      if (trace_mps) {
        char *str = (char *) pst_getSV(obj);
        WARN("push exec:  %s", str);
        if (str) RELEASE(str);
      }
    } else {
      if (trace_mps) {
        char *str = (char *) pst_getSV(obj);
        WARN("push oprnd: %s", pst_getSV(obj));
        if (str) RELEASE(str);
      }
      dpx_stack_push(&p->stack.operand, obj);
      continue;
    }

    while (!error && (obj = dpx_stack_pop(&p->stack.exec)) != NULL) {
      if (!obj->attr.is_exec) {
        dpx_stack_push(&p->stack.operand, obj);
        if (trace_mps) {
          char *str = (char *) pst_getSV(obj);
          WARN("push oprnd: %s", str);
          if (str) RELEASE(str);
        }
        continue;
      }
      if (trace_mps) {
        if (PST_OPERATORTYPE(obj)) {
          WARN("exec oprnd: %s", ((pst_operator *)obj->data)->name);
        } else {
          char *str = (char *) pst_getSV(obj);
          WARN("exec oprnd: %s (type=%d)", str, obj->type);
          if (str) RELEASE(str);
        }
      }
      switch (obj->type) {
      case PST_TYPE_OPERATOR:
        error = mps_eval__operator(p, obj);
        if (error && trace_mps) { /* DEBUG */
          WARN("eval_op failed: %s", p->cur_op);
        }
        break;
      case PST_TYPE_NAME:
        error = mps_eval__name(p, obj);
        if (error && trace_mps) { /* DEBUG */
          char *str = (char *) pst_getSV(obj);
          WARN("eval_name failed: %s", str);
          if (str) RELEASE(str);
        }
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
      pst_release_obj(obj);
    }
    skip_white(strptr, endptr);
  }
  if (error) {
    WARN("Offending command: %s", p->cur_op ? p->cur_op : "null");
    ERROR("Cannot continue");
  }

  return error;
}

int
mps_stack_depth (mpsi *p)
{
  return dpx_stack_depth(&p->stack.operand);
}

int
mps_exec_inline (mpsi *mps_intrp, const char **p, const char *endptr, double x_user, double y_user)
{
  int   error = 0;
  int   dirmode, autorotate;

  if (trace_mps) {
    WARN("mps_exec_inline called...");
  }
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
  error = mps_parse_body(mps_intrp, p, endptr);

  // pdf_color_pop(); /* ... */
  pdf_dev_set_param(PDF_DEV_PARAM_AUTOROTATE, autorotate);
  pdf_dev_set_dirmode(dirmode);

  return error;
}
