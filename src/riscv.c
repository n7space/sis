/* This file is part of SIS (SPARC/RISCV instruction simulator)

   Copyright (C) 1995-2019 Free Software Foundation, Inc.
   Contributed by Jiri Gaisler, Sweden.

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

#include "riscv.h"
#include <inttypes.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <fenv.h>

#ifdef WORDS_BIGENDIAN
#define BEH 1
#else
#define BEH 0
#endif

/* This routine should return the accrued FPU exceptions */
static int
riscv_get_accex ()
{
  int fexc, accx;

  fexc = fetestexcept (FE_ALL_EXCEPT);
  accx = 0;
  if (fexc & FE_INEXACT)
    accx |= 1;
  if (fexc & FE_UNDERFLOW)
    accx |= 2;
  if (fexc & FE_OVERFLOW)
    accx |= 4;
  if (fexc & FE_DIVBYZERO)
    accx |= 8;
  if (fexc & FE_INVALID)
    accx |= 0x10;
  return accx;
}

/* How to map RISCV FSR onto the host */
static void
riscv_set_fsr (fsr)
     uint32 fsr;
{
  int fround;

  fsr >>= 5;
  fsr &= 0x3;
  switch (fsr)
    {
    case 0:
      fround = FE_TONEAREST;
      break;
    case 1:
      fround = FE_TOWARDZERO;
      break;
    case 2:
      fround = FE_DOWNWARD;
      break;
    case 3:
      fround = FE_UPWARD;
      break;
    }
  fesetround (fround);
}

static int
set_csr (address, sregs, value)
     uint32 address;
     struct pstate *sregs;
     uint32 value;
{
  int res = 0;
  switch (address)
    {
    case CSR_MSTATUS:
      sregs->mstatus = value & MSTATUS_MASK;
      break;
    case CSR_MTVEC:
      sregs->mtvec = value;
      break;
    case CSR_MEPC:
      sregs->epc = value;
      break;
    case CSR_MIE:
      sregs->mie = value;
      break;
    case CSR_MIP:
      sregs->mip = value;
      break;
    case CSR_MSCRATCH:
      sregs->mscratch = value;
      break;
    case CSR_MCAUSE:
      sregs->mcause = value;
      break;
    case CSR_FFLAGS:
      sregs->fsr = (sregs->fsr & ~0x1f) | value;
      riscv_set_fsr (sregs->fsr);
      break;
    case CSR_FRM:
      sregs->fsr = (sregs->fsr & ~0xe0) | (value << 5);
      riscv_set_fsr (sregs->fsr);
      break;
    case CSR_FCSR:
      sregs->fsr = value;
      riscv_set_fsr (sregs->fsr);
      break;
    default:
      res = 1;
    }
  if (sis_verbose > 1)
    printf (" %8" PRIu64 " set csr 0x%03X :  %08X\n",
	    sregs->simtime, address, value);
  rv32_check_lirq (sregs->cpu);
  return res;
}

int
get_csr (address, sregs)
     uint32 address;
     struct pstate *sregs;
{
  uint64 tmp;
  switch (address)
    {
    case CSR_MSTATUS:
      return (sregs->mstatus);
      break;
    case CSR_MCAUSE:
      return (sregs->mcause);
      break;
    case CSR_MTVEC:
      return (sregs->mtvec);
      break;
    case CSR_MEPC:
      return (sregs->epc);
      break;
    case CSR_MIP:
      if (ext_irl[sregs->cpu])
	return (sregs->mip | MIP_MEIP);
      else
	return (sregs->mip);
      break;
    case CSR_MIE:
      return (sregs->mie);
      break;
    case CSR_MTVAL:
      return (sregs->mtval);
      break;
    case CSR_MISA:
      return (0x40000100);
      break;
    case CSR_TIME:
      return (sregs->simtime & 0xffffffff);
      break;
    case CSR_TIMEH:
      tmp = sregs->simtime >> 32;
      return tmp & 0xffffffff;
      break;
    case CSR_MHARTID:
      return (sregs->cpu);
      break;
    case CSR_MSCRATCH:
      return (sregs->mscratch);
      break;
    case CSR_FFLAGS:
      return (sregs->fsr & 0x1f);
      break;
    case CSR_FRM:
      return ((sregs->fsr >> 5) & 0x7);
      break;
    case CSR_FCSR:
      return (sregs->fsr);
      break;
    default:
      return 0;
    }
}

int
rv32_check_lirq (int cpu)
{
  uint32 tmpirq;

  if (sregs[cpu].mstatus & MSTATUS_MIE)
    {
      tmpirq = sregs[cpu].mip & sregs[cpu].mie;
      if (tmpirq & MIP_MEIP)
	ext_irl[cpu] = 0x1b;
      else if (tmpirq & MIP_MSIP)
	ext_irl[cpu] = 0x13;
      else if (tmpirq & MIP_MTIP)
	ext_irl[cpu] = 0x17;
      if ((ext_irl[cpu]) && sregs[cpu].pwd_mode)
	{
	  sregs[cpu].pwdtime += sregs[cpu].simtime - sregs[cpu].pwdstart;
	  sregs[cpu].pwd_mode = 0;
	}
    }
}

static int
riscv_dispatch_instruction (sregs)
     struct pstate *sregs;
{

  uint32 op1, op2, op3, rd, rs1, rs2, npc, btrue, inst, *wdata;
  int32 sop1, sop2, result, offset;
  int32 pc, data, address, ws, mexc, fcc;
  unsigned char op, funct3, funct5, rs1p, rs2p, funct2, frs1, frs2, frd;
  int64 sop64a, sop64b;
  uint64 op64a, op64b;

  sregs->ninst++;

#ifdef C_EXTENSION
  if ((sregs->inst & 3) != 3)
    {
      /* Compressed instructions  (RV32C) */
      npc = sregs->pc + 2;
      funct3 = (sregs->inst >> 13) & 7;
      rs1p = ((sregs->inst >> 7) & 7) | 8;
      rs2p = ((sregs->inst >> 2) & 7) | 8;
      rs1 = ((sregs->inst >> 7) & 0x1f);
      rs2 = ((sregs->inst >> 2) & 0x1f);
      switch (sregs->inst & 3)
	{
	case 0:
	  address =
	    (int32) sregs->r[rs1p] + (int32) EXTRACT_RVC_LW_IMM (sregs->inst);
	  switch (funct3)
	    {
	    case CADDI4SPN:	/* addi rd', x2, nzuimm[9:2] */
	      if ((sregs->inst & 0x0ffff) == 0)
		{
		  sregs->trap = TRAP_ILLEG;
		  break;
		}
	      sregs->r[rs2p] =
		(int32) sregs->r[2] +
		(int32) EXTRACT_RVC_ADDI4SPN_IMM (sregs->inst);
	      break;
	    case CLW:		/* lw rd', offset[6:2](rs1') */
	      if (address & 0x3)
		{
		  sregs->trap = TRAP_LMALI;
		  sregs->wpaddress = address;
		  break;
		}
	      mexc = ms->memory_read (address, &op1, &ws);
	      sregs->hold += ws;
	      if (mexc)
		{
		  sregs->trap = TRAP_LEXC;
		  sregs->wpaddress = address;
		}
	      else
		{
		  sregs->r[rs2p] = op1;
		}
	      break;
	    case CSW:		/* sw rs2', offset[6:2](rs1') */
	      if (address & 0x3)
		{
		  sregs->trap = TRAP_SMALI;
		  sregs->wpaddress = address;
		  break;
		}
	      mexc = ms->memory_write (address, &sregs->r[rs2p], 2, &ws);
	      sregs->hold += ws;
	      if (mexc)
		{
		  sregs->trap = TRAP_SEXC;
		  sregs->wpaddress = address;
		}
	      break;
	    case CFLW:		/* lw rd', offset[6:2](rs1') */
	      if (address & 0x3)
		{
		  sregs->trap = TRAP_LMALI;
		  sregs->wpaddress = address;
		  break;
		}
	      mexc = ms->memory_read (address, &op1, &ws);
	      sregs->hold += ws;
	      if (mexc)
		{
		  sregs->trap = TRAP_LEXC;
		  sregs->wpaddress = address;
		}
	      else
		{
		  sregs->fsi[(rs2p << 1) + BEH] = op1;
#ifdef FPU_D_ENABLED
		  sregs->fsi[(rs2p << 1) + 1 - BEH] = -1;
#endif
		}
	      break;
#ifdef FPU_D_ENABLED
	    case CFLD:		/* ld frd', offset[7:3](rs1') */
	      address = (int32) sregs->r[rs1p] +
		(int32) EXTRACT_RVC_LD_IMM (sregs->inst);
	      if (address & LDDM)
		{
		  sregs->trap = TRAP_LMALI;
		  sregs->wpaddress = address;
		  break;
		}
	      mexc = ms->memory_read (address, &op1, &ws);
	      sregs->hold += ws;
	      mexc |= ms->memory_read (address + 4, &op2, &ws);
	      sregs->hold += ws;
	      if (mexc)
		{
		  sregs->trap = TRAP_LEXC;
		  sregs->wpaddress = address;
		}
	      else
		{
		  sregs->fsi[(rs2p << 1) + BEH] = op1;
		  sregs->fsi[(rs2p << 1) + 1 - BEH] = op2;
		}
	      break;
#endif
	    case CFSW:		/* sw rs2', offset[6:2](rs1') */
	      if (address & 0x3)
		{
		  sregs->trap = TRAP_SMALI;
		  sregs->wpaddress = address;
		  break;
		}
	      mexc =
		ms->memory_write (address,
				  (uint32 *) & sregs->fsi[(rs2p << 1) + BEH],
				  2, &ws);
	      sregs->hold += ws;
	      if (mexc)
		{
		  sregs->trap = TRAP_SEXC;
		  sregs->wpaddress = address;
		}
	      break;
#ifdef FPU_D_ENABLED
	    case CFSD:		/* sd frs2', offset[7:3](rs1') */
	      address = (int32) sregs->r[rs1p] +
		(int32) EXTRACT_RVC_LD_IMM (sregs->inst);
	      if (address & LDDM)
		{
		  sregs->trap = TRAP_SMALI;
		  sregs->wpaddress = address;
		  break;
		}
	      mexc =
		ms->memory_write (address,
				  (uint32 *) & sregs->fsi[(rs2p << 1) + BEH],
				  2, &ws);
	      sregs->hold += ws;
	      mexc |=
		ms->memory_write (address + 4,
				  (uint32 *) & sregs->fsi[(rs2p << 1) + 1 -
							  BEH], 2, &ws);
	      sregs->hold += ws;
	      if (mexc)
		{
		  sregs->trap = TRAP_SEXC;
		  sregs->wpaddress = address;
		}
	      break;
#endif
	    default:
	      sregs->trap = TRAP_ILLEG;
	    }
	  break;
	case 1:
	  switch (funct3)
	    {
	    case CADDI:	/* addi rd, rd, nzimm[5:0] */
	      sop1 = sregs->r[rs1];
	      sop2 = EXTRACT_RVC_IMM (sregs->inst);
	      sregs->r[rs1] = sop1 + sop2;
	      break;
	    case CLI:		/* addi rd, x0, imm[5:0] */
	      sregs->r[rs1] = EXTRACT_RVC_IMM (sregs->inst);
	      break;
	    case CJAL:		/* jal x1, offset[11:1] */
	    case CJNL:		/* jal x0, offset[11:1] */
#ifdef STAT
	      sregs->nbranch++;
#endif
	      offset = EXTRACT_RVC_J_IMM (sregs->inst);
	      if (funct3 == CJAL)
		sregs->r[1] = npc;
	      npc = sregs->pc + offset;
	      npc &= ~1;
	      if (!npc)
		sregs->trap = NULL_TRAP;	// halt on null pointer
	      if (ebase.coven)
		cov_jmp (sregs->pc, npc);
	      break;
	    case CADDI16SP:	/* addi x2, x2, nzimm[9:4] */
	      if (rs1 == 2)
		{
		  sop1 = sregs->r[rs1];
		  sop2 = EXTRACT_RVC_ADDI16SP_IMM (sregs->inst);
		  sregs->r[rs1] = sop1 + sop2;
		}
	      else
		{		/* CLUI:  lui rd, nzuimm[17:12 */
		  sregs->r[rs1] = EXTRACT_RVC_LUI_IMM (sregs->inst);
		}
	      break;
	    case CARITH:
	      sop2 = EXTRACT_RVC_IMM (sregs->inst);
	      switch ((sregs->inst >> 10) & 7)
		{
		case 0:	/* srli rd', rd', shamt[5:0] */
		  op1 = sregs->r[rs1p];
		  sregs->r[rs1p] = op1 >> sop2;	/* SRL */
		  break;
		case 1:	/* srai rd', rd', shamt[5:0] */
		  sop1 = sregs->r[rs1p];
		  sregs->r[rs1p] = sop1 >> sop2;	/* SRA */
		  break;
		case 2:
		case 6:	/* andi rd', rd', imm[5:0] */
		  sregs->r[rs1p] &= sop2;	/* ANDI */
		  break;
		case 3:
		  switch ((sregs->inst >> 5) & 3)
		    {
		    case 0:	/* sub rd', rd', rs2' */
		      sregs->r[rs1p] -= sregs->r[rs2p];	/* SUB */
		      break;
		    case 1:	/* xor rd', rd', rs2' */
		      sregs->r[rs1p] ^= sregs->r[rs2p];	/* XOR */
		      break;
		    case 2:	/* or rd', rd', rs2' */
		      sregs->r[rs1p] |= sregs->r[rs2p];	/* OR */
		      break;
		    case 3:	/* and rd', rd', rs2' */
		      sregs->r[rs1p] &= sregs->r[rs2p];	/* AND */
		      break;
		    }
		  break;
		default:
		  sregs->trap = TRAP_ILLEG;
		}
	      break;
	    case CBEQZ:	/* beq rs1', x0, offset[8:1] */
	      offset = EXTRACT_RVC_B_IMM (sregs->inst);
	      if (!sregs->r[rs1p])
		{
		  npc = sregs->pc + offset;
		  if (offset >= 0)
		    sregs->icnt += T_BMISS;
		  if (ebase.coven)
		    cov_bt (sregs->pc, npc);
		}
	      else
		{
		  if (offset < 0)
		    sregs->icnt += T_BMISS;
		  if (ebase.coven)
		    {
		      cov_bnt (sregs->pc);
		      cov_start (npc);
		    }
		}
	      npc &= ~1;
	      break;
	    case CBNEZ:	/* bne rs1', x0, offset[8:1] */
	      offset = EXTRACT_RVC_B_IMM (sregs->inst);
	      if (sregs->r[rs1p])
		{
		  npc = sregs->pc + offset;
		  if (offset >= 0)
		    sregs->icnt += T_BMISS;
		  if (ebase.coven)
		    cov_bt (sregs->pc, npc);
		}
	      else
		{
		  if (offset < 0)
		    sregs->icnt += T_BMISS;
		  if (ebase.coven)
		    {
		      cov_bnt (sregs->pc);
		      cov_start (npc);
		    }
		}
	      npc &= ~1;
	      break;
	    default:
	      sregs->trap = TRAP_ILLEG;
	    }
	  break;
	case 2:
	  switch (funct3)
	    {
	    case 0:		/* slli rd', rd', shamt[5:0] */
	      sop2 = EXTRACT_RVC_IMM (sregs->inst);
	      sregs->r[rs1] <<= sop2;	/* SLL */
	      break;
	    case 2:		/* LWSP: lw rd, offset[7:2](x2) */
	      address = sregs->r[2] + EXTRACT_RVC_LWSP_IMM (sregs->inst);
	      if (address & 0x3)
		{
		  sregs->trap = TRAP_LMALI;
		  sregs->wpaddress = address;
		  break;
		}
	      mexc = ms->memory_read (address, &op1, &ws);
	      sregs->hold += ws;
	      if (mexc)
		{
		  sregs->trap = TRAP_LEXC;
		  sregs->wpaddress = address;
		}
	      else
		{
		  sregs->r[rs1] = op1;
		}
	      break;
#ifdef FPU_D_ENABLED
	    case 1:		/* FLDSP: ld frd, offset[8:3](x2) */
	      address = sregs->r[2] + EXTRACT_RVC_LDSP_IMM (sregs->inst);
	      if (address & LDDM)
		{
		  sregs->trap = TRAP_LMALI;
		  sregs->wpaddress = address;
		  break;
		}
	      mexc = ms->memory_read (address, &op1, &ws);
	      sregs->hold += ws;
	      if (!mexc)
		mexc = ms->memory_read (address + 4, &op2, &ws);
	      sregs->hold += ws;
	      if (mexc)
		{
		  sregs->trap = TRAP_LEXC;
		  sregs->wpaddress = address;
		}
	      else
		{
		  sregs->fsi[(rs1 << 1) + BEH] = op1;
		  sregs->fsi[(rs1 << 1) + 1 - BEH] = op2;
		}
	      break;
#endif
	    case 3:		/* FLWSP: lw frd, offset[7:2](x2) */
	      address = sregs->r[2] + EXTRACT_RVC_LWSP_IMM (sregs->inst);
	      if (address & 0x3)
		{
		  sregs->trap = TRAP_LMALI;
		  sregs->wpaddress = address;
		  break;
		}
	      mexc = ms->memory_read (address, &op1, &ws);
	      sregs->hold += ws;
	      if (mexc)
		{
		  sregs->trap = TRAP_LEXC;
		  sregs->wpaddress = address;
		}
	      else
		{
		  sregs->fsi[(rs1 << 1) + BEH] = op1;
		}
	      break;
	    case 4:
	      if ((sregs->inst >> 12) & 1)
		{
		  if (rs1)
		    {
		      if (rs2)
			{	/* add rd, rd, rs2 */
			  sregs->r[rs1] =
			    (int32) sregs->r[rs1] + (int32) sregs->r[rs2];
			}
		      else
			{	/* jalr x1, rs1, 0 */
#ifdef STAT
			  sregs->nbranch++;
#endif
			  sregs->r[1] = npc;
			  npc = sregs->r[rs1];
			  npc &= ~1;
			  if (ebase.coven)
			    cov_jmp (sregs->pc, npc);
			}
		    }
		  else
		    {		/* EBREAK */
		      if (sis_gdb_break)
			{
			  sregs->trap = WPT_TRAP;
			  sregs->bphit = 1;
			}
		      else
			sregs->trap = TRAP_EBREAK;
		    }
		}
	      else
		{
		  if (rs2)
		    {		/* MV */
		      sregs->r[rs1] = sregs->r[rs2];
		    }
		  else
		    {		/* jalr x0, rs1, 0 */
		      npc = sregs->r[rs1];
		      npc &= ~1;
		      if (ebase.coven)
			cov_jmp (sregs->pc, npc);
		    }
		}
	      break;
	    case 6:		/* SWSP: sw rs2, offset[7:2](x2) */
	      address = sregs->r[2] + EXTRACT_RVC_SWSP_IMM (sregs->inst);
	      if (address & 0x3)
		{
		  sregs->trap = TRAP_SMALI;
		  sregs->wpaddress = address;
		  break;
		}
	      mexc = ms->memory_write (address, &sregs->r[rs2], 2, &ws);
	      sregs->hold += ws;
	      if (mexc)
		{
		  sregs->trap = TRAP_SEXC;
		  sregs->wpaddress = address;
		}
	      break;
#ifdef FPU_D_ENABLED
	    case 5:		/* FSDSP: sw frs2, offset[8:3](x2) */
	      address = sregs->r[2] + EXTRACT_RVC_SDSP_IMM (sregs->inst);
	      if (address & LDDM)
		{
		  sregs->trap = TRAP_SMALI;
		  sregs->wpaddress = address;
		  break;
		}
	      mexc =
		ms->memory_write (address,
				  (uint32 *) & sregs->fsi[(rs2 << 1) + BEH],
				  2, &ws);
	      sregs->hold += ws;
	      mexc |=
		ms->memory_write (address + 4,
				  (uint32 *) & sregs->fsi[(rs2 << 1) + 1 -
							  BEH], 2, &ws);
	      sregs->hold += ws;
	      if (mexc)
		{
		  sregs->trap = TRAP_SEXC;
		  sregs->wpaddress = address;
		}
	      break;
#endif
	    case 7:		/* FSWSP: sw frs2, offset[7:2](x2) */
	      address = sregs->r[2] + EXTRACT_RVC_SWSP_IMM (sregs->inst);
	      if (address & 0x3)
		{
		  sregs->trap = TRAP_SMALI;
		  sregs->wpaddress = address;
		  break;
		}
	      mexc =
		ms->memory_write (address,
				  (uint32 *) & sregs->fsi[(rs2 << 1) + BEH],
				  2, &ws);
	      sregs->hold += ws;
	      if (mexc)
		{
		  sregs->trap = TRAP_SEXC;
		  sregs->wpaddress = address;
		}
	      break;
	    default:
	      sregs->trap = TRAP_ILLEG;
	    }
	  break;
	default:
	  sregs->trap = TRAP_ILLEG;
	}
    }
  else
#else
  if (1)
#endif
    {
      /* Regular instructions  (RV32IA) */
      op = (sregs->inst >> 2) & 0x1f;
      funct3 = (sregs->inst >> 12) & 0x7;
      rd = (sregs->inst >> 7) & 0x1f;
      rs1 = (sregs->inst >> 15) & 0x1f;
      rs2 = (sregs->inst >> 20) & 0x1f;
      npc = sregs->pc + 4;

      op1 = sregs->r[rs1];
      op2 = sregs->r[rs2];

      switch (op)
	{
	case OP_LUI:
	  sop1 = sregs->inst;
	  sregs->r[rd] = ((sop1 >> 12) << 12);
	  break;
	case OP_BRANCH:
#ifdef STAT
	  sregs->nbranch++;
#endif
	  btrue = 0;
	  offset = EXTRACT_SBTYPE_IMM (sregs->inst);
	  sop1 = op1;
	  sop2 = op2;
	  switch (funct3)
	    {
	    case B_BE:
	      if (op1 == op2)
		btrue = 1;
	      break;
	    case B_BNE:
	      if (op1 != op2)
		btrue = 1;
	      break;
	    case B_BGE:
	      if (sop1 >= sop2)
		btrue = 1;
	      break;
	    case B_BGEU:
	      if (op1 >= op2)
		btrue = 1;
	      break;
	    case B_BLT:
	      if (sop1 < sop2)
		btrue = 1;
	      break;
	    case B_BLTU:
	      if (op1 < op2)
		btrue = 1;
	      break;
	    default:
	      sregs->trap = TRAP_ILLEG;
	    }
	  if (btrue)
	    {
	      npc = sregs->pc + offset;
	      if (ebase.coven)
		cov_bt (sregs->pc, npc);
	      if (offset >= 0)
		sregs->icnt += T_BMISS;
	    }
	  else
	    {
	      if (offset < 0)
		sregs->icnt += T_BMISS;
	      if (ebase.coven)
		{
		  cov_bnt (sregs->pc);
		  cov_start (npc);
		}
	    }
	  npc &= ~1;
	  break;
	case OP_JAL:		/* JAL */
#ifdef STAT
	  sregs->nbranch++;
#endif
	  offset = EXTRACT_UJTYPE_IMM (sregs->inst);
	  sregs->r[rd] = npc;
	  npc = sregs->pc + offset;
	  npc &= ~1;
	  if (!npc)
	    sregs->trap = NULL_TRAP;	// halt on null pointer
	  if (ebase.coven)
	    cov_jmp (sregs->pc, npc);
	  break;

	case OP_JALR:		/* JALR */
#ifdef STAT
	  sregs->nbranch++;
#endif
	  offset = EXTRACT_ITYPE_IMM (sregs->inst);
	  sregs->r[rd] = npc;
	  npc = op1 + offset;
	  npc &= ~1;
	  if (!npc)
	    sregs->trap = NULL_TRAP;	// halt on null pointer
	  if (ebase.coven)
	    cov_jmp (sregs->pc, npc);
	  sregs->icnt += T_JALR;
	  break;

	case OP_AUIPC:		/* AUIPC */
	  sop1 = sregs->inst;
	  sop1 = ((sop1 >> 12) << 12);
	  sregs->r[rd] = sregs->pc + sop1;
	  break;
	case OP_IMM:		/* IMM */
	  sop2 = EXTRACT_ITYPE_IMM (sregs->inst);
	  switch (funct3)
	    {
	    case IXOR:
	      sregs->r[rd] = op1 ^ sop2;
	      break;
	    case IOR:
	      sregs->r[rd] = op1 | sop2;
	      break;
	    case IAND:
	      sregs->r[rd] = op1 & sop2;
	      break;
	    case ADD:
	      sop1 = op1;
	      sregs->r[rd] = sop1 + sop2;
	      break;
	    case SLL:
	      sregs->r[rd] = op1 << (rs2);
	      break;
	    case SRL:
	      if ((sregs->inst >> 30) & 1)
		{
		  sop1 = op1;
		  sregs->r[rd] = sop1 >> rs2;	/* SRA */
		}
	      else
		sregs->r[rd] = op1 >> rs2;	/* SRL */
	      break;
	    case SLT:
	      sop1 = op1;
	      if (sop1 < sop2)
		sregs->r[rd] = 1;
	      else
		sregs->r[rd] = 0;
	      break;
	    case SLTU:
	      op2 = sop2;
	      if (op1 < op2)
		sregs->r[rd] = 1;
	      else
		sregs->r[rd] = 0;
	      break;
	    default:
	      sregs->trap = TRAP_ILLEG;
	    }
	  break;
	case OP_REG:		/* REG */
	  switch ((sregs->inst >> 25) & 3)
	    {
	    case 0:
	      switch (funct3)
		{
		case IXOR:
		  sregs->r[rd] = op1 ^ op2;
		  break;
		case IOR:
		  sregs->r[rd] = op1 | op2;
		  break;
		case IAND:
		  sregs->r[rd] = op1 & op2;
		  break;
		case ADD:
		  sop1 = op1;
		  sop2 = op2;
		  if ((sregs->inst >> 30) & 1)
		    sregs->r[rd] = op1 - op2;
		  else
		    sregs->r[rd] = op1 + op2;
		  break;
		case SLL:
		  sregs->r[rd] = op1 << (op2 & 0x1f);
		  break;
		case SRL:
		  if ((sregs->inst >> 30) & 1)
		    {
		      sop1 = op1;
		      sregs->r[rd] = sop1 >> (op2 & 0x1f);	/* SRA */
		    }
		  else
		    sregs->r[rd] = op1 >> (op2 & 0x1f);	/* SRL */
		  break;
		case SLT:
		  sop1 = op1;
		  sop2 = op2;
		  if (sop1 < sop2)
		    sregs->r[rd] = 1;
		  else
		    sregs->r[rd] = 0;
		  break;
		case SLTU:
		  if (op1 < op2)
		    sregs->r[rd] = 1;
		  else
		    sregs->r[rd] = 0;
		  break;
		default:
		  sregs->trap = TRAP_ILLEG;
		}
	      break;
	    case 1:		/* MUL/DIV */
	      switch (funct3)
		{
		case 0:	/* MUL */
		  sop1 = op1;
		  sop2 = op2;
		  sop2 = op1 * op2;
		  sregs->r[rd] = sop2;
		  sregs->icnt = T_MUL;
		  break;
		case 1:	/* MULH */
		  sop64a = (int64) op1 *(int64) op2;
		  sregs->r[rd] = (sop64a >> 32) & 0xffffffff;
		  sregs->icnt = T_MUL;
		  break;
		case 2:	/* MULHSU */
		  sop64a = (int64) op1 *(uint64) op2;
		  sregs->r[rd] = (sop64a >> 32) & 0xffffffff;
		  sregs->icnt = T_MUL;
		  break;
		case 3:	/* MULHU */
		  op64a = (uint64) op1 *(uint64) op2;
		  sregs->r[rd] = (op64a >> 32) & 0xffffffff;
		  sregs->icnt = T_MUL;
		  break;
		case 4:	/* DIV */
		  sop1 = op1;
		  sop2 = op2;
		  result = sop1 / sop2;
		  sregs->r[rd] = result;
		  sregs->icnt = T_DIV;
		  break;
		case 5:	/* DIVU */
		  sregs->r[rd] = op1 / op2;
		  sregs->icnt = T_DIV;
		  break;
		case 6:	/* REM */
		  sop1 = op1;
		  sop2 = op2;
		  sop1 = sop1 % sop2;
		  sregs->r[rd] = sop1;
		  sregs->icnt = T_DIV;
		  break;
		case 7:	/* REMU */
		  sregs->r[rd] = op1 % op2;
		  sregs->icnt = T_DIV;
		  break;
		}
	      break;
	    default:
	      sregs->trap = TRAP_ILLEG;
	    }
	  break;
	case OP_STORE:		/* store instructions */

	  /* skip store if we resume after a write watchpoint */
	  if (sis_gdb_break && ebase.wphit)
	    {
	      ebase.wphit = 0;
	      break;
	    }

#if defined(STAT) || defined(ENABLE_L1CACHE)
	  sregs->nstore++;
#endif
	  offset = EXTRACT_STYPE_IMM (sregs->inst);
	  address = op1 + offset;
	  wdata = &(sregs->r[rs2]);

	  if (ebase.wpwnum)
	    {
	      if ((ebase.wphit = check_wpw (sregs, address, funct3 & 3)))
		{
		  sregs->trap = WPT_TRAP;
		  /* gdb seems to expect that the write goes trough when the
		   * watchpoint is hit, but PC stays on the store instruction */
		  if (!sis_gdb_break)
		    break;
		}
	    }

	  switch (funct3)
	    {
	    case SW:
	      if (address & 0x3)
		{
		  sregs->trap = TRAP_SMALI;
		  sregs->wpaddress = address;
		  break;
		}
	      mexc = ms->memory_write (address, wdata, 2, &ws);
	      sregs->hold += ws;
	      if (mexc)
		{
		  sregs->trap = TRAP_SEXC;
		  sregs->wpaddress = address;
		}
	      break;
	    case SB:
	      mexc = ms->memory_write (address, wdata, 0, &ws);
	      sregs->hold += ws;
	      if (mexc)
		{
		  sregs->trap = TRAP_SEXC;
		  sregs->wpaddress = address;
		}
	      break;
	    case SH:
	      if (address & 0x1)
		{
		  sregs->trap = TRAP_SMALI;
		  sregs->wpaddress = address;
		  break;
		}
	      mexc = ms->memory_write (address, wdata, 1, &ws);
	      sregs->hold += ws;
	      if (mexc)
		{
		  sregs->trap = TRAP_SEXC;
		  sregs->wpaddress = address;
		}
	      break;
	    default:
	      sregs->trap = TRAP_ILLEG;
	    }
#ifdef ENABLE_L1CACHE
	  if (ncpu > 1)
	    {
	      l1data_update (address, sregs->cpu);
	      l1data_snoop (address, sregs->cpu);
	    }
#endif
	  break;
	case OP_FSW:		/* F store instructions */

	  if (sis_gdb_break && ebase.wphit)
	    {
	      ebase.wphit = 0;
	      break;
	    }
#if defined(STAT) || defined(ENABLE_L1CACHE)
	  sregs->nstore++;
#endif
	  offset = EXTRACT_STYPE_IMM (sregs->inst);
	  address = op1 + offset;
	  wdata = (uint32 *) & sregs->fsi[rs2 << 1];

	  if (ebase.wpwnum)
	    {
	      if ((ebase.wphit = check_wpw (sregs, address, funct3 & 3)))
		{
		  sregs->trap = WPT_TRAP;
		  if (!sis_gdb_break)
		    break;
		}
	    }

	  switch (funct3)
	    {
	    case 2:		/* FSW */
	      if (address & 0x3)
		{
		  sregs->trap = TRAP_SMALI;
		  sregs->wpaddress = address;
		  break;
		}
	      mexc = ms->memory_write (address, &wdata[BEH], 2, &ws);
	      sregs->hold += ws;
	      if (mexc)
		{
		  sregs->trap = TRAP_SEXC;
		  sregs->wpaddress = address;
		}
	      break;
	    case 3:		/* FSD */
	      if (address & LDDM)
		{
		  sregs->trap = TRAP_SMALI;
		  sregs->wpaddress = address;
		  break;
		}
	      mexc = ms->memory_write (address, &wdata[BEH], 2, &ws);
	      sregs->hold += ws;
	      mexc |= ms->memory_write (address + 4, &wdata[1 - BEH], 2, &ws);
	      sregs->hold += ws;
	      if (mexc)
		{
		  sregs->trap = TRAP_SEXC;
		  sregs->wpaddress = address;
		}
	      break;
	    default:
	      sregs->trap = TRAP_ILLEG;
	    }
#ifdef ENABLE_L1CACHE
	  if (ncpu > 1)
	    {
	      l1data_update (address, sregs->cpu);
	      l1data_snoop (address, sregs->cpu);
	    }
#endif
	  break;
	case OP_LOAD:		/* load instructions */
#if defined(STAT) || defined(ENABLE_L1CACHE)
	  sregs->nload++;
#endif
	  offset = EXTRACT_ITYPE_IMM (sregs->inst);
	  address = op1 + offset;
	  if (ebase.wprnum)
	    {
	      if ((ebase.wphit = check_wpr (sregs, address, funct3 & 3)))
		{
		  sregs->trap = WPT_TRAP;
		  break;
		}
	    }


	  /* Decode load/store instructions */

	  switch (funct3)
	    {
	    case LW:
	      if (address & 0x3)
		{
		  sregs->trap = TRAP_LMALI;
		  sregs->wpaddress = address;
		  break;
		}
	      mexc = ms->memory_read (address, &op1, &ws);
	      sregs->hold += ws;
	      if (mexc)
		{
		  sregs->trap = TRAP_LEXC;
		  sregs->wpaddress = address;
		}
	      else
		{
		  sregs->r[rd] = op1;
		}
	      break;
	    case LB:
	      if (sregs->inst == 0)
		{
		  sregs->trap = TRAP_ILLEG;
		  break;
		}
	      mexc = ms->memory_read (address & ~3, (uint32 *) & data, &ws);
	      sregs->hold += ws;
	      if (mexc)
		{
		  sregs->trap = TRAP_LEXC;
		  sregs->wpaddress = address;
		  break;
		}
	      data >>= (address & 3) * 8;
	      data = (data << 24) >> 24;
	      sregs->r[rd] = data;
	      break;
	    case LBU:
	      mexc = ms->memory_read (address & ~3, &op1, &ws);
	      sregs->hold += ws;
	      if (mexc)
		{
		  sregs->trap = TRAP_LEXC;
		  sregs->wpaddress = address;
		  break;
		}
	      op1 >>= (address & 3) * 8;
	      sregs->r[rd] = op1 & 0x0ff;
	      break;
	    case LH:
	      if (address & 0x1)
		{
		  sregs->trap = TRAP_LMALI;
		  sregs->wpaddress = address;
		  break;
		}
	      mexc = ms->memory_read (address & ~3, (uint32 *) & data, &ws);
	      sregs->hold += ws;
	      if (mexc)
		{
		  sregs->trap = TRAP_LEXC;
		  sregs->wpaddress = address;
		  break;
		}
	      data >>= (address & 2) * 8;
	      data = (data << 16) >> 16;
	      sregs->r[rd] = data;
	      break;
	    case LHU:
	      if (address & 0x1)
		{
		  sregs->trap = TRAP_LMALI;
		  sregs->wpaddress = address;
		  break;
		}
	      mexc = ms->memory_read (address & ~3, &op1, &ws);
	      sregs->hold += ws;
	      if (mexc)
		{
		  sregs->trap = TRAP_LEXC;
		  sregs->wpaddress = address;
		  break;
		}
	      op1 >>= (address & 2) * 8;
	      op1 &= 0x0ffff;
	      sregs->r[rd] = op1;
	      break;

	    default:
	      sregs->trap = TRAP_ILLEG;
	    }
#ifdef ENABLE_L1CACHE
	  if (ncpu > 1)
	    {
	      l1data_update (address, sregs->cpu);
	    }
#endif
	  break;
	case OP_AMO:		/* atomic instructions */
	  address = op1;
	  funct5 = (sregs->inst >> 27) & 0x1f;
#if defined(STAT) || defined(ENABLE_L1CACHE)
	  sregs->nstore++;
	  sregs->nload++;
#endif
	  sregs->icnt = T_AMO;
	  switch (funct5)
	    {
	    case LRQ:
	      if (address & 0x3)
		{
		  sregs->trap = TRAP_LMALI;
		  sregs->wpaddress = address;
		  break;
		}
	      mexc = ms->memory_read (address, &op1, &ws);
	      sregs->hold += ws;
	      if (mexc)
		{
		  sregs->trap = TRAP_LEXC;
		  sregs->wpaddress = address;
		}
	      else
		{
		  sregs->r[rd] = op1;
		  sregs->lrqa = address;
		  sregs->lrq = 1;
#ifdef DEBUG
		  if (sis_verbose)
		    printf (" %8" PRIu64 " cpu %d: LRQ at address 0x%08x\n",
			    sregs->simtime, sregs->cpu, address);
#endif
		}
	      break;
	    case SCQ:
	      if (address & 0x3)
		{
		  sregs->trap = TRAP_LMALI;
		  sregs->wpaddress = address;
		  break;
		}
	      if (sregs->lrq && (sregs->lrqa == address))
		{
		  mexc = ms->memory_write (address, &op2, 2, &ws);
		  sregs->hold += ws;
		  if (mexc)
		    {
		      sregs->trap = TRAP_SEXC;
		      sregs->wpaddress = address;
		    }
		  else
		    {
		      sregs->r[rd] = 0;
#ifdef DEBUG
		      if (sis_verbose)
			printf (" %8" PRIu64
				" cpu %d: SCQ at address 0x%08x\n",
				sregs->simtime, sregs->cpu, address);
#endif
		    }
		}
	      else
		{
		  sregs->r[rd] = 1;
#ifdef DEBUG
		  if (sis_verbose)
		    printf (" %8" PRIu64
			    " cpu %d: failed SCQ at address 0x%08x\n",
			    sregs->simtime, sregs->cpu, address);
#endif
		}
	      sregs->lrq = 0;
	      break;
	    default:		/* AMOXXX */
	      if (address & 0x3)
		{
		  sregs->trap = TRAP_LMALI;
		  sregs->wpaddress = address;
		  break;
		}
	      mexc = ms->memory_read (address, (uint32 *) & data, &ws);
	      sregs->hold += ws;
	      if (mexc)
		{
		  sregs->trap = TRAP_LEXC;
		  sregs->wpaddress = address;
		  break;
		}
	      switch (funct5)
		{
		case AMOSWAP:
		  break;
		case AMOADD:
		  op2 = (int32) data + (int32) op2;
		  break;
		case AMOXOR:
		  op2 = data ^ op2;
		  break;
		case AMOOR:
		  op2 = data | op2;
		  break;
		case AMOAND:
		  op2 = data & op2;
		  break;
		case AMOMIN:
		  if ((int32) data < (int32) op2)
		    op2 = data;
		  break;
		case AMOMAX:
		  if ((int32) data > (int32) op2)
		    op2 = data;
		  break;
		case AMOMINU:
		  if ((uint32) data < (uint32) op2)
		    op2 = data;
		  break;
		case AMOMAXU:
		  if ((uint32) data > (uint32) op2)
		    op2 = data;
		  break;
		default:
		  sregs->trap = TRAP_ILLEG;
		}
	      if (sregs->trap)
		break;
	      mexc = ms->memory_write (address, &op2, 2, &ws);
	      sregs->hold += ws;
	      if (mexc)
		{
		  sregs->trap = TRAP_SEXC;
		  sregs->wpaddress = address;
		  break;
		}
	      sregs->r[rd] = data;
	    }
	  break;
	case OP_SYS:
	  address = sregs->inst >> 20;
	  switch (funct3)
	    {
	    case 0:		/* ecall, xret */
	      switch (rs2)
		{
		case 0:	/* ecall */
		  sregs->trap = ERROR_TRAP;
		  break;
		case 1:	/* ebreak */
		  if (sis_gdb_break)
		    {
		      sregs->trap = WPT_TRAP;
		      sregs->bphit = 1;
		    }
		  else
		    sregs->trap = TRAP_EBREAK;
		  break;
		case 2:	/* xret */
		  npc = sregs->epc;
		  sregs->mode = sregs->mpp;
		  sregs->mstatus |= (sregs->mstatus >> 4) & MSTATUS_MIE;
		  sregs->mstatus |= MSTATUS_MPIE;	// set mstatus.mpie
		  rv32_check_lirq (sregs->cpu);
		  if (ebase.coven)
		    cov_jmp (sregs->pc, npc);
		  break;
		case 5:	/* wfi */
		  pwd_enter (sregs);
		  if (sync_rt)
		    rt_sync ();
		  break;
		default:
		  sregs->trap = TRAP_ILLEG;
		}
	      break;
	    case CSRRW:
	      op2 = get_csr (address, sregs);
	      if (set_csr (address, sregs, op1))
		sregs->trap = TRAP_ILLEG;
	      else
		sregs->r[rd] = op2;
	      break;
	    case CSRRS:
	      op2 = get_csr (address, sregs);
	      if ((rs1) && set_csr (address, sregs, op1 | op2))
		sregs->trap = TRAP_ILLEG;
	      if (!sregs->trap)
		sregs->r[rd] = op2;
	      break;
	    case CSRRC:
	      op2 = get_csr (address, sregs);
	      if ((rs1) && set_csr (address, sregs, ~op1 & op2))
		sregs->trap = TRAP_ILLEG;
	      if (!sregs->trap)
		sregs->r[rd] = op2;
	      break;
	    case CSRRWI:
	      op2 = get_csr (address, sregs);
	      op1 = (sregs->inst >> 15) & 0x1f;
	      if (set_csr (address, sregs, op1))
		sregs->trap = TRAP_ILLEG;
	      else
		sregs->r[rd] = op2;
	      break;
	    case CSRRSI:
	      op2 = get_csr (address, sregs);
	      op1 = (sregs->inst >> 15) & 0x1f;
	      if ((rs1) && set_csr (address, sregs, op1 | op2))
		sregs->trap = TRAP_ILLEG;
	      if (!sregs->trap)
		sregs->r[rd] = op2;
	      break;
	    case CSRRCI:
	      op2 = get_csr (address, sregs);
	      op1 = (sregs->inst >> 15) & 0x1f;
	      if ((rs1) && set_csr (address, sregs, ~op1 & op2))
		sregs->trap = TRAP_ILLEG;
	      if (!sregs->trap)
		sregs->r[rd] = op2;
	      break;
	    default:
	      sregs->trap = TRAP_ILLEG;
	    }
	  break;
	case OP_FLOAD:		/* float load instructions */
#if defined(STAT) || defined(ENABLE_L1CACHE)
	  sregs->nload++;
#endif
	  offset = EXTRACT_ITYPE_IMM (sregs->inst);
	  address = op1 + offset;
	  if (ebase.wprnum)
	    {
	      if ((ebase.wphit = check_wpr (sregs, address, funct3 & 3)))
		{
		  sregs->trap = WPT_TRAP;
		  break;
		}
	    }


	  /* Decode load/store instructions */

	  switch (funct3)
	    {
	    case LW:
	      if (address & 0x3)
		{
		  sregs->trap = TRAP_LMALI;
		  sregs->wpaddress = address;
		  break;
		}
	      mexc = ms->memory_read (address, &op1, &ws);
	      sregs->hold += ws;
	      if (mexc)
		{
		  sregs->trap = TRAP_LEXC;
		  sregs->wpaddress = address;
		}
	      else
		{
		  sregs->fsi[(rd << 1) + BEH] = op1;
		  sregs->fsi[(rd << 1) + 1 - BEH] = -1;
		}
	      break;
	    case LD:
	      if (address & LDDM)
		{
		  sregs->trap = TRAP_LMALI;
		  sregs->wpaddress = address;
		  break;
		}
	      mexc = ms->memory_read (address, &op1, &ws);
	      sregs->hold += ws;
	      if (!mexc)
		mexc = ms->memory_read (address + 4, &op2, &ws);
	      sregs->hold += ws;
	      if (mexc)
		{
		  sregs->trap = TRAP_LEXC;
		  sregs->wpaddress = address;
		}
	      else
		{
		  sregs->fsi[(rd << 1) + BEH] = op1;
		  sregs->fsi[(rd << 1) + 1 - BEH] = op2;
		}
	      break;
	    default:
	      sregs->trap = TRAP_ILLEG;
	    }
#ifdef ENABLE_L1CACHE
	  if (ncpu > 1)
	    {
	      l1data_update (address, sregs->cpu);
	    }
#endif
	  break;
#ifdef FPU_ENABLED
	case OP_FPU:
	  sregs->finst++;
	  clear_accex ();
	  funct2 = (sregs->inst >> 25) & 3;
	  funct5 = (sregs->inst >> 27);
	  switch (funct2)
	    {
	    case 0:		/* single-precision ops */
	      frs1 = (rs1 << 1) + BEH;
	      frs2 = (rs2 << 1) + BEH;
	      frd = (rd << 1) + BEH;
	      switch (funct5)
		{
		case 0:	/* FADDS */
		  sregs->fs[frd] = sregs->fs[frs1] + sregs->fs[frs2];
		  sregs->fsi[frd ^ 1] = -1;
		  sregs->fhold += T_FADDs;
		  break;
		case 1:	/* FSUBS */
		  sregs->fs[frd] = sregs->fs[frs1] - sregs->fs[frs2];
		  sregs->fsi[frd ^ 1] = -1;
		  sregs->fhold += T_FSUBs;
		  break;
		case 2:	/* FMULS */
		  sregs->fs[frd] = sregs->fs[frs1] * sregs->fs[frs2];
		  sregs->fsi[frd ^ 1] = -1;
		  sregs->fhold += T_FMULs;
		  break;
		case 3:	/* FDIVS */
		  sregs->fs[frd] = sregs->fs[frs1] / sregs->fs[frs2];
		  sregs->fsi[frd ^ 1] = -1;
		  sregs->fhold += T_FDIVs;
		  break;
		case 4:	/* FSGX */
		  switch (funct3)
		    {
		    case 0:	/* FSGNJ */
		      sregs->fsi[frd] = (sregs->fsi[frs1] & 0x7fffffff) |
			(sregs->fsi[frs2] & 0x80000000);
		      sregs->fsi[frd ^ 1] = -1;
		      break;
		    case 1:	/* FSGNJN */
		      sregs->fsi[frd] = (sregs->fsi[frs1] & 0x7fffffff) |
			(~sregs->fsi[frs2] & 0x80000000);
		      sregs->fsi[frd ^ 1] = -1;
		      break;
		    case 2:	/* FSGNJX */
		      sregs->fsi[frd] =
			sregs->fsi[frs1] ^ (sregs->fsi[frs2] & 0x80000000);
		      sregs->fsi[frd ^ 1] = -1;
		      break;
		    default:
		      sregs->trap = TRAP_ILLEG;
		    }
		  break;
		case 5:	/* FMINS / FMAXS */
		  if ((sregs->fs[frs1] <
		       sregs->fs[frs2]) ^ ((sregs->inst >> 12) & 1))
		    sregs->fs[frd] = sregs->fs[frs1];
		  else
		    sregs->fs[frd] = sregs->fs[frs2];
		  sregs->fsi[frd ^ 1] = -1;
		  sregs->fhold += T_FSUBs;
		  break;
#ifdef FPU_D_ENABLED
		case 0x08:	/* FCVTSD / FCVTDS */
		  switch (funct2)
		    {
		    case 0:	/* FCVTSD */
		      sregs->fs[frd] = (float32) sregs->fd[rs1];
		      sregs->fsi[frd ^ 1] = -1;
		      break;
		    default:
		      sregs->trap = TRAP_ILLEG;
		    }
		  break;
#endif
		case 0x0b:	/* FSQRTS */
		  sregs->fs[frd] = sqrtf (sregs->fs[frs1]);
		  sregs->fsi[frd ^ 1] = -1;
		  sregs->fhold += T_FSQRTs;
		  break;
		case 0x14:	/* FCMPS */
		  switch (funct3)
		    {
		    case 0:	/* FLES */
		      if ((sregs->fs[frs1] == sregs->fs[frs2]) ||
			  (sregs->fs[frs1] < sregs->fs[frs2]))
			sregs->r[rd] = 1;
		      else
			sregs->r[rd] = 0;
		      break;
		    case 1:	/* FLTS */
		      if (sregs->fs[frs1] < sregs->fs[frs2])
			sregs->r[rd] = 1;
		      else
			sregs->r[rd] = 0;
		      break;
		    case 2:	/* FEQS */
		      if (sregs->fs[frs1] == sregs->fs[frs2])
			sregs->r[rd] = 1;
		      else
			sregs->r[rd] = 0;
		      break;
		    default:
		      sregs->trap = TRAP_ILLEG;
		    }
		  break;
		case 0x18:	/* FCVTW */
		  switch (rs2)
		    {
		    case 0:	/* FCVTWS */
		      sregs->r[rd] = (int32) sregs->fs[frs1];
		      break;
		    case 1:	/* FCVTWUS */
		      sregs->r[rd] = (uint32) sregs->fs[frs1];
		      break;
		    default:
		      sregs->trap = TRAP_ILLEG;
		    }
		  break;
		case 0x1a:	/* FCVT */
		  switch (rs2)
		    {
		    case 0:	/* FCVTSW */
		      sop1 = sregs->r[rs1];
		      sregs->fs[frd] = (float32) sop1;
		      sregs->fsi[frd ^ 1] = -1;
		      break;
		    case 1:	/* FCVTSWU */
		      op1 = sregs->r[rs1];
		      sregs->fs[frd] = (float32) op1;
		      sregs->fsi[frd ^ 1] = -1;
		      break;
		    default:
		      sregs->trap = TRAP_ILLEG;
		    }
		  break;
		case 0x1c:
		  switch (funct3)
		    {
		    case 0:	/* FMVXS */
		      sregs->r[rd] = sregs->fsi[frs1];
		      break;
		    case 1:	/* FCLASS */
		      op1 = fpclassify (sregs->fs[frs1]);
		      switch (op1)
			{
			case FP_NAN:
			  op1 = (1 << 8);	// FIX ME, add quiet NaN
			  break;
			case FP_INFINITE:
			  if (sregs->fsi[frs1] & 0x80000000)
			    op1 = (1 << 0);
			  else
			    op1 = (1 << 7);
			  break;
			case FP_ZERO:
			  if (sregs->fsi[frs1] & 0x80000000)
			    op1 = (1 << 3);
			  else
			    op1 = (1 << 4);
			  break;
			case FP_SUBNORMAL:
			  if (sregs->fsi[frs1] & 0x80000000)
			    op1 = (1 << 2);
			  else
			    op1 = (1 << 5);
			  break;
			case FP_NORMAL:
			  if (sregs->fsi[frs1] & 0x80000000)
			    op1 = (1 << 1);
			  else
			    op1 = (1 << 6);
			  break;
			}
		      sregs->r[rd] = op1;
		      break;
		    default:
		      sregs->trap = TRAP_ILLEG;
		    }
		  break;
		case 0x1e:	/* FMVSX */
		  sregs->fsi[frd] = sregs->r[rs1];
		  sregs->fsi[frd ^ 1] = -1;
		  break;
		default:
		  sregs->trap = TRAP_ILLEG;
		}
	      break;
#ifdef FPU_D_ENABLED
	    case 1:		/* double-precision ops */
	      switch (funct5)
		{
		case 0:
		  sregs->fd[rd] = sregs->fd[rs1] + sregs->fd[rs2];
		  sregs->fhold += T_FADDd;
		  break;
		case 1:
		  sregs->fd[rd] = sregs->fd[rs1] - sregs->fd[rs2];
		  sregs->fhold += T_FSUBd;
		  break;
		case 2:
		  sregs->fd[rd] = sregs->fd[rs1] * sregs->fd[rs2];
		  sregs->fhold += T_FMULd;
		  break;
		case 3:
		  sregs->fd[rd] = sregs->fd[rs1] / sregs->fd[rs2];
		  sregs->fhold += T_FDIVd;
		  break;
		case 4:	/* FSGX */
		  frd = (rd << 1);
		  frs1 = (rs1 << 1);
		  frs2 = (rs2 << 1);
		  switch (funct3)
		    {
		    case 0:	/* FSGNJ */
		      sregs->fsi[frd + BEH] = sregs->fsi[frs1 + BEH];
		      sregs->fsi[frd + 1 - BEH] =
			(sregs->fsi[frs1 + 1 -
				    BEH] & 0x7fffffff) | (sregs->fsi[frs2 +
								     1 -
								     BEH] &
							  0x80000000);
		      break;
		    case 1:	/* FSGNJN */
		      sregs->fsi[frd + BEH] = sregs->fsi[frs1 + BEH];
		      sregs->fsi[frd + 1 - BEH] =
			(sregs->fsi[frs1 + 1 -
				    BEH] & 0x7fffffff) | (~sregs->fsi[frs2 +
								      1 -
								      BEH] &
							  0x80000000);
		      break;
		    case 2:	/* FSGNJX */
		      sregs->fsi[frd + BEH] = sregs->fsi[frs1 + BEH];
		      sregs->fsi[frd + 1 - BEH] =
			sregs->fsi[frs1 +
				   1 - BEH] ^ (sregs->fsi[frs2 + 1 -
							  BEH] & 0x80000000);
		      break;
		    default:
		      sregs->trap = TRAP_ILLEG;
		    }
		  break;
		case 5:	/* FMIND / FMAXD */
		  if ((sregs->fd[rs1] <
		       sregs->fd[rs2]) ^ ((sregs->inst >> 12) & 1))
		    sregs->fd[rd] = sregs->fd[rs1];
		  else
		    sregs->fd[rd] = sregs->fd[rs2];
		  sregs->fhold += T_FSUBd;
		  break;
#ifdef FPU_D_ENABLED
		case 0x08:	/* FCVTSD / FCVTDS */
		  switch (funct2)
		    {
		    case 1:	/* FCVTDS */
		      sregs->fd[rd] = (float64) sregs->fs[(rs1 << 1) + BEH];
		      break;
		    default:
		      sregs->trap = TRAP_ILLEG;
		    }
		  break;
#endif
		case 0x0b:	/* FSQRTD */
		  sregs->fd[rd] = sqrt (sregs->fd[rs1]);
		  sregs->fhold += T_FSQRTd;
		  break;
		case 0x14:	/* FCMPD */
		  switch (funct3)
		    {
		    case 0:	/* FLED */
		      if ((sregs->fd[rs1] == sregs->fd[rs2]) ||
			  (sregs->fd[rs1] < sregs->fd[rs2]))
			sregs->r[rd] = 1;
		      else
			sregs->r[rd] = 0;
		      break;
		    case 1:	/* FLTD */
		      if (sregs->fd[rs1] < sregs->fd[rs2])
			sregs->r[rd] = 1;
		      else
			sregs->r[rd] = 0;
		      break;
		    case 2:	/* FEQD */
		      if (sregs->fd[rs1] == sregs->fd[rs2])
			sregs->r[rd] = 1;
		      else
			sregs->r[rd] = 0;
		      break;
		    default:
		      sregs->trap = TRAP_ILLEG;
		    }
		  break;
		case 0x18:	/* FCVTW */
		  switch (rs2)
		    {
		    case 0:	/* FCVTWD */
		      sregs->r[rd] = (int32) sregs->fd[rs1];
		      break;
		    case 1:	/* FCVTWUD */
		      sregs->r[rd] = (uint32) sregs->fd[rs1];
		      break;
		    default:
		      sregs->trap = TRAP_ILLEG;
		    }
		  break;
		case 0x1a:	/* FCVTD */
		  switch (rs2)
		    {
		    case 0:	/* FCVTDW */
		      sop1 = sregs->r[rs1];
		      sregs->fd[rd] = (float64) sop1;
		      break;
		    case 1:	/* FCVTDWU */
		      op1 = sregs->r[rs1];
		      sregs->fd[rd] = (float64) op1;
		      break;
		    default:
		      sregs->trap = TRAP_ILLEG;
		    }
		  break;
		case 0x1c:	/* FCLASSD */
		  switch (funct3)
		    {
		    case 1:
		      op1 = fpclassify (sregs->fd[rs1]);
		      switch (op1)
			{
			case FP_NAN:
			  op1 = (1 << 8);	// FIX ME, add quiet NaN
			  break;
			case FP_INFINITE:
			  if (sregs->fsi[(rs1 << 1) + 1 - BEH] & 0x80000000)
			    op1 = (1 << 0);
			  else
			    op1 = (1 << 7);
			  break;
			case FP_ZERO:
			  if (sregs->fsi[(rs1 << 1) + 1 - BEH] & 0x80000000)
			    op1 = (1 << 3);
			  else
			    op1 = (1 << 4);
			  break;
			case FP_SUBNORMAL:
			  if (sregs->fsi[(rs1 << 1) + 1 - BEH] & 0x80000000)
			    op1 = (1 << 2);
			  else
			    op1 = (1 << 5);
			  break;
			case FP_NORMAL:
			  if (sregs->fsi[(rs1 << 1) + 1 - BEH] & 0x80000000)
			    op1 = (1 << 1);
			  else
			    op1 = (1 << 6);
			  break;
			}
		      sregs->r[rd] = op1;
		      break;
		    default:
		      sregs->trap = TRAP_ILLEG;
		    }
		  break;
		default:
		  sregs->trap = TRAP_ILLEG;
		}
	      break;
#endif
	    default:
	      sregs->trap = TRAP_ILLEG;
	    }
	  sregs->fsr |= riscv_get_accex ();
	  clear_accex ();
	  break;
	case OP_FMADD:
	  sregs->finst++;
	  clear_accex ();
	  switch ((sregs->inst >> 25) & 3)
	    {
	    case 0:		/* OP_FMADDS */
	      sregs->fs[(rd << 1) + BEH] =
		(sregs->fs[(rs1 << 1) + BEH] * sregs->fs[(rs2 << 1) + BEH]) +
		sregs->fs[((sregs->inst >> 27) << 1) + BEH];
	      sregs->fsi[(rd << 1) + 1 - BEH] = -1;
	      sregs->fhold += T_FADDs + T_FMULs;
	      break;
#ifdef FPU_D_ENABLED
	    case 1:		/* OP_FMADDD */
	      sregs->fd[rd] = (sregs->fd[rs1] * sregs->fd[rs2])
		+ sregs->fd[sregs->inst >> 27];
	      sregs->fhold += T_FADDd + T_FMULd;
	      break;
#endif
	    default:
	      sregs->trap = TRAP_ILLEG;
	    }
	  sregs->fsr |= riscv_get_accex ();
	  clear_accex ();
	  break;
	case OP_FMSUB:
	  sregs->finst++;
	  clear_accex ();
	  switch ((sregs->inst >> 25) & 3)
	    {
	    case 0:		/* OP_FMSUBS */
	      sregs->fs[(rd << 1) + BEH] =
		(sregs->fs[(rs1 << 1) + BEH] * sregs->fs[(rs2 << 1) + BEH]) -
		sregs->fs[((sregs->inst >> 27) << 1) + BEH];
	      sregs->fsi[(rd << 1) + 1 - BEH] = -1;
	      sregs->fhold += T_FMULs + T_FSUBs;
	      break;
#ifdef FPU_D_ENABLED
	    case 1:		/* OP_FMSUBD */
	      sregs->fd[rd] = (sregs->fd[rs1] * sregs->fd[rs2])
		- sregs->fd[sregs->inst >> 27];
	      sregs->fhold += T_FMULd + T_FSUBd;
	      break;
#endif
	    default:
	      sregs->trap = TRAP_ILLEG;
	    }
	  sregs->fsr |= riscv_get_accex ();
	  clear_accex ();
	  break;
	case OP_FNMSUB:
	  sregs->finst++;
	  clear_accex ();
	  switch ((sregs->inst >> 25) & 3)
	    {
	    case 0:		/* OP_FNMSUBS */
	      sregs->fs[(rd << 1) + BEH] =
		(-sregs->fs[(rs1 << 1) + BEH] * sregs->fs[(rs2 << 1) + BEH]) +
		sregs->fs[((sregs->inst >> 27) << 1) + BEH];
	      sregs->fsi[(rd << 1) + 1 - BEH] = -1;
	      sregs->fhold += T_FADDs + T_FSUBs;
	      break;
#ifdef FPU_D_ENABLED
	    case 1:		/* OP_FNMSUBD */
	      sregs->fd[rd] = (-sregs->fd[rs1] * sregs->fd[rs2])
		+ sregs->fd[sregs->inst >> 27];
	      sregs->fhold += T_FADDd + T_FSUBd;
	      break;
#endif
	    default:
	      sregs->trap = TRAP_ILLEG;
	    }
	  sregs->fsr |= riscv_get_accex ();
	  clear_accex ();
	  break;
	case OP_FNMADD:
	  sregs->finst++;
	  clear_accex ();
	  switch ((sregs->inst >> 25) & 3)
	    {
	    case 0:		/* OP_FNMADDS */
	      sregs->fs[(rd << 1) + BEH] =
		(-sregs->fs[(rs1 << 1) + BEH] * sregs->fs[(rs2 << 1) + BEH]) -
		sregs->fs[((sregs->inst >> 27) << 1) + BEH];
	      sregs->fsi[(rd << 1) + 1 - BEH] = -1;
	      sregs->fhold += T_FADDs + T_FMULs;
	      break;
#ifdef FPU_D_ENABLED
	    case 1:		/* OP_FNMADDD */
	      sregs->fd[rd] = (-sregs->fd[rs1] * sregs->fd[rs2])
		- sregs->fd[sregs->inst >> 27];
	      sregs->fhold += T_FADDs + T_FMULs;
	      break;
#endif
	    default:
	      sregs->trap = TRAP_ILLEG;
	    }
	  sregs->fsr |= riscv_get_accex ();
	  clear_accex ();
	  break;
#endif
	case OP_FENCE:
	  sregs->icnt = TRAP_C;
	  break;
	default:
	  sregs->trap = TRAP_ILLEG;
	  break;
	}
    }

  sregs->r[0] = 0;
  if (!sregs->trap)
    {
      sregs->pc = npc;
    }
  return 0;
}

static int
riscv_execute_trap (sregs)
     struct pstate *sregs;
{
  if (sis_verbose > 1)
    printf (" %8" PRIu64 " cpu %d trap :  %08X\n",
	    sregs->simtime, sregs->cpu, sregs->trap);
  if (sregs->trap >= 256)
    {
      switch (sregs->trap)
	{
	case 256:
	  sregs->pc = 0;
	  sregs->trap = 0;
	  break;
	case ERROR_TRAP:
	  return (ERROR_MODE);
	case WPT_TRAP:
	  return (WPT_HIT);
	case NULL_TRAP:
	  return (NULL_HIT);

	}
    }
  else
    {

      if (ebase.coven)
	cov_jmp (sregs->pc, sregs->mtvec);
      sregs->epc = sregs->pc;
      sregs->mpp = sregs->mode;
      sregs->mode = 1;
      sregs->pc = sregs->mtvec;
      sregs->mcause = sregs->trap;
      sregs->lrq = 0;
      switch (sregs->trap)
	{
	case TRAP_IEXC:
	  sregs->err_mode = 1;
	case TRAP_EBREAK:
	  sregs->mtval = sregs->epc;
	  break;
	case TRAP_ILLEG:
	  sregs->mtval = sregs->inst;
	  if ((sregs->inst & 0x0ffff) && (sregs->inst != 0xc0001073))
	    /* Not a RTEMS UNIMP test - stop execution ... */
	    sregs->err_mode = 1;
	  break;
	case TRAP_LEXC:
	case TRAP_SEXC:
	case TRAP_LMALI:
	case TRAP_SMALI:
	  sregs->mtval = sregs->wpaddress;
	  sregs->err_mode = 1;
	  break;
	}

      if (((sregs->trap >= 16) && (sregs->trap < 32))
	  || ((sregs->trap == 0x23) || (sregs->trap == 0x27)
	      || (sregs->trap == 0x2b)))
	{
	  sregs->mcause &= 0x1f;	// filter trap cause
	  sregs->mcause |= 0x80000000;	// indicate async interrupt
	  if ((sregs->trap > 16) && (sregs->trap < 32))
	    sregs->intack (sregs->trap - 16, sregs->cpu);
	  else
	    ext_irl[sregs->cpu] = 0;
	}
      if (sregs->trap == 0x23)
	sregs->mip &= ~MIP_MSIP;
      if (sregs->trap == 0x27)
	sregs->mip &= ~MIP_MTIP;
      if (sregs->trap == 0x2b)
	sregs->mip &= ~MIP_MEIP;

      // save mstatus.mie in mstatus.mpie
      sregs->mstatus |= (sregs->mstatus << 4) & MSTATUS_MPIE;
      sregs->mstatus &= ~MSTATUS_MIE;	// clear mstatus.mie
      /* single vector trapping! */
      /*
         if ( 0 != (1 & (sregs->asr17 >> 13)) ) {
         sregs->mtvec = (sregs->mtvec & 0xfffff000) | (sregs->trap << 4);
         }
       */

      /* Increase simulator time and add some jitter */
      sregs->icnt = TRAP_C + (sregs->ninst ^ sregs->simtime) & 0x7;
      sregs->trap = 0;

      if (sregs->err_mode)
	return (ERROR_MODE);
    }


  return 0;

}

static int
riscv_check_interrupts (sregs)
     struct pstate *sregs;
{
  if ((ext_irl[sregs->cpu]) &&
      ((sregs->mstatus & MSTATUS_MIE) && (sregs->mie & MIE_MEIE)))
    {
      if (sregs->pwd_mode)
	{
	  sregs->pwdtime += sregs->simtime - sregs->pwdstart;
	  sregs->pwd_mode = 0;
	}
      if (sregs->trap == 0)
	{
	  return ext_irl[sregs->cpu];
	}
    }
  return 0;
}

static void
riscv_set_regi (sregs, reg, rval)
     struct pstate *sregs;
     int32 reg;
     uint32 rval;
{

  if ((reg >= 0) && (reg < 32))
    {
      sregs->r[reg] = rval;
    }
  else if (reg == 32)
    {
      sregs->pc = rval;
      last_load_addr = rval;
    }
  else if ((reg >= 33) && (reg < 65))
    {
      sregs->fsi[reg - 33] = rval;
    }
  else if ((reg >= 65) && (reg < 4161))
    {
      set_csr (reg - 65, sregs, rval);
    }
}

static void
riscv_get_regi (struct pstate *sregs, int32 reg, char *buf, int length)
{
  uint32 rval = 0;

  if ((reg >= 0) && (reg < 32))
    {
      rval = sregs->r[reg];
    }
  else if (reg == 32)
    {
      rval = sregs->pc;
    }
  else if ((reg >= 33) && (reg < 65))
    {
      if (length == 8)
	{
	  rval = sregs->fsi[((reg - 33) << 1) + 1 - BEH];
	  buf[7] = (rval >> 24) & 0x0ff;
	  buf[6] = (rval >> 16) & 0x0ff;
	  buf[5] = (rval >> 8) & 0x0ff;
	  buf[4] = rval & 0x0ff;
	}
      rval = sregs->fsi[((reg - 33) << 1) + BEH];
    }
  else if ((reg >= 65) && (reg < 4161))
    {
      get_csr (reg - 65, sregs);
    }
  buf[3] = (rval >> 24) & 0x0ff;
  buf[2] = (rval >> 16) & 0x0ff;
  buf[1] = (rval >> 8) & 0x0ff;
  buf[0] = rval & 0x0ff;
}

static int
riscv_gdb_get_reg (char *buf)
{
  int i;

  for (i = 0; i < 65; i++)
    riscv_get_regi (&sregs[cpu], i, &buf[i * 4], 4);

  return (65 * 4);
}

static void
riscv_set_rega (struct pstate *sregs, char *reg, uint32 rval)
{
  int32 err = 0;

  if (strcmp (reg, "psr") == 0)
    sregs->psr = (rval = (rval & 0x00f03fff));
  else if (strcmp (reg, "mtvec") == 0)
    sregs->mtvec = (rval = (rval & 0xfffffff0));
  else if (strcmp (reg, "mstatus") == 0)
    sregs->mstatus = (rval = (rval & 0x0ff));
  else if (strcmp (reg, "pc") == 0)
    sregs->pc = rval;
  else if (strcmp (reg, "fsr") == 0)
    {
      sregs->fsr = rval;
      riscv_set_fsr (rval);
    }
  else if (strcmp (reg, "g0") == 0)
    err = 2;
  else if (strcmp (reg, "x1") == 0)
    sregs->r[1] = rval;
  else if (strcmp (reg, "x2") == 0)
    sregs->r[2] = rval;
  else if (strcmp (reg, "x3") == 0)
    sregs->r[3] = rval;
  else if (strcmp (reg, "x4") == 0)
    sregs->r[4] = rval;
  else if (strcmp (reg, "x5") == 0)
    sregs->r[5] = rval;
  else if (strcmp (reg, "x6") == 0)
    sregs->r[6] = rval;
  else if (strcmp (reg, "x7") == 0)
    sregs->r[7] = rval;
  else
    err = 1;
  switch (err)
    {
    case 0:
      printf ("%s = %d (0x%08x)\n", reg, rval, rval);
      break;
    case 1:
      printf ("no such regiser: %s\n", reg);
      break;
    case 2:
      printf ("cannot set x0\n");
      break;
    default:
      break;
    }

}

static void
riscv_set_register (struct pstate *sregs, char *reg, uint32 rval, uint32 addr)
{
  if (reg == NULL)
    riscv_set_regi (sregs, addr, rval);
  else
    riscv_set_rega (sregs, reg, rval);
}

static void
riscv_display_registers (struct pstate *sregs)
{

  int i;

  printf ("\n        0 - 7        8 - 15        16 - 23       24 - 31\n");
  printf (" z0:  %08X  s0: %08X  a6: %08X  s8: %08X\n",
	  sregs->r[0], sregs->r[8], sregs->r[16], sregs->r[24]);
  printf (" ra:  %08X  s1: %08X  a7: %08X  s9: %08X\n",
	  sregs->r[1], sregs->r[9], sregs->r[17], sregs->r[25]);
  printf (" sp:  %08X  a0: %08X  s2: %08X s10: %08X\n",
	  sregs->r[2], sregs->r[10], sregs->r[18], sregs->r[26]);
  printf (" gp:  %08X  a1: %08X  s3: %08X s11: %08X\n",
	  sregs->r[3], sregs->r[11], sregs->r[19], sregs->r[27]);
  printf (" tp:  %08X  a2: %08X  s4: %08X  t3: %08X\n",
	  sregs->r[4], sregs->r[12], sregs->r[20], sregs->r[28]);
  printf (" t0:  %08X  a3: %08X  s5: %08X  t4: %08X\n",
	  sregs->r[5], sregs->r[13], sregs->r[21], sregs->r[29]);
  printf (" t1:  %08X  a4: %08X  s6: %08X  t5: %08X\n",
	  sregs->r[6], sregs->r[14], sregs->r[22], sregs->r[30]);
  printf (" t2:  %08X  a5: %08X  s7: %08X  t6: %08X\n",
	  sregs->r[7], sregs->r[15], sregs->r[23], sregs->r[31]);
}



static void
riscv_display_ctrl (struct pstate *sregs)
{
  uint32 i;

  printf ("\n mtvec: %08X  mcause: %08X  mepc: %08X  mtval: %08X  \
mstatus: %08X\n", get_csr (CSR_MTVEC, sregs), get_csr (CSR_MCAUSE, sregs), get_csr (CSR_MEPC, sregs), sregs->mtval, get_csr (CSR_MSTATUS, sregs));
  ms->sis_memory_read (sregs->pc, (char *) &i, 4);
  printf ("\n pc: %08X = %08X    ", sregs->pc, i);
  print_insn_sis (sregs->pc);
  if (sregs->err_mode)
    printf ("\n CPU in error mode");
  else if (sregs->pwd_mode)
    printf ("\n IU in power-down mode");
  printf ("\n\n");
}

static void
riscv_display_special (struct pstate *sregs)
{
  uint32 i;

  printf ("\n 0x001  fcsr    :  %08X\n", get_csr (CSR_FCSR, sregs));
  printf (" 0x300  mstatus :  %08X\n", get_csr (CSR_MSTATUS, sregs));
  printf (" 0x301  misa    :  %08X\n", get_csr (CSR_MISA, sregs));
  printf (" 0x304  mie     :  %08X\n", get_csr (CSR_MIE, sregs));
  printf (" 0x305  mtvec   :  %08X\n", get_csr (CSR_MTVEC, sregs));
  printf (" 0x341  mepc    :  %08X\n", get_csr (CSR_MEPC, sregs));
  printf (" 0x342  mcause  :  %08X\n", get_csr (CSR_MCAUSE, sregs));
  printf (" 0x343  mtval   :  %08X\n", get_csr (CSR_MTVAL, sregs));
  printf (" 0x344  mip     :  %08X\n", get_csr (CSR_MIP, sregs));
  printf (" 0xC01  time    :  %08X\n", get_csr (CSR_TIME, sregs));
  printf (" 0xC81  timeh   :  %08X\n\n", get_csr (CSR_TIMEH, sregs));

}

static void
riscv_display_fpu (struct pstate *sregs)
{
  int i;
  float t;

  printf ("\n fsr: %08X\n\n", sregs->fsr);
  printf
    ("                 hex                   single             double\n");

  for (i = 0; i < 32; i++)
    {
      if (i < 8)
	printf ("ft%d  ", i);
      else if (i < 10)
	printf ("fs%d  ", i - 8);
      else if (i < 18)
	printf ("fa%d  ", i - 10);
      else if (i < 26)
	printf ("fs%d  ", i - 16);
      else if (i < 28)
	printf ("fs%d ", i - 16);
      else if (i < 30)
	printf ("ft%d  ", i - 20);
      else
	printf ("ft%d ", i - 20);

      printf (" f%02d  %08x%08x  %.15e  %.15e\n", i,
	      sregs->fsi[(i << 1) + 1 - BEH], sregs->fsi[(i << 1) + BEH],
	      sregs->fs[(i << 1) + BEH], sregs->fd[i]);
    }
  printf ("\n");
}

static char rtbl[32][8] = { "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
  "s0", "s1", "a0", "a1", "a2", "a3", "a4", "a5",
  "a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7",
  "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"
};

static char ftbl[32][8] =
  { "ft0", "ft1", "ft2", "ft3", "ft4", "ft5", "ft6", "ft7",
  "fs0", "fs1", "fa0", "fa1", "fa2", "fa3", "fa4", "fa5",
  "fa6", "fa7", "fs2", "fs3", "fs4", "fs5", "fs6", "fs7",
  "fs8", "fs9", "fs10", "fs11", "ft8", "ft9", "ft10", "ft11"
};

char *
ctbl (uint32 address)
{
  switch (address)
    {
    case CSR_MSTATUS:
      return "mstatus";
      break;
    case CSR_MCAUSE:
      return "mcause";
      break;
    case CSR_MTVEC:
      return "mtvec";
      break;
    case CSR_MEPC:
      return "mepc";
      break;
    case CSR_MIP:
      return "mip";
      break;
    case CSR_MIE:
      return "mie";
      break;
    case CSR_MTVAL:
      return "mtval";
      break;
    case CSR_MISA:
      return "misa";
      break;
    case CSR_TIME:
      return "time";
      break;
    case CSR_TIMEH:
      return "timeh";
      break;
    case CSR_MHARTID:
      return "hartid";
      break;
    case CSR_MSCRATCH:
      return "mscratch";
      break;
    case CSR_FFLAGS:
      return "fflags";
      break;
    case CSR_FRM:
      return "frm";
      break;
    case CSR_FCSR:
      return "fcsr";
      break;
    default:
      return "unimp";
    }
}

static void
riscv_disas (char *st, uint32 pc, uint32 inst)
{
  uint32 op1, op2, op3, rd, rs1, rs2, npc, btrue;
  int32 sop1, sop2, *wdata, result, offset;
  int32 data, address, ws, mexc, fcc, z;
  unsigned char op, funct3, funct5, rs1p, rs2p, funct2, frs1, frs2, frd;
  int64 sop64a, sop64b;
  uint64 op64a, op64b;
  char opc[16], param[32], stmp[16];

  strcpy (opc, "unimp");
  strcpy (param, "");

  if ((inst & 3) != 3)
    {
      /* Compressed instructions  (RV32C) */
      npc = pc + 2;
      funct3 = (inst >> 13) & 7;
      rs1p = ((inst >> 7) & 7) | 8;
      rs2p = ((inst >> 2) & 7) | 8;
      rs1 = ((inst >> 7) & 0x1f);
      rs2 = ((inst >> 2) & 0x1f);
      switch (inst & 3)
	{
	case 0:
	  address = (int32) EXTRACT_RVC_LW_IMM (inst);
	  sprintf (param, "%s,%d(%s)", rtbl[rs2p], address, rtbl[rs1p]);
	  switch (funct3)
	    {
	    case CADDI4SPN:	/* addi rd', x2, nzuimm[9:2] */
	      if ((inst & 0x0ffff) == 0)
		{
		  param[0] = 0;
		  break;
		}
	      strcpy (opc, "addi");
	      sprintf (param, "%s,%s,%d", rtbl[rs2p], rtbl[2],
		       (int32) EXTRACT_RVC_ADDI4SPN_IMM (inst));
	      break;
	    case CLW:		/* lw rd', offset[6:2](rs1') */
	      strcpy (opc, "lw");
	      break;
	    case CSW:		/* sw rs2', offset[6:2](rs1') */
	      strcpy (opc, "sw");
	      break;
	    case CFLW:		/* lw rd', offset[6:2](rs1') */
	      strcpy (opc, "flw");
	      sprintf (param, "%s,%d(%s)", ftbl[rs2p], address, rtbl[rs1p]);
	      break;
	    case CFLD:		/* ld frd', offset[7:3](rs1') */
	      address = (int32) EXTRACT_RVC_LD_IMM (inst);
	      strcpy (opc, "fld");
	      sprintf (param, "%s,%d(%s)", ftbl[rs2p], address, rtbl[rs1p]);
	      break;
	    case CFSW:		/* sw rs2', offset[6:2](rs1') */
	      strcpy (opc, "fsw");
	      sprintf (param, "%s,%d(%s)", ftbl[rs2p], address, rtbl[rs1p]);
	      break;
	    case CFSD:		/* sd frs2', offset[7:3](rs1') */
	      address = (int32) EXTRACT_RVC_LD_IMM (inst);
	      strcpy (opc, "fsd");
	      sprintf (param, "%s,%d(%s)", ftbl[rs2p], address, rtbl[rs1p]);
	      break;
	    }
	  break;
	case 1:
	  switch (funct3)
	    {
	    case CADDI:	/* addi rd, rd, nzimm[5:0] */
	      sop2 = EXTRACT_RVC_IMM (inst);
	      strcpy (opc, "addi");
	      sprintf (param, "%s,%s,%d", rtbl[rs1], rtbl[rs1], sop2);
	      break;
	    case CLI:		/* addi rd, x0, imm[5:0] */
	      sop2 = EXTRACT_RVC_IMM (inst);
	      strcpy (opc, "li");
	      sprintf (param, "%s,%d", rtbl[rs1], sop2);
	      break;
	    case CJAL:		/* jal x1, offset[11:1] */
	    case CJNL:		/* jal x0, offset[11:1] */
	      offset = EXTRACT_RVC_J_IMM (inst);
	      npc = pc + offset;
	      if (funct3 == CJAL)
		{
		  strcpy (opc, "jal");
		  sprintf (param, "%s,0x%x", rtbl[1], npc);
		}
	      else
		{
		  strcpy (opc, "j");
		  sprintf (param, "0x%x", npc);
		}
	      break;
	    case CADDI16SP:	/* addi x2, x2, nzimm[9:4] */
	      if (rs1 == 2)
		{
		  sop2 = EXTRACT_RVC_ADDI16SP_IMM (inst);
		  strcpy (opc, "addi");
		  sprintf (param, "%s,%s,%d", rtbl[rs1], rtbl[rs1], sop2);
		}
	      else
		{		/* CLUI:  lui rd, nzuimm[17:12 */
		  strcpy (opc, "lui");
		  sprintf (param, "%s,0x%x", rtbl[rs1],
			   EXTRACT_RVC_LUI_IMM (inst));
		}
	      break;
	    case CARITH:
	      sop2 = EXTRACT_RVC_IMM (inst);
	      sprintf (param, "%s,%s,%d", rtbl[rs1p], rtbl[rs1p], sop2);
	      switch ((inst >> 10) & 7)
		{
		case 0:	/* srli rd', rd', shamt[5:0] */
		  strcpy (opc, "srli");
		  sprintf (param, "%s,%s,%d", rtbl[rs1p], rtbl[rs1p],
			   sop2 & 0x1f);
		  break;
		case 1:	/* srai rd', rd', shamt[5:0] */
		  strcpy (opc, "srai");
		  sprintf (param, "%s,%s,%d", rtbl[rs1p], rtbl[rs1p],
			   sop2 & 0x1f);
		  break;
		case 2:
		case 6:	/* andi rd', rd', imm[5:0] */
		  strcpy (opc, "andi");
		  break;
		case 3:
		  sprintf (param, "%s,%s,%s", rtbl[rs1p], rtbl[rs1p],
			   rtbl[rs2p]);
		  switch ((inst >> 5) & 3)
		    {
		    case 0:	/* sub rd', rd', rs2' */
		      strcpy (opc, "sub");
		      break;
		    case 1:	/* xor rd', rd', rs2' */
		      strcpy (opc, "xor");
		      break;
		    case 2:	/* or rd', rd', rs2' */
		      strcpy (opc, "or");
		      break;
		    case 3:	/* and rd', rd', rs2' */
		      strcpy (opc, "and");
		      break;
		    }
		  break;
		}
	      break;
	    case CBEQZ:	/* beq rs1', x0, offset[8:1] */
	      offset = EXTRACT_RVC_B_IMM (inst);
	      strcpy (opc, "beqz");
	      sprintf (param, "%s,0x%08x", rtbl[rs1p], pc + offset);
	      break;
	    case CBNEZ:	/* bne rs1', x0, offset[8:1] */
	      offset = EXTRACT_RVC_B_IMM (inst);
	      strcpy (opc, "bnez");
	      sprintf (param, "%s,0x%08x", rtbl[rs1p], pc + offset);
	      break;
	    }
	  break;
	case 2:
	  switch (funct3)
	    {
	    case 0:		/* slli rd', rd', shamt[5:0] */
	      sop2 = EXTRACT_RVC_IMM (inst);
	      strcpy (opc, "slli");
	      sprintf (param, "%s,%s,%d", rtbl[rs1], rtbl[rs1], sop2 & 0x1f);
	      break;
	    case 2:		/* LWSP: lw rd, offset[7:2](x2) */
	      offset = EXTRACT_RVC_LWSP_IMM (inst);
	      sprintf (param, "%s,%d(%s)", rtbl[rs1], offset, rtbl[2]);
	      strcpy (opc, "lw");
	      break;
	    case 1:		/* FLDSP: ld frd, offset[8:3](x2) */
	      offset = EXTRACT_RVC_LDSP_IMM (inst);
	      sprintf (param, "%s,%d(%s)", ftbl[rs1], offset, rtbl[2]);
	      strcpy (opc, "fld");
	      break;
	    case 3:		/* FLWSP: lw frd, offset[7:2](x2) */
	      offset = EXTRACT_RVC_LWSP_IMM (inst);
	      sprintf (param, "%s,%d(%s)", ftbl[rs1], offset, rtbl[2]);
	      strcpy (opc, "flw");
	      break;
	    case 4:
	      if ((inst >> 12) & 1)
		{
		  if (rs1)
		    {
		      if (rs2)
			{	/* add rd, rd, rs2 */
			  strcpy (opc, "add");
			  sprintf (param, "%s,%s,%s", rtbl[rs1], rtbl[rs1],
				   rtbl[rs2]);
			}
		      else
			{	/* jalr x1, rs1, 0 */
			  strcpy (opc, "jalr");
			  sprintf (param, "%s,%s", rtbl[1], rtbl[rs1]);
			}
		    }
		  else
		    {		/* EBREAK */
		      strcpy (opc, "ebreak");
		    }
		}
	      else
		{
		  if (rs2)
		    {		/* MV */
		      strcpy (opc, "mv");
		      sprintf (param, "%s,%s", rtbl[rs1], rtbl[rs2]);
		    }
		  else
		    {		/* jalr x0, rs1, 0 */
		      if (rs1 == 1)
			{
			  strcpy (opc, "ret");
			}
		      else
			{
			  strcpy (opc, "j");
			  sprintf (param, "%s", rtbl[rs1]);
			}
		    }
		}
	      break;
	    case 6:		/* SWSP: sw rs2, offset[7:2](x2) */
	      address = EXTRACT_RVC_SWSP_IMM (inst);
	      strcpy (opc, "sw");
	      sprintf (param, "%s,%d(sp)", rtbl[rs2], address);
	      break;
	    case 5:		/* FSDSP: sw frs2, offset[8:3](x2) */
	      address = EXTRACT_RVC_SDSP_IMM (inst);
	      strcpy (opc, "fsd");
	      sprintf (param, "%s,%d(sp)", ftbl[rs2], address);
	      break;
	    case 7:		/* FSWSP: sw frs2, offset[7:2](x2) */
	      address = EXTRACT_RVC_SWSP_IMM (inst);
	      strcpy (opc, "fsw");
	      sprintf (param, "%s,%d(sp)", ftbl[rs2], address);
	      break;
	    }
	  break;
	}
    }
  else
    {
      op = (inst >> 2) & 0x1f;
      funct3 = (inst >> 12) & 0x7;
      rd = (inst >> 7) & 0x1f;
      rs1 = (inst >> 15) & 0x1f;
      rs2 = (inst >> 20) & 0x1f;
      switch (op)
	{
	case OP_LUI:
	  strcpy (opc, "lui");
	  sprintf (param, "%s,0x%x", rtbl[rd], inst >> 12);
	  break;
	case OP_BRANCH:
	  offset = EXTRACT_SBTYPE_IMM (inst);
	  switch (funct3)
	    {
	    case B_BE:
	      strcpy (opc, "beq ");
	      break;
	    case B_BNE:
	      strcpy (opc, "bne ");
	      break;
	    case B_BGE:
	      strcpy (opc, "bge ");
	      break;
	    case B_BGEU:
	      strcpy (opc, "bgeu");
	      break;
	    case B_BLT:
	      if (rs1)
		strcpy (opc, "blt ");
	      else
		strcpy (opc, "bgtz");
	      break;
	    case B_BLTU:
	      strcpy (opc, "bltu");
	      break;
	    }
	  if (rs1)
	    {
	      sprintf (stmp, "%s,", rtbl[rs1]);
	      strcat (param, stmp);
	    }
	  if (rs2)
	    {
	      sprintf (stmp, "%s,", rtbl[rs2]);
	      strcat (param, stmp);
	    }
	  else
	    opc[3] = 'z';
	  sprintf (stmp, "0x%08x", pc + offset);
	  strcat (param, stmp);
	  break;
	case OP_JAL:		/* JAL */
	  offset = EXTRACT_UJTYPE_IMM (inst);
	  npc = (pc + offset) & ~1;
	  if (!rd)
	    {
	      strcpy (opc, "j");
	      sprintf (param, "0x%x", npc);
	    }
	  else
	    {
	      strcpy (opc, "jal");
	      sprintf (param, "%s,0x%x", rtbl[rd], npc);
	    }
	  break;
	case OP_JALR:		/* JALR */
	  offset = EXTRACT_ITYPE_IMM (inst);
	  if (!rd && !offset && (rs1 == 1))
	    {
	      strcpy (opc, "ret");
	      param[0] = 0;
	    }
	  else
	    {
	      strcpy (opc, "jalr");
	      if (rd == 1)
		sprintf (param, "%s", rtbl[rs1]);
	      else
		sprintf (param, "%s,%s", rtbl[rd], rtbl[rs1]);
	      if (offset)
		{
		  sprintf (stmp, ",0x%x", offset);
		  strcat (param, stmp);
		}
	    }
	  break;
	case OP_AUIPC:		/* AUIPC */
	  strcpy (opc, "auipc");
	  sprintf (param, "%s,0x%x", rtbl[rd], inst >> 12);
	  break;
	case OP_IMM:		/* IMM */
	  sop2 = EXTRACT_ITYPE_IMM (inst);
	  sprintf (param, "%s,%s,%d", rtbl[rd], rtbl[rs1], sop2);
	  switch (funct3)
	    {
	    case IXOR:
	      strcpy (opc, "xori");
	      break;
	    case IOR:
	      strcpy (opc, "ori");
	      break;
	    case IAND:
	      strcpy (opc, "andi");
	      break;
	    case ADD:
	      if (!rs1)
		{
		  strcpy (opc, "li");
		  sprintf (param, "%s,%d", rtbl[rd], sop2);
		}
	      else if (!sop2)
		{
		  strcpy (opc, "mv");
		  sprintf (param, "%s,%d", rtbl[rd], sop2);
		  sprintf (param, "%s,%s", rtbl[rd], rtbl[rs1]);
		}
	      else
		strcpy (opc, "addi");
	      break;
	    case SLL:
	      strcpy (opc, "slli");
	      sprintf (param, "%s,%s,%d", rtbl[rd], rtbl[rs1], sop2 & 0x1f);
	      break;
	    case SRL:
	      if ((inst >> 30) & 1)
		strcpy (opc, "srai");
	      else
		strcpy (opc, "srli");
	      sprintf (param, "%s,%s,%d", rtbl[rd], rtbl[rs1], sop2 & 0x1f);
	      break;
	    case SLT:
	      strcpy (opc, "slti");
	      break;
	    case SLTU:
	      strcpy (opc, "sltiu");
	      break;
	    }
	  break;
	case OP_REG:		/* REG */
	  sprintf (param, "%s,%s,%s", rtbl[rd], rtbl[rs1], rtbl[rs2]);
	  switch ((inst >> 25) & 3)
	    {
	    case 0:
	      switch (funct3)
		{
		case IXOR:
		  strcpy (opc, "xor");
		  break;
		case IOR:
		  strcpy (opc, "or");
		  break;
		case IAND:
		  strcpy (opc, "and");
		  break;
		case ADD:
		  if ((inst >> 30) & 1)
		    strcpy (opc, "sub");
		  else
		    strcpy (opc, "add");
		  break;
		case SLL:
		  strcpy (opc, "sll");
		  break;
		case SRL:
		  if ((inst >> 30) & 1)
		    {
		      strcpy (opc, "sra");
		    }
		  else
		    strcpy (opc, "srl");
		  break;
		case SLT:
		  strcpy (opc, "slt");
		  break;
		case SLTU:
		  strcpy (opc, "sltu");
		  break;
		}
	      break;
	    case 1:		/* MUL/DIV */
	      switch (funct3)
		{
		case 0:	/* MUL */
		  strcpy (opc, "mul");
		  break;
		case 1:	/* MULH */
		  strcpy (opc, "mulh");
		  break;
		case 2:	/* MULHSU */
		  strcpy (opc, "mulhsu");
		  break;
		case 3:	/* MULHU */
		  strcpy (opc, "mulhu");
		  break;
		case 4:	/* DIV */
		  strcpy (opc, "div");
		  break;
		case 5:	/* DIVU */
		  strcpy (opc, "divu");
		  break;
		case 6:	/* REM */
		  strcpy (opc, "rem");
		  break;
		case 7:	/* REMU */
		  strcpy (opc, "remu");
		  break;
		}
	      break;
	    }
	  break;
	case OP_STORE:		/* store instructions */
	  offset = EXTRACT_STYPE_IMM (inst);
	  sprintf (param, "%s,%d(%s)", rtbl[rs2], offset, rtbl[rs1]);
	  switch (funct3)
	    {
	    case SW:
	      strcpy (opc, "sw");
	      break;
	    case SB:
	      strcpy (opc, "sb");
	      break;
	    case SH:
	      strcpy (opc, "sh");
	      break;
	    }
	  break;
	case OP_FSW:		/* F store instructions */
	  offset = EXTRACT_STYPE_IMM (inst);
	  sprintf (param, "%s,%d(%s)", ftbl[rs2], offset, rtbl[rs1]);
	  switch (funct3)
	    {
	    case 2:		/* FSW */
	      strcpy (opc, "fsw");
	      break;
	    case 3:		/* FSD */
	      strcpy (opc, "fsd");
	    }
	  break;
	case OP_LOAD:		/* load instructions */
	  offset = EXTRACT_ITYPE_IMM (inst);
	  sprintf (param, "%s,%d(%s)", rtbl[rd], offset, rtbl[rs1]);
	  switch (funct3)
	    {
	    case LW:
	      strcpy (opc, "lw");
	      break;
	    case LB:
	      strcpy (opc, "lb");
	      break;
	    case LBU:
	      strcpy (opc, "lbu");
	      break;
	    case LH:
	      strcpy (opc, "lh");
	      break;
	    case LHU:
	      strcpy (opc, "lhu");
	      break;
	    }
	  break;
	case OP_AMO:		/* atomic instructions */
	  sprintf (param, "%s,%s,(%s)", rtbl[rd], rtbl[rs2], rtbl[rs1]);
	  funct5 = (inst >> 27) & 0x1f;
	  switch (funct5)
	    {
	    case LRQ:
	      sprintf (param, "%s,(%s)", rtbl[rd], rtbl[rs1]);
	      strcpy (opc, "lr.w");
	      if ((inst >> 26) & 1)
		strcat (opc, ".aq");
	      if ((inst >> 25) & 1)
		strcat (opc, ".rl");
	      break;
	    case SCQ:
	      strcpy (opc, "sc.w");
	      if ((inst >> 26) & 1)
		strcat (opc, ".aq");
	      if ((inst >> 25) & 1)
		strcat (opc, ".rl");
	      break;
	    default:		/* AMOXXX */
	      switch (funct5)
		{
		case AMOSWAP:
		  strcpy (opc, "amoswap");
		  break;
		case AMOADD:
		  strcpy (opc, "amoadd");
		  break;
		case AMOXOR:
		  strcpy (opc, "amoxor");
		  break;
		case AMOOR:
		  strcpy (opc, "amoor");
		  break;
		case AMOAND:
		  strcpy (opc, "amoand");
		  break;
		case AMOMIN:
		  strcpy (opc, "amomin");
		  break;
		case AMOMAX:
		  strcpy (opc, "amomax");
		  break;
		case AMOMINU:
		  strcpy (opc, "amominu");
		  break;
		case AMOMAXU:
		  strcpy (opc, "amomaxu");
		  break;
		}
	    }
	  break;
	case OP_SYS:
	  address = inst >> 20;
	  sprintf (param, "%s,%s,%s", rtbl[rd], ctbl (address), rtbl[rs1]);
	  op1 = (inst >> 15) & 0x1f;
	  switch (funct3)
	    {
	    case 0:		/* ecall, xret */
	      param[0] = 0;
	      switch (rs2)
		{
		case 0:	/* ecall */
		  strcpy (opc, "ecall");
		  break;
		case 1:	/* ebreak */
		  strcpy (opc, "ebreak");
		  break;
		case 2:	/* xret */
		  strcpy (opc, "mret");
		  break;
		case 5:	/* wfi */
		  strcpy (opc, "wfi");
		  break;
		}
	      break;
	    case CSRRW:
	      if (rd)
		strcpy (opc, "csrrw");
	      else
		{
		  sprintf (param, "%s,%s", ctbl (address), rtbl[rs1]);
		  strcpy (opc, "csrw");
		}
	      break;
	    case CSRRS:
	      if (rd)
		strcpy (opc, "csrrs");
	      else
		{
		  sprintf (param, "%s,%s", ctbl (address), rtbl[rs1]);
		  strcpy (opc, "csrs");
		}
	      break;
	    case CSRRC:
	      strcpy (opc, "csrrc");
	      break;
	    case CSRRWI:
	      strcpy (opc, "csrrwi");
	      sprintf (param, "%s,%s,%d", rtbl[rd], ctbl (address), op1);
	      break;
	    case CSRRCI:
	      strcpy (opc, "csrrci");
	      sprintf (param, "%s,%s,%d", rtbl[rd], ctbl (address), op1);
	      break;
	    case CSRRSI:
	      if (rd)
		strcpy (opc, "csrrsi");
	      else
		{
		  strcpy (opc, "csrsi");
		  sprintf (param, "%s,%d", ctbl (address), op1);
		}
	      break;
	    }
	  break;
	case OP_FLOAD:		/* float load instructions */
	  offset = EXTRACT_ITYPE_IMM (inst);
	  sprintf (param, "%s,%d(%s)", ftbl[rd], offset, rtbl[rs1]);
	  switch (funct3)
	    {
	    case LW:
	      strcpy (opc, "flw");
	      break;
	    case LD:
	      strcpy (opc, "fld");
	      break;
	    }
	  break;
	case OP_FPU:
	  funct2 = (inst >> 25) & 3;
	  funct5 = (inst >> 27);
	  sprintf (param, "%s,%s,%s", ftbl[rd], ftbl[rs1], ftbl[rs2]);
	  switch (funct2)
	    {
	    case 0:		/* single-precision ops */
	      switch (funct5)
		{
		case 0:	/* FADDS */
		  strcpy (opc, "fadd.s");
		  break;
		case 1:	/* FSUBS */
		  strcpy (opc, "fsub.s");
		  break;
		case 2:	/* FMULS */
		  strcpy (opc, "fmul.s");
		  break;
		case 3:	/* FDIVS */
		  strcpy (opc, "fdiv.s");
		  break;
		case 4:	/* FSGX */
		  switch (funct3)
		    {
		    case 0:	/* FSGNJ */
		      strcpy (opc, "fsgnj.s");
		      break;
		    case 1:	/* FSGNJN */
		      strcpy (opc, "fsgnjn.s");
		      break;
		    case 2:	/* FSGNJX */
		      strcpy (opc, "fsgnjx.s");
		      break;
		    }
		  break;
		case 5:	/* FMINS / FMAXS */
		  if ((inst >> 12) & 1)
		    strcpy (opc, "fmax.s");
		  else
		    strcpy (opc, "fmin.s");
		  break;
		case 0x08:	/* FCVTSD / FCVTDS */
		  switch (funct2)
		    {
		    case 0:	/* FCVTSD */
		      strcpy (opc, "fcvt.s.d");
		      sprintf (param, "%s,%s", ftbl[rd], ftbl[rs1]);
		      break;
		    }
		  break;
		case 0x0b:	/* FSQRTS */
		  strcpy (opc, "fsqrt.s");
		  sprintf (param, "%s,%s", ftbl[rd], ftbl[rs1]);
		  break;
		case 0x14:	/* FCMPS */
		  sprintf (param, "%s,%s,%s", rtbl[rd], ftbl[rs1], ftbl[rs2]);
		  switch (funct3)
		    {
		    case 0:	/* FLES */
		      strcpy (opc, "fle.s");
		      break;
		    case 1:	/* FLTS */
		      strcpy (opc, "flt.s");
		      break;
		    case 2:	/* FEQS */
		      strcpy (opc, "feq.s");
		      break;
		    }
		  break;
		case 0x18:	/* FCVTW */
		  sprintf (param, "%s,%s", rtbl[rd], ftbl[rs1]);
		  switch (rs2)
		    {
		    case 0:	/* FCVTWS */
		      strcpy (opc, "fcvt.w.s");
		      break;
		    case 1:	/* FCVTWUS */
		      strcpy (opc, "fcvt.wu.s");
		      break;
		    }
		  break;
		case 0x1a:	/* FCVT */
		  sprintf (param, "%s,%s", ftbl[rd], rtbl[rs1]);
		  switch (rs2)
		    {
		    case 0:	/* FCVTSW */
		      strcpy (opc, "fcvt.s.w");
		      break;
		    case 1:	/* FCVTSWU */
		      strcpy (opc, "fcvt.s.wu");
		      break;
		    }
		  break;
		case 0x1c:
		  switch (funct3)
		    {
		    case 0:	/* FMVXS */
		      sprintf (param, "%s,%s", rtbl[rd], ftbl[rs1]);
		      strcpy (opc, "fmv.x.s");
		      break;
		    case 1:	/* FCLASS */
		      sprintf (param, "%s,%s", rtbl[rd], ftbl[rs1]);
		      strcpy (opc, "fclass.s");
		      break;
		    }
		  break;
		case 0x1e:	/* FMVSX */
		  sprintf (param, "%s,%s", ftbl[rd], rtbl[rs1]);
		  strcpy (opc, "fmv.s.x");
		  break;
		}
	      break;
	    case 1:		/* double-precision ops */
	      switch (funct5)
		{
		case 0:
		  strcpy (opc, "fadd.d");
		  break;
		case 1:
		  strcpy (opc, "fsub.d");
		  break;
		case 2:
		  strcpy (opc, "fmul.d");
		  break;
		case 3:
		  strcpy (opc, "fdiv.d");
		  break;
		case 4:	/* FSGX */
		  switch (funct3)
		    {
		    case 0:	/* FSGNJ */
		      strcpy (opc, "fsgnj.d");
		      break;
		    case 1:	/* FSGNJN */
		      strcpy (opc, "fsgnjn.d");
		      break;
		    case 2:	/* FSGNJX */
		      strcpy (opc, "fsgnjx.d");
		      break;
		    }
		  break;
		case 5:	/* FMIND / FMAXD */
		  if ((inst >> 12) & 1)
		    strcpy (opc, "fmax.d");
		  else
		    strcpy (opc, "fmin.d");
		  break;
		case 0x08:	/* FCVTSD / FCVTDS */
		  switch (funct2)
		    {
		    case 1:	/* FCVTDS */
		      strcpy (opc, "fcvt.d.s");
		      sprintf (param, "%s,%s", ftbl[rd], ftbl[rs1]);
		      break;
		    }
		  break;
		case 0x0b:	/* FSQRTD */
		  strcpy (opc, "fsqrt.d");
		  sprintf (param, "%s,%s", ftbl[rd], ftbl[rs1]);
		  break;
		case 0x14:	/* FCMPD */
		  sprintf (param, "%s,%s,%s", rtbl[rd], ftbl[rs1], ftbl[rs2]);
		  switch (funct3)
		    {
		    case 0:	/* FLED */
		      strcpy (opc, "fle.d");
		      break;
		    case 1:	/* FLTD */
		      strcpy (opc, "flt.d");
		      break;
		    case 2:	/* FEQD */
		      strcpy (opc, "feq.d");
		      break;
		    }
		  break;
		case 0x18:	/* FCVTW */
		  sprintf (param, "%s,%s", rtbl[rd], ftbl[rs1]);
		  switch (rs2)
		    {
		    case 0:	/* FCVTWD */
		      strcpy (opc, "fcvt.w.d");
		      break;
		    case 1:	/* FCVTWUD */
		      strcpy (opc, "fcvt.wu.d");
		      break;
		    }
		  break;
		case 0x1a:	/* FCVTD */
		  sprintf (param, "%s,%s", ftbl[rd], rtbl[rs1]);
		  switch (rs2)
		    {
		    case 0:	/* FCVTDW */
		      strcpy (opc, "fcvt.d.w");
		      break;
		    case 1:	/* FCVTDWU */
		      strcpy (opc, "fcvt.d.wu");
		      break;
		    }
		  break;
		case 0x1c:	/* FCLASSD */
		  sprintf (param, "%s,%s", rtbl[rd], ftbl[rs1]);
		  switch (funct3)
		    {
		    case 1:
		      strcpy (opc, "fclass.d");
		    }
		  break;
		}
	      break;
	    }
	  break;
	case OP_FMADD:
	  sprintf (param, "%s,%s, %s, %s", ftbl[rd], ftbl[rs1], ftbl[rs2],
		   ftbl[inst >> 27]);
	  switch ((inst >> 25) & 3)
	    {
	    case 0:		/* OP_FMADDS */
	      strcpy (opc, "fmadd.s");
	      break;
	    case 1:		/* OP_FMADDD */
	      strcpy (opc, "fmadd.d");
	      break;
	    }
	  break;
	case OP_FMSUB:
	  sprintf (param, "%s,%s, %s, %s", ftbl[rd], ftbl[rs1], ftbl[rs2],
		   ftbl[inst >> 27]);
	  switch ((inst >> 25) & 3)
	    {
	    case 0:		/* OP_FMSUBS */
	      strcpy (opc, "fmsub.s");
	      break;
	    case 1:		/* OP_FMSUBD */
	      strcpy (opc, "fmsub.d");
	      break;
	    }
	  break;
	case OP_FNMSUB:
	  sprintf (param, "%s,%s, %s, %s", ftbl[rd], ftbl[rs1], ftbl[rs2],
		   ftbl[inst >> 27]);
	  switch ((inst >> 25) & 3)
	    {
	    case 0:		/* OP_FNMSUBS */
	      strcpy (opc, "fnmsub.s");
	      break;
	    case 1:		/* OP_FNMSUBD */
	      strcpy (opc, "fnmsub.d");
	      break;
	    }
	  break;
	case OP_FNMADD:
	  sprintf (param, "%s,%s, %s, %s", ftbl[rd], ftbl[rs1], ftbl[rs2],
		   ftbl[inst >> 27]);
	  switch ((inst >> 25) & 3)
	    {
	    case 0:		/* OP_FNMADDS */
	      strcpy (opc, "fnmadd.s");
	      break;
	    case 1:		/* OP_FNMADDD */
	      strcpy (opc, "fnmadd.d");
	      break;
	    }
	  break;
	case OP_FENCE:
	  strcpy (opc, "fence");
	  break;
	}
    }

  sprintf (st, "%-12s%s", opc, param);

}

static void
riscv_print_insn (uint32 addr)
{
  char tmp[128];
  uint32 insn;
  int32 hold;

  ms->memory_iread (addr, &insn, &hold);
  riscv_disas (tmp, addr, insn);
  printf (" %s", tmp);
}

const struct cpu_arch riscv = {
#ifdef HOST_LITTLE_ENDIAN
  0,
#else
  3,
#endif
  riscv_dispatch_instruction,
  riscv_execute_trap,
  riscv_check_interrupts,
  riscv_print_insn,
  riscv_gdb_get_reg,
  riscv_set_register,
  riscv_display_registers,
  riscv_display_ctrl,
  riscv_display_special,
  riscv_display_fpu
};
