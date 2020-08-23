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

static pst_obj *
pst_new_array (size_t size)
{
  pst_obj   *obj;
  pst_array *data;
  int        i;

  data = NEW(1, pst_array);
  data->link = 0;
  data->size   = size;
  data->values = NEW(size, pst_obj *);
  for (i = 0; i < size; i++) {
    data->values[i] = pst_new_null();
  }
  obj = pst_new_obj(PST_TYPE_ARRAY, data);

  return obj;
}

static int mps_op__array (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *num, *array;
  int        n;

  if (dpx_stack_depth(stk) < 1)
    return -1;
  num = dpx_stack_top(stk);
  if (!PST_INTEGERTYPE(num))
    return -1;
  
  num = dpx_stack_pop(stk);
  n   = pst_getIV(num);
  pst_release_obj(num);

  array = pst_new_array(n);
  dpx_stack_push(stk, array);

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

static int mps_op__dict (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *num, *dict;
  int        n;

  if (dpx_stack_depth(stk) < 1)
    return -1;
  num = dpx_stack_top(stk);
  if (!PST_INTEGERTYPE(num))
    return -1;
  
  num = dpx_stack_pop(stk);
  n   = pst_getIV(num);
  pst_release_obj(num);

  dict = pst_new_dict(n);
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

static int mps_op__get (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *obj = NULL, *obj1, *obj2;

  if (dpx_stack_depth(stk) < 2)
    return -1;
  obj1 = dpx_stack_at(stk, 0);
  obj2 = dpx_stack_at(stk, 1);
  if (!(PST_NAMETYPE(obj1) && PST_DICTTYPE(obj2)) &&
      !(PST_INTEGERTYPE(obj1) && PST_ARRAYTYPE(obj2)) &&
      !(PST_INTEGERTYPE(obj1) && PST_STRINGTYPE(obj2)))
    return -1;
  
  obj1 = dpx_stack_pop(stk);
  obj2 = dpx_stack_pop(stk);

  switch (obj2->type) {
  case PST_TYPE_DICT:
    {
      pst_dict *data = obj2->data;
      char     *key;

      key  = (char *) pst_getSV(obj1);
      
      obj = ht_lookup_table(data->values, key, strlen(key));

      RELEASE(key);
    }
    if (!obj)
      error = -1; /* undefiend */
    break;
  case PST_TYPE_ARRAY:
    {
      pst_array *data = obj2->data;
      int        idx;

      idx = pst_getIV(obj1);
      if (idx < 0 || idx >= obj2->comp.size) {
        error = -1; /* rangecheck */
      } else {
        idx += obj2->comp.off;
        obj  = data->values[idx];
      }
    }
    break;
   case PST_TYPE_STRING:
    {
      pst_string *data = obj2->data;
      int         idx;

      idx = pst_getIV(obj1);
      if (idx < 0 || idx >= obj2->comp.size) {
        error = -1; /* rangecheck */
      } else {
        idx += obj2->comp.off;
        obj  = pst_new_integer(data->value[idx]);
      }
    }
    break;   
  }
  if (!error && obj) {
    dpx_stack_push(stk, pst_copy_obj(obj));
    pst_release_obj(obj1);
    pst_release_obj(obj2);
  }

  return error;
}

static int mps_op__forall_dict (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *proc, *obj, *copy;

  proc  = dpx_stack_pop(stk);
  obj   = dpx_stack_pop(stk);
  if (pst_length_of(obj) == 0 || (obj->comp.iter && ht_iter_getval(obj->comp.iter) == NULL)) {
    pst_release_obj(proc);
    pst_release_obj(obj);
    return 0;
  }

  {
    pst_obj *cvx, *this;

    this = mps_search_systemdict(p, "forall");
    dpx_stack_push(&p->stack.exec, pst_copy_obj(this));
    cvx  = mps_search_systemdict(p, "cvx");
    dpx_stack_push(&p->stack.exec, pst_copy_obj(cvx));
  }
  copy = pst_copy_obj(proc);
  copy->attr.is_exec = 0; /* cvlit */
  dpx_stack_push(&p->stack.exec, copy);
  dpx_stack_push(&p->stack.exec, obj);

  dpx_stack_push(&p->stack.exec, proc);
  {
    struct ht_iter *iter = obj->comp.iter;
    pst_dict       *dict = obj->data;
    int             keylen = 0;
    char           *key;
    pst_obj        *value;

    if (!iter) {
      iter = NEW(1, struct ht_iter);
      ht_clear_iter(iter);
      ht_set_iter(dict->values, iter);
      obj->comp.iter = iter;
    }
    key   = ht_iter_getkey(iter, &keylen);
    value = ht_iter_getval(iter);
    dpx_stack_push(&p->stack.operand, pst_new_name(key, 0));
    dpx_stack_push(&p->stack.operand, pst_copy_obj(value));

    ht_iter_next(iter);
  }

  return error;
}

static int mps_op__forall_array (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *proc, *obj, *copy;

  proc  = dpx_stack_pop(stk);
  obj   = dpx_stack_pop(stk);
  if (pst_length_of(obj) == 0) {
    pst_release_obj(proc);
    pst_release_obj(obj);
    return 0;
  }
  
  {
    pst_obj *cvx, *this;

    this = mps_search_systemdict(p, "forall");
    dpx_stack_push(&p->stack.exec, pst_copy_obj(this));
    cvx  = mps_search_systemdict(p, "cvx");
    dpx_stack_push(&p->stack.exec, pst_copy_obj(cvx));
  }
  copy = pst_copy_obj(proc);
  copy->attr.is_exec = 0; /* cvlit */
  dpx_stack_push(&p->stack.exec, copy);
  dpx_stack_push(&p->stack.exec, obj);

  dpx_stack_push(&p->stack.exec, proc);
  {
    pst_array *array = obj->data;
    pst_obj   *value;

    value = array->values[obj->comp.off];
    obj->comp.off++;
    obj->comp.size--;

    dpx_stack_push(&p->stack.operand, pst_copy_obj(value));
  }

  return error;
}

static int mps_op__forall_string (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk   = &p->stack.operand;
  pst_obj   *proc, *obj, *copy;

  proc  = dpx_stack_pop(stk);
  obj   = dpx_stack_pop(stk);
  if (pst_length_of(obj) == 0) {
    pst_release_obj(proc);
    pst_release_obj(obj);
    return 0;
  }
  
  {
    pst_obj *cvx, *this;

    this = mps_search_systemdict(p, "forall");
    dpx_stack_push(&p->stack.exec, pst_copy_obj(this));
    cvx  = mps_search_systemdict(p, "cvx");
    dpx_stack_push(&p->stack.exec, pst_copy_obj(cvx));
  }
  copy = pst_copy_obj(proc);
  copy->attr.is_exec = 0; /* cvlit */
  dpx_stack_push(&p->stack.exec, copy);
  dpx_stack_push(&p->stack.exec, obj);

  dpx_stack_push(&p->stack.exec, proc);
  {
    pst_string *str = obj->data;
    int         value;

    value = (int) str->value[obj->comp.off];
    obj->comp.off++;
    obj->comp.size--;

    dpx_stack_push(&p->stack.operand, pst_new_integer(value));
  }

  return error;
}

static int mps_op__forall (mpsi *p)
{
  int        error = 0;
  dpx_stack *stk = &p->stack.operand;
  pst_obj   *proc, *obj;

  if (dpx_stack_depth(stk) < 2)
    return -1;
  proc = dpx_stack_at(stk, 0);
  if (!PST_ARRAYTYPE(proc) || !proc->attr.is_exec)
    return -1;
  obj  = dpx_stack_at(stk, 1);
  switch (obj->type) {
  case PST_TYPE_DICT:
    error = mps_op__forall_dict(p);
    break;
  case PST_TYPE_ARRAY:
    error = mps_op__forall_array(p);
    break;
  case PST_TYPE_STRING:
    error = mps_op__forall_string(p);
    break;
  default:
    error = -1; /* typecheck */
  }

  return error;
}


/* Duplicate */
static int mps_op__mark (mpsi *p)
{
  dpx_stack *stk = &p->stack.operand;

  dpx_stack_push(stk, pst_new_mark());

  return 0;
}

#define NUM_PS_OPERATORS  (sizeof(operators)/sizeof(operators[0]))

static pst_operator operators[] = {
  {"array",        mps_op__array},
  {"[",            mps_op__mark},
  {"]",            mps_op__array_to_mark},

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
  {"forall",       mps_op__forall},

};

int mps_op_objm_load (mpsi *p)
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
