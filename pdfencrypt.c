/* This is dvipdfmx, an eXtended version of dvipdfm by Mark A. Wicks.

    Copyright (C) 2002-2014 by Jin-Hwan Cho and Shunsaku Hirata,
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef WIN32
#include <conio.h>
#define getch _getch
#else  /* !WIN32 */
#include <unistd.h>
#endif /* WIN32 */

#include "system.h"
#include "mem.h"
#include "error.h"
#include "pdfobj.h"
#include "dpxcrypt.h"

#include "pdfencrypt.h"

#include "dvipdfmx.h"

#define MAX_KEY_LEN 16
#define MAX_STR_LEN 32

static struct pdf_sec {
   unsigned char key[MAX_KEY_LEN];
   int           key_size;

   unsigned char ID[MAX_KEY_LEN];
   unsigned char O[MAX_STR_LEN], U[MAX_STR_LEN];
   int     V, R;
   int64_t P;

   struct {
     int use_aes;
     int encrypt_metadata;
   } setting;

   struct {
     uint64_t objnum;
     uint16_t gennum;
   } label;
} sec_data;

static const unsigned char padding_bytes[MAX_STR_LEN] = {
  0x28, 0xbf, 0x4e, 0x5e, 0x4e, 0x75, 0x8a, 0x41,
  0x64, 0x00, 0x4e, 0x56, 0xff, 0xfa, 0x01, 0x08,
  0x2e, 0x2e, 0x00, 0xb6, 0xd0, 0x68, 0x3e, 0x80,
  0x2f, 0x0c, 0xa9, 0xfe, 0x64, 0x53, 0x69, 0x7a
};

static unsigned char verbose = 0;

void pdf_enc_set_verbose (void)
{
  if (verbose < 255) verbose++;
}

void
pdf_enc_init (int use_aes, int encrypt_metadata)
{
  struct pdf_sec *p = &sec_data;

  srand((unsigned) time(NULL)); /* For AES IV */
  p->setting.use_aes = 1;
  p->setting.encrypt_metadata = 1;
}

#define PRODUCER \
"%s-%s, Copyright 2002-2014 by Jin-Hwan Cho, Matthias Franz, and Shunsaku Hirata"

void
pdf_enc_compute_id_string (char *dviname, char *pdfname)
{
  struct pdf_sec *p = &sec_data;
  char           *date_string, *producer;
  time_t          current_time;
  struct tm      *bd_time;
  MD5_CONTEXT     md5;

  pdf_enc_init(1, 1);

  MD5_init(&md5);

  date_string = NEW(15, char);
  time(&current_time);
  bd_time = localtime(&current_time);
  sprintf(date_string, "%04d%02d%02d%02d%02d%02d",
          bd_time->tm_year + 1900, bd_time->tm_mon + 1, bd_time->tm_mday,
          bd_time->tm_hour, bd_time->tm_min, bd_time->tm_sec);
  MD5_write(&md5, (unsigned char *)date_string, strlen(date_string));
  RELEASE(date_string);

  producer = NEW(strlen(PRODUCER)+strlen(my_name)+strlen(VERSION), char);
  sprintf(producer, PRODUCER, my_name, VERSION);
  MD5_write(&md5, (unsigned char *)producer, strlen(producer));
  RELEASE(producer);

  if (dviname)
    MD5_write(&md5, (unsigned char *)dviname, strlen(dviname));
  if (pdfname)
    MD5_write(&md5, (unsigned char *)pdfname, strlen(pdfname));
  MD5_final(p->ID, &md5);
}

static void
passwd_padding (const char *src, unsigned char *dst)
{
  int len = strlen((char *)src);

  if (len > MAX_STR_LEN)
    len = MAX_STR_LEN;

  memcpy(dst, src, len);
  memcpy(dst + len, padding_bytes, MAX_STR_LEN - len);
}

static void
compute_owner_password (struct pdf_sec *p,
                        const char *opasswd, const char *upasswd)
{
  int  i, j;
  unsigned char padded[MAX_STR_LEN];
  MD5_CONTEXT   md5;
  ARC4_CONTEXT  arc4;
  unsigned char hash[MAX_KEY_LEN];
 
  /*
   * Algorithm 3.3 Computing the encryption dictionary's O (owner password)
   *               value
   */
  passwd_padding((strlen(opasswd) > 0 ? opasswd : upasswd), padded);
 
  MD5_init (&md5);
  MD5_write(&md5, padded, MAX_STR_LEN);
  MD5_final(hash, &md5);
  if (p->R >= 3) {
    for (i = 0; i < 50; i++) {
      /*
       * NOTE: We truncate each MD5 hash as in the following step.
       *       Otherwise Adobe Reader won't decrypt the PDF file.
       */
      MD5_init (&md5);
      MD5_write(&md5, hash, p->key_size);
      MD5_final(hash, &md5);
    }
  }
  ARC4_set_key(&arc4, p->key_size, hash); 
  passwd_padding(upasswd, padded);
  {
    unsigned char tmp1[MAX_STR_LEN], tmp2[MAX_STR_LEN];
    unsigned char key[MAX_KEY_LEN];

    ARC4(&arc4, MAX_STR_LEN, padded, tmp1);
    if (p->R >= 3) {
      for (i = 1; i <= 19; i++) {
        memcpy(tmp2, tmp1, MAX_STR_LEN);
        for (j = 0; j < p->key_size; j++)
          key[j] = hash[j] ^ i;
        ARC4_set_key(&arc4, p->key_size, key);
        ARC4(&arc4, MAX_STR_LEN, tmp2, tmp1);
      }
    }
  }
  memcpy(p->O, hash, MAX_STR_LEN);
}

static void
compute_encryption_key (struct pdf_sec *p, const char *passwd)
{
  int  i;
  unsigned char hash[MAX_STR_LEN], padded[MAX_STR_LEN];
  MD5_CONTEXT   md5;
  /*
   * Algorithm 3.2 Computing an encryption key
   */
  passwd_padding(passwd, padded);
  MD5_init (&md5);
  MD5_write(&md5, padded, MAX_STR_LEN);
  MD5_write(&md5, p->O, MAX_STR_LEN);
  {
    unsigned char tmp[4];

    tmp[0] = (unsigned char)(p->P) & 0xFF;
    tmp[1] = (unsigned char)(p->P >> 8) & 0xFF;
    tmp[2] = (unsigned char)(p->P >> 16) & 0xFF;
    tmp[3] = (unsigned char)(p->P >> 24) & 0xFF;
    MD5_write(&md5, tmp, 4);
  }
  MD5_write(&md5, p->ID, MAX_KEY_LEN);
#if 0
  /* Not Supported Yet */
  if (!p->setting.encrypt_metadata) {
    unsigned char tmp[4] = {0xff, 0xff, 0xff, 0xff};
    MD5_write(&md5, tmp, 4);
  }
#endif
  MD5_final(hash, &md5);

  if (p->R >= 3) {
    for (i = 0; i < 50; i++) {
      /*
       * NOTE: We truncate each MD5 hash as in the following step.
       *       Otherwise Adobe Reader won't decrypt the PDF file.
       */
      MD5_init (&md5);
      MD5_write(&md5, hash, p->key_size);
      MD5_final(hash, &md5);
    }
  }
  memcpy(p->key, hash, p->key_size);
}

static void
compute_user_password (struct pdf_sec *p, const char *uplain)
{
  int           i, j;
  ARC4_CONTEXT  arc4;
  MD5_CONTEXT   md5;
  unsigned char upasswd[MAX_STR_LEN];
  /*
   * Algorithm 3.4 Computing the encryption dictionary's U (user password)
   *               value (Revision 2)
   */
  /*
   * Algorithm 3.5 Computing the encryption dictionary's U (user password)
   *               value (Revision 3)
   */
  compute_encryption_key(p, uplain);

  switch (p->R) {
  case 2:
    ARC4_set_key(&arc4, p->key_size, p->key);
    ARC4(&arc4, MAX_STR_LEN, padding_bytes, upasswd);
    break;
  case 3: case 4:
    {
      unsigned char hash[MAX_STR_LEN];
      unsigned char tmp1[MAX_STR_LEN], tmp2[MAX_STR_LEN];

      MD5_init (&md5);
      MD5_write(&md5, padding_bytes, MAX_STR_LEN);

      MD5_write(&md5, p->ID, MAX_KEY_LEN);
      MD5_final(hash, &md5);

      ARC4_set_key(&arc4, p->key_size, p->key);
      ARC4(&arc4, MAX_KEY_LEN, hash, tmp1);

      for (i = 1; i <= 19; i++) {
        unsigned char key[MAX_KEY_LEN];

        memcpy(tmp2, tmp1, MAX_KEY_LEN);
        for (j = 0; j < p->key_size; j++)
          key[j] = p->key[j] ^ i;
        ARC4_set_key(&arc4, p->key_size, key);
        ARC4(&arc4, MAX_KEY_LEN, tmp2, tmp1);
      }
      memcpy(upasswd, tmp1, MAX_STR_LEN);
    }
    break;
  default:
    ERROR("Invalid revision number.\n");
  }

  memcpy(p->U, upasswd, MAX_STR_LEN);
}

#ifdef WIN32
static char *
getpass (const char *prompt)
{
  static char pwd_buf[128];
  size_t i;

  fputs(prompt, stderr);
  fflush(stderr);
  for (i = 0; i < sizeof(pwd_buf)-1; i++) {
    pwd_buf[i] = getch();
    if (pwd_buf[i] == '\r' || pwd_buf[i] == '\n')
      break;
    fputs("*", stderr);
    fflush(stderr);
  }
  pwd_buf[i] = '\0';
  fputs("\n", stderr);
  return pwd_buf;
}
#endif

void 
pdf_enc_set_passwd (unsigned bits, unsigned perm,
                    const char *oplain, const char *uplain)
{
  struct pdf_sec *p = &sec_data;
  char            opasswd[MAX_PWD_LEN], upasswd[MAX_PWD_LEN];
  char           *retry_passwd;

  if (oplain) {
    strncpy(opasswd, oplain, MAX_PWD_LEN);
  } else {
    while (1) {
      strncpy(opasswd, getpass("Owner password: "), MAX_PWD_LEN);
      retry_passwd = getpass("Re-enter owner password: ");
      if (!strncmp(opasswd, retry_passwd, MAX_PWD_LEN))
        break;
      fputs("Password is not identical.\nTry again.\n", stderr);
      fflush(stderr);
    }
  }
  if (uplain) {
    strncpy(upasswd, uplain, MAX_PWD_LEN);
  } else {
    while (1) {
      strncpy(upasswd, getpass("User password: "), MAX_PWD_LEN);
      retry_passwd = getpass("Re-enter user password: ");
      if (!strncmp(upasswd, retry_passwd, MAX_PWD_LEN))
        break;
      fputs("Password is not identical.\nTry again.\n", stderr);
      fflush(stderr);
    }
  }

  p->key_size = (int) (bits / 8);
  if (p->key_size == 5) /* 40bit */
    p->V = 1;
  else if (p->key_size <= 16) {
    p->V = p->setting.use_aes ? 4 : 2;
  } else {
    WARN("Key length %d unsupported.", bits);
    p->V = 2;
  }
  p->P = (long) (perm | 0xC0U);
  switch (p->V) {
  case 1:
    p->R = (p->P < 0x100L) ? 2 : 3;
    break;
  case 2: case 3:
    p->R = 3;
    break;
  case 4:
    p->R = 4;
    break;
  default:
    p->R = 3;
    break;
  }

  if (p->R >= 3)
    p->P |= ~0xFFFL;

  compute_owner_password(p, opasswd, upasswd);
  compute_user_password (p, upasswd);
}

void
pdf_encrypt_data (const unsigned char *plain, size_t plain_len,
                  unsigned char **cipher, size_t *cipher_len)
{
  struct pdf_sec *p = &sec_data;
  int             tmp_len = p->key_size + 5;
  unsigned char   tmp[MAX_KEY_LEN + 9];
  unsigned char   key[MAX_KEY_LEN];
  MD5_CONTEXT     md5;

  memcpy(tmp, p->key, p->key_size);
  tmp[p->key_size  ] = (unsigned char) p->label.objnum        & 0xFF;
  tmp[p->key_size+1] = (unsigned char)(p->label.objnum >>  8) & 0xFF;
  tmp[p->key_size+2] = (unsigned char)(p->label.objnum >> 16) & 0xFF;
  tmp[p->key_size+3] = (unsigned char)(p->label.gennum)       & 0xFF;
  tmp[p->key_size+4] = (unsigned char)(p->label.gennum >>  8) & 0xFF;
  if (p->V >= 4) {
    tmp[p->key_size + 5] = 0x73;
    tmp[p->key_size + 6] = 0x41;
    tmp[p->key_size + 7] = 0x6c;
    tmp[p->key_size + 8] = 0x54;
    tmp_len += 4;
  }
  MD5_init (&md5);
  MD5_write(&md5, tmp, tmp_len);
  MD5_final(key, &md5);

  if (p->V < 4) {
    ARC4_CONTEXT arc4;

    *cipher_len = plain_len;
    *cipher     = NEW(*cipher_len, unsigned char);
    ARC4_set_key(&arc4, (p->key_size + 5 > 16 ? 16 : p->key_size + 5), key);
    ARC4(&arc4, plain_len, plain, *cipher);
  } else if (p->V == 4) {
    AES_CONTEXT aes;

    AES_cbc_set_key(&aes, (p->key_size + 5 > 16 ? 16 : p->key_size + 5), key);
    AES_cbc_encrypt(&aes, plain, plain_len, cipher, cipher_len);
  }
}

pdf_obj *
pdf_encrypt_obj (void)
{
  struct pdf_sec *p = &sec_data;
  pdf_obj *doc_encrypt;

  doc_encrypt = pdf_new_dict();

  pdf_add_dict(doc_encrypt,  pdf_new_name("Filter"), pdf_new_name("Standard"));
  pdf_add_dict(doc_encrypt,  pdf_new_name("V"),      pdf_new_number(p->V));
  if (p->V > 1)
    pdf_add_dict(doc_encrypt,
                 pdf_new_name("Length"), pdf_new_number(p->key_size * 8));
  if (p->V >= 4) {
    pdf_obj *CF, *StdCF;
    CF    = pdf_new_dict();
    StdCF = pdf_new_dict();
    pdf_add_dict(StdCF, pdf_new_name("CFM"),       pdf_new_name("AESV2")); /* 128bit AES */
    pdf_add_dict(StdCF, pdf_new_name("AuthEvent"), pdf_new_name("DocOpen"));
    pdf_add_dict(StdCF, pdf_new_name("Length"),    pdf_new_number(p->key_size));
    pdf_add_dict(CF, pdf_new_name("StdCF"), StdCF);
    pdf_add_dict(doc_encrypt, pdf_new_name("CF"), CF);
    pdf_add_dict(doc_encrypt, pdf_new_name("StmF"), pdf_new_name("StdCF"));
    pdf_add_dict(doc_encrypt, pdf_new_name("StrF"), pdf_new_name("StdCF"));
#if 0
    if (!p->setting.encrypt_metadata)
      pdf_add_dict(doc_encrypt,
                   pdf_new_name("EncryptMetadata"), pdf_new_boolean(false));
#endif
  }
  pdf_add_dict(doc_encrypt, pdf_new_name("R"), pdf_new_number(p->R));
  pdf_add_dict(doc_encrypt, pdf_new_name("O"), pdf_new_string(p->O, 32));
  pdf_add_dict(doc_encrypt, pdf_new_name("U"), pdf_new_string(p->U, 32));
  pdf_add_dict(doc_encrypt,	pdf_new_name("P"), pdf_new_number(p->P));

  return doc_encrypt;
}

pdf_obj *pdf_enc_id_array (void)
{
  struct pdf_sec *p = &sec_data;
  pdf_obj *id = pdf_new_array();

  pdf_add_array(id, pdf_new_string(p->ID, MAX_KEY_LEN));
  pdf_add_array(id, pdf_new_string(p->ID, MAX_KEY_LEN));
  
  return id;
}

void pdf_enc_set_label (unsigned long label)
{
  struct pdf_sec *p = &sec_data;

  p->label.objnum = label;
}

void pdf_enc_set_generation (unsigned generation)
{
  struct pdf_sec *p = &sec_data;

  p->label.gennum = generation;
}
