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

#ifndef _MPS_MAIN_H_
#define _MPS_MAIN_H_

#include  "mfileio.h"
#include  "dpxutil.h"
#include  "pst.h"

extern int trace_mps;

struct mpsi {
  const char *cur_op;
  struct {
    dpx_stack operand;
    dpx_stack dict;
    dpx_stack exec;
  } stack;
  pst_obj *systemdict;
  pst_obj *globaldict;
  pst_obj *userdict;
  
  int      rand_seed;
  int      compat_mode;
};

/* Compatibility */
#define MP_CMODE_MPOST    0
#define MP_CMODE_DVIPSK   1
#define MP_CMODE_PTEXVERT 2
#define MP_CMODE_NATIVE   3

typedef struct mpsi mpsi;
typedef int (*mps_op_fn_ptr) (mpsi *);
typedef struct {
  const char    *name;
  mps_op_fn_ptr  action;
} pst_operator;

extern int  mps_add_systemdict (mpsi *p, pst_obj *obj);
extern pst_obj *pst_new_dict (size_t size);

#endif /* _MPS_MAIN_H_ */
