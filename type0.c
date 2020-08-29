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
 * Type0 font support:
 * 
 * TODO:
 *
 *  Composite font (multiple descendants) - not supported in PDF
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "system.h"
#include "mem.h"
#include "error.h"
#include "dpxconf.h"
#include "dpxfile.h"

#include "pdfobj.h"
#include "fontmap.h"

#include "cmap.h"
#include "cid.h"

#include "type0.h"


#define TYPE0FONT_DEBUG_STR "Type0"
#define TYPE0FONT_DEBUG     3

static pdf_obj *pdf_read_ToUnicode_file (const char *cmap_name);

/*
 * used_chars:
 *
 *  Single bit is used for each CIDs since used_chars can be reused as a
 *  stream content of CIDSet by doing so. See, cid.h for add_to_used() and
 *  is_used().
 */

static char *
new_used_chars2(void)
{
  char *used_chars;

  used_chars = NEW(8192, char);
  memset(used_chars, 0, 8192);

  return used_chars;
}

/* PLEASE FIX THIS */
#include "tt_cmap.h"

/* Try to load ToUnicode CMap from file system first, if not found fallback to
 * font CMap reverse lookup.
 * CHANGED: CMap here is not always Unicode to CID mapping. Don't use reverse lookup.
 */
static pdf_obj *
Type0Font_try_load_ToUnicode_stream(pdf_font *font, char *cmap_base) {
  pdf_obj *tounicode;
  char    *cmap_name;

  cmap_name = NEW(strlen(cmap_base)+strlen("-UTF16")+1, char);
  sprintf(cmap_name, "%s-UTF16", cmap_base);
  tounicode = pdf_read_ToUnicode_file(cmap_name);
  if (!tounicode) {
    sprintf(cmap_name, "%s-UCS2", cmap_base);
    tounicode = pdf_read_ToUnicode_file(cmap_name);
  }
  RELEASE(cmap_name);

  if (!tounicode) {
    CIDFont *cidfont = CIDFont_cache_get(font->type0.descendant);
    tounicode = otf_create_ToUnicode_stream(CIDFont_get_ident(cidfont),
                                            CIDFont_get_opt_index(cidfont),
                                            CIDFont_get_fontname(cidfont),
                                            font->usedchars);   
  }

  return tounicode;
}

static void
Type0Font_attach_ToUnicode_stream (pdf_font *font)
{
  pdf_obj    *tounicode;
  CIDFont    *cidfont = CIDFont_cache_get(font->type0.descendant);
  CIDSysInfo *csi;
  char       *fontname;

  /*
   * ToUnicode CMap:
   *
   *  ToUnicode CMaps are usually not required for standard character
   *  collections such as Adobe-Japan1. Identity-H is used for UCS
   *  ordering CID-keyed fonts. External resource must be loaded for
   *  others.
   */
  ASSERT(cidfont);

  if (CIDFont_is_ACCFont(cidfont)) {
    /* No need to embed ToUnicode */
    return;
  } else if (CIDFont_is_UCSFont(cidfont)) {
    /*
     * Old version of dvipdfmx mistakenly used Adobe-Identity as Unicode.
     */
    tounicode = pdf_read_ToUnicode_file("Adobe-Identity-UCS2");
    if (!tounicode) { /* This should work */
      tounicode = pdf_new_name("Identity-H");
    }
    pdf_add_dict(font->resource, pdf_new_name("ToUnicode"), tounicode);
    return;
  }

  tounicode = NULL;
  csi       = CIDFont_get_CIDSysInfo(cidfont);
  fontname  = CIDFont_get_fontname(cidfont);
  if (CIDFont_get_embedding(cidfont)) {
    fontname += 7; /* FIXME: Skip pseudo unique tag... */
  }

  if (!strcmp(csi->registry, "Adobe") && !strcmp(csi->ordering, "Identity")) {
    switch (CIDFont_get_subtype(cidfont)) {
    case CIDFONT_TYPE2:
    /* PLEASE FIX THIS */
      {
        tounicode = otf_create_ToUnicode_stream(CIDFont_get_ident(cidfont),
                                                CIDFont_get_opt_index(cidfont),
                                                CIDFont_get_fontname(cidfont),
                                                font->usedchars);
      }
      break;
    default:
      if (CIDFont_get_flag(cidfont, CIDFONT_FLAG_TYPE1C)) {
        tounicode = otf_create_ToUnicode_stream(CIDFont_get_ident(cidfont),
                                                CIDFont_get_opt_index(cidfont),
                                                CIDFont_get_fontname(cidfont),
                                                font->usedchars);
      } else if (CIDFont_get_flag(cidfont, CIDFONT_FLAG_TYPE1)) {
        /* FIXME: handled on very different timing.
         * Font loader will create ToUnicode and set.
         */
        return;
      } else {
        tounicode = Type0Font_try_load_ToUnicode_stream(font, fontname);
      }
      break;
    }
  } else {
    char *cmap_base = NEW(strlen(csi->registry) + strlen(csi->ordering) + 2, char);
    sprintf(cmap_base, "%s-%s", csi->registry, csi->ordering);
    tounicode = Type0Font_try_load_ToUnicode_stream(font, cmap_base);
    RELEASE(cmap_base);
  }

  if (tounicode) {
    pdf_add_dict(font->resource, pdf_new_name("ToUnicode"), tounicode);
  } else {
#if defined(LIBDPX)
    if (dpx_conf.verbose_level > 0)
      WARN("Failed to load ToUnicode CMap for font \"%s\"", fontname);
#else
    WARN("Failed to load ToUnicode CMap for font \"%s\"", fontname);
#endif /* LIBDPX */
  }

  return;
}

void
pdf_font_load_type0 (pdf_font *font)
{
  if (!font || !font->reference)
    return;

  /* FIXME: Should move to pdffont.c */
  if (!pdf_lookup_dict(font->resource, "ToUnicode")) {
    Type0Font_attach_ToUnicode_stream(font);
  }
  if (!pdf_lookup_dict(font->resource, "DescendantFonts")) {
    pdf_obj *array;
    CIDFont *cidfont = CIDFont_cache_get(font->type0.descendant);

    array = pdf_new_array();
    pdf_add_array(array, CIDFont_get_resource(cidfont));
    pdf_add_dict(font->resource, pdf_new_name("DescendantFonts"), array);
  }
}

int
pdf_font_check_type0_opened (const char *map_name, int cmap_id, fontmap_opt *fmap_opt)
{
  CIDFont    *cidfont;
  CMap       *cmap;
  CIDSysInfo *csi;
  int         cid_id = -1, parent_id = -1, wmode = 0;

  if (!map_name || cmap_id < 0)
    return -1;

  /*
   * Encoding is Identity-H or Identity-V according as thier WMode value.
   * 
   * We do not use match against the map_name since fonts (TrueType) covers
   * characters across multiple character collection (eg, Adobe-Japan1 and
   * Adobe-Japan2) must be splited into multiple CID-keyed fonts.
   */

  cmap = CMap_cache_get(cmap_id);
  csi  = (CMap_is_Identity(cmap)) ? NULL : CMap_get_CIDSysInfo(cmap) ;

  cid_id = CIDFont_cache_find(map_name, csi, fmap_opt);

  if (cid_id < 0) 
    return -1;

  cidfont = CIDFont_cache_get(cid_id);
  wmode   = CMap_get_wmode(cmap);

  /* Does CID-keyed font already have parent ? */
  parent_id = CIDFont_get_parent_id(cidfont, wmode);

  return parent_id;
}

int
pdf_font_open_type0 (pdf_font *font, int font_id, fontmap_opt *fmap_opt)
{
  CIDFont    *cidfont;
  CMap       *cmap;
  CIDSysInfo *csi;
  char       *fontname = NULL;
  int         cid_id = -1, parent_id = -1, wmode = 0;

  ASSERT(font->encoding_id >= 0);
  /*
   * Encoding is Identity-H or Identity-V according as thier WMode value.
   * 
   * We do not use match against the map_name since fonts (TrueType) covers
   * characters across multiple character collection (eg, Adobe-Japan1 and
   * Adobe-Japan2) must be splited into multiple CID-keyed fonts.
   */

  cmap = CMap_cache_get(font->encoding_id);
  csi  = (CMap_is_Identity(cmap)) ? NULL : CMap_get_CIDSysInfo(cmap) ;

  cid_id = CIDFont_cache_find(font->filename, csi, fmap_opt);

  if (cid_id < 0) 
    return -1;

  cidfont = CIDFont_cache_get(cid_id);
  wmode   = CMap_get_wmode(cmap);

  font->type0.wmode      = wmode;
  font->type0.descendant = cid_id;

  /*
   * PostScript Font name:
   *
   *  Type0 font's fontname is usually descendant CID-keyed font's font name 
   *  appended by -ENCODING.
   */
  fontname = CIDFont_get_fontname(cidfont);

  if (dpx_conf.verbose_level > 0) {
    if (CIDFont_get_embedding(cidfont) && strlen(fontname) > 7)
      MESG("(CID:%s)", fontname+7); /* skip XXXXXX+ */
    else
      MESG("(CID:%s)", fontname);
  }

  /*
   * The difference between CID-keyed font and TrueType font appears here.
   *
   * Glyph substitution for vertical writing is done in CMap mapping process
   * for CID-keyed fonts. But we must rely on OpenType layout table in the
   * case of TrueType fonts. So, we must use different used_chars for each
   * horizontal and vertical fonts in that case.
   *
   * In most PDF file, encoding name is not appended to fontname for Type0
   * fonts having CIDFontType 2 font as their descendant.
   */

  parent_id = CIDFont_get_parent_id(cidfont, wmode);    

  switch (CIDFont_get_subtype(cidfont)) {
  case CIDFONT_TYPE0:
    font->fontname = NEW(strlen(fontname)+strlen("Identity-V")+2, char);
    sprintf(font->fontname, "%s-%s", fontname, wmode ? "Identity-V" : "Identity-H");
    if (parent_id < 0) {
      int other_id = CIDFont_get_parent_id(cidfont, wmode ? 0 : 1);
      if (other_id < 0) {
        font->usedchars  = new_used_chars2();
      } else {
        font->usedchars  = pdf_get_font_usedchars(other_id);
        font->flags     |= PDF_FONT_FLAG_USEDCHAR_SHARED;
      }
    } else {
      font->usedchars  = pdf_get_font_usedchars(parent_id);
      font->flags     |= PDF_FONT_FLAG_USEDCHAR_SHARED;
    }
    break;
  case CIDFONT_TYPE2:
    font->fontname = NEW(strlen(fontname)+1, char);
    strcpy(font->fontname, fontname);
    /* Adobe-Identity here means use GID as CID directly. No need to use GSUB for finding
     * vertical glyphs hence separate used_chars for H and V instances are not needed.
     */
    if (!csi || (!strcmp(csi->registry, "Adobe") && !strcmp(csi->ordering, "Identity"))) {
      if (parent_id < 0) {
        int other_id = CIDFont_get_parent_id(cidfont, wmode ? 0 : 1);
        if (other_id < 0) {
          font->usedchars  = new_used_chars2();
        } else {
          font->usedchars  = pdf_get_font_usedchars(other_id);
          font->flags     |= PDF_FONT_FLAG_USEDCHAR_SHARED;
        }
      } else {
        font->usedchars  = pdf_get_font_usedchars(parent_id);
        font->flags     |= PDF_FONT_FLAG_USEDCHAR_SHARED;
      }
    } else {
      if (parent_id < 0) {
        font->usedchars  = new_used_chars2();
      } else {
        font->usedchars  = pdf_get_font_usedchars(parent_id);
        font->flags     |= PDF_FONT_FLAG_USEDCHAR_SHARED;
      }
    }
    break;
  }

  if (parent_id < 0) {
    CIDFont_attach_parent(cidfont, font_id, wmode);
    font->resource = pdf_new_dict();
    pdf_add_dict(font->resource, pdf_new_name ("Type"),    pdf_new_name ("Font"));
    pdf_add_dict(font->resource, pdf_new_name ("Subtype"), pdf_new_name ("Type0"));
    pdf_add_dict(font->resource,
                 pdf_new_name("BaseFont"), pdf_new_name(font->fontname));
    pdf_add_dict(font->resource,
                 pdf_new_name("Encoding"), pdf_new_name(wmode ? "Identity-V" : "Identity-H"));
  } else {
    font->resource = pdf_link_obj(pdf_get_font_resource(parent_id));
  }

  return 0;
}

/******************************** COMPAT ********************************/

#ifndef WITHOUT_COMPAT

#include "cmap_read.h"
#include "cmap_write.h"
#include "pdfresource.h"
#include "pdfencoding.h"

static pdf_obj *
create_dummy_CMap (void)
{
  pdf_obj *stream;
  char     buf[32];
  int      i, n;

#define CMAP_PART0 "\
%!PS-Adobe-3.0 Resource-CMap\n\
%%DocumentNeededResources: ProcSet (CIDInit)\n\
%%IncludeResource: ProcSet (CIDInit)\n\
%%BeginResource: CMap (Adobe-Identity-UCS2)\n\
%%Title: (Adobe-Identity-UCS2 Adobe UCS2 0)\n\
%%Version: 1.0\n\
%%Copyright:\n\
%% ---\n\
%%EndComments\n\n\
"
#define CMAP_PART1 "\
/CIDInit /ProcSet findresource begin\n\
\n\
12 dict begin\n\nbegincmap\n\n\
/CIDSystemInfo 3 dict dup begin\n\
  /Registry (Adobe) def\n\
  /Ordering (UCS2) def\n\
  /Supplement 0 def\n\
end def\n\n\
/CMapName /Adobe-Identity-UCS2 def\n\
/CMapVersion 1.0 def\n\
/CMapType 2 def\n\n\
2 begincodespacerange\n\
<0000> <FFFF>\n\
endcodespacerange\n\
"
#define CMAP_PART3 "\
endcmap\n\n\
CMapName currentdict /CMap defineresource pop\n\n\
end\nend\n\n\
%%EndResource\n\
%%EOF\n\
"

  stream = pdf_new_stream(STREAM_COMPRESS);
  pdf_add_stream(stream, CMAP_PART0, strlen(CMAP_PART0));
  pdf_add_stream(stream, CMAP_PART1, strlen(CMAP_PART1));
  pdf_add_stream(stream, "\n100 beginbfrange\n", strlen("\n100 beginbfrange\n"));
  for (i = 0; i < 0x64; i++) {
    n = sprintf(buf,
                "<%02X00> <%02XFF> <%02X00>\n", i, i, i);
    pdf_add_stream(stream, buf, n);
  }
  pdf_add_stream(stream, "endbfrange\n\n", strlen("endbfrange\n\n"));

  pdf_add_stream(stream, "\n100 beginbfrange\n", strlen("\n100 beginbfrange\n"));
  for (i = 0x64; i < 0xc8; i++) {
    n = sprintf(buf,
                "<%02X00> <%02XFF> <%02X00>\n", i, i, i);
    pdf_add_stream(stream, buf, n);
  }
  pdf_add_stream(stream, "endbfrange\n\n", strlen("endbfrange\n\n"));

  pdf_add_stream(stream, "\n48 beginbfrange\n", strlen("\n48 beginbfrange\n"));
  for (i = 0xc8; i <= 0xd7; i++) {
    n = sprintf(buf,
                "<%02X00> <%02XFF> <%02X00>\n", i, i, i);
    pdf_add_stream(stream, buf, n);
  }
  for (i = 0xe0; i <= 0xff; i++) {
    n = sprintf(buf,
                "<%02X00> <%02XFF> <%02X00>\n", i, i, i);
    pdf_add_stream(stream, buf, n);
  }
  pdf_add_stream(stream, "endbfrange\n\n", strlen("endbfrange\n\n"));

  pdf_add_stream(stream, CMAP_PART3, strlen(CMAP_PART3));

  return  stream;
}

static pdf_obj *
pdf_read_ToUnicode_file (const char *cmap_name)
{
  pdf_obj *stream;
  int      res_id = -1;

  ASSERT(cmap_name);

  res_id = pdf_findresource("CMap", cmap_name);
  if (res_id < 0) {
    if (!strcmp(cmap_name, "Adobe-Identity-UCS2"))
      stream = create_dummy_CMap();
    else {
      stream = pdf_load_ToUnicode_stream(cmap_name);
    }
    if (stream) {
      res_id   = pdf_defineresource("CMap",
                                    cmap_name,
                                    stream, PDF_RES_FLUSH_IMMEDIATE);
    }
  }

  return  (res_id < 0 ? NULL : pdf_get_resource_reference(res_id));
}
#endif /* !WITHOUT_COMPAT */
