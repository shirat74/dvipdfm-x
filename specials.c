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

#include <stdarg.h>

#include "system.h"
#include "mem.h"
#include "error.h"
#include "numbers.h"
#include "dpxconf.h"

#include "dvi.h"

#include "pdfobj.h"
#include "pdfparse.h"
#include "pdfdoc.h"
#include "pdfnames.h"

#include "pdfdraw.h"
#include "pdfdev.h"

#include "spc_pdfm.h"
#include "spc_tpic.h"
#include "spc_html.h"
#include "spc_misc.h"
#include "spc_color.h"
#include "spc_dvips.h"
#include "spc_dvipdfmx.h"
#include "spc_xtx.h"

#include "specials.h"

#define THEBUFFLENGTH 1024
void
spc_warn (struct spc_env *spe, const char *fmt, ...)
{
  va_list  ap;
  static char buf[THEBUFFLENGTH];

  va_start(ap, fmt);

  vsnprintf(buf, THEBUFFLENGTH, fmt, ap);
  WARN(buf);

  va_end(ap);

  return;
}


/* This is currently just to make other spc_xxx to not directly
 * call dvi_xxx.
 */
int
spc_begin_annot (struct spc_env *spe, pdf_obj *dict)
{
  pdf_doc_begin_annot(dict);
  dvi_tag_depth(); /* Tell dvi interpreter to handle line-break. */
  return  0;
}

int
spc_end_annot (struct spc_env *spe)
{
  dvi_untag_depth();
  pdf_doc_end_annot();
  return  0;
}

int
spc_resume_annot (struct spc_env *spe)
{
  dvi_link_annot(1);
  return  0;
}

int
spc_suspend_annot (struct spc_env *spe)
{
  dvi_link_annot(0);
  return  0;
}

/* Added this for supporting bann-eann erea only with \phantom text */
int
spc_is_tracking_boxes (struct spc_env *spe)
{
  return dvi_is_tracking_boxes();
}

void
spc_set_linkmode (struct spc_env *spe, int mode)
{
  dvi_set_linkmode(mode);
}

void
spc_set_phantom (struct spc_env *spe, double height, double depth)
{
  dvi_set_phantom_height(height, depth);
}


/* reserved keys */
static const char *_rkeys[] = {
#define  K_OBJ__XPOS      0
#define  K_OBJ__YPOS      1
  "xpos", "ypos",
#define  K_OBJ__THISPAGE  2
#define  K_OBJ__PREVPAGE  3
#define  K_OBJ__NEXTPAGE  4
  "thispage", "prevpage", "nextpage",
#define  K_OBJ__RESOURCES 5
  "resources",
#define  K_OBJ__PAGES     6
#define  K_OBJ__NAMES     7
  "pages", "names",
#define  K_OBJ__CATALOG   8
#define  K_OBJ__DOCINFO   9
  "catalog", "docinfo",
#if  0
#define  K_OBJ__TRAILER  10
  "trailer",
#endif /* NYI */
  NULL
};

/* pageN where N is a positive integer.
 * Note that page need not exist at this time.
 */
static int
ispageref (const char *key)
{
  const char  *p;
  if (strlen(key) <= strlen("page") ||
      memcmp(key, "page", strlen("page")))
    return  0;
  else {
    for (p = key + 4; *p && *p >= '0' && *p <= '9'; p++);
    if (*p != '\0')
      return  0;
  }
  return  1;
}

/*
 * The following routine returns copies, not the original object.
 */
pdf_obj *
spc_lookup_reference (const char *key)
{
  pdf_obj    *value = NULL;
  pdf_coord   cp;
  int         k;

  if (!key)
    return  NULL;

  for (k = 0; _rkeys[k] && strcmp(key, _rkeys[k]); k++);
  switch (k) {
  /* xpos and ypos must be position in device space here. */
  case  K_OBJ__XPOS:
    cp.x = dvi_dev_xpos(); cp.y = dvi_dev_ypos();
    pdf_dev_transform(&cp, NULL);
    value = pdf_new_number(ROUND(cp.x, .01));
    break;
  case  K_OBJ__YPOS:
    cp.x = dvi_dev_xpos(); cp.y = dvi_dev_ypos();
    pdf_dev_transform(&cp, NULL);
    value = pdf_new_number(ROUND(cp.y, .01));
    break;
  case  K_OBJ__THISPAGE:
    value = pdf_doc_this_page_ref();
    break;
  case  K_OBJ__PREVPAGE:
    value = pdf_doc_prev_page_ref();
    break;
  case  K_OBJ__NEXTPAGE:
    value = pdf_doc_next_page_ref();
    break;
  case  K_OBJ__PAGES:
    value = pdf_ref_obj(pdf_doc_page_tree());
    break;
  case  K_OBJ__NAMES:
    value = pdf_ref_obj(pdf_doc_names());
    break;
  case  K_OBJ__RESOURCES:
    value = pdf_ref_obj(pdf_doc_current_page_resources());
    break;
  case  K_OBJ__CATALOG:
    value = pdf_ref_obj(pdf_doc_catalog());
    break;
  case  K_OBJ__DOCINFO:
    value = pdf_ref_obj(pdf_doc_docinfo());
    break;
  default:
    if (ispageref(key))
      value = pdf_doc_ref_page(atoi(key + 4));
    else {
      value = pdf_names_lookup_reference(global_names, key, strlen(key));
    }
    break;
  }

  if (!value) {
    ERROR("Object reference %s not exist.", key);
  }

  return  value;
}

pdf_obj *
spc_lookup_object (const char *key)
{
  pdf_obj    *value = NULL;
  pdf_coord   cp;
  int         k;

  if (!key)
    return  NULL;

  for (k = 0; _rkeys[k] && strcmp(key, _rkeys[k]); k++);
  switch (k) {
  case  K_OBJ__XPOS:
    cp.x = dvi_dev_xpos(); cp.y = dvi_dev_ypos();
    pdf_dev_transform(&cp, NULL);
    value = pdf_new_number(ROUND(cp.x, .01));
    break;
  case  K_OBJ__YPOS:
    cp.x = dvi_dev_xpos(); cp.y = dvi_dev_ypos();
    pdf_dev_transform(&cp, NULL);
    value = pdf_new_number(ROUND(cp.y, .01));
    break;
  case  K_OBJ__THISPAGE:
    value = pdf_doc_this_page();
    break;
  case  K_OBJ__PAGES:
    value = pdf_doc_page_tree();
    break;
  case  K_OBJ__NAMES:
    value = pdf_doc_names();
    break;
  case  K_OBJ__RESOURCES:
    value = pdf_doc_current_page_resources();
    break;
  case  K_OBJ__CATALOG:
    value = pdf_doc_catalog();
    break;
  case  K_OBJ__DOCINFO:
    value = pdf_doc_docinfo();
    break;
  default:
    value = pdf_names_lookup_object(global_names, key, strlen(key));
    break;
  }

/* spc_handler_pdfm_bead() in spc_pdfm.c controls NULL too.
  if (!value) {
    ERROR("Object reference %s not exist.", key);
  }
*/

  return  value;
}

void
spc_push_object (struct spc_env *spe, const char *key, pdf_obj *value)
{
  if (!key || !value)
    return;

  pdf_names_add_object(global_names, key, strlen(key), value);
}

void
spc_flush_object (struct spc_env *spe, const char *key)
{
  pdf_names_close_object(global_names, key, strlen(key));
}

void
spc_clear_objects (struct spc_env *spe)
{
  /* Do nothing... */
}

/* Migrated form pdf_dev.c
 * No need to palce this into pdfdev.c at all.
 */
static dpx_stack coords;

void
spc_get_coord (struct spc_env *spe, double *x, double *y)
{
  ASSERT(x && y );

  if (dpx_stack_depth(&coords) > 0) {
    pdf_coord *p = dpx_stack_top(&coords);
    *x = p->x;
    *y = p->y;
  } else {
    *x = *y = 0.0;
  }
}

void
spc_push_coord (struct spc_env *spe, double x, double y)
{
  pdf_coord *p;

  p = NEW(1, pdf_coord);
  p->x = x; p->y = y;
  dpx_stack_push(&coords, p);
  dvi_set_compensation(x, y);
}

void
spc_pop_coord (struct spc_env *spe)
{
  double     x, y;
  pdf_coord *p;

  p = dpx_stack_pop(&coords);
  if (p)
    RELEASE(p);

  spc_get_coord(spe, &x, &y);
  dvi_set_compensation(x, y);
}

/* Migrated from pdfdraw.c.
 *
 * pt_fixee is obviously not a PDF graphics state parameter.
 *
 */

static dpx_stack pt_fixee;

void
spc_set_fixed_point (struct spc_env *spe, double x, double y)
{
  pdf_coord *p;

  p = dpx_stack_top(&pt_fixee);
  if (p) {
    p->x = x;
    p->y = y;
  }
}

void
spc_get_fixed_point (struct spc_env *spe, double *x, double *y)
{
  pdf_coord *p;

  ASSERT(x && y);

  p = dpx_stack_top(&pt_fixee);
  if (p) {
    *x = p->x;
    *y = p->y;
  }
}

void
spc_put_fixed_point (struct spc_env *spe, double x, double y)
{
  pdf_coord *p;

  p = NEW(1, pdf_coord);
  p->x = x;
  p->y = y;
  dpx_stack_push(&pt_fixee, p);
}

void
spc_dup_fixed_point (struct spc_env *spe)
{
  pdf_coord *p1, *p2;

  p1 = dpx_stack_top(&pt_fixee);
  p2 = NEW(1, pdf_coord);
  p2->x = p1->x; p2->y = p1->y;
  dpx_stack_push(&pt_fixee, p2);
}

void
spc_pop_fixed_point (struct spc_env *spe)
{
  pdf_coord *p;
  p = dpx_stack_pop(&pt_fixee);
  if (p)
    RELEASE(p);
}

void
spc_clear_fixed_point (struct spc_env *spe)
{
  pdf_coord *p;

  for (;;) {
    p = dpx_stack_pop(&pt_fixee);
    if (!p)
      break;
    else
      RELEASE(p);
  }
}

void
spc_get_current_point (struct spc_env *spe, pdf_coord *cp)
{
  double xoff, yoff;

  if (!spe || !cp)
    return;

  spc_get_coord(spe, &xoff, &yoff);
  cp->x = spe->x_user - xoff;
  cp->y = spe->y_user - yoff;
}

void
spc_put_image (struct spc_env *spe, int res_id, transform_info *ti, double xpos, double ypos)
{
  double xoff, yoff;

  spc_get_coord(spe, &xoff, &yoff);
  pdf_dev_put_image(res_id, ti, xpos - xoff, ypos - yoff, &spe->info.rect);
  spe->info.is_drawable = 1;
}


static int
spc_handler_unknown (struct spc_env *spe, struct spc_arg *args)
{
  ASSERT(spe && args);

  args->curptr = args->endptr;

  return  -1;
}

static void
init_special (struct spc_handler *special,
	      struct spc_env *spe,
	      struct spc_arg *args,
	      const char *p, uint32_t size,
	      double x_user, double y_user, double mag)
{

  special->key  = NULL;
  special->exec = (spc_handler_fn_ptr) &spc_handler_unknown;

  spe->x_user = x_user;
  spe->y_user = y_user;
  spe->mag    = mag;
  spe->pg     = pdf_doc_current_page_number(); /* _FIXME_ */
  spe->info.is_drawable = 0;
  spe->info.rect.llx    = 0.0;
  spe->info.rect.lly    = 0.0;
  spe->info.rect.urx    = 0.0;
  spe->info.rect.ury    = 0.0;

  args->curptr = p;
  args->endptr = args->curptr + size;
  args->base   = args->curptr;
  args->command = NULL;

  return;
}

static void
check_garbage (struct spc_arg *args)
{
  ASSERT(args);

  if (args->curptr >= args->endptr)
    return;

  skip_white(&args->curptr, args->endptr);
  if (args->curptr < args->endptr) {
    WARN("Unparsed material at end of special ignored.");
    dump(args->curptr, args->endptr);
  }

  return;
}

static struct {
  const char  *key;
  int (*bodhk_func) (void);
  int (*eodhk_func) (void);
  int (*bophk_func) (void);
  int (*eophk_func) (void);
  int (*check_func) (const char *, int);
  int (*setup_func) (struct spc_handler *, struct spc_env *, struct spc_arg *);
} known_specials[] = {

  {"pdf:",
   spc_pdfm_at_begin_document,
   spc_pdfm_at_end_document,
   NULL,
   spc_pdfm_at_end_page,
   spc_pdfm_check_special,
   spc_pdfm_setup_handler
  },

  {"x:",
   NULL,
   NULL,
   NULL,
   NULL,
   spc_xtx_check_special,
   spc_xtx_setup_handler
  },

  {"dvipdfmx:",
   NULL,
   NULL,
   NULL,
   NULL,
   spc_dvipdfmx_check_special,
   spc_dvipdfmx_setup_handler
  },

  {"ps:",
   spc_dvips_at_begin_document,
   spc_dvips_at_end_document,
   spc_dvips_at_begin_page,
   spc_dvips_at_end_page,
   spc_dvips_check_special,
   spc_dvips_setup_handler
  },

  {"color",
   NULL,
   NULL,
   NULL,
   NULL,
   spc_color_check_special,
   spc_color_setup_handler
  },

  {"tpic",
   spc_tpic_at_begin_document,
   spc_tpic_at_end_document,
   spc_tpic_at_begin_page,
   spc_tpic_at_end_page,
   spc_tpic_check_special,
   spc_tpic_setup_handler
  },

  {"html:",
   spc_html_at_begin_document,
   spc_html_at_end_document,
   spc_html_at_begin_page,
   spc_html_at_end_page,
   spc_html_check_special,
   spc_html_setup_handler
  },

  {"unknown",
   NULL,
   NULL,
   NULL,
   NULL,
   spc_misc_check_special,
   spc_misc_setup_handler
  },

  {NULL} /* end */
};

int
spc_exec_at_begin_page (void)
{
  int  error = 0;
  int  i;

  for (i = 0; known_specials[i].key != NULL; i++) {
    if (known_specials[i].bophk_func) {
      error = known_specials[i].bophk_func();
    }
  }

  return error;
}

int
spc_exec_at_end_page (void)
{
  int  error = 0;
  int  i;

  for (i = 0; known_specials[i].key != NULL; i++) {
    if (known_specials[i].eophk_func) {
      error = known_specials[i].eophk_func();
    }
  }

  return error;
}

int
spc_exec_at_begin_document (void)
{
  int  error = 0;
  int  i;

  for (i = 0; known_specials[i].key != NULL; i++) {
    if (known_specials[i].bodhk_func) {
      error = known_specials[i].bodhk_func();
    }
  }
  
  dpx_stack_init(&coords);
  dpx_stack_init(&pt_fixee);

  return error;
}

int
spc_exec_at_end_document (void)
{
  int        error = 0;
  int        i;
  pdf_coord *p;

  for (i = 0; known_specials[i].key != NULL; i++) {
    if (known_specials[i].eodhk_func) {
      error = known_specials[i].eodhk_func();
    }
  }

  while ((p = dpx_stack_pop(&coords)) != NULL) {  
    RELEASE(p);
  }
  while ((p = dpx_stack_pop(&pt_fixee)) != NULL) {  
    RELEASE(p);
  }

  return error;
}

static void
print_error (const char *name, struct spc_env *spe, struct spc_arg *ap)
{
  const char *p;
  char      ebuf[64];
  int       i;
  int       pg = spe->pg;
  pdf_coord c;

  c.x = spe->x_user; c.y = spe->y_user;
  pdf_dev_transform(&c, NULL);

  if (ap->command && name) {
    WARN("Interpreting special command %s (%s) failed.", ap->command, name);
    WARN(">> at page=\"%ld\" position=\"(%g, %g)\" (in PDF)", pg, c.x, c.y);
  }
  for (i = 0, p = ap->base; i < 63 && p < ap->endptr; p++) {
    if (isprint((unsigned char)*p))
      ebuf[i++] = *p;
    else if (i + 4 < 63)
      i += sprintf(ebuf + i, "\\x%02x", (unsigned char)*p);
    else
      break;
  }
  ebuf[i] = '\0'; 
  if (ap->curptr < ap->endptr) {
    while (i-- > 60)
      ebuf[i] = '.';
  }
  WARN(">> xxx \"%s\"", ebuf);

  if (ap->curptr < ap->endptr) {
    for (i = 0, p = ap->curptr; i < 63 && p < ap->endptr; p++) {
      if (isprint((unsigned char)*p))
        ebuf[i++] = *p;
      else if (i + 4 < 63)
        i += sprintf(ebuf + i, "\\x%02x", (unsigned char)*p);
      else
        break;
    }
    ebuf[i] = '\0'; 
    if (ap->curptr < ap->endptr) {
      while (i-- > 60)
        ebuf[i] = '.';
    }
    WARN(">> Reading special command stopped around >>%s<<", ebuf);

    ap->curptr = ap->endptr;
  }
}

int
spc_exec_special (const char *buffer, int32_t size,
                  double x_user, double y_user, double mag,
                  int *is_drawable, pdf_rect *rect)
{
  int    error = -1;
  int    i, found;
  struct spc_env     spe;
  struct spc_arg     args;
  struct spc_handler special;

  if (dpx_conf.verbose_level > 3) {
    MESG("Executing special command: ");
    dump(buffer, buffer + size);
  }

  init_special(&special, &spe, &args, buffer, size, x_user, y_user, mag);

  for (i = 0; known_specials[i].key != NULL; i++) {
    found = known_specials[i].check_func(buffer, size);
    if (found) {
      error = known_specials[i].setup_func(&special, &spe, &args);
      if (!error) {
        error = special.exec(&spe, &args);
      }
      if (error) {
        print_error(known_specials[i].key, &spe, &args);
      } else {
        if (is_drawable)
          *is_drawable = spe.info.is_drawable;
        if (rect) {
          rect->llx    = spe.info.rect.llx;
          rect->lly    = spe.info.rect.lly;
          rect->urx    = spe.info.rect.urx;
          rect->ury    = spe.info.rect.ury;
        }
      }
      break;
    }
  } 

  check_garbage(&args);

  return error;
}
