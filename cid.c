/* This is dvipdfmx, an eXtended version of dvipdfm by Mark A. Wicks.

    Copyright (C) 2002-2020 by Jin-Hwan Cho and Shunsaku Hirata,
    the dvipdfmx project team.
    
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

/*
 * CID-keyed font support:
 *
 *  See also, cidtype0, and cidtype2
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "system.h"
#include "mem.h"
#include "error.h"
#include "dpxconf.h"
#include "dpxutil.h"

#include "pdfobj.h"

#include "cidtype0.h"
#include "cidtype2.h"
#include "cid_p.h"
#include "cid.h"

#define PDF_CID_SUPPORT_MIN 2
#define PDF_CID_SUPPORT_MAX 6

/*
 * Unicode and PDF Standard Character Collections.
 *
 *  Adobe-Identity is only for TrueType fonts and it means font's
 *  internal glyph ordering.
 */
static struct {
  const char *registry;
  const char *ordering;
  /* Heighest Supplement values supported by PDF-1.0, 1.1, ...; see
   * also http://partners.adobe.com/public/developer/font/index.html#ckf
   */
  int   supplement[21];
} CIDFont_stdcc_def[] = {
  {"Adobe", "UCS",      {-1, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0}},
  {"Adobe", "GB1",      {-1, -1, 0, 2, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4}},
  {"Adobe", "CNS1",     {-1, -1, 0, 0, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4}},
  {"Adobe", "Japan1",   {-1, -1, 2, 2, 4, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 6}},
  {"Adobe", "Korea1",   {-1, -1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2}},
  {"Adobe", "Identity", {-1, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0}},
  {NULL,    NULL,       { 0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0}}
};
#define SUP_IDX_MAX 20
#define UCS_CC    0
#define ACC_START 1
#define ACC_END   4

static char registry_Adobe[] = "Adobe";
static char ordering_Identity[] = "Identity";
static char ordering_UCS[] = "UCS";

CIDSysInfo CSI_IDENTITY = {
  registry_Adobe,
  ordering_Identity,
  0
};

CIDSysInfo CSI_UNICODE = {
  registry_Adobe,
  ordering_UCS,
  0
};

/*
 * Optional supplement after alias name.
 */

static struct {
  const char *name;
  int   index;
} CIDFont_stdcc_alias[] = {
  {"AU",     0}, {"AG1",    1}, {"AC1",    2}, {"AJ1",    3}, {"AK1",    4}, {"AI", 5},
  {"UCS",    0}, {"GB1",    1}, {"CNS1",   2}, {"Japan1", 3}, {"Korea1", 4}, {"Identity", 5},
  {"U",      0}, {"G",      1}, {"C",      2}, {"J",      3}, {"K",      4}, {"I", 5},
  {NULL,     0}
};

static void release_opt (cid_opt *opt);
static int  get_cidsysinfo (CIDSysInfo *csi, const char *map_name, fontmap_opt *fmap_opt);

int opt_flags_cidfont = 0;

int
CIDFont_is_ACCFont (pdf_font *font)
{
  int         i;
  CIDSysInfo *csi;

  ASSERT(font);

  csi = &font->cid.csi;
  for (i = ACC_START; i <= ACC_END ; i++) {
    if (!strcmp(csi->registry, CIDFont_stdcc_def[i].registry) &&
        !strcmp(csi->ordering, CIDFont_stdcc_def[i].ordering))
      return 1;
  }

  return 0;
}

int
CIDFont_is_UCSFont (pdf_font *font)
{
  CIDSysInfo *csi;

  ASSERT(font);

  csi = &font->cid.csi;

  if (!strcmp(csi->ordering, "UCS") ||
      !strcmp(csi->ordering, "UCS2"))
    return 1;

  return 0;
}

char *
CIDFont_get_usedchars (pdf_font *font)
{
  if (!font->usedchars) {
    font->usedchars = NEW(8192, char);
    memset(font->usedchars, 0, 8192*sizeof(char));
  }

  return font->usedchars;
}

char *
CIDFont_get_usedchars_v (pdf_font *font)
{
  if (!font->cid.usedchars_v) {
    font->cid.usedchars_v = NEW(8192, char);
    memset(font->cid.usedchars_v, 0, 8192*sizeof(char));
  }

  return font->cid.usedchars_v;
}


static int
source_font_type (pdf_font *font)
{
  int type = PDF_FONT_FONTTYPE_CIDTYPE0;

  ASSERT(font);

  if (font->flags & CIDFONT_FLAG_TYPE1) {
    type = PDF_FONT_FONTTYPE_TYPE1;
  } else if (font->flags & CIDFONT_FLAG_TYPE1C) {
    type = PDF_FONT_FONTTYPE_TYPE1C;
  } else if (font->flags & CIDFONT_FLAG_TRUETYPE) {
    type = PDF_FONT_FONTTYPE_TRUETYPE;
  }

  return type;
}

void
pdf_font_load_cidfont (pdf_font *font)
{
  if (!font || !font->reference)
    return;

  if (dpx_conf.verbose_level > 0)
    MESG(":%s", font->ident);
  if (dpx_conf.verbose_level > 1) {
    if (font->fontname)
      MESG("[%s]", font->fontname);
  }

  switch (font->subtype) {
  case PDF_FONT_FONTTYPE_CIDTYPE0:
    if(dpx_conf.verbose_level > 0)
      MESG("[CIDFontType0]");
    switch (source_font_type(font)) {
    case PDF_FONT_FONTTYPE_TYPE1:
      CIDFont_type0_t1dofont(font);
      break;
    case PDF_FONT_FONTTYPE_TYPE1C:
      CIDFont_type0_t1cdofont(font);
      break;
    default:
      CIDFont_type0_dofont(font);
      break;
    }
    break;
  case PDF_FONT_FONTTYPE_CIDTYPE2:
    if(dpx_conf.verbose_level > 0)
      MESG("[CIDFontType2]");
    CIDFont_type2_dofont(font);
    break;
  }

  return;
}

int
CIDFont_is_BaseFont (pdf_font *font)
{
  ASSERT(font);
  return (font->flags & PDF_FONT_FLAG_BASEFONT) ? 1 : 0;
}

#include "pdfparse.h"
#include "cid_basefont.h"

static int
CIDFont_base_open (pdf_font *font, const char *name, CIDSysInfo *cmap_csi, cid_opt *opt)
{
  pdf_obj *fontdict, *descriptor;
  char    *fontname = NULL;
  int      idx;

  ASSERT(font);

  for (idx = 0; cid_basefont[idx].fontname != NULL; idx++) {
    if (!strcmp(name, cid_basefont[idx].fontname) ||
        (strlen(name) == strlen(cid_basefont[idx].fontname) - strlen("-Acro") &&
         !strncmp(name, cid_basefont[idx].fontname,
                  strlen(cid_basefont[idx].fontname)-strlen("-Acro")))
        )
      break;
  }

  if (cid_basefont[idx].fontname == NULL)
    return -1;

  fontname = NEW(strlen(name)+12, char);
  memset(fontname, 0, strlen(name)+12);
  strcpy(fontname, name);

  switch (opt->style) {
  case FONT_STYLE_BOLD:
    strcat(fontname, ",Bold");
    break;
  case FONT_STYLE_ITALIC:
    strcat(fontname, ",Italic");
    break;
  case FONT_STYLE_BOLDITALIC:
    strcat(fontname, ",BoldItalic");
    break;
  }
  {
    const char *start;
    const char *end;

    start = cid_basefont[idx].fontdict;
    end   = start + strlen(start);
    fontdict   = parse_pdf_dict(&start, end, NULL);
    start = cid_basefont[idx].descriptor;
    end   = start + strlen(start);
    descriptor = parse_pdf_dict(&start, end, NULL);

    ASSERT(fontdict && descriptor);
  }

  font->fontname = fontname;
  font->flags   |= PDF_FONT_FLAG_BASEFONT;
  {
    char    *registry, *ordering;
    int      supplement;
    pdf_obj *tmp;

    tmp = pdf_lookup_dict(fontdict, "CIDSystemInfo");

    ASSERT( tmp && pdf_obj_typeof(tmp) == PDF_DICT );

    registry   = pdf_string_value(pdf_lookup_dict(tmp, "Registry"));
    ordering   = pdf_string_value(pdf_lookup_dict(tmp, "Ordering"));
    supplement = pdf_number_value(pdf_lookup_dict(tmp, "Supplement"));
    if (cmap_csi) { /* NULL for accept any */
      if (strcmp(registry, cmap_csi->registry) ||
          strcmp(ordering, cmap_csi->ordering))
        ERROR("Inconsistent CMap used for CID-keyed font %s.",
              cid_basefont[idx].fontname);
      else if (supplement < cmap_csi->supplement) {
        WARN("CMap has higher supplement number than CIDFont: %s", fontname);
        WARN("Some chracters may not be displayed or printed.");
      }
    }
    font->cid.csi.registry = NEW(strlen(registry)+1, char);
    font->cid.csi.ordering = NEW(strlen(ordering)+1, char);
    strcpy(font->cid.csi.registry, registry);
    strcpy(font->cid.csi.ordering, ordering);
    font->cid.csi.supplement = supplement;
  }

  {
    pdf_obj *tmp;
    char    *type;

    tmp  = pdf_lookup_dict(fontdict, "Subtype");
    ASSERT( tmp != NULL && pdf_obj_typeof(tmp) == PDF_NAME );

    type = pdf_name_value(tmp);
    if (!strcmp(type, "CIDFontType0"))
      font->subtype = PDF_FONT_FONTTYPE_CIDTYPE0;
    else if (!strcmp(type, "CIDFontType2"))
      font->subtype = PDF_FONT_FONTTYPE_CIDTYPE2;
    else {
      ERROR("Unknown CIDFontType \"%s\"", type);
    }
  }

  if (opt_flags_cidfont & CIDFONT_FORCE_FIXEDPITCH) {
    if (pdf_lookup_dict(fontdict, "W")) {
       pdf_remove_dict(fontdict, "W");
    }
    if (pdf_lookup_dict(fontdict, "W2")) {
       pdf_remove_dict(fontdict, "W2");
    }
  }

  pdf_add_dict(fontdict,   pdf_new_name("Type"),     pdf_new_name("Font"));
  pdf_add_dict(fontdict,   pdf_new_name("BaseFont"), pdf_new_name(fontname));
  pdf_add_dict(descriptor, pdf_new_name("Type"),     pdf_new_name("FontDescriptor"));
  pdf_add_dict(descriptor, pdf_new_name("FontName"), pdf_new_name(fontname));

  font->resource   = fontdict;
  font->descriptor = descriptor;

  opt->embed = 0;

  return  0;
}

int
pdf_font_cidfont_lookup_cache (pdf_font *fonts, int count, const char *map_name, CIDSysInfo *cmap_csi, fontmap_opt *fmap_opt)
{
  int       font_id = -1;
  pdf_font *font    = NULL;
  cid_opt   opt;
  int       has_csi;

  ASSERT(fonts);

  opt.style = fmap_opt->style;
  opt.embed = (fmap_opt->flags & FONTMAP_OPT_NOEMBED) ? 0 : 1;
  opt.csi.registry   = NULL;
  opt.csi.ordering   = NULL;
  opt.csi.supplement = 0;
  has_csi   = get_cidsysinfo(&opt.csi, map_name, fmap_opt);
  opt.stemv = fmap_opt->stemv;

  if (!has_csi && cmap_csi) {
    /*
     * No CIDSystemInfo supplied explicitly. Copy from CMap's one if available.
     * It is not neccesary for CID-keyed fonts. But TrueType requires them.
     */
    opt.csi.registry   = NEW(strlen(cmap_csi->registry)+1, char);
    strcpy(opt.csi.registry, cmap_csi->registry);
    opt.csi.ordering   = NEW(strlen(cmap_csi->ordering)+1, char);
    strcpy(opt.csi.ordering, cmap_csi->ordering);
    opt.csi.supplement = cmap_csi->supplement;
    has_csi = 1;
  }
  /*
   * Here, we do not compare font->ident and map_name because of
   * implicit CIDSystemInfo supplied by CMap for TrueType.
   */
  for (font_id = 0; font_id < count; font_id++) {
    font = &fonts[font_id];
    if (font->subtype != PDF_FONT_FONTTYPE_CIDTYPE0 &&
        font->subtype != PDF_FONT_FONTTYPE_CIDTYPE2)
      continue;
    if (!strcmp(font->filename, map_name) &&
        font->cid.options.style == opt.style &&
        font->index == fmap_opt->index) {
      if (font->cid.options.embed == opt.embed) {
        /*
         * Case 1: CSI not available (Identity CMap)
         *         Font is TrueType --> continue
         *         Font is CIDFont  --> break
         * Case 2: CSI matched      --> break
         */
        if (!has_csi) {
          if (font->subtype == PDF_FONT_FONTTYPE_CIDTYPE2)
            continue;
          else
            break;
        } else if (!strcmp(font->cid.csi.registry, opt.csi.registry) &&
                   !strcmp(font->cid.csi.ordering, opt.csi.ordering)) {
          if (font->subtype == PDF_FONT_FONTTYPE_CIDTYPE2)
            font->cid.csi.supplement =
              MAX(opt.csi.supplement, font->cid.csi.supplement); /* FIXME: font modified */
          break;
        }
      } else if (CIDFont_is_BaseFont(font)) {
        break;
      }
    }
  }
  release_opt(&opt);

  return (font_id < count) ? font_id : -1;
}

int
pdf_font_open_cidfont (pdf_font *font, const char *map_name, CIDSysInfo *cmap_csi, fontmap_opt *fmap_opt)
{
  int      error = 0;
  cid_opt  opt;
  int      has_csi;

  opt.style = fmap_opt->style;
  opt.embed = (fmap_opt->flags & FONTMAP_OPT_NOEMBED) ? 0 : 1;
  opt.csi.registry   = NULL;
  opt.csi.ordering   = NULL;
  opt.csi.supplement = 0;
  has_csi   = get_cidsysinfo(&opt.csi, map_name, fmap_opt);
  opt.stemv = fmap_opt->stemv;

  if (!has_csi && cmap_csi) {
    /*
     * No CIDSystemInfo supplied explicitly. Copy from CMap's one if available.
     * It is not neccesary for CID-keyed fonts. But TrueType requires them.
     */
    opt.csi.registry   = NEW(strlen(cmap_csi->registry)+1, char);
    strcpy(opt.csi.registry, cmap_csi->registry);
    opt.csi.ordering   = NEW(strlen(cmap_csi->ordering)+1, char);
    strcpy(opt.csi.ordering, cmap_csi->ordering);
    opt.csi.supplement = cmap_csi->supplement;
    has_csi = 1;
  }

  if (CIDFont_type0_open(font, map_name, fmap_opt->index, cmap_csi, &opt, 0) < 0 &&
      CIDFont_type2_open(font, map_name, fmap_opt->index, cmap_csi, &opt)    < 0 &&
      CIDFont_type0_open(font, map_name, fmap_opt->index, cmap_csi, &opt,
                         CIDFONT_FLAG_TYPE1)               < 0 &&
      CIDFont_type0_open(font, map_name, fmap_opt->index, cmap_csi, &opt,
                         CIDFONT_FLAG_TYPE1C)              < 0 &&
      CIDFont_base_open (font, map_name, cmap_csi, &opt)    < 0) {
    release_opt(&opt);
    error = -1;
  } else {
    font->filename = NEW(strlen(map_name)+1, char);
    strcpy(font->filename,  map_name);
    font->ident    = NEW(strlen(map_name)+1, char);
    strcpy(font->ident, map_name);
    font->index    = fmap_opt->index;
    font->cid.options.embed = opt.embed;
    font->cid.options.stemv = opt.stemv;
    font->cid.options.style = opt.style;
    font->cid.options.csi   = opt.csi;
    error = 0;
  }

  return error;
}


/******************************* OPTIONS *******************************/

/*
 * FORMAT:
 *
 *   (:int:)?!?string(/string)?(,string)?
 */

static void
release_opt (cid_opt *opt)
{
  if (opt->csi.registry)
    RELEASE(opt->csi.registry);
  if (opt->csi.ordering)
    RELEASE(opt->csi.ordering);
}

static int
get_cidsysinfo (CIDSysInfo *csi, const char *map_name, fontmap_opt *fmap_opt)
{
  int has_csi = 0;
  int sup_idx;
  int i, csi_idx = -1, n, m;

  sup_idx = pdf_get_version() - 10;
  sup_idx = (sup_idx > SUP_IDX_MAX) ? SUP_IDX_MAX : sup_idx;

  if (!fmap_opt || !fmap_opt->charcoll)
    return 0;

  /* First try alias for standard one. */
  for (i = 0; CIDFont_stdcc_alias[i].name != NULL; i++) {
    n = strlen(CIDFont_stdcc_alias[i].name);
    if (!strncmp(fmap_opt->charcoll,
                 CIDFont_stdcc_alias[i].name, n)) {
      csi_idx  = CIDFont_stdcc_alias[i].index;
      csi->registry = NEW(strlen(CIDFont_stdcc_def[csi_idx].registry)+1, char);
      strcpy(csi->registry, CIDFont_stdcc_def[csi_idx].registry);
      csi->ordering = NEW(strlen(CIDFont_stdcc_def[csi_idx].ordering)+1, char);
      strcpy(csi->ordering, CIDFont_stdcc_def[csi_idx].ordering);
      if (strlen(fmap_opt->charcoll) > n) {
        csi->supplement = (int) strtoul(&(fmap_opt->charcoll[n]), NULL, 10);
      } else { /* Use heighest supported value for current output PDF version. */
        csi->supplement = CIDFont_stdcc_def[csi_idx].supplement[sup_idx];
      }
      has_csi = 1;
      break;
    }
  }
  if (!has_csi) {
    char *p, *q;

    p   = (char *) fmap_opt->charcoll;

    /* Full REGISTRY-ORDERING-SUPPLEMENT */
    p = strchr(fmap_opt->charcoll, '-');
    if (!p || p[1] == '\0')
      ERROR("String can't be converted to REGISTRY-ORDERING-SUPPLEMENT: %s", fmap_opt->charcoll);
    p++;

    q = strchr(p, '-');
    if (!q || q[1] == '\0')
      ERROR("String can't be converted to REGISTRY-ORDERING-SUPPLEMENT: %s", fmap_opt->charcoll);
    q++;

    if (!isdigit((unsigned char)q[0]))
      ERROR("String can't be converted to REGISTRY-ORDERING-SUPPLEMENT: %s", fmap_opt->charcoll);

    n = strlen(fmap_opt->charcoll) - strlen(p) - 1;
    csi->registry = NEW(n+1, char);
    memcpy(csi->registry, fmap_opt->charcoll, n);
    csi->registry[n] = '\0';

    m = strlen(p) - strlen(q) - 1;
    csi->ordering = NEW(m+1, char);
    memcpy(csi->ordering, p, m);
    csi->ordering[m] = '\0';

    csi->supplement = (int) strtoul(q, NULL, 10);

    has_csi = 1;

    /* Check for standart character collections. */
    for (i = 0; CIDFont_stdcc_def[i].ordering != NULL; i++) {
      if ((CIDFont_stdcc_def[i].registry &&
           !strcmp(csi->registry, CIDFont_stdcc_def[i].registry)) &&
          !strcmp(csi->ordering, CIDFont_stdcc_def[i].ordering)) {
        csi_idx = i;
        break;
      }
    }
  }

  if (csi && csi_idx >= 0) {
    if (csi->supplement > CIDFont_stdcc_def[csi_idx].supplement[sup_idx]
        && (fmap_opt->flags & FONTMAP_OPT_NOEMBED)) {
      WARN("Heighest supplement number supported in PDF-%d.%d for %s-%s is %d.",
           pdf_get_version_major(), pdf_get_version_minor(),
           csi->registry, csi->ordering,
           CIDFont_stdcc_def[csi_idx].supplement[sup_idx]);
      WARN("Some character may not shown without embedded font (--> %s).", map_name);
    }
  }

  return has_csi;
}

void
CIDFont_set_flags (int flags)
{
  opt_flags_cidfont |= flags;
}
