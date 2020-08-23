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
  {"restore",      mps_op__pop} 
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
  mps_op_arith_load(p);
  mps_op_basic_load(p);
  mps_op_graph_load(p);
  mps_op_objm_load (p);
#endif

  dpx_stack_init(&p->stack.exec);

  p->rand_seed = 0;

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
