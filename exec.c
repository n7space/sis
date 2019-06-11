/* This file is part of SIS (SPARC instruction simulator)

   Copyright (C) 1995-2017 Free Software Foundation, Inc.
   Contributed by Jiri Gaisler, European Space Agency

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include "config.h"
#include "sis.h"
#include <inttypes.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

int ext_irl[NCPU];

#define SIGN_BIT 0x80000000

/* Add two unsigned 32-bit integers, and calculate the carry out. */

static uint32
add32 (uint32 n1, uint32 n2, int *carry)
{
  uint32 result = n1 + n2;

  *carry = result < n1 || result < n2;
  return result;
}

/* Multiply two 32-bit integers.  */

void
mul64 (uint32 n1, uint32 n2, uint32 *result_hi, uint32 *result_lo, int msigned)
{
  uint32 lo, mid1, mid2, hi, reg_lo, reg_hi;
  int carry;
  int sign = 0;

  /* If this is a signed multiply, calculate the sign of the result
     and make the operands positive.  */
  if (msigned)
    {
      sign = (n1 ^ n2) & SIGN_BIT;
      if (n1 & SIGN_BIT)
	n1 = -n1;
      if (n2 & SIGN_BIT)
	n2 = -n2;
      
    }
  
  /* We can split the 32x32 into four 16x16 operations. This ensures
     that we do not lose precision on 32bit only hosts: */
  lo =   ((n1 & 0xFFFF) * (n2 & 0xFFFF));
  mid1 = ((n1 & 0xFFFF) * ((n2 >> 16) & 0xFFFF));
  mid2 = (((n1 >> 16) & 0xFFFF) * (n2 & 0xFFFF));
  hi =   (((n1 >> 16) & 0xFFFF) * ((n2 >> 16) & 0xFFFF));
  
  /* We now need to add all of these results together, taking care
     to propogate the carries from the additions: */
  reg_lo = add32 (lo, (mid1 << 16), &carry);
  reg_hi = carry;
  reg_lo = add32 (reg_lo, (mid2 << 16), &carry);
  reg_hi += (carry + ((mid1 >> 16) & 0xFFFF) + ((mid2 >> 16) & 0xFFFF) + hi);

  /* Negate result if necessary. */
  if (sign)
    {
      reg_hi = ~ reg_hi;
      reg_lo = - reg_lo;
      if (reg_lo == 0)
	reg_hi++;
    }
  
  *result_lo = reg_lo;
  *result_hi = reg_hi;
}


/* Divide a 64-bit integer by a 32-bit integer.  We cheat and assume
   that the host compiler supports long long operations.  */

void
div64 (uint32 n1_hi, uint32 n1_low, uint32 n2, uint32 *result, int msigned)
{
  uint64 n1;

  n1 = ((uint64) n1_hi) << 32;
  n1 |= ((uint64) n1_low) & 0xffffffff;

  if (msigned)
    {
      int64 n1_s = (int64) n1;
      int32 n2_s = (int32) n2;
      n1_s = n1_s / n2_s;
      n1 = (uint64) n1_s;
    }
  else
    n1 = n1 / n2;

  *result = (uint32) (n1 & 0xffffffff);
}

void
init_regs(sregs)
    struct pstate  *sregs;
{
  int i;

  ebase.wphit = 0;

  for (i=0; i<NCPU; i++) {  
    sregs[i].pc = 0;
    sregs[i].npc = 4;
    sregs[i].trap = 0;
    sregs[i].psr &= 0x00f03fdf;
    if (cputype == CPU_LEON3)
      sregs[i].psr |= 0xF3000080;	/* Set supervisor bit */
    else
    if (cputype == CPU_LEON2)
      sregs[i].psr |= 0x00000080;	/* Set supervisor bit */
    else
      sregs[i].psr |= 0x11000080;	/* Set supervisor bit */
    sregs[i].breakpoint = 0;
    sregs[i].fpstate = 0;
    sregs[i].fpqn = 0;
    sregs[i].ftime = 0;
    sregs[i].ltime = 0;
    sregs[i].err_mode = 0;
    ext_irl[i] = 0;
    sregs[i].g[0] = 0;
    sregs[i].r[0] = 0;
    sregs[i].fs = (float32 *) sregs[i].fd;
    sregs[i].fsi = (int32 *) sregs[i].fd;
    sregs[i].fsr = 0;
    sregs[i].fpu_pres = !nfp;
    set_fsr(sregs[i].fsr);
    sregs[i].ildreg = 0;
    sregs[i].ildtime = 0;

    sregs[i].y = 0;
    sregs[i].asr17 = 0;

    sregs[i].rett_err = 0;
    sregs[i].jmpltime = 0;
    if (cputype == CPU_LEON3) {
        sregs[i].asr17 = 0x04000107 | (i << 28);
        if (!nfp) sregs[i].asr17 |= (3 << 10);  /* Meiko FPU */
    }
    sregs[i].cpu = i;
    sregs[i].simtime = 0;
    sregs[i].pwdtime = 0;
    sregs[i].pwdstart = 0;
    if (i == 0)
        sregs[i].pwd_mode = 0;
    else
        sregs[i].pwd_mode = 1;
    sregs[i].mip = 0;
    sregs[i].mstatus = 0;
    sregs[i].mie = 0;
    sregs[i].mpp = 0;
    sregs[i].mode = 1;
    sregs[i].lrq = 0;
    sregs[i].bphit = 0;
  }
}

#ifdef ENABLE_L1CACHE
void
l1data_snoop(uint32 address, uint32 cpu)
{
	int i;
	for (i=0; i<ncpu; i++) {
		if (sregs[i].l1dtags[(address >> L1DLINEBITS) & L1DMASK] == (address >> L1DLINEBITS)) {
			if (cpu != i) {
				sregs[i].l1dtags[(address >> L1DLINEBITS) & L1DMASK] = 0;
//				printf("l1 snoop hit : 0x%08X,  %d  %d\n", address, cpu, i);
			}
		}
	}
}

void
l1data_update(uint32 address, uint32 cpu)
{
      if (sregs[cpu].l1dtags[address >> L1DLINEBITS & L1DMASK] != (address >> L1DLINEBITS))
        {
	  sregs[cpu].l1dtags[(address >> L1DLINEBITS) & L1DMASK] = (address >> L1DLINEBITS);
	  sregs[cpu].hold += T_L1DMISS;
	  sregs[cpu].l1dmiss++;
        }
}
#endif
