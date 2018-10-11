# Dvipdfmx and Xdvipdfmx for TeX Live

## Introduction

The dvipdfmx (formerly dvipdfm-cjk) project provides an eXtended
version of the dvipdfm, a DVI to PDF translator developed by
Mark A. Wicks.

The primary goal of this project is to support multi-byte character
encodings and large character sets such as for East Asian languages.
This project started as a combined work of the dvipdfm-jpn project by
Shunsaku Hirata and its modified one, dvipdfm-kor, by Jin-Hwan Cho.

Extensions to dvipdfm includes,

* Support for OpenType and TrueType font including partial support
  for OpenType Layout GSUB Feature for finding glyphs and vertical
  writing.
* Support for CJK-LaTeX and HLaTeX with SubFont Definition Files.
* Support for various legacy multi-byte encodings via PostScript CMap
  Resources.
* Unicode related features: Unicode as an input encoding and
  auto-creation of ToUnicode CMaps.
* Support for pTeX (a Japanese localized variant of TeX) including
  vertical writing extension.
* Some extended DVI specials.
* Reduction of output files size with on-the-fly Type1 to CFF (Type1C)
  conversion and PDF object stream.
* Advanced raster image support including alpha channels, embedded
  ICC profiles, 16-bit bit-depth colors, and so on.
* Basic PDF password security support for PDF output.

Missing features are

* Linearization.
* Color Management.
* Resampling of images.
* Selection of compression filters.
* Variable font and OpenType 1.8.
* and many more...

Dvipdfmx is now maintained as part of TeX Live.

### Xdvipdfmx

Xdvipdfmx is an extended version of dvipdfmx, and is now incorporated
in the same sources.

The xdvipdfmx extensions provides support for the Extended
DVI (.xdv) format generated by XeTeX, including support for
platform-native fonts and the XeTeX graphics primitives,
as well as Unicode text and OpenType font.

XeTeX originally used a Mac-specific program called xdv2pdf as
the backend instead of xdvipdfmx. The xdv2pdf program supported
a couple of special effects that are not yet available through
xdvipdfmx: The Quartz graphics-based shadow support, AAT "variation"
fonts like Skia, transparency as an attribute of font color, and so
on. It would be nice if they continue to be supported.
Suggestions and help are welcomed.

## Installation

Typical usage and installation steps are not different from the
original dvipdfm.
Please refer documents from dvipdfm distribution for detailed
instruction on how to install and how to use dvipdfm. The dvipdfm
manual is available from CTAN site:

https://www.ctan.org/tex-archive/dviware/dvipdfm

Optionally the libpaper library may be used to handle paper sizes.


## Auxiliary Files

This section is mostly for supporting legacy encodings and legacy
font format such as PostScript Type1 font.
XeTeX users may skip this section.

Dvipdfmx has a capability to handle various input encodings from
classic 7-bit encodings to variable-width multi-byte encodings.
It also has some sort of support for Unicode. Various auxiliary files
which are not common to TeX users are needed to enable those
features.
This section shortly describes those auxiliary files.

### PostScript CMap Resources

pTeX users should install PostScript CMap Resources. They are
provided by `adobemapping` package.

PostScript CMap Resources are required for supporting lagacy
encodings such as Shift-JIS, EUC-JP, Big5, and other East Asian
encodings.
Dvipdfmx internally identifies glyphs in fonts with identifiers (CID)
represented as an integer ranging from 0 to 65535. The PostScript
CMap Resource describes the mapping between sequences of input
character codes and CIDs. Dvipdfmx has an extensible support for
multi-byte encodings via PostScript CMap Resources.

CMap files for standard East Asian encodings for use with
Adobe's character collections are included in `adobemapping`
package.
The latest version of those CMap files maintained by Adobe can be
found at:

https://github.com/adobe-type-tools/cmap-resources

Those files are required for supporting pTeX.

### SubFont Definition Files

HLaTeX and CJK-LaTeX users are requied to install SubFont Definition
Files. They are available as a part of `ttfutils` package.

CJK fonts usually contain several thousand of glyphs.
For using such fonts with (original) TeX, which can only handle 8-bit
encodings, it is necessary to split such large fonts into several
subfonts. The SubFont Definition File (SFD) specify the way how those
fonts are split into subfonts.
Dvipdfmx uses SFD files to convert subfonts back to a single font.

SFD files are not required for use with TeX variants which can handle
multi-byte character encodings such as pTeX, upTeX, XeTeX, and Omega.
HLaTeX and CJK-LaTeX users are required to have those files to be
installed.
SubFont Definition Files are available as a part of `ttfutils`
package for TeX Live users.

### Adobe Glyph List and ToUnicode Mapping

Anyone who want to use Type1 font should install Adobe Glyph List
which is provided by `glyphlist` package for TeX Live. pTeX users
are usually required to install ToUnicode Mappings which is contained
in `adobemapping` package.

The Adobe Glyph List (AGL) describes correspondence between
PostScript glyph names (e.g., AE, Aacute, ...) and it's Unicode
character sequences.
Some features described in the section "Unicode Support"
requires this file.

Dvipdfmx looks for the file `glyphlist.txt` when conversion from
PostScript glyph names to Unicode sequences is necessary.
This conversion is done in various situations;
when creating ToUnicode CMaps for 8-bit encoding fonts, finding glyph
descriptions from TrueType and OpenType fonts when the font itself
does not provide a mapping from PostScript glyph names to glyph
indices (version 2.0 "post" table), and when the encoding `unicode`
is specified for Type1 font.

AGL files is included in `glyphlist` package for
TeX Live. The latest version maintained by Adobe is found at:

https://github.com/adobe-type-tools/agl-aglfn

ToUnicode Mappings are similar to AGL but they describe
correspondence between CID numbers (instead of glyph names) and
Unicode values.
The content of those files are the same as CMap Resources.
They are required when using TrueType fonts (including OpenType fonts
with TrueType outline) emulated as CID-keyed fonts.
They should be installed in the same directory as ordinary CMap
files.

ToUnicode Mapping files are included in `adobemapping` package for
TeX Live.
Those files are not required for XeTeX users.

## CJK Support

There are various extensions made for dvipdfmx to support CJK
languages.

### Legacy Multi-byte Encodings

Dvipdfmx has an extensible support for multi-byte encodings by means of
the PostScript CMap Resource.
Just like `enc` files are written for 8-bit encodings, one can write
their own CMap files to support custom encodings.
See, Adobe's technical note for details on CMap Resources.
Adobe provides a set of CMap files necessary for processing various
CJK encodings in conjunction with their character collections.
See, section of "PostScript CMap Resources".

### Vertical Writing

Dvipdfmx supports vertical writing extension used by pTeX and upTeX.
A DVI instruction to set writing mode is supported. OpenType Layout
GSUB Feature is supported for selecting vertical version of glyphs.

## Unicode Support

There are several features for supporting Unicode in dvipdfmx.

### Unicode as Input Encoding

Dvipdfmx supports an additional keyword `unicode` in the fontmap
encoding field. It can be used when character codes are specified
as Unicode in the input DVI file. Unicode support is basically
limited to Basic Multilingual Plane (BMP) since there are no support
for code ranges requires more than three bytes in TFM and extended
TFM formats. The fontmap option `-p` can be used for specifying plane
other than BMP. IVS support is not available.

### ToUnicode CMap Support

In PDF, texts are often not encoded in Unicode encodings.
However, modern applications usually require texts to be encoded
in Unicode encodings to make them reusable. The ToUnicode CMap is
a bridge between PDF text string encoding and Unicode and makes it
possible to extract texts in PDF as Unicode encoded strings.
It is important to make PDF search-able and texts in PDF
copy-and-past-able. Dvipdfmx supports auto-creation of ToUnicode
CMaps.

It is done in various ways, by inverse lookup of OpenType Unicode
cmap for OpenType fonts or by converting PostScript glyph names to
Unicode sequences via Adobe Glyph List for Type1 fonts.

It will not work properly for multiply encoded glyphs due to
fundamental limitations of Unicode conversion mechanism with
ToUnicode CMaps.

## Graphics and Image Format

Dvipdfmx does not support various features common to graphics manipulation
programs such as resampling, color conversion, and selection of
compression filters.
Thus, it is recommended to use other programs specialized in
image manipulation for preparation of images.

### Supported Graphics File Format

Supported formats are, PNG, JPEG, JPEG2000, BMP, PDF,
and MetaPost generated EPS. All other format images, such as SVG
and PostScript, must be converted to other format supported by
dvipdfmx before inclusion.
The `-D` option, as in dvipdfm, can be used for this purpose.

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
an experimental stage. J2C format and transparency are not supported.

PDF inclusion is supported too. However, Tagged PDF may cause
problems and annotations are not preserved. Dvipdfmx also supports
inclusion of PDF pages other than the first page.

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

Identifier and filename (specified as a PDF string object) are
mandatory and a dictionary object which is to be added to the stream
dictionary following the filename is optional.

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
(0.16 0 0 0.16 0 0 cm 4 w
50 0 m 50 28 28 50 0 50 c S 100 50
m 72 50 50 28 50 0 c S
50 100 m 50 72 72 50 100 50 c S
0 50 m 28 50 50 72 50 100 c S
100 50 m 100 78 78 100 50 100 c 22 100 0 78 0 50 c
0 22 22 0 50 0 c 78 0 100 22 100 50 c S
0 0 m 20 10 25 5 25 0 c f 0 0 m 10 20 5 25 0 25 c f
100 0 m 80 10 75 5 75 0 c f
100 0 m 90 20 95 25 100 25 c f
100 100 m 80 90 75 95 75 100 c f
100 100 m 90 80 95 75 100 75 c f
0 100 m 20 90 25 95 25 100 c f
0 100 m 10 80 5 75 0 75 c f
50 50 m 70 60 75 55 75 50 c 75 45 70 40 50 50 c f
50 50 m 60 70 55 75 50 75 c 45 75 40 70 50 50 c f
50 50 m 30 60 25 55 25 50 c
25 45 30 40 50 50 c f
50 50 m 60 30 55 25 50 25 c 45 25 40 30 50 50 c f)
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

Other notable extensions are `code`,  `bcontent`, and `econtent`.
The `code` special can be used to insert raw PDF graphics instructions
into output page content stream. It is different from dvipdfm's
`content` special in that it does not enclose contents with a `q`
and `Q` (save-restore of graphics state) pair.
Be careful on using this special as it is very easy to generate
broken PDF files. The `bcontent` and `econtent` pair is fragile and
often incompatible to other groups of special commands. It is not
always guaranteed to work as 'expected'.

### Dvipdfmx Extensions

A new special `dvipdfmx:config` is added to make it possible to
invoke a command line option. Most single letter command line options
except `D` are supported. For examples,

```
dvipdfmx:config V 7
```

sets PDF version to 1.7.

## Font Mapping

Syntax of fontmap file is the same as dvipdfm. There are few
extensions in dvipdfmx. In addition to 8-bit `enc` files and
keyword `builtin` and `none`, dvipdfmx accepts CMap name and
the keyword `unicode` in the encoding field.

### Extended Syntax and Options

Few options are available in dvipdfmx in addition to the original
dvipdfm's one. All options that makes dvipdfmx to use unembedded
fonts are deprecated as by using them makes divpdfmx to create PDF
files which are not compliant to "ISO" spec.

#### SFD Specification

For bundling up fonts split into multiple subfonts via SFD back into
a single font, dvipdfmx supports an extended sytax of the form

```
tfm_name@SFD@  encoding  filename  options
```

A typical example might look like:

```
gbsn@EUC@  GB-EUC-H  gbsn00lp
```

where TFMs `gbsn00, gbsn01, gbsn02...` are mapped into a single font
`gbsn00lp` via the rule described in the SFD file `EUC`.

#### TrueType Collection Index

TrueType Collection index number can be specified with `:n:`
in front of TrueType font name:

```
min10  H  :1:mincho
```

In this example, the option ``:1:`` tells dvipdfmx to select first
TrueType font from the TTC font `mincho.ttc`. Alternatively, the
`-i` option can be used in the option field to specify TTC index:

```
min10  H  mincho  -i 1
```

#### No-embed Switch (deprecated)

*Use of this option is deprecated.*

It is possible to block embedding glyph data with the character `!`
in front of the font name in the font mapping file.

This feature reduces the size of the final PDF output, but the PDF
file may not be viewed exactly in other systems on which
appropriate fonts are not installed.

NOTE: Dvipdfmx always convert input encodings to CIDs and then
uses Identity CMaps in the output PDF. However, ISO-32000-1:2008
describes as

> The Identity-H and Identity-V CMaps shall not be used with a
> non-embedded font. Only standardized character sets may be used.

which had never been in Adobe's PDF References. This makes all
PDF files generated by dvipdfmx with non-embedded CID-keyed fonts
non-compliant to ISO-32000.

#### 'Standard' CJK Fonts (deprecated)

*This feature is deprecated.*

This feature should never be used for new documents. It is described
here since it might still be useful for testing purpose.

Dvipdfmx recognizes several 'Standard' CJK fonts although there are
no such notion in PDF. In older days where there were not so many
freely available CJK fonts, it was sometimes useful to create PDF
files without embedding fonts and let PDF viewers and printers to
use substitute fonts installed in their systems. Dvipdfmx 'knows'
several fonts which might be available in PostScript printers and
PDF viewers such as Acrobat Reader, and uses them without having it.

The list of available 'Standard' CJK fonts is:

Widely available fonts for Japanese PostScript printers,
```
Ryumin-Light
GothicBBB-Medium
```

from Adobe Asian Font Packs for Acrobat Reader 4,

```
MHei-Medium-Acro
MSung-Light-Acro
STSong-Light-Acro
STHeiti-Regular-Acro
HeiseiKakuGo-W5-Acro
HeiseiMin-W3-Acro
HYGoThic-Medium-Acro
HYSMyeongJo-Medium-Acro
```

from Adobe Asian Font Packs for Acrobat Reader 5,

```
MSungStd-Light-Acro
STSongStd-Light-Acro
HYSMyeongJoStd-Medium-Acro
```

from Asian Font Packs for Adobe Reader 6,

```
AdobeMingStd-Light-Acro
AdobeSongStd-Light-Acro
KozMinPro-Regular-Acro
KozGoPro-Medium-Acro
AdobeMyungjoStd-Medium-Acro
```

additions in Adobe Reader 7 and 8,

```
KozMinProVI-Regular
AdobeHeitiStd-Regular
```

Those fonts only support fixed-pitch glyphs. (quarter, third, half,
and full widths)

#### Stylistic Variants (deprecated)

*Use of this option is deprecated.*

Keywords `,Bold`, `,Italic`, and `,BoldItalic` can be used to create
synthetic bold, italic, and bolditalic style variants from other font
using PDF viewer's (or OS's) function.

```
jbtmo@UKS@     UniKSCms-UCS2-H  :0:!batang,Italic
jbtb@Unicode@  Identity-H       !batang/UCS,Bold
```

Availability of this feature highly depends on the implementation of
PDF viewers. This feature is not supported for embedded fonts in the
most of PDF viewers, like Adobe Acrobat and GNU Ghostscript.

Notice that this option automatically disable font embedding thus
use of this option is deprecated.

#### Specifying the Unicode Plane

As there are no existing 3-bytes or 4-bytes TFM formats, the only
way to use Unicode characters other than BMP is to map code range
0-65535 to different planes via (e.g., to plane 1)

```
-p 1
```

fontmap option.

#### OpentType Layout Feature Support

OpenType Layout Feature options mentioned below are only
meaningful when `unicode` encoding is specified.

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
Feature tags like

```
-l vkna:jp04
```

Script and language may be additionally specified as

```
 -l kana.JAN.ruby
```

An example can be

```
uprml-v  unicode  SourceHanSerifJP-Light.otf  -w 1 -l jp90
```

which declares that font should be treated as for vertical writing and
use JIS1990 form for Kanjis.

## Incompatible Changes from Dvipdfm

There are various minor incompatible changes to dvipdfm.

The `-C` command line option  may be used for compatibility to
dvipdfm or older versions of dvipdfmx. The `-C` option takes flags
meaning

* bit position 2: Use semi-transparent filling for tpic shading
  command, instead of opaque gray color. (requires PDF 1.4)
* bit position 3: Treat all CIDFont as fixed-pitch font. This is only
  for backward compatibility.
* bit position 4: Do not replace duplicate fontmap entries.
  Dvipdfm behavior.
* bit position 5: Do not optimize PDF destinations. Use this if you
  want to refer from other files to destinations in the current file.
* bit position 6: Do not use predictor filter for Flate compression.
* bit position 7: Do not use object stream.

The remap option `-r` in fontmaps is no longer supported and is
silently ignored.

## Other Improvements

Numerous improvement over dvipdfm had been done. Especially, font
support is hugely enhanced. Various legacy multi-byte encodings
support is added.

### Encryption

Dvipdfmx offers basic PDF password security support including
256-bits AES encryption.
Use `-S` command line option to enable encryption and use `-P`
option to set permission flags. Each bits of the integer number given
to the `-P` option represents user access permissions; e.g., bit
position 3 for allowing "print", 4 for "modify the contents", 5 for
"copy or extract texts", and so on.

For examples,

```
-P 0x14
```

enables printing, copying and extraction of texts.
 See, Adobe's PDF spec. for full description of those permission
 flags.

Use `-K` option to specify encryption key length. Key length must be
multiple of 8 in the range 40 to 128, or 256 (for PDF version 1.7 plus
Adobe Extension or forthcoming PDF version 2.0).

Input of password will be asked when encryption is enabled. It may
not work correctly on Windows systems. Windows users may want to use
the `pdf:encrypt` special instead of command line options.

### Additional Extensions Related to Font

Dvipdfmx accepts the following syntax for glyph names in `enc` files:
`uni0130`, `zero.onum` and `T_h.liga`.
Each represents a glyph accessed with Unicode value `U+0130`,
oldstyle number for zero and "Th" ligature accessed via OpenType
Layout GSUB Feature `onum` and `liga` respectively.
Note that dvipdfmx does not understand glyph names which directly
use glyph indices such as `index0102` or `gid2104`, since those
indices are private to each font.


## Font Licensing and Embedding

In OpenType format, information regarding how a font should be treated
when creating documents can be recorded. Dvipdfmx uses this
information to decide whether embedding font is permitted.

This font embedding information is indicated by a flag called
`fsType`; each bit representing different restrictions on font
embedding.
If multiple flag bits are set in `fsType`, the least restrictive
license granted takes precedence in dvipdfmx.
The `fsType` flag bits recognized by dvipdfmx is as follows:

* Installable embedding
* Editable embedding
* Embedding for Preview & Print only

Dvipdfmx give the following warning message for fonts with 'Preview &
Print only' setting:

```
This document contains 'Preview & Print' only licensed font
```

For fonts with this type of licensing, font embedding is allowed
solely for the purpose of (on-screen) viewing and/or printing;
further editing of the document or extracting embedded font data
for other purpose is not allowed. To ensure this condition, you must
at least protect your document with non-empty password.

All other flags are treated as more restrictive license than any of
the above flags and treated as "No embedding allowed"; e.g., if both
of the editable-embedding flag and unrecognized license flag is set,
the font is treated as editable-embedding allowed, however, if only
unrecognized flags are set, the font is not embedded.

Embedding flags are preserved in embedded font if the font is embedded
as a TrueType font or a CIDFontType2 CID-keyed font.
For all font embedded as a PostScript font (Type1C and CIDFontType0
CID-keyed font), they are not preserved.
Only /Copyright and /Notice in the FontInfo dictionary are preserved
in this case.

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

## Copyright and Licensing

Copyright (C) 2002-2017 by Jin-Hwan Cho, Shunsaku Hirata,
Matthias Franz, and the dvipdfmx project team.
This package is released under the GNU GPL, version 2,
or (at your option) any later version.
