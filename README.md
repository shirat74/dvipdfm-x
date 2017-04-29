# Dvipdfmx and xdvipdfmx for TeX Live

## The dvipdfmx Project

Copyright (C) 2002-2016 by Jin-Hwan Cho, Shunsaku Hirata,
Matthias Franz, and the dvipdfmx project team.  This package is released
under the GNU GPL, version 2, or (at your option) any later version.

### xdvipdfmx

xdvipdfmx is an extended version of dvipdfmx, and is now incorporated in
the same sources.

The extensions provided by xdvipdfmx provide support for the Extended DVI
(.xdv) format used by xetex, including support for platform-native fonts
and the xetex graphics primitives, as well as Unicode/OpenType text.

Like its direct ancestor dvipdfmx, this is free software and may be
redistributed under the terms of the GNU General Public License,
version 2 or (at your option) any later version.

Jonathan Kew mentions that in the past, XeTeX used a Mac-specific
program xdv2pdf as the backend instead of xdvipdfmx.  xdv2pdf supported
a couple of special effects that are not yet available through
xdvipdfmx: the Quartz graphics-based shadow support, AAT "variation"
fonts like Skia, transparency as an attribute of font color, maybe other
things.  It would be nice for those things to continue to be supported,
if anyone is looking for some nontrivial but not-impossible job and
happens across this file.

Dvipdfmx is now maintained as part of TeX Live.

## Introduction

The dvipdfmx (formerly dvipdfm-cjk) project provides an eXtended version
of the dvipdfm, a DVI to PDF translator developed by Mark A. Wicks.

The primary goal of this project is to support multi-byte character
encodings and large character sets such as for East Asian languages.
This project started as a combined work of the dvipdfm-jpn project by
Shunsaku Hirata and its modified one, dvipdfm-kor, by Jin-Hwan Cho.

Extension to dvipdfm includes,

* Support for OpenType and TrueType font including partial support
  for OpenType Layout GSUB Feature for finding glyphs and vertical
  writing support.
* Advanced support for CJK-LaTeX and Subfont Definition Files.
* Support for various legacy multi-byte encodings via PostScript CMap
  Resources.
* Unicode related features: Unicode as input encoding and
  auto-creation of ToUnicode CMaps.
* Support for pTeX (a Japanese localized variant of TeX).
* Some pdfTeX compatible DVI specials.
* Reduction of files size with on-the-fly Type1 to CFF (Type1C)
  conversion and PDF object stream.
* Advanced raster image support including alpha channels, embedded
  ICC profiles, 16-bit colors, and so on.
* Basic PDF password security support for PDF output.

Missing features are

* Linearization.
* Color Management.
* Resampling of images.
* Selection of compression filters.
* and many more...

## Installation

Typical usage and installation steps are not different from the
original dvipdfm.
Please refer documents from dvipdfm distribution for detailed
instruction on how to install and how to use dvipdfm. The dvipdfm
manual is available from CTAN site:

https://www.ctan.org/tex-archive/dviware/dvipdfm

Optionally the libpaper library may be used to handle paper sizes.


## Auxiliary Files

This section is for supporting legacy encodings and legacy font format
such as PostScript Type1 font. XeTeX users may skip this section.

Dvipdfmx requires various auxiliary files.

### PostScript CMap Resources

Those files are required only for supporting lagacy encodings such as
Shif-JIS, EUC-JP and other East Asian encodings.

Dvipdfmx internally identifies glyphs in fonts with identifiers (CID)
represented as an integer ranging from 0 to 65535.
The PostScript CMap Resource defines how input character codes are
translated to CIDs. Various CMap resources for Adobe's standard
character collections for use with widely used encodings can be
found at:

https://github.com/adobe-type-tools/cmap-resources

CMap Resources are basically not required for use with XeTeX.

### SubFont Definition Files

CJK fonts usually contain several thousand of glyphs.
For using such fonts with (original) TeX, which can only handle 8-bit
encodings, it is necessary to split such large fonts into several subfonts.
SubFont Definition File (SFD) specify the way how those fonts are split
into subfonts.

Dvipdfmx uses SFD files to convert subfonts back to a single font. SFD files
are not required for use with TeX variants which can handle multi-byte
character encodings such as pTeX, upTeX, XeTeX, and Omega.

HLaTeX and CJK-LaTeX users are required to have those files to be installed.

### Adobe Glyph List and ToUnicode Mapping Files

The Adobe Glyph List (AGL) describes correspondence between PostScript
glyph names (e.g., AE, Aacute, ...) and it's Unicode character sequences.
Some features described in the section "Unicode Support" requires this file.

Dvipdfmx looks for file `glyphlist.txt` when conversion from PostScript
glyph names to Unicode sequence is necessary. This conversion is done in
various situations;
when creating ToUnicode CMaps for 8-bit encoding fonts, finding glyph
descriptions from TrueType/OpenType fonts when the font itself does not
provide a mapping from PostScript glyph names to glyph indices (version 2.0
"post" table), and when the encoding `unicode` is specified for Type 1 font.

The AGL file maintained by Adobe is found at:

https://github.com/adobe-type-tools/agl-aglfn

ToUnicode Mapping Files are similar to the Adobe glyph list file but they
describe correspondence between CID numbers (instead of glyph names) and
Unicode values. The content
of those files are the same as CMap Resources. They are required
for when using TrueType fonts (including OpenType fonts with TrueType
outline) emulated as CID-keyed fonts. They should be installed in the same
directory as ordinary CMap files.

Those files are not required for XeTeX users.

## CJK Support

There are various extensions made for dvipdfmx to support CJK
languages.

### Legacy Multibyte Encodings

Dvipdfmx has an extensible support for encodings by means of
The PostScript CMap Resource.
Just like `enc` files for 8-bit encodings, one can write their
own custom CMap files to support custom encodings.
See, Adobe's technical note for details on CMap Resources.
Adobe provides a set of CMap files necessary for processing various
CJK encodings. See, section of "PostScript CMap Resources".

### Vertical Writing

Dvipdfmx supports vertical writing extension used by pTeX and upTeX.
A DVI instruction to set writing mode is supported. OpenType Layout
GSUB Feature is supported for selecting vertical version of glyphs.

## Unicode Support

Dvipdfmx supports an additional keyword in fontmap encoding field `unicode`.
It can be used when character codes are specified as Unicode in DVI
and TFM.

### ToUnicode CMap Support

In PDF, texts are often not encoded in Unicode encodings. However, modern
applications usually require texts to be encoded in Unicode encodings to make
them reusable. ToUnicode CMap makes it possible to extract texts in PDF as
Unicode encoded strings. It is also necessary to make PDF search-able and
texts in PDF copy-and-past-able. Dvipdfmx supports auto-creation of ToUnicode
CMap.

It is done in various ways, by inverse lookup of OpenType Unicode cmap or
by converting PostScript glyph names to Unicode sequences via Adobe Glyph
List.

## Graphics and Image Format

Dvipdfmx does not support various features common to graphics manipulation
programs such as resampling, color conversion, and selection of
compression filters.
Thus, it is recommended to use other programs specialized for
image manipulation for preparation of images.

### Supported Graphics File Format

Supported formats are, PNG, JPEG, JPEG2000, BMP, PDF,
and MetaPost generated EPS. All other format images, such as SVG
and PostScript, must be converted to other format supported by
dvipdfmx before inclusion.
The `-D` option, as in dvipdfm, can be used to filter images.

PNG support includes nearly all features of PNG such as color palette,
transparency, XMP metadata, ICC Profiles, and calibrated colors
specified by gAMA and cHRM chunks.
All bit-depth are supported.
Predictor filter may be applied for Flate compression which result in
better compression for larger images.

JPEG is relatively well supported. Dvipdfmx supports embedded ICC
Profiles and CMYK color. Embedded XMP metadata is also supported.
There is an issue regarding determination of image sizes when there
is an inconsistency between JFIF and Exif data.

BMP support is limited to uncompressed or RLE-compressed raster
images. Extensions are unsupported.

JPEG2000 is also supported. It is restricted to JP2 and JPX baseline
subset as required by PDF spec. It is not well supported and still in
the experimental stage. J2C format and transparency are not supported.

PDF inclusion is supported too. However, Tagged PDF may cause
problems and annotations are not preserved.

Dvipdfmx also supports MetaPost mode. When dvipdfmx is invoked with `-M`
option, it enters in MetaPost mode and processes a MetaPost generated EPS
file as input.

## DVI Specials

Dvipdfmx is mostly compatible to dvipdfm. There are few additional specials
supported by dvipdfmx.

### Additions to Dvipdfm's pdf: Special

The `pdf:fstream` special is added, which enables creation of PDF stream
object from an existing file.

```
pdf:fstream @identifier (filename) <<dictionary>>
```

Identifier and filename (specified as a PDF string object) are mandatory
and a dictionary object which is to be added to stream dictionary
following filename is optional.

For examples, to incorporate a XMP Metadata,

```
\special{pdf:fstream @xmp (test.xmp) <<
  /Type /Metadata
  /Subtype /XML
>>}
\special{pdf:put @catalog << /Metadata @xmp >>}
```

Similary, `pdf:stream` special can be used to create PDF stream
object from a PDF string instead of a file:

```
\special{pdf:stream @MyPattern01
(0.16 0 0 0.16 0 0 cm 4 w 50 0 m 50 28 28 50 0 50 c S 100 50 m 72 50 50 28 50 0 c S
50 100 m 50 72 72 50 100 50 c S 0 50 m 28 50 50 72 50 100 c S
100 50 m 100 78 78 100 50 100 c 22 100 0 78 0 50 c 0 22 22 0 50 0 c 78 0 100 22 100 50 c S
0 0 m 20 10 25 5 25 0 c f 0 0 m 10 20 5 25 0 25 c f 100 0 m 80 10 75 5 75 0 c f
100 0 m 90 20 95 25 100 25 c f 100 100 m 80 90 75 95 75 100 c f
100 100 m 90 80 95 75 100 75 c f 0 100 m 20 90 25 95 25 100 c f
0 100 m 10 80 5 75 0 75 c f 50 50 m 70 60 75 55 75 50 c 75 45 70 40 50 50 c f
50 50 m 60 70 55 75 50 75 c 45 75 40 70 50 50 c f 50 50 m 30 60 25 55 25 50 c
25 45 30 40 50 50 c f 50 50 m 60 30 55 25 50 25 c 45 25 40 30 50 50 c f)
<<
  /BBox [0 0 16 16]
  /PaintType 2
  /PatternType 1
  /Resources <<
    /ProcSet [/PDF]
  >>
  /TilingType 3
  /Type /Pattern
  /XStep 16
  /YStep 16
>>
}
```

The above example defines a tiling pattern.

`pdf:mapline` and `pdf:mapfile` specials can be used to append a fontmap
line or load a fontmap file:

```
pdf:mapline foo unicode bar
pdf:mapfile foo.map
```

`pdf:majorversion` and `pdf:minorversion` specials can be used to
specify major and minor version of output PDF.

```
pdf:minorversion 3
```

Use `pdf:encrypt` special

```
pdf:encrypt userpw (user password) ownerpw (owner password)
            length number
            perm   number
```

to encrypt output PDF files. Where user-password and owner-password
must be specified as PDF string objects. (which might be empty)
Numbers here must be decimal numbers.


### Dvipdfmx Extensions

A new special `dvipdfmx:config` is added to make it possible to
invoke a command line option. All command line options except `D` option
are supported. (although some may completely useless) For examples,

```
dvipdfmx:config V 7
```

sets PDF version to 1.7.

## Font Mapping

Syntax of fontmap file is the same as dvipdfm. There are few
extensions in dvipdfmx. In addition to 8-bit `enc` files and
keyword `builtin` and `none`, dvipdfmx accepts CMap name and
the keyword `unicode` in the encoding field.

### Options for CJK Font

Few options are available in dvipdfmx in addition to the original
dvipdfm's one. All options that makes dvipdfmx to use unembedded
fonts are deprecated as by using them makes divpdfmx to create PDF
files which are not compliant to "ISO" spec.

#### TTC Index

TrueType Collection index number can be specified with `:n:`
in front of TrueType font name:

```
min10  H :1:mincho
```

In this example, the option ``:1:`` tells dvipdfmx to select first
TrueType font from the TTC font `mincho.ttc`. Alternatively, the
`-i` option can be used in the option field to specify TTC index:

```
min10 H mincho -i 1
```

#### No-embed Switch (deprecated)

It is possible to block embedding glyph data with the character `!`
in front of the font name in the font mapping file.

This feature reduces the size of the final PDF output, but the PDF
file may not be viewed exactly in other systems on which
appropriate fonts are not installed.

Use of this option is deprecated for TrueType fonts.

#### Stylistic Variants (deprecated)

Keywords `,Bold`, `,Italic`, and `,BoldItalic` can be used to create
synthetic bold, italic, and bolditalic style variants from other font
using PDF viewer's (or OS's) function.

```
jbtmo@UKS@     UniKSCms-UCS2-H :0:!batang,Italic
jbtb@Unicode@  Identity-H      !batang/UCS,Bold
```

Availability of this feature highly depends on the implementation of PDF
viewers. This feature is not supported for embedded fonts in the most of
PDF viewers, like Adobe Acrobat Reader and GNU Ghostscript.

Notice that this option automatically disable font embedding.

#### OpentType Layout Feature support

With the `-w` option in the option field, writing mode can be specified.

```
-w 1
```

denotes the font is for vertical writing. It enables OpentType Layout
Feature related to vertical writing, namely, vert or vrt2, to choose
proper glyphs for vertical texts.

The `-l` option can be used to enable various OpenType Layout GSUB Features.
For examples,

```
 -l jp04
```

enables "jp04" feature to select JIS2004 forms for Kanjis.
Features can be specified as a ":" separated list of OpenType Layout
Feature tags. Script and language may be additionally specified as

```
 -l kana.JAN.ruby
```

An example can be

```
uprml-v unicode SourceHanSerifJP-Light.otf -w 1 -l jp90
```

which declares that font should be treated as for vertical writing and
use JIS1990 form for Kanjis.

## Incompatible Changes From Dvipdfm

There are various minor incompatible changes to dvipdfm.

The `-C` command line option flags may be used for compatibility to
dvipdfm or older versions of dvipdfmx.

* bit position 2: Use semi-transparent filling for tpic shading command, instead of opaque gray color. (requires PDF 1.4)
* bit position 3: Treat all CIDFont as fixed-pitch font. This is only for backward compatibility.
* bit position 4: Do not replace duplicate fontmap entries. Dvipdfm behavior.
* bit position 5: Do not optimize PDF destinations. Use this if you want to refer from other files to destinations in the current file.
* bit position 6: Do not use predictor filter for Flate compression.



## Other Improvement Over Dvipdfm

Numerous improvement over dvipdfm had been done. Especially, font support
is hugely enhanced. Various legacy multi-byte encodings support is added.

### Encryption

Dvipdfmx offers basic PDF password security support including
256-bits AES encryption.
Use `-S` command line option to enable encryption and use `-P` option to set
permission flags. Each bits of the integer number given to the `-P` option
represents user access permissions; e.g., bit position 3 for allowing "print",
4 for "modify the contents", 5 for "copy or extract texts", and so on.

For examples,

```
-P 0x14
```

enables printing, copying and extraction of texts.
 See, Adobe's PDF spec. for full description of those permission
 flags.

Use `-K` option to specify encryption key length. Key length must be
multiple of 8 in the range 40 to 128, or 256 (for PDF version 1.7 plus
Adobe Extension).

Input of passowrd will be asked when ecryption is enabled. It may not work
correctly on Windows system. Windows users may want to use the `pdf:encrypt`
special instead of command line options.

### Font

Dvipdfmx includes Type1-to-CFF conversion for better compression, TrueType
font subset embedding, CFF OpenType font support. Various other improvement
had been done.

## Font Licensing and Embedding

In OpenType format, information regarding how the font should be treated
when creating documents can be recorded. Dvipdfmx uses this information
to decide whether embedding font is permitted.

This font embedding information is indicated by a flag called
"fsType"; each bit representing different restrictions on font
embedding.
If multiple flag bits are set in fsType, the least restrictive
license granted takes precedence in dvipdfmx.
The fsType flag bits recognized by dvipdfmx is as follows:

* Installable embedding

  All font with this type of license can be embedded.

* Editable embedding

   All font with this type of license can be embedded.

* Embedding for Preview & Print only

   Dvipdfmx give the following warning message for fonts with this
   type of license:

    `This document contains 'Preview & Print' only licensed font`

   For the font with this type of licensing, font embedding is allowed
   solely for the purpose of (on-screen) viewing and/or printing the
   document; further editing of the document or extracting an embedded
   font data for other purpose is not allowed. To ensure this condition,
   you must at least protect your document with non-empty password.

All other flags are treated as more restrictive license than any of the
above flags and treated as "No embedding allowed"; e.g., if both of the
editable-embedding flag and unrecognized license flag is set, the font
is treated as editable-embedding allowed, however, if only unrecognized
flags are set, the font is not embedded.

Embedding flags are preserved in embedded font if the font is embedded
as a TrueType font or a CIDFontType 2 CIDFont. For all font embedded as
a PostScript font (CFF, CIDFontType 0 CIDFont), they are not preserved.
Only /Copyright and /Notice in the FontInfo dictionary are preserved in
this case.

Some font vendors put different embedding restrictions for different
condition; e.g., font embedding might be not permitted for commercial
materials unless you acquire "commercial license" separately.
Please read EULA carefully before making decision on font usage.


Adobe provide a font licensing FAQ and a list of embedding permissions
for Adobe Type Library fonts:

http://www.adobe.com/type/browser/legal/

For Japanese font in general, embedding permission tend to be somewhat
restrictive. Japanese users should read the statement regarding font
embedding from Japan Typography Association (in Japanese):

http://www.typography.or.jp/act/morals/moral4.html
