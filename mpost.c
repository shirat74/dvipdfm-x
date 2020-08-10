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
#include "dpxutil.h"
#include "pst.h"

typedef struct ht_table pst_dict;

typedef struct {
  int       size;
  pst_obj **values;
} pst_array;

typedef struct {
  struct {
    dpx_stack operand;
    dpx_stack dictionary;
  } stack;
  pst_dict systemdict;
  pst_dict globaldict;
  pst_dict userdict;
} mpsi;

typedef int (*mps_op_fn_ptr) (mpsi *);

typedef struct {
  const char    *name;
  mps_op_fn_ptr  action;
} pst_operator;


static mpsi mps_intrp;

static int
mps_pop_get_numbers (mpsi *p, double *values, int count)
{
  int        error = 0;
  dpx_stack *s = &p->stack.operand;
  pst_obj   *tmp;

  while (!error && count-- > 0) {
    tmp = dpx_stack_pop(s);
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
  dpx_stack *s = &p->stack.operand;
  int        i;

  for (i = 0; i < dpx_stack_depth(s); i++) {
    pst_obj *obj;
    obj = dpx_stack_at(&p->stack.operand, i);
    if (PST_MARKTYPE(obj))
      break;
  }

  return i;
}

static int mps_op__exch (mpsi *p)
{
  int        error = 0;
  dpx_stack *s = &p->stack.operand;

  if (dpx_stack_depth(s) < 2) {
    error = -1;
  } else {
    pst_obj *obj1, *obj2;

    obj1 = dpx_stack_pop(s);
    obj2 = dpx_stack_pop(s);
    dpx_stack_push(s, obj1);
    dpx_stack_push(s, obj2);
  }

  return error;
}

static int mps_op__pop (mpsi *p)
{
  int        error = 0;
  dpx_stack *s = &p->stack.operand;
  pst_obj   *obj;

  obj = dpx_stack_pop(s);
  if (!obj) {
    error = -1;
  } else {
    pst_release_obj(obj);
  }

  return error;
}

static int mps_op__dup (mpsi *p)
{
  int error = 0;

  /* Not Implemented Yet */

  return error;
}

static int mps_op__clear (mpsi *p)
{
  int        error = 0;
  dpx_stack *s = &p->stack.operand;
  pst_obj   *obj;

  while ((obj = dpx_stack_pop(s)) != NULL) {
    pst_release_obj(obj);
  }

  return error;
}

static int mps_op__add (mpsi *p)
{
  int        error = 0;
  dpx_stack *s = &p->stack.operand;
  double     v[2];

  error = mps_pop_get_numbers(p, v, 2);
  if (!error)
    dpx_stack_push(s, pst_new_real(v[0] + v[1]));

  return error;
}

static int mps_op__sub (mpsi *p)
{
  int        error = 0;
  dpx_stack *s = &p->stack.operand;
  double     v[2];

  error = mps_pop_get_numbers(p, v, 2);
  if (!error)
    dpx_stack_push(s, pst_new_real(v[0] - v[1]));

  return error;
}

static int mps_op__mul (mpsi *p)
{
  int        error = 0;
  dpx_stack *s = &p->stack.operand;
  double     v[2];

  error = mps_pop_get_numbers(p, v, 2);
  if (!error)
    dpx_stack_push(s, pst_new_real(v[0] * v[1]));

  return error;
}

static int mps_op__div (mpsi *p)
{
  int        error = 0;
  dpx_stack *s = &p->stack.operand;
  double     v[2];

  error = mps_pop_get_numbers(p, v, 2);
  if (!error)
    dpx_stack_push(s, pst_new_real(v[0] / v[1]));

  return error;
}

static int mps_op__neg (mpsi *p)
{
  int        error = 0;
  dpx_stack *s = &p->stack.operand;
  double     v;

  error = mps_pop_get_numbers(p, &v, 1);
  if (!error)
    dpx_stack_push(s, pst_new_real(-v));

  return error;
}

static int mps_op__truncate (mpsi *p)
{
  int        error = 0;
  dpx_stack *s = &p->stack.operand;
  double     v;

  error = mps_pop_get_numbers(p, &v, 1);
  if (!error)
    dpx_stack_push(s, pst_new_integer((v > 0) ? floor(v) : ceil(v)));

  return error;
}

static int mps_op__def (mpsi *p)
{
  int        error = 0;
  dpx_stack *s = &p->stack.operand;
  pst_obj   *key, *value;
  char      *str;
  
  /* NYI: */
  if (dpx_stack_depth(s) >= 2) {
    value = dpx_stack_pop(s);
    key   = dpx_stack_pop(s);
    if (PST_NAMETYPE(key)) {
      str   = pst_getSV(key);
      ht_insert_table(&p->userdict, str, strlen(str), value);
    } else {
      error = -1;
    }
    pst_release_obj(key);
  } else {
    error = -1;
  }

  return error;
}

static int mps_op__bracket_close_sq (mpsi *p)
{
  int        error = 0;
  dpx_stack *s = &p->stack.operand;
  int        count;
  pst_obj   *obj;
  pst_array *array;

  count = mps_count_to_mark(p);
  array = NEW(1, pst_array);
  array->size   = count;
  array->values = NEW(count, pst_obj *);
  while (count-- > 0) {
    obj = dpx_stack_pop(s);
    array->values[count] = obj;
  }
  obj = NEW(1, pst_obj);
  obj->link = 0;
  obj->attr.is_exec = 0;
  obj->type = PST_TYPE_ARRAY;
  obj->data = array;

  dpx_stack_push(s, obj);

  return error;
}

static int mps_op__equal (mpsi *p)
{
  int        error = 0;
  dpx_stack *s = &p->stack.operand;
  pst_obj   *obj;
  char      *str;

  obj = dpx_stack_pop(s);
  if (!obj) {
    error = -1;
  } else {
    str = (char *)pst_getSV(obj);
    pst_release_obj(obj);
    WARN(str);
    RELEASE(str);
  }

  return error;
}

static pst_operator operators[] = {
  {"add",          mps_op__add},
  {"mul",          mps_op__mul},
  {"div",          mps_op__div},
  {"neg",          mps_op__neg},
  {"sub",          mps_op__sub},
  {"truncate",     mps_op__truncate},

  {"clear",        mps_op__clear},
  {"exch",         mps_op__exch},
  {"pop",          mps_op__pop},
  {"dup",          mps_op__dup},

  {"def",          mps_op__def},

  {"]",            mps_op__bracket_close_sq},
  {"=",            mps_op__equal}
};

static int
mps_init_intrp (mpsi *p)
{
  int  error = 0, i;

  dpx_stack_init(&p->stack.operand);
  dpx_stack_init(&p->stack.dictionary);
  ht_init_table(&p->systemdict, pst_release_obj); /* NYI */
  for (i = 0; !error && i < NUM_PS_OPERATORS; i++) {
    pst_obj *obj;
    obj = NEW(1, pst_obj);
    obj->attr.is_exec = 1;
    obj->link = 0;
    obj->type = PST_TYPE_OPERATOR;
    obj->data = &operators[i];
    ht_insert_table(&p->systemdict, operators[i].name, strlen(operators[i].name), obj);
  }
  ht_init_table(&p->userdict, pst_release_obj); /* NYI: */
  ht_init_table(&p->globaldict, pst_release_obj); /* NYI */

  return 0;
}

static int
mps_clean_intrp (mpsi *p)
{
  pst_obj *obj;

  while ((obj = dpx_stack_pop(&p->stack.operand)) != NULL) {
    pst_release_obj(obj);
  }
  while ((obj = dpx_stack_pop(&p->stack.dictionary)) != NULL) {
    pst_release_obj(obj);
  }

  ht_clear_table(&p->systemdict);

  return 0;
}

static int
mps_exec (mpsi *p, pst_obj *obj)
{
  int        error = 0;
  dpx_stack *s = &p->stack.operand;

  switch (obj->type) {
  case PST_TYPE_ARRAY:
    {
      pst_array *data;
      int        i;

      data = obj->data;
      for (i = 0; i < data->size; i++) {
        pst_obj *elem;

        elem = data->values[i];
        if (elem->attr.is_exec) {
          error = mps_exec(p, elem);
        } else {
          dpx_stack_push(s, elem);        
        }
      }
    }
    break;
  case PST_TYPE_NAME:
    if (obj->attr.is_exec) {

    } else {
      dpx_stack_push(s, obj);
    }
  case PST_TYPE_OPERATOR:
    {
      pst_operator *op = obj->data;

      if (op->action)
        error = op->action(p);
    }
    break;
  default:
    dpx_stack_push(s, obj);
  }

  return error;
}

/*
 * The only sections that need to know x_user and y _user are those
 * dealing with texfig.
 */
static int
mp_parse_body (mpsi *mps, const char **p, const char *endptr, double x_user, double y_user)
{
  dpx_stack *s = &mps->stack.operand;
  pst_obj   *obj;
  int        scanning_proc = 0;
  int        error = 0;

  skip_white(p, endptr);
  while (*p < endptr && !error) {
    obj = pst_scan_token((unsigned char **)p, (unsigned char *)endptr);
    if (obj) {
      if (PST_NAMETYPE(obj) && pst_obj_is_exec(obj)) {
        pst_obj *op   = NULL;
        char    *name = (char *)pst_getSV(obj);
        WARN("scanner: %s", name);
        if (scanning_proc) {
          dpx_stack_push(s, obj);
        } else if (!strcmp(name, "}")) {
          pst_obj   *array;
          pst_array *data;
          int        count;

          scanning_proc--;
          count = mps_count_to_mark(mps);
          data  = NEW(1, pst_array);
          data->size = count;
          data->values = NEW(count, pst_obj *);
          while (!error && count-- > 0) {
            pst_obj *elem = dpx_stack_pop(&mps->stack.operand);
            if (elem) {
              data->values[count] = elem;
            } else {
              error = -1;
            }
          }
          if (error) {
            RELEASE(data->values);
            RELEASE(data);
          } else {
            array = NEW(1, pst_obj);
            array->attr.is_exec = 1;
            array->link = 0;
            array->type = PST_TYPE_ARRAY;
            array->data = data;
            dpx_stack_push(s, array);
          }
        } else {
          /* NYI: scan dict stack */
          op = ht_lookup_table(&mps->userdict, name, strlen(name));
          if (op) {
            error = mps_exec(mps, op);
          } else {
            op = ht_lookup_table(&mps->systemdict, name, strlen(name)); 
            if (op) {
              error = mps_exec(mps, op);
            }
          }
        }
        RELEASE(name);
      } else if (PST_UNKNOWNTYPE(obj)) {
        char *name = (char *)pst_getSV(obj);
        if (!strcmp(name, "{")) {
          scanning_proc++;
        }
      } else {
        dpx_stack_push(s, obj);
      }
    }
    skip_white(p, endptr);
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
  error = mp_parse_body(&mps_intrp, p, endptr, x_user, y_user);

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
  skip_prolog(&p, endptr);

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
  error = mp_parse_body(&mps_intrp, &p, endptr, 0.0, 0.0);
  RELEASE(buffer);

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

  skip_prolog(&start, end);

  mps_init_intrp(&mps_intrp);
  error = mp_parse_body(&mps_intrp, &start, end, 0.0, 0.0);

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
