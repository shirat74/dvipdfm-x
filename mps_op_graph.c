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

#if 1
static int
mps_add_systemdict (mpsi *p, pst_obj *obj)
{
  pst_dict *systemdict = p->systemdict->data;
  pst_operator *op = obj->data;

  ht_insert_table(systemdict->values, op->name, strlen(op->name), obj);

  return 0;
}

static int
pop_get_numbers (mpsi *p, double *values, int n)
{
  if (dpx_stack_depth(&p->stack.operand) < n)
    return -1;
  while (n-- > 0) {
    pst_obj *obj = dpx_stack_pop(&p->stack.operand);
    if (!PST_NUMBERTYPE(obj)) {
      pst_release_obj(obj);
      return -1;
    }
    values[n] = pst_getRV(obj);
    pst_release_obj(obj);
  }

  return 0;
}

static int
mps_cvr_array (mpsi *p, double *values, int n)
{
  pst_obj   *obj;
  pst_array *array;

  if (dpx_stack_depth(&p->stack.operand) < n)
    return -1;
  obj = dpx_stack_pop(&p->stack.operand);
  if (!PST_ARRAYTYPE(obj)) {
    pst_release_obj(obj);
    return -1;
  }
  if (obj->comp.size != n) {
    pst_release_obj(obj);
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
  pst_release_obj(obj);

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

/* Compatibility */
#define MP_CMODE_MPOST    0
#define MP_CMODE_DVIPSK   1
#define MP_CMODE_PTEXVERT 2
static int mp_cmode = MP_CMODE_MPOST;

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
#define IDTRANSFORM	81
#define DTRANSFORM	82
#define TRANSFORM 83
#define ITRANSFORM 84

#define FINDFONT        201
#define SCALEFONT       202
#define SETFONT         203
#define CURRENTFONT     204

#define STRINGWIDTH     210

#define CURRENTFLAT     900
#define SETFLAT         901
#define CLIPPATH        902

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

  {"currentpoint", CURRENTPOINT}, /* This is here for rotate support
				     in graphics package-not MP support */
  {"dtransform",   DTRANSFORM},
  {"idtransform",  IDTRANSFORM},
  {"transform",   TRANSFORM},
  {"itransform",  ITRANSFORM},

  {"findfont",     FINDFONT},
  {"scalefont",    SCALEFONT},
  {"setfont",      SETFONT},
  {"currentfont",  CURRENTFONT},

  {"stringwidth",  STRINGWIDTH},
#if 1
  /* NYI */
  {"currentflat",  CURRENTFLAT},
  {"setflat",      SETFLAT},
  {"clippath",     CLIPPATH},
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
  M->a *= DEVICE_RESOLUTION; M->b *= DEVICE_RESOLUTION;
  M->c *= DEVICE_RESOLUTION; M->d *= DEVICE_RESOLUTION;
  M->e *= DEVICE_RESOLUTION; M->f *= DEVICE_RESOLUTION;

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
  case CURRENTFLAT:
    mps_push_stack(p, pst_new_integer(1));
    break;
  case SETFLAT:
    error = pop_get_numbers(p, values, 1);
    break;
  case CLIPPATH:
    break;
#endif

    /* Path construction */
  case MOVETO:
    error = pop_get_numbers(p, values, 2);
    if (!error)
      error = pdf_dev_moveto(values[0], values[1]);
    break;
  case RMOVETO:
    error = pop_get_numbers(p, values, 2);
    if (!error)
      error = pdf_dev_rmoveto(values[0], values[1]);
    break;
  case LINETO:
    error = pop_get_numbers(p, values, 2);
    if (!error)
      error = pdf_dev_lineto(values[0], values[1]);
    break;
  case RLINETO:
    error = pop_get_numbers(p, values, 2);
    if (!error)
      error = pdf_dev_rlineto(values[0], values[1]);
    break;
  case CURVETO:
    error = pop_get_numbers(p, values, 6);
    if (!error)
      error = pdf_dev_curveto(values[0], values[1],
			      values[2], values[3],
			      values[4], values[5]);
    break;
  case RCURVETO:
    error = pop_get_numbers(p, values, 6);
    if (!error)
      error = pdf_dev_rcurveto(values[0], values[1],
			                         values[2], values[3],
                               values[4], values[5]);
    break;
  case CLOSEPATH:
    error = pdf_dev_closepath();
    break;
  case ARC:
    error = pop_get_numbers(p, values, 5);
    if (!error)
      error = pdf_dev_arc(values[0], values[1],
			                    values[2], /* rad */
			                    values[3], values[4]);
    break;
  case ARCN:
    error = pop_get_numbers(p, values, 5);
    if (!error)
      error = pdf_dev_arcn(values[0], values[1],
			                     values[2], /* rad */
			                     values[3], values[4]);
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
    error = mps_cvr_array(p, values, 6); /* This does pdf_release_obj() */
    if (error)
      WARN("Missing array before \"concat\".");
    else {
      pdf_setmatrix(&matrix,
		    values[0], values[1],
		    values[2], values[3],
		    values[4], values[5]);
      error = pdf_dev_concat(&matrix);
    }
    break;
  case SCALE:
    error = pop_get_numbers(p, values, 2);
    if (!error) {
      switch (mp_cmode) {
#ifndef WITHOUT_ASCII_PTEX
      case MP_CMODE_PTEXVERT:
	pdf_setmatrix(&matrix,
		      values[1], 0.0,
		      0.0      , values[0],
		      0.0      , 0.0);
	break;
#endif /* !WITHOUT_ASCII_PTEX */
      default:
	pdf_setmatrix(&matrix,
		      values[0], 0.0,
		      0.0      , values[1],
		      0.0      , 0.0);
	break;
      }

      error = pdf_dev_concat(&matrix);
    }
    break;
    /* Positive angle means clock-wise direction in graphicx-dvips??? */
  case ROTATE:
    error = pop_get_numbers(p, values, 1);
    if (!error) {
      values[0] = values[0] * M_PI / 180;

      switch (mp_cmode) {
      case MP_CMODE_DVIPSK:
      case MP_CMODE_MPOST: /* Really? */
#ifndef WITHOUT_ASCII_PTEX
      case MP_CMODE_PTEXVERT:
#endif /* !WITHOUT_ASCII_PTEX */
	pdf_setmatrix(&matrix,
		      cos(values[0]), -sin(values[0]),
		      sin(values[0]),  cos(values[0]),
		      0.0,             0.0);
	break;
      default:
	pdf_setmatrix(&matrix,
		      cos(values[0]) , sin(values[0]),
		      -sin(values[0]), cos(values[0]),
		      0.0,             0.0);
	break;
      }
      error = pdf_dev_concat(&matrix);
    }
    break;
  case TRANSLATE:
    error = pop_get_numbers(p, values, 2);
    if (!error) {
      pdf_setmatrix(&matrix,
		    1.0,       0.0,
		    0.0,       1.0,
		    values[0], values[1]);
      error = pdf_dev_concat(&matrix);
    }
    break;
#if 1
  case SETDASH:
    error = pop_get_numbers(p, values, 1);
#if 0
    if (!error) {
      pdf_obj *pattern, *dash;
      int      i, num_dashes;
      double   dash_values[PDF_DASH_SIZE_MAX];
      double   offset;

      offset  = values[0];
      pattern = POP_STACK();
      if (!PDF_OBJ_ARRAYTYPE(pattern)) {
	      if (pattern)
	        pdf_release_obj(pattern);
	      error = 1;
	      break;
      }
      num_dashes = pdf_array_length(pattern);
      if (num_dashes > PDF_DASH_SIZE_MAX) {
	WARN("Too many dashes...");
	pdf_release_obj(pattern);
	error = 1;
	break;
      }
      for (i = 0;
	   i < num_dashes && !error ; i++) {
	dash = pdf_get_array(pattern, i);
	if (!PDF_OBJ_NUMBERTYPE(dash))
	  error = 1;
	else {
	  dash_values[i] = pdf_number_value(dash);
	}
      }
      pdf_release_obj(pattern);
      if (!error) {
	error = pdf_dev_setdash(num_dashes, dash_values, offset);
      }
    }
#endif
    break;
#endif
  case SETLINECAP:
    error = pop_get_numbers(p, values, 1);
    if (!error)
      error = pdf_dev_setlinecap((int)values[0]);
    break;
  case SETLINEJOIN:
    error = pop_get_numbers(p, values, 1);
    if (!error)
      error = pdf_dev_setlinejoin((int)values[0]);
    break;
  case SETLINEWIDTH:
    error = pop_get_numbers(p, values, 1);
    if (!error)
      error = pdf_dev_setlinewidth(values[0]);
    break;
  case SETMITERLIMIT:
    error = pop_get_numbers(p, values, 1);
    if (!error)
      error = pdf_dev_setmiterlimit(values[0]);
    break;

  case SETCMYKCOLOR:
    error = pop_get_numbers(p, values, 4);
    /* Not handled properly */
    if (!error) {
      pdf_color_cmykcolor(&color,
			  values[0], values[1],
			  values[2], values[3]);
      pdf_dev_set_strokingcolor(&color);
      pdf_dev_set_nonstrokingcolor(&color);
    }
    break;
  case SETGRAY:
    /* Not handled properly */
    error = pop_get_numbers(p, values, 1);
    if (!error) {
      pdf_color_graycolor(&color, values[0]);
      pdf_dev_set_strokingcolor(&color);
      pdf_dev_set_nonstrokingcolor(&color);
    }
    break;
  case SETRGBCOLOR:
    error = pop_get_numbers(p, values, 3);
    if (!error) {
      pdf_color_rgbcolor(&color,
			 values[0], values[1], values[2]);
      pdf_dev_set_strokingcolor(&color);
      pdf_dev_set_nonstrokingcolor(&color);
    }
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

#if 1
  case TRANSFORM:
    {
      int  has_matrix = 0;

#if 0
	    error = mps_cvr_array(p, values, 6);
	    if (error)
	      break;
	    pdf_setmatrix(&matrix,
		      values[0], values[1],
		      values[2], values[3],
		      values[4], values[5]);
	    has_matrix = 1; /* FIXME */
#else
      has_matrix = 0;
#endif      
      error = pop_get_numbers(p, values, 2);
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
    }
    break;

  case ITRANSFORM:
    {
      int  has_matrix = 0;

#if 0
	    error = mps_cvr_array(p, values, 6);
	    if (error)
	      break;
	    pdf_setmatrix(&matrix,
		      values[0], values[1],
		      values[2], values[3],
		      values[4], values[5]);
	    has_matrix = 1; /* FIXME */
#else
      has_matrix = 0;
#endif
      error = pop_get_numbers(p, values, 2);
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
#endif
    }
    break;

#if 0
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

  return 0;
}
