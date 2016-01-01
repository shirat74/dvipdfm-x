/*

    This is dvipdfmx, an eXtended version of dvipdfm by Mark A. Wicks.

    Copyright (C) 2007-2015 by Jin-Hwan Cho and Shunsaku Hirata,
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

#ifndef _PDFENCRYPT_H_
#define _PDFENCRYPT_H_

#include "pdfobj.h"

#define MAX_PWD_LEN 127

typedef struct pdf_sec pdf_sec;

extern pdf_sec *pdf_sec_init             (const unsigned char *id,
                                          int keybits, int32_t permission,
                                          int use_aes, int encrypt_metadata);
extern void     pdf_sec_delete           (pdf_sec **p);

/* Setup */
extern void     pdf_sec_set_password     (pdf_sec *p,
                                          const char *opasswd,
                                          const char *upasswd);
extern pdf_obj *pdf_sec_get_encrypt_dict (pdf_sec *p);

/* Encryption */
extern void pdf_sec_set_label      (pdf_sec *p, uint32_t label);
extern void pdf_sec_set_generation (pdf_sec *p, uint16_t generation);
extern void pdf_sec_encrypt_data   (pdf_sec *p,
                                    const unsigned char *plain,
                                    size_t               plain_len,
                                    unsigned char      **cipher,
                                    size_t              *cipher_len);

#endif /* _PDFENCRYPT_H_ */
