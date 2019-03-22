#include "sparc.h"
#include <inttypes.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

static uint32
sub_cc (psr, operand1, operand2, result)
     uint32 psr;
     int32 operand1;
     int32 operand2;
     int32 result;
{
  psr = ((psr & ~PSR_N) | ((result >> 8) & PSR_N));
  if (result)
    psr &= ~PSR_Z;
  else
    psr |= PSR_Z;
  psr = (psr & ~PSR_V) | ((((operand1 & ~operand2 & ~result) |
			    (~operand1 & operand2 & result)) >> 10) & PSR_V);
  psr = (psr & ~PSR_C) | ((((~operand1 & operand2) |
			    ((~operand1 | operand2) & result)) >> 11) &
			  PSR_C);
  return psr;
}

uint32
add_cc (psr, operand1, operand2, result)
     uint32 psr;
     int32 operand1;
     int32 operand2;
     int32 result;
{
  psr = ((psr & ~PSR_N) | ((result >> 8) & PSR_N));
  if (result)
    psr &= ~PSR_Z;
  else
    psr |= PSR_Z;
  psr = (psr & ~PSR_V) | ((((operand1 & operand2 & ~result) |
			    (~operand1 & ~operand2 & result)) >> 10) & PSR_V);
  psr = (psr & ~PSR_C) | ((((operand1 & operand2) |
			    ((operand1 | operand2) & ~result)) >> 11) &
			  PSR_C);
  return psr;
}

static void
log_cc (result, sregs)
     int32 result;
     struct pstate *sregs;
{
  sregs->psr &= ~(PSR_CC);	/* Zero CC bits */
  sregs->psr = (sregs->psr | ((result >> 8) & PSR_N));
  if (result == 0)
    sregs->psr |= PSR_Z;
}

static int
chk_asi (sregs, asi, op3)
     struct pstate *sregs;
     uint32 *asi, op3;

{
  if (!(sregs->psr & PSR_S))
    {
      sregs->trap = TRAP_PRIVI;
      return 0;
    }
  else if (sregs->inst & INST_I)
    {
      sregs->trap = TRAP_UNIMP;
      return 0;
    }
  else
    *asi = (sregs->inst >> 5) & 0x0ff;
  return 1;
}


/* Decode watchpoint address mask from opcode. Not correct for LDST,
   SWAP and STFSR but watchpoints will work anyway. */

static unsigned char
wpmask (uint32 op3)
{
  switch (op3 & 3)
    {
    case 0:
      return (3);		/* word */
    case 1:
      return (0);		/* byte */
    case 2:
      return (1);		/* half-word */
    case 3:
      return (7);		/* double word */
    }
}

static int
extract_byte_signed (uint32 data, uint32 address)
{
  uint32 tmp = ((data >> ((3 - (address & 3)) * 8)) & 0xff);
  if (tmp & 0x80)
    tmp |= 0xffffff00;
  return tmp;
}


int
extract_short (uint32 data, uint32 address)
{
  return ((data >> ((2 - (address & 2)) * 8)) & 0xffff);
}

int
extract_short_signed (uint32 data, uint32 address)
{
  uint32 tmp = ((data >> ((2 - (address & 2)) * 8)) & 0xffff);
  if (tmp & 0x8000)
    tmp |= 0xffff0000;
  return tmp;
}

int
extract_byte (uint32 data, uint32 address)
{
  return ((data >> ((3 - (address & 3)) * 8)) & 0xff);
}


static int
sparc_dispatch_instruction (sregs)
     struct pstate *sregs;
{

  uint32 cwp, op, op2, op3, asi, rd, cond, rs1, rs2;
  uint32 ldep, icc;
  int32 operand1, operand2, *rdd, result, eicc, new_cwp;
  int32 pc, npc, data, address, ws, mexc, fcc, annul;
  int32 ddata[2];

  sregs->ninst++;
  cwp = ((sregs->psr & PSR_CWP) << 4);
  op = sregs->inst >> 30;
  pc = sregs->npc;
  npc = sregs->npc + 4;
  op3 = rd = rs1 = operand2 = eicc = 0;
  rdd = 0;
  annul = 0;

  if (op & 2)
    {

      op3 = (sregs->inst >> 19) & 0x3f;
      rs1 = (sregs->inst >> 14) & 0x1f;
      rd = (sregs->inst >> 25) & 0x1f;

#ifdef LOAD_DEL

      /* Check if load dependecy is possible */
      if (sregs->simtime <= sregs->ildtime)
	ldep = (((op3 & 0x38) != 0x28) && ((op3 & 0x3e) != 0x34)
		&& (sregs->ildreg != 0));
      else
	ldep = 0;
      if (sregs->inst & INST_I)
	{
	  if (ldep && (sregs->ildreg == rs1))
	    sregs->hold++;
	  operand2 = sregs->inst;
	  operand2 = ((operand2 << 19) >> 19);	/* sign extend */
	}
      else
	{
	  rs2 = sregs->inst & INST_RS2;
	  if (rs2 > 7)
	    operand2 = sregs->r[(cwp + rs2) & 0x7f];
	  else
	    operand2 = sregs->g[rs2];
	  if (ldep && ((sregs->ildreg == rs1) || (sregs->ildreg == rs2)))
	    sregs->hold++;
	}
#else
      if (sregs->inst & INST_I)
	{
	  operand2 = sregs->inst;
	  operand2 = ((operand2 << 19) >> 19);	/* sign extend */
	}
      else
	{
	  rs2 = sregs->inst & INST_RS2;
	  if (rs2 > 7)
	    operand2 = sregs->r[(cwp + rs2) & 0x7f];
	  else
	    operand2 = sregs->g[rs2];
	}
#endif

      if (rd > 7)
	rdd = &(sregs->r[(cwp + rd) & 0x7f]);
      else
	rdd = &(sregs->g[rd]);
      if (rs1 > 7)
	rs1 = sregs->r[(cwp + rs1) & 0x7f];
      else
	rs1 = sregs->g[rs1];
    }
  switch (op)
    {
    case 0:
      op2 = (sregs->inst >> 22) & 0x7;
      switch (op2)
	{
	case SETHI:
	  rd = (sregs->inst >> 25) & 0x1f;
	  if (rd > 7)
	    rdd = &(sregs->r[(cwp + rd) & 0x7f]);
	  else
	    rdd = &(sregs->g[rd]);
	  *rdd = sregs->inst << 10;
	  break;
	case BICC:
#ifdef STAT
	  sregs->nbranch++;
#endif
	  icc = sregs->psr >> 20;
	  cond = ((sregs->inst >> 25) & 0x0f);
	  switch (cond)
	    {
	    case BICC_BN:
	      eicc = 0;
	      break;
	    case BICC_BE:
	      eicc = ICC_Z;
	      break;
	    case BICC_BLE:
	      eicc = ICC_Z | (ICC_N ^ ICC_V);
	      break;
	    case BICC_BL:
	      eicc = (ICC_N ^ ICC_V);
	      break;
	    case BICC_BLEU:
	      eicc = ICC_C | ICC_Z;
	      break;
	    case BICC_BCS:
	      eicc = ICC_C;
	      break;
	    case BICC_NEG:
	      eicc = ICC_N;
	      break;
	    case BICC_BVS:
	      eicc = ICC_V;
	      break;
	    case BICC_BA:
	      eicc = 1;
	      if (sregs->inst & 0x20000000)
		annul = 1;
	      break;
	    case BICC_BNE:
	      eicc = ~(ICC_Z);
	      break;
	    case BICC_BG:
	      eicc = ~(ICC_Z | (ICC_N ^ ICC_V));
	      break;
	    case BICC_BGE:
	      eicc = ~(ICC_N ^ ICC_V);
	      break;
	    case BICC_BGU:
	      eicc = ~(ICC_C | ICC_Z);
	      break;
	    case BICC_BCC:
	      eicc = ~(ICC_C);
	      break;
	    case BICC_POS:
	      eicc = ~(ICC_N);
	      break;
	    case BICC_BVC:
	      eicc = ~(ICC_V);
	      break;
	    }
	  if (eicc & 1)
	    {
	      operand1 = sregs->inst;
	      operand1 = ((operand1 << 10) >> 8);	/* sign extend */
	      npc = sregs->pc + operand1;
	      if (ebase.coven)
		{
		  if (cond == BICC_BA)
		    cov_jmp (sregs->pc, npc);
		  else
		    cov_bt (sregs->pc, npc);
		  cov_exec (pc);	/* delay slot executed */
		}
	    }
	  else
	    {
	      if (sregs->inst & 0x20000000)
		{
		  if (ebase.coven)
		    {
		      cov_start (npc);	/* jump over delay slot */
		    }
		  annul = 1;
		}
	      else
		{
		  if (ebase.coven)
		    cov_start (sregs->npc);	/* delay slot executed */
		}
	      if (ebase.coven)
		cov_bnt (sregs->pc);
	    }
	  break;
	case FPBCC:
#ifdef STAT
	  sregs->nbranch++;
#endif
	  if (!((sregs->psr & PSR_EF) && FP_PRES))
	    {
	      sregs->trap = TRAP_FPDIS;
	      break;
	    }
	  if (sregs->simtime < sregs->ftime)
	    {
	      sregs->ftime = sregs->simtime + sregs->hold;
	    }
	  cond = ((sregs->inst >> 25) & 0x0f);
	  fcc = (sregs->fsr >> 10) & 0x3;
	  switch (cond)
	    {
	    case FBN:
	      eicc = 0;
	      break;
	    case FBNE:
	      eicc = (fcc != FCC_E);
	      break;
	    case FBLG:
	      eicc = (fcc == FCC_L) || (fcc == FCC_G);
	      break;
	    case FBUL:
	      eicc = (fcc == FCC_L) || (fcc == FCC_U);
	      break;
	    case FBL:
	      eicc = (fcc == FCC_L);
	      break;
	    case FBUG:
	      eicc = (fcc == FCC_G) || (fcc == FCC_U);
	      break;
	    case FBG:
	      eicc = (fcc == FCC_G);
	      break;
	    case FBU:
	      eicc = (fcc == FCC_U);
	      break;
	    case FBA:
	      eicc = 1;
	      if (sregs->inst & 0x20000000)
		annul = 1;
	      break;
	    case FBE:
	      eicc = !(fcc != FCC_E);
	      break;
	    case FBUE:
	      eicc = !((fcc == FCC_L) || (fcc == FCC_G));
	      break;
	    case FBGE:
	      eicc = !((fcc == FCC_L) || (fcc == FCC_U));
	      break;
	    case FBUGE:
	      eicc = !(fcc == FCC_L);
	      break;
	    case FBLE:
	      eicc = !((fcc == FCC_G) || (fcc == FCC_U));
	      break;
	    case FBULE:
	      eicc = !(fcc == FCC_G);
	      break;
	    case FBO:
	      eicc = !(fcc == FCC_U);
	      break;
	    }
	  if (eicc)
	    {
	      operand1 = sregs->inst;
	      operand1 = ((operand1 << 10) >> 8);	/* sign extend */
	      npc = sregs->pc + operand1;
	      if (ebase.coven)
		{
		  cov_bt (sregs->pc, npc);
		  cov_exec (pc);	/* delay slot executed */
		}
	    }
	  else
	    {
	      if (sregs->inst & 0x20000000)
		{
		  if (ebase.coven)
		    {
		      cov_start (npc);	/* jump over delay slot */
		    }
		  annul = 1;
		}
	      else
		{
		  if (ebase.coven)
		    cov_start (pc);	/* delay slot executed */
		}
	      if (ebase.coven)
		cov_bnt (sregs->pc);
	    }
	  break;

	default:
	  sregs->trap = TRAP_UNIMP;
	  break;
	}
      break;
    case 1:			/* CALL */
#ifdef STAT
      sregs->nbranch++;
#endif
      sregs->r[(cwp + 15) & 0x7f] = sregs->pc;
      npc = sregs->pc + (sregs->inst << 2);
      if (ebase.coven)
	{
	  cov_jmp (sregs->pc, npc);
	  cov_exec (pc);	/* delay slot executed */
	}
      break;

    case 2:
      if ((op3 >> 1) == 0x1a)
	{
	  if (!((sregs->psr & PSR_EF) && FP_PRES))
	    {
	      sregs->trap = TRAP_FPDIS;
	    }
	  else
	    {
	      rs1 = (sregs->inst >> 14) & 0x1f;
	      rs2 = sregs->inst & 0x1f;
	      sregs->trap = fpexec (op3, rd, rs1, rs2, sregs);
	    }
	}
      else
	{

	  switch (op3)
	    {
	    case TICC:
	      icc = sregs->psr >> 20;
	      cond = ((sregs->inst >> 25) & 0x0f);
	      switch (cond)
		{
		case BICC_BN:
		  eicc = 0;
		  break;
		case BICC_BE:
		  eicc = ICC_Z;
		  break;
		case BICC_BLE:
		  eicc = ICC_Z | (ICC_N ^ ICC_V);
		  break;
		case BICC_BL:
		  eicc = (ICC_N ^ ICC_V);
		  break;
		case BICC_BLEU:
		  eicc = ICC_C | ICC_Z;
		  break;
		case BICC_BCS:
		  eicc = ICC_C;
		  break;
		case BICC_NEG:
		  eicc = ICC_N;
		  break;
		case BICC_BVS:
		  eicc = ICC_V;
		  break;
		case BICC_BA:
		  eicc = 1;
		  break;
		case BICC_BNE:
		  eicc = ~(ICC_Z);
		  break;
		case BICC_BG:
		  eicc = ~(ICC_Z | (ICC_N ^ ICC_V));
		  break;
		case BICC_BGE:
		  eicc = ~(ICC_N ^ ICC_V);
		  break;
		case BICC_BGU:
		  eicc = ~(ICC_C | ICC_Z);
		  break;
		case BICC_BCC:
		  eicc = ~(ICC_C);
		  break;
		case BICC_POS:
		  eicc = ~(ICC_N);
		  break;
		case BICC_BVC:
		  eicc = ~(ICC_V);
		  break;
		}
	      if (eicc & 1)
		{
		  sregs->trap = (0x80 | ((rs1 + operand2) & 0x7f));
		  if ((sregs->trap == 129) && (sis_gdb_break) &&
		      (sregs->inst == 0x91d02001))
		    {
		      sregs->trap = WPT_TRAP;
		      sregs->bphit = 1;
		    }
		}
	      break;

	    case MULScc:
	      operand1 =
		(((sregs->psr & PSR_V) ^ ((sregs->psr & PSR_N) >> 2))
		 << 10) | (rs1 >> 1);
	      if ((sregs->y & 1) == 0)
		operand2 = 0;
	      *rdd = operand1 + operand2;
	      sregs->y = (rs1 << 31) | (sregs->y >> 1);
	      sregs->psr = add_cc (sregs->psr, operand1, operand2, *rdd);
	      break;
	    case SMUL:
	      {
		mul64 (rs1, operand2, &sregs->y, rdd, 1);
	      }
	      break;
	    case SMULCC:
	      {
		uint32 result;

		mul64 (rs1, operand2, &sregs->y, &result, 1);

		if (result & 0x80000000)
		  sregs->psr |= PSR_N;
		else
		  sregs->psr &= ~PSR_N;

		if (result == 0)
		  sregs->psr |= PSR_Z;
		else
		  sregs->psr &= ~PSR_Z;

		*rdd = result;
	      }
	      break;
	    case UMUL:
	      {
		mul64 (rs1, operand2, &sregs->y, rdd, 0);
	      }
	      break;
	    case UMULCC:
	      {
		uint32 result;

		mul64 (rs1, operand2, &sregs->y, &result, 0);

		if (result & 0x80000000)
		  sregs->psr |= PSR_N;
		else
		  sregs->psr &= ~PSR_N;

		if (result == 0)
		  sregs->psr |= PSR_Z;
		else
		  sregs->psr &= ~PSR_Z;

		*rdd = result;
	      }
	      break;
	    case SDIV:
	      {
		if (operand2 == 0)
		  {
		    sregs->trap = TRAP_DIV0;
		    break;
		  }

		div64 (sregs->y, rs1, operand2, rdd, 1);
	      }
	      break;
	    case SDIVCC:
	      {
		uint32 result;

		if (operand2 == 0)
		  {
		    sregs->trap = TRAP_DIV0;
		    break;
		  }

		div64 (sregs->y, rs1, operand2, &result, 1);

		if (result & 0x80000000)
		  sregs->psr |= PSR_N;
		else
		  sregs->psr &= ~PSR_N;

		if (result == 0)
		  sregs->psr |= PSR_Z;
		else
		  sregs->psr &= ~PSR_Z;

		/* FIXME: should set overflow flag correctly.  */
		sregs->psr &= ~(PSR_C | PSR_V);

		*rdd = result;
	      }
	      break;
	    case UDIV:
	      {
		if (operand2 == 0)
		  {
		    sregs->trap = TRAP_DIV0;
		    break;
		  }

		div64 (sregs->y, rs1, operand2, rdd, 0);
	      }
	      break;
	    case UDIVCC:
	      {
		uint32 result;

		if (operand2 == 0)
		  {
		    sregs->trap = TRAP_DIV0;
		    break;
		  }

		div64 (sregs->y, rs1, operand2, &result, 0);

		if (result & 0x80000000)
		  sregs->psr |= PSR_N;
		else
		  sregs->psr &= ~PSR_N;

		if (result == 0)
		  sregs->psr |= PSR_Z;
		else
		  sregs->psr &= ~PSR_Z;

		/* FIXME: should set overflow flag correctly.  */
		sregs->psr &= ~(PSR_C | PSR_V);

		*rdd = result;
	      }
	      break;
	    case IXNOR:
	      *rdd = rs1 ^ ~operand2;
	      break;
	    case IXNORCC:
	      *rdd = rs1 ^ ~operand2;
	      log_cc (*rdd, sregs);
	      break;
	    case IXOR:
	      *rdd = rs1 ^ operand2;
	      break;
	    case IXORCC:
	      *rdd = rs1 ^ operand2;
	      log_cc (*rdd, sregs);
	      break;
	    case IOR:
	      *rdd = rs1 | operand2;
	      break;
	    case IORCC:
	      *rdd = rs1 | operand2;
	      log_cc (*rdd, sregs);
	      break;
	    case IORN:
	      *rdd = rs1 | ~operand2;
	      break;
	    case IORNCC:
	      *rdd = rs1 | ~operand2;
	      log_cc (*rdd, sregs);
	      break;
	    case IANDNCC:
	      *rdd = rs1 & ~operand2;
	      log_cc (*rdd, sregs);
	      break;
	    case IANDN:
	      *rdd = rs1 & ~operand2;
	      break;
	    case IAND:
	      *rdd = rs1 & operand2;
	      break;
	    case IANDCC:
	      *rdd = rs1 & operand2;
	      log_cc (*rdd, sregs);
	      break;
	    case SUB:
	      *rdd = rs1 - operand2;
	      break;
	    case SUBCC:
	      *rdd = rs1 - operand2;
	      sregs->psr = sub_cc (sregs->psr, rs1, operand2, *rdd);
	      break;
	    case SUBX:
	      *rdd = rs1 - operand2 - ((sregs->psr >> 20) & 1);
	      break;
	    case SUBXCC:
	      *rdd = rs1 - operand2 - ((sregs->psr >> 20) & 1);
	      sregs->psr = sub_cc (sregs->psr, rs1, operand2, *rdd);
	      break;
	    case ADD:
	      *rdd = rs1 + operand2;
	      break;
	    case ADDCC:
	      *rdd = rs1 + operand2;
	      sregs->psr = add_cc (sregs->psr, rs1, operand2, *rdd);
	      break;
	    case ADDX:
	      *rdd = rs1 + operand2 + ((sregs->psr >> 20) & 1);
	      break;
	    case ADDXCC:
	      *rdd = rs1 + operand2 + ((sregs->psr >> 20) & 1);
	      sregs->psr = add_cc (sregs->psr, rs1, operand2, *rdd);
	      break;
	    case TADDCC:
	      *rdd = rs1 + operand2;
	      sregs->psr = add_cc (sregs->psr, rs1, operand2, *rdd);
	      if ((rs1 | operand2) & 0x3)
		sregs->psr |= PSR_V;
	      break;
	    case TSUBCC:
	      *rdd = rs1 - operand2;
	      sregs->psr = sub_cc (sregs->psr, rs1, operand2, *rdd);
	      if ((rs1 | operand2) & 0x3)
		sregs->psr |= PSR_V;
	      break;
	    case TADDCCTV:
	      *rdd = rs1 + operand2;
	      result = add_cc (0, rs1, operand2, *rdd);
	      if ((rs1 | operand2) & 0x3)
		result |= PSR_V;
	      if (result & PSR_V)
		{
		  sregs->trap = TRAP_TAG;
		}
	      else
		{
		  sregs->psr = (sregs->psr & ~PSR_CC) | result;
		}
	      break;
	    case TSUBCCTV:
	      *rdd = rs1 - operand2;
	      result = add_cc (0, rs1, operand2, *rdd);
	      if ((rs1 | operand2) & 0x3)
		result |= PSR_V;
	      if (result & PSR_V)
		{
		  sregs->trap = TRAP_TAG;
		}
	      else
		{
		  sregs->psr = (sregs->psr & ~PSR_CC) | result;
		}
	      break;
	    case SLL:
	      *rdd = rs1 << (operand2 & 0x1f);
	      break;
	    case SRL:
	      *rdd = rs1 >> (operand2 & 0x1f);
	      break;
	    case SRA:
	      *rdd = ((int) rs1) >> (operand2 & 0x1f);
	      break;
	    case FLUSH:
	      if (ift)
		sregs->trap = TRAP_UNIMP;
	      break;
	    case SAVE:
	      new_cwp = ((sregs->psr & PSR_CWP) - 1) & PSR_CWP;
	      if (sregs->wim & (1 << new_cwp))
		{
		  sregs->trap = TRAP_WOFL;
		  break;
		}
	      if (rd > 7)
		rdd = &(sregs->r[((new_cwp << 4) + rd) & 0x7f]);
	      *rdd = rs1 + operand2;
	      sregs->psr = (sregs->psr & ~PSR_CWP) | new_cwp;
	      break;
	    case RESTORE:

	      new_cwp = ((sregs->psr & PSR_CWP) + 1) & PSR_CWP;
	      if (sregs->wim & (1 << new_cwp))
		{
		  sregs->trap = TRAP_WUFL;
		  break;
		}
	      if (rd > 7)
		rdd = &(sregs->r[((new_cwp << 4) + rd) & 0x7f]);
	      *rdd = rs1 + operand2;
	      sregs->psr = (sregs->psr & ~PSR_CWP) | new_cwp;
	      break;
	    case RDPSR:
	      if (!(sregs->psr & PSR_S))
		{
		  sregs->trap = TRAP_PRIVI;
		  break;
		}
	      *rdd = sregs->psr;
	      break;
	    case RDY:
	      *rdd = sregs->y;
	      if (cputype == CPU_LEON3)
		{
		  int rs1_is_asr = (sregs->inst >> 14) & 0x1f;
		  if (0 == rs1_is_asr)
		    *rdd = sregs->y;
		  else if (17 == rs1_is_asr)
		    {
		      *rdd = sregs->asr17;
		    }
		}
	      break;
	    case RDWIM:
	      if (!(sregs->psr & PSR_S))
		{
		  sregs->trap = TRAP_PRIVI;
		  break;
		}
	      *rdd = sregs->wim;
	      break;
	    case RDTBR:
	      if (!(sregs->psr & PSR_S))
		{
		  sregs->trap = TRAP_PRIVI;
		  break;
		}
	      *rdd = sregs->tbr;
	      break;
	    case WRPSR:
	      if ((sregs->psr & 0x1f) > 7)
		{
		  sregs->trap = TRAP_UNIMP;
		  break;
		}
	      if (!(sregs->psr & PSR_S))
		{
		  sregs->trap = TRAP_PRIVI;
		  break;
		}
	      sregs->psr = (sregs->psr & 0xff000000) |
		(rs1 ^ operand2) & 0x00f03fff;
	      break;
	    case WRWIM:
	      if (!(sregs->psr & PSR_S))
		{
		  sregs->trap = TRAP_PRIVI;
		  break;
		}
	      sregs->wim = (rs1 ^ operand2) & 0x0ff;
	      break;
	    case WRTBR:
	      if (!(sregs->psr & PSR_S))
		{
		  sregs->trap = TRAP_PRIVI;
		  break;
		}
	      sregs->tbr = (sregs->tbr & 0x00000ff0) |
		((rs1 ^ operand2) & 0xfffff000);
	      break;
	    case WRY:
	      sregs->y = (rs1 ^ operand2);
	      if (cputype == CPU_LEON3)
		{
		  if (17 == rd)
		    {
		      sregs->asr17 &= ~0x0FFFE000;
		      sregs->asr17 |= 0x0FFFE000 & (rs1 ^ operand2);
		    }
		  else if (19 == rd)
		    {
		      pwd_enter (sregs);
		    }
		}
	      break;
	    case JMPL:

#ifdef STAT
	      sregs->nbranch++;
#endif
	      sregs->icnt = T_JMPL;	/* JMPL takes two cycles */
	      if (rs1 & 0x3)
		{
		  sregs->trap = TRAP_UNALI;
		  break;
		}
	      *rdd = sregs->pc;
	      npc = rs1 + operand2;
	      if (!npc)
		sregs->trap = NULL_TRAP;	// halt on null pointer
	      if (ebase.coven)
		{
		  cov_jmp (sregs->pc, npc);
		  cov_exec (pc);	/* delay slot executed */
		}
	      break;
	    case RETT:
	      address = rs1 + operand2;
	      new_cwp = ((sregs->psr & PSR_CWP) + 1) & PSR_CWP;
	      sregs->icnt = T_RETT;	/* RETT takes two cycles */
	      if (sregs->psr & PSR_ET)
		{
		  sregs->trap = TRAP_UNIMP;
		  break;
		}
	      if (!(sregs->psr & PSR_S))
		{
		  sregs->trap = TRAP_PRIVI;
		  break;
		}
	      if (sregs->wim & (1 << new_cwp))
		{
		  sregs->trap = TRAP_WUFL;
		  break;
		}
	      if (address & 0x3)
		{
		  sregs->trap = TRAP_UNALI;
		  break;
		}
	      if (!address)
		sregs->trap = NULL_TRAP;	// halt on null pointer
	      sregs->psr = (sregs->psr & ~PSR_CWP) | new_cwp | PSR_ET;
	      sregs->psr =
		(sregs->psr & ~PSR_S) | ((sregs->psr & PSR_PS) << 1);
	      npc = address;
	      if (ebase.coven)
		{
		  cov_jmp (sregs->pc, npc);
		  cov_exec (pc);	/* delay slot executed */
		}
	      break;

	    default:
	      sregs->trap = TRAP_UNIMP;
	      break;
	    }
	}
      break;
    case 3:			/* Load/store instructions */

      address = rs1 + operand2;

      if (op3 & 4)
	{
	  sregs->icnt = T_ST;	/* Set store instruction count */
	  if (ebase.wpwnum)
	    {
	      if (ebase.wphit = check_wpw (sregs, address, wpmask (op3)))
		{
		  sregs->trap = WPT_TRAP;
		  break;
		}
	    }
#ifdef STAT
	  sregs->nstore++;
#endif
	}
      else
	{
	  sregs->icnt = T_LD;	/* Set load instruction count */
	  if (ebase.wprnum)
	    {
	      if (ebase.wphit = check_wpr (sregs, address, wpmask (op3)))
		{
		  sregs->trap = WPT_TRAP;
		  break;
		}
	    }
#ifdef STAT
	  sregs->nload++;
#endif
	}

      /* Decode load/store instructions */

      switch (op3)
	{
	case LDDA:
	  if (!chk_asi (sregs, &asi, op3))
	    break;
	case LDD:
	  if (address & 0x7)
	    {
	      sregs->trap = TRAP_UNALI;
	      break;
	    }
	  if (rd & 1)
	    {
	      rd &= 0x1e;
	      if (rd > 7)
		rdd = &(sregs->r[(cwp + rd) & 0x7f]);
	      else
		rdd = &(sregs->g[rd]);
	    }
	  mexc = ms->memory_read (address, ddata, &ws);
	  sregs->hold += ws;
	  mexc |= ms->memory_read (address + 4, &ddata[1], &ws);
	  sregs->hold += ws;
	  sregs->icnt = T_LDD;
	  if (mexc)
	    {
	      sregs->trap = TRAP_DEXC;
	    }
	  else
	    {
	      rdd[0] = ddata[0];
	      rdd[1] = ddata[1];
#ifdef STAT
	      sregs->nload++;	/* Double load counts twice */
#endif
	    }
	  break;

	case LDA:
	  if (!chk_asi (sregs, &asi, op3))
	    break;
	  if (address & 0x3)
	    {
	      sregs->trap = TRAP_UNALI;
	      break;
	    }
	  if ((cputype == CPU_LEON3) && (asi == 2))
	    {
	      if (address == 0)
		*rdd = sregs->cache_ctrl;
	      else
		*rdd = 1 << 27;
	      break;
	    }
	case LD:
	  if (address & 0x3)
	    {
	      sregs->trap = TRAP_UNALI;
	      break;
	    }
	  mexc = ms->memory_read (address, &data, &ws);
	  sregs->hold += ws;
	  if (mexc)
	    {
	      sregs->trap = TRAP_DEXC;
	    }
	  else
	    {
	      *rdd = data;
	    }
	  break;
	case LDSTUBA:
	  if (!chk_asi (sregs, &asi, op3))
	    break;
	  /* fall through to LDSTUB */
	case LDSTUB:
	  mexc = ms->memory_read (address & ~3, &data, &ws);
	  sregs->hold += ws;
	  sregs->icnt = T_LDST;
	  if (mexc)
	    {
	      sregs->trap = TRAP_DEXC;
	      break;
	    }
	  data = extract_byte (data, address);
	  *rdd = data;
	  data = 0x0ff;
	  mexc = ms->memory_write (address, &data, 0, &ws);
	  sregs->hold += ws;
	  if (mexc)
	    {
	      sregs->trap = TRAP_DEXC;
	    }
#ifdef STAT
	  sregs->nload++;
#endif
	  break;
	case LDSBA:
	case LDUBA:
	  if (!chk_asi (sregs, &asi, op3))
	    break;
	  /* fall through to LDSB */
	case LDSB:
	case LDUB:
	  mexc = ms->memory_read (address & ~3, &data, &ws);
	  sregs->hold += ws;
	  if (mexc)
	    {
	      sregs->trap = TRAP_DEXC;
	      break;
	    }
	  if (op3 == LDSB)
	    data = extract_byte_signed (data, address);
	  else
	    data = extract_byte (data, address);
	  *rdd = data;
	  break;
	case LDSHA:
	case LDUHA:
	  if (!chk_asi (sregs, &asi, op3))
	    break;
	  /* fall through to LDSB */
	case LDSH:
	case LDUH:
	  if (address & 0x1)
	    {
	      sregs->trap = TRAP_UNALI;
	      break;
	    }
	  mexc = ms->memory_read (address & ~3, &data, &ws);
	  sregs->hold += ws;
	  if (mexc)
	    {
	      sregs->trap = TRAP_DEXC;
	      break;
	    }
	  if (op3 == LDSH)
	    data = extract_short_signed (data, address);
	  else
	    data = extract_short (data, address);
	  *rdd = data;
	  break;
	case LDF:
	  if (!((sregs->psr & PSR_EF) && FP_PRES))
	    {
	      sregs->trap = TRAP_FPDIS;
	      break;
	    }
	  if (address & 0x3)
	    {
	      sregs->trap = TRAP_UNALI;
	      break;
	    }
#ifdef HOST_LITTLE_ENDIAN
	  rd ^= 1;
#endif
	  if (sregs->simtime < sregs->ftime)
	    {
	      if ((sregs->frd == rd) || (sregs->frs1 == rd) ||
		  (sregs->frs2 == rd))
		sregs->fhold += (sregs->ftime - sregs->simtime);
	    }
	  mexc = ms->memory_read (address, &data, &ws);
	  sregs->hold += ws;
	  sregs->flrd = rd;
	  sregs->ltime = sregs->simtime + sregs->icnt + FLSTHOLD +
	    sregs->hold + sregs->fhold;
	  if (mexc)
	    {
	      sregs->trap = TRAP_DEXC;
	    }
	  else
	    {
	      sregs->fsi[rd] = data;
	    }
	  break;
	case LDDF:
	  if (!((sregs->psr & PSR_EF) && FP_PRES))
	    {
	      sregs->trap = TRAP_FPDIS;
	      break;
	    }
	  if (address & 0x7)
	    {
	      sregs->trap = TRAP_UNALI;
	      break;
	    }
	  if (sregs->simtime < sregs->ftime)
	    {
	      if (((sregs->frd >> 1) == (rd >> 1)) ||
		  ((sregs->frs1 >> 1) == (rd >> 1)) ||
		  ((sregs->frs2 >> 1) == (rd >> 1)))
		sregs->fhold += (sregs->ftime - sregs->simtime);
	    }
	  mexc = ms->memory_read (address, ddata, &ws);
	  sregs->hold += ws;
	  mexc |= ms->memory_read (address + 4, &ddata[1], &ws);
	  sregs->hold += ws;
	  sregs->icnt = T_LDD;
	  if (mexc)
	    {
	      sregs->trap = TRAP_DEXC;
	    }
	  else
	    {
#ifdef HOST_LITTLE_ENDIAN
	      rd ^= 1;
#endif
	      sregs->fsi[rd] = ddata[0];
#ifdef STAT
	      sregs->nload++;	/* Double load counts twice */
#endif
	      rd ^= 1;
	      sregs->fsi[rd] = ddata[1];
	      sregs->ltime = sregs->simtime + sregs->icnt + FLSTHOLD +
		sregs->hold + sregs->fhold;
	      rd &= 0x1E;
	      sregs->flrd = rd;
	    }
	  break;
	case LDFSR:
	  if (sregs->simtime < sregs->ftime)
	    {
	      sregs->fhold += (sregs->ftime - sregs->simtime);
	    }
	  if (!((sregs->psr & PSR_EF) && FP_PRES))
	    {
	      sregs->trap = TRAP_FPDIS;
	      break;
	    }
	  if (address & 0x3)
	    {
	      sregs->trap = TRAP_UNALI;
	      break;
	    }
	  mexc = ms->memory_read (address, &data, &ws);
	  sregs->hold += ws;
	  if (mexc)
	    {
	      sregs->trap = TRAP_DEXC;
	    }
	  else
	    {
	      sregs->fsr = (sregs->fsr & 0x7FF000) | (data & ~0x7FF000);
	      set_fsr (sregs->fsr);
	    }
	  break;
	case STFSR:
	  if (!((sregs->psr & PSR_EF) && FP_PRES))
	    {
	      sregs->trap = TRAP_FPDIS;
	      break;
	    }
	  if (address & 0x3)
	    {
	      sregs->trap = TRAP_UNALI;
	      break;
	    }
	  if (sregs->simtime < sregs->ftime)
	    {
	      sregs->fhold += (sregs->ftime - sregs->simtime);
	    }
	  mexc = ms->memory_write (address, &sregs->fsr, 2, &ws);
	  sregs->hold += ws;
	  if (mexc)
	    {
	      sregs->trap = TRAP_DEXC;
	    }
	  break;

	case STA:
	  if (!chk_asi (sregs, &asi, op3))
	    break;
	  if (address & 0x3)
	    {
	      sregs->trap = TRAP_UNALI;
	      break;
	    }
	  if ((cputype == CPU_LEON3) && (asi == 2))
	    {
	      sregs->cache_ctrl = *rdd;
	      break;
	    }
	case ST:
	  if (address & 0x3)
	    {
	      sregs->trap = TRAP_UNALI;
	      break;
	    }
	  mexc = ms->memory_write (address, rdd, 2, &ws);
	  sregs->hold += ws;
	  if (mexc)
	    {
	      sregs->trap = TRAP_DEXC;
	    }
	  break;
	case STBA:
	  if (!chk_asi (sregs, &asi, op3))
	    break;
	  /* fall through to STB */
	case STB:
	  mexc = ms->memory_write (address, rdd, 0, &ws);
	  sregs->hold += ws;
	  if (mexc)
	    {
	      sregs->trap = TRAP_DEXC;
	    }
	  break;
	case STDA:
	  if (!chk_asi (sregs, &asi, op3))
	    break;
	case STD:
	  if (address & 0x7)
	    {
	      sregs->trap = TRAP_UNALI;
	      break;
	    }
	  if (rd & 1)
	    {
	      rd &= 0x1e;
	      if (rd > 7)
		rdd = &(sregs->r[(cwp + rd) & 0x7f]);
	      else
		rdd = &(sregs->g[rd]);
	    }
	  mexc = ms->memory_write (address, rdd, 3, &ws);
	  sregs->hold += ws;
	  sregs->icnt = T_STD;
#ifdef STAT
	  sregs->nstore++;	/* Double store counts twice */
#endif
	  if (mexc)
	    {
	      sregs->trap = TRAP_DEXC;
	      break;
	    }
	  break;
	case STDFQ:
	  if ((sregs->psr & 0x1f) > 7)
	    {
	      sregs->trap = TRAP_UNIMP;
	      break;
	    }
	  if (!((sregs->psr & PSR_EF) && FP_PRES))
	    {
	      sregs->trap = TRAP_FPDIS;
	      break;
	    }
	  if (address & 0x7)
	    {
	      sregs->trap = TRAP_UNALI;
	      break;
	    }
	  if (!(sregs->fsr & FSR_QNE))
	    {
	      sregs->fsr = (sregs->fsr & ~FSR_TT) | FP_SEQ_ERR;
	      break;
	    }
	  rdd = &(sregs->fpq[0]);
	  mexc = ms->memory_write (address, rdd, 3, &ws);
	  sregs->hold += ws;
	  sregs->icnt = T_STD;
#ifdef STAT
	  sregs->nstore++;	/* Double store counts twice */
#endif
	  if (mexc)
	    {
	      sregs->trap = TRAP_DEXC;
	      break;
	    }
	  else
	    {
	      sregs->fsr &= ~FSR_QNE;
	      sregs->fpstate = FP_EXE_MODE;
	    }
	  break;
	case STHA:
	  if (!chk_asi (sregs, &asi, op3))
	    break;
	case STH:
	  if (address & 0x1)
	    {
	      sregs->trap = TRAP_UNALI;
	      break;
	    }
	  mexc = ms->memory_write (address, rdd, 1, &ws);
	  sregs->hold += ws;
	  if (mexc)
	    {
	      sregs->trap = TRAP_DEXC;
	    }
	  break;
	case STF:
	  if (!((sregs->psr & PSR_EF) && FP_PRES))
	    {
	      sregs->trap = TRAP_FPDIS;
	      break;
	    }
	  if (address & 0x3)
	    {
	      sregs->trap = TRAP_UNALI;
	      break;
	    }
	  if (sregs->simtime < sregs->ftime)
	    {
	      if (sregs->frd == rd)
		sregs->fhold += (sregs->ftime - sregs->simtime);
	    }
#ifdef HOST_LITTLE_ENDIAN
	  rd ^= 1;
#endif
	  mexc = ms->memory_write (address, &sregs->fsi[rd], 2, &ws);
	  sregs->hold += ws;
	  if (mexc)
	    {
	      sregs->trap = TRAP_DEXC;
	    }
	  break;
	case STDF:
	  if (!((sregs->psr & PSR_EF) && FP_PRES))
	    {
	      sregs->trap = TRAP_FPDIS;
	      break;
	    }
	  if (address & 0x7)
	    {
	      sregs->trap = TRAP_UNALI;
	      break;
	    }
	  rd &= 0x1E;
	  if (sregs->simtime < sregs->ftime)
	    {
	      if ((sregs->frd == rd) || (sregs->frd + 1 == rd))
		sregs->fhold += (sregs->ftime - sregs->simtime);
	    }
#ifdef HOST_LITTLE_ENDIAN
	  ddata[0] = sregs->fsi[rd ^ 1];
	  ddata[1] = sregs->fsi[rd];
#else
	  ddata[0] = sregs->fsi[rd];
	  ddata[1] = sregs->fsi[rd ^ 1];
#endif
	  mexc = ms->memory_write (address, ddata, 3, &ws);
	  sregs->hold += ws;
	  sregs->icnt = T_STD;
#ifdef STAT
	  sregs->nstore++;	/* Double store counts twice */
#endif
	  if (mexc)
	    {
	      sregs->trap = TRAP_DEXC;
	    }
	  break;
	case SWAPA:
	  if (!chk_asi (sregs, &asi, op3))
	    break;
	case SWAP:
	  if (address & 0x3)
	    {
	      sregs->trap = TRAP_UNALI;
	      break;
	    }
	  mexc = ms->memory_read (address, &data, &ws);
	  sregs->hold += ws;
	  if (mexc)
	    {
	      sregs->trap = TRAP_DEXC;
	      break;
	    }
	  mexc = ms->memory_write (address, rdd, 2, &ws);
	  sregs->hold += ws;
	  sregs->icnt = T_LDST;
	  if (mexc)
	    {
	      sregs->trap = TRAP_DEXC;
	      break;
	    }
	  else
	    *rdd = data;
#ifdef STAT
	  sregs->nload++;
#endif
	  break;
	case CASA:
	  asi = (sregs->inst >> 5) & 0x0ff;
	  address = rs1;
	  if (!((asi == 10) || (asi == 11)))
	    if (!chk_asi (sregs, &asi, op3))
	      break;
	  if (address & 0x3)
	    {
	      sregs->trap = TRAP_UNALI;
	      break;
	    }
	  mexc = ms->memory_read (address, &data, &ws);
	  sregs->hold += ws;
	  if (mexc)
	    {
	      sregs->trap = TRAP_DEXC;
	      break;
	    }
	  if (data == operand2)
	    {
	      mexc = ms->memory_write (address, rdd, 2, &ws);
	      if (mexc)
		{
		  sregs->trap = TRAP_DEXC;
		  break;
		}
	      else
		*rdd = data;
	    }
	  else
	    *rdd = data;
#ifdef STAT
	  sregs->nload++;
#endif
	  break;

	default:
	  sregs->trap = TRAP_UNIMP;
	  break;
	}

#ifdef LOAD_DEL

      if (!(op3 & 4))
	{
	  sregs->ildtime = sregs->simtime + sregs->hold + sregs->icnt;
	  sregs->ildreg = rd;
	  if ((op3 | 0x10) == 0x13)
	    sregs->ildreg |= 1;	/* Double load, odd register loaded
				 * last */
	}
#endif
      break;

    default:
      sregs->trap = TRAP_UNIMP;
      break;
    }
  sregs->g[0] = 0;
  if (!sregs->trap)
    {
      sregs->pc = pc;
      sregs->npc = npc;
      if (annul)
	{
	  sregs->pc = sregs->npc;
	  sregs->npc = sregs->npc + 4;
	  sregs->icnt += 1;
	}
    }
  return 0;
}


#define FABSs	0x09
#define FADDs	0x41
#define FADDd	0x42
#define FCMPs	0x51
#define FCMPd	0x52
#define FCMPEs	0x55
#define FCMPEd	0x56
#define FDIVs	0x4D
#define FDIVd	0x4E
#define FMOVs	0x01
#define FMULs	0x49
#define FMULd	0x4A
#define FsMULd	0x69
#define FNEGs	0x05
#define FSQRTs	0x29
#define FSQRTd	0x2A
#define FSUBs	0x45
#define FSUBd	0x46
#define FdTOi	0xD2
#define FdTOs	0xC6
#define FiTOs	0xC4
#define FiTOd	0xC8
#define FsTOi	0xD1
#define FsTOd	0xC9


static int
fpexec (op3, rd, rs1, rs2, sregs)
     uint32 op3, rd, rs1, rs2;
     struct pstate *sregs;
{
  uint32 opf, tem, accex;
  int32 fcc;
  uint32 ldadj;

  if (sregs->fpstate == FP_EXC_MODE)
    {
      sregs->fsr = (sregs->fsr & ~FSR_TT) | FP_SEQ_ERR;
      sregs->fpstate = FP_EXC_PE;
      return 0;
    }
  if (sregs->fpstate == FP_EXC_PE)
    {
      sregs->fpstate = FP_EXC_MODE;
      return TRAP_FPEXC;
    }
  opf = (sregs->inst >> 5) & 0x1ff;

  /* Store float registers in host order and swap reg address */
#ifdef HOST_LITTLE_ENDIAN
  rs1 ^= 1;
  rs2 ^= 1;
  rd ^= 1;
#endif

  /*
   * Check if we already have an FPop in the pipe. If so, halt until it is
   * finished by incrementing fhold with the remaining execution time
   */

  if (sregs->simtime < sregs->ftime)
    {
      sregs->fhold = (sregs->ftime - sregs->simtime);
    }
  else
    {
      sregs->fhold = 0;

      /* Check load dependencies. */

      if (sregs->simtime < sregs->ltime)
	{

	  /* Don't check rs1 if single operand instructions */

	  if (((opf >> 6) == 0) || ((opf >> 6) == 3))
	    rs1 = 32;

	  /* Adjust for double floats */

	  ldadj = opf & 1;
	  if (!
	      (((sregs->flrd - rs1) >> ldadj)
	       && ((sregs->flrd - rs2) >> ldadj)))
	    sregs->fhold++;
	}
    }

  sregs->finst++;

  sregs->frs1 = rs1;		/* Store src and dst for dependecy check */
  sregs->frs2 = rs2;
  sregs->frd = rd;

  sregs->ftime = sregs->simtime + sregs->hold + sregs->fhold;

  clear_accex ();

  switch (opf)
    {
    case FABSs:
      sregs->fs[rd] = fabs (sregs->fs[rs2]);
      sregs->ftime += T_FABSs;
      sregs->frs1 = 32;		/* rs1 ignored */
      break;
    case FADDs:
      sregs->fs[rd] = sregs->fs[rs1] + sregs->fs[rs2];
      sregs->ftime += T_FADDs;
      break;
    case FADDd:
      sregs->fd[rd >> 1] = sregs->fd[rs1 >> 1] + sregs->fd[rs2 >> 1];
      sregs->ftime += T_FADDd;
      break;
    case FCMPs:
    case FCMPEs:
      if (sregs->fs[rs1] == sregs->fs[rs2])
	fcc = 3;
      else if (sregs->fs[rs1] < sregs->fs[rs2])
	fcc = 2;
      else if (sregs->fs[rs1] > sregs->fs[rs2])
	fcc = 1;
      else
	fcc = 0;
      sregs->fsr |= 0x0C00;
      sregs->fsr &= ~(fcc << 10);
      sregs->ftime += T_FCMPs;
      sregs->frd = 32;		/* rd ignored */
      if ((fcc == 0) && (opf == FCMPEs))
	{
	  sregs->fpstate = FP_EXC_PE;
	  sregs->fsr = (sregs->fsr & ~0x1C000) | (1 << 14);
	}
      break;
    case FCMPd:
    case FCMPEd:
      if (sregs->fd[rs1 >> 1] == sregs->fd[rs2 >> 1])
	fcc = 3;
      else if (sregs->fd[rs1 >> 1] < sregs->fd[rs2 >> 1])
	fcc = 2;
      else if (sregs->fd[rs1 >> 1] > sregs->fd[rs2 >> 1])
	fcc = 1;
      else
	fcc = 0;
      sregs->fsr |= 0x0C00;
      sregs->fsr &= ~(fcc << 10);
      sregs->ftime += T_FCMPd;
      sregs->frd = 32;		/* rd ignored */
      if ((fcc == 0) && (opf == FCMPEd))
	{
	  sregs->fpstate = FP_EXC_PE;
	  sregs->fsr = (sregs->fsr & ~FSR_TT) | FP_IEEE;
	}
      break;
    case FDIVs:
      sregs->fs[rd] = sregs->fs[rs1] / sregs->fs[rs2];
      sregs->ftime += T_FDIVs;
      break;
    case FDIVd:
      sregs->fd[rd >> 1] = sregs->fd[rs1 >> 1] / sregs->fd[rs2 >> 1];
      sregs->ftime += T_FDIVd;
      break;
    case FMOVs:
      sregs->fsi[rd] = sregs->fsi[rs2];
      sregs->ftime += T_FMOVs;
      sregs->frs1 = 32;		/* rs1 ignored */
      break;
    case FMULs:
      sregs->fs[rd] = sregs->fs[rs1] * sregs->fs[rs2];
      sregs->ftime += T_FMULs;
      break;
    case FsMULd:
      if (cputype == CPU_LEON3)
	{			/* FSMULD only supported for LEON3 */
	  sregs->fd[rd >> 1] =
	    (double) sregs->fs[rs1] * (double) sregs->fs[rs2];
	  sregs->ftime += T_FMULd;
	}
      else
	{
	  sregs->fsr = (sregs->fsr & ~FSR_TT) | FP_UNIMP;
	  sregs->fpstate = FP_EXC_PE;
	}
      break;
    case FMULd:
      sregs->fd[rd >> 1] = sregs->fd[rs1 >> 1] * sregs->fd[rs2 >> 1];
      sregs->ftime += T_FMULd;
      break;
    case FNEGs:
      sregs->fs[rd] = -sregs->fs[rs2];
      sregs->ftime += T_FNEGs;
      sregs->frs1 = 32;		/* rs1 ignored */
      break;
    case FSQRTs:
      if (sregs->fs[rs2] < 0.0)
	{
	  sregs->fpstate = FP_EXC_PE;
	  sregs->fsr = (sregs->fsr & ~FSR_TT) | FP_IEEE;
	  sregs->fsr = (sregs->fsr & 0x1f) | 0x10;
	  break;
	}
      sregs->fs[rd] = sqrtf (sregs->fs[rs2]);
      sregs->ftime += T_FSQRTs;
      sregs->frs1 = 32;		/* rs1 ignored */
      break;
    case FSQRTd:
      if (sregs->fd[rs2 >> 1] < 0.0)
	{
	  sregs->fpstate = FP_EXC_PE;
	  sregs->fsr = (sregs->fsr & ~FSR_TT) | FP_IEEE;
	  sregs->fsr = (sregs->fsr & 0x1f) | 0x10;
	  break;
	}
      sregs->fd[rd >> 1] = sqrt (sregs->fd[rs2 >> 1]);
      sregs->ftime += T_FSQRTd;
      sregs->frs1 = 32;		/* rs1 ignored */
      break;
    case FSUBs:
      sregs->fs[rd] = sregs->fs[rs1] - sregs->fs[rs2];
      sregs->ftime += T_FSUBs;
      break;
    case FSUBd:
      sregs->fd[rd >> 1] = sregs->fd[rs1 >> 1] - sregs->fd[rs2 >> 1];
      sregs->ftime += T_FSUBd;
      break;
    case FdTOi:
      sregs->fsi[rd] = (int) sregs->fd[rs2 >> 1];
      sregs->ftime += T_FdTOi;
      sregs->frs1 = 32;		/* rs1 ignored */
      break;
    case FdTOs:
      sregs->fs[rd] = (float32) sregs->fd[rs2 >> 1];
      sregs->ftime += T_FdTOs;
      sregs->frs1 = 32;		/* rs1 ignored */
      break;
    case FiTOs:
      sregs->fs[rd] = (float32) sregs->fsi[rs2];
      sregs->ftime += T_FiTOs;
      sregs->frs1 = 32;		/* rs1 ignored */
      break;
    case FiTOd:
      sregs->fd[rd >> 1] = (float64) sregs->fsi[rs2];
      sregs->ftime += T_FiTOd;
      sregs->frs1 = 32;		/* rs1 ignored */
      break;
    case FsTOi:
      sregs->fsi[rd] = (int) sregs->fs[rs2];
      sregs->ftime += T_FsTOi;
      sregs->frs1 = 32;		/* rs1 ignored */
      break;
    case FsTOd:
      sregs->fd[rd >> 1] = sregs->fs[rs2];
      sregs->ftime += T_FsTOd;
      sregs->frs1 = 32;		/* rs1 ignored */
      break;

    default:
      sregs->fsr = (sregs->fsr & ~FSR_TT) | FP_UNIMP;
      sregs->fpstate = FP_EXC_PE;
    }

#ifdef ERRINJ
  if (errftt)
    {
      sregs->fsr = (sregs->fsr & ~FSR_TT) | (errftt << 14);
      sregs->fpstate = FP_EXC_PE;
      if (sis_verbose)
	printf ("Inserted fpu error %X\n", errftt);
      errftt = 0;
    }
#endif

  accex = get_accex ();

  if (sregs->fpstate == FP_EXC_PE)
    {
      sregs->fpq[0] = sregs->pc;
      sregs->fpq[1] = sregs->inst;
      sregs->fsr |= FSR_QNE;
    }
  else
    {
      tem = (sregs->fsr >> 23) & 0x1f;
      if (tem & accex)
	{
	  sregs->fpstate = FP_EXC_PE;
	  sregs->fsr = (sregs->fsr & ~FSR_TT) | FP_IEEE;
	  sregs->fsr = ((sregs->fsr & ~0x1f) | accex);
	}
      else
	{
	  sregs->fsr = ((((sregs->fsr >> 5) | accex) << 5) | accex);
	}
      if (sregs->fpstate == FP_EXC_PE)
	{
	  sregs->fpq[0] = sregs->pc;
	  sregs->fpq[1] = sregs->inst;
	  sregs->fsr |= FSR_QNE;
	}
    }
  clear_accex ();

  return 0;


}

static int
sparc_execute_trap (sregs)
     struct pstate *sregs;
{
  int32 cwp;

  if (sregs->trap >= 256)
    {
      switch (sregs->trap)
	{
	case 256:
	  sregs->pc = 0;
	  sregs->npc = 4;
	  sregs->trap = 0;
	  break;
	case ERROR_TRAP:
	  return (ERROR);
	case WPT_TRAP:
	  return (WPT_HIT);
	case NULL_TRAP:
	  return (NULL_HIT);
	}
    }
  else
    {

      if ((sregs->psr & PSR_ET) == 0)
	return ERROR;
      if ((sregs->trap > 16) && (sregs->trap < 32))
	sregs->intack (sregs->trap - 16, sregs->cpu);

      sregs->tbr = (sregs->tbr & 0xfffff000) | (sregs->trap << 4);
      sregs->trap = 0;
      sregs->psr &= ~PSR_ET;
      sregs->psr |= ((sregs->psr & PSR_S) >> 1);
      sregs->psr =
	(((sregs->psr & PSR_CWP) - 1) & 0x7) | (sregs->psr & ~PSR_CWP);
      cwp = ((sregs->psr & PSR_CWP) << 4);
      sregs->r[(cwp + 17) & 0x7f] = sregs->pc;
      sregs->r[(cwp + 18) & 0x7f] = sregs->npc;
      sregs->psr |= PSR_S;
      if (ebase.coven)
	cov_jmp (sregs->pc, sregs->tbr);
      sregs->pc = sregs->tbr;
      sregs->npc = sregs->tbr + 4;

      if (0 != (1 & (sregs->asr17 >> 13)))
	{
	  /* single vector trapping! */
	  sregs->pc = sregs->tbr & 0xfffff000;
	  sregs->npc = sregs->pc + 4;
	}

      /* Increase simulator time */
      sregs->icnt = TRAP_C;

    }


  return 0;

}

static int
sparc_check_interrupts (sregs)
     struct pstate *sregs;
{
  if ((ext_irl[sregs->cpu]) && (sregs->psr & PSR_ET) &&
      ((ext_irl[sregs->cpu] == 15)
       || (ext_irl[sregs->cpu] > (int) ((sregs->psr & PSR_PIL) >> 8))))
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
sparc_disp_regs (struct pstate *sregs, int cwp)
{

  int i;

  cwp = ((cwp & 0x7) << 4);
  printf ("\n\t  INS       LOCALS      OUTS     GLOBALS\n");
  for (i = 0; i < 8; i++)
    {
      printf ("   %d:  %08X   %08X   %08X   %08X\n", i,
	      sregs->r[(cwp + i + 24) & 0x7f],
	      sregs->r[(cwp + i + 16) & 0x7f], sregs->r[(cwp + i + 8) & 0x7f],
	      sregs->g[i]);
    }
}

static void
sparc_display_registers (struct pstate *sregs)
{
  sparc_disp_regs(sregs, sregs->psr);
}

static void
sparc_display_ctrl ( struct pstate *sregs)
{

  uint32 i;

  printf ("\n psr: %08X   wim: %08X   tbr: %08X   y: %08X\n",
	  sregs->psr, sregs->wim, sregs->tbr, sregs->y);
  ms->sis_memory_read (sregs->pc, (char *) &i, 4);
  printf ("\n  pc: %08X = %08X    ", sregs->pc, i);
  print_insn_sis (sregs->pc);
  ms->sis_memory_read (sregs->npc, (char *) &i, 4);
  printf ("\n npc: %08X = %08X    ", sregs->npc, i);
  print_insn_sis (sregs->npc);
  if (sregs->err_mode)
    printf ("\n IU in error mode");
  else if (sregs->pwd_mode)
    printf ("\n IU in power-down mode");
  printf ("\n\n");
}

static void
sparc_display_special (struct pstate *sregs)
{
  printf ("\n cache ctrl  :   %08X\n asr17       :   %08X\n\n",
	  sregs->cache_ctrl, sregs->asr17);
}

static void
sparc_set_regi (sregs, reg, rval)
     struct pstate *sregs;
     int32 reg;
     uint32 rval;
{
  uint32 cwp;

  cwp = ((sregs->psr & 0x7) << 4);
  if ((reg > 0) && (reg < 8))
    {
      sregs->g[reg] = rval;
    }
  else if ((reg >= 8) && (reg < 32))
    {
      sregs->r[(cwp + reg) & 0x7f] = rval;
    }
  else if ((reg >= 32) && (reg < 64))
    {
#ifdef HOST_LITTLE_ENDIAN
      reg ^= 1;
#endif
      sregs->fsi[reg - 32] = rval;
    }
  else
    {
      switch (reg)
	{
	case 64:
	  sregs->y = rval;
	  break;
	case 65:
	  sregs->psr = rval;
	  break;
	case 66:
	  sregs->wim = rval;
	  break;
	case 67:
	  sregs->tbr = rval;
	  break;
	case 68:
	  sregs->pc = rval;
	  last_load_addr = rval;
	  break;
	case 69:
	  sregs->npc = rval;
	  break;
	case 70:
	  sregs->fsr = rval;
	  set_fsr (rval);
	  break;
	default:
	  break;
	}
    }
}

static void
sparc_get_regi (struct pstate *sregs, int32 reg, char *buf, int length)
{
  uint32 cwp;
  uint32 rval = 0;

  cwp = ((sregs->psr & 0x7) << 4);
  if ((reg >= 0) && (reg < 8))
    {
      rval = sregs->g[reg];
    }
  else if ((reg >= 8) && (reg < 32))
    {
      rval = sregs->r[(cwp + reg) & 0x7f];
    }
  else if ((reg >= 32) && (reg < 64))
    {
#ifdef HOST_LITTLE_ENDIAN
      reg ^= 1;
#endif
      rval = sregs->fsi[reg - 32];
    }
  else
    {
      switch (reg)
	{
	case 64:
	  rval = sregs->y;
	  break;
	case 65:
	  rval = sregs->psr;
	  break;
	case 66:
	  rval = sregs->wim;
	  break;
	case 67:
	  rval = sregs->tbr;
	  break;
	case 68:
	  rval = sregs->pc;
	  break;
	case 69:
	  rval = sregs->npc;
	  break;
	case 70:
	  rval = sregs->fsr;
	  break;
	default:
	  break;
	}
    }
  buf[0] = (rval >> 24) & 0x0ff;
  buf[1] = (rval >> 16) & 0x0ff;
  buf[2] = (rval >> 8) & 0x0ff;
  buf[3] = rval & 0x0ff;
}

void
sparc_set_rega (struct pstate *sregs, char *reg, uint32 rval)
{
  uint32 cwp;
  int32 err = 0;

  cwp = ((sregs->psr & 0x7) << 4);
  if (strcmp (reg, "psr") == 0)
    sregs->psr = (rval = (rval & 0x00f03fff));
  else if (strcmp (reg, "tbr") == 0)
    sregs->tbr = (rval = (rval & 0xfffffff0));
  else if (strcmp (reg, "wim") == 0)
    sregs->wim = (rval = (rval & 0x0ff));
  else if (strcmp (reg, "y") == 0)
    sregs->y = rval;
  else if (strcmp (reg, "pc") == 0)
    sregs->pc = rval;
  else if (strcmp (reg, "npc") == 0)
    sregs->npc = rval;
  else if (strcmp (reg, "fsr") == 0)
    {
      sregs->fsr = rval;
      set_fsr (rval);
    }
  else if (strcmp (reg, "g0") == 0)
    err = 2;
  else if (strcmp (reg, "g1") == 0)
    sregs->g[1] = rval;
  else if (strcmp (reg, "g2") == 0)
    sregs->g[2] = rval;
  else if (strcmp (reg, "g3") == 0)
    sregs->g[3] = rval;
  else if (strcmp (reg, "g4") == 0)
    sregs->g[4] = rval;
  else if (strcmp (reg, "g5") == 0)
    sregs->g[5] = rval;
  else if (strcmp (reg, "g6") == 0)
    sregs->g[6] = rval;
  else if (strcmp (reg, "g7") == 0)
    sregs->g[7] = rval;
  else if (strcmp (reg, "o0") == 0)
    sregs->r[(cwp + 8) & 0x7f] = rval;
  else if (strcmp (reg, "o1") == 0)
    sregs->r[(cwp + 9) & 0x7f] = rval;
  else if (strcmp (reg, "o2") == 0)
    sregs->r[(cwp + 10) & 0x7f] = rval;
  else if (strcmp (reg, "o3") == 0)
    sregs->r[(cwp + 11) & 0x7f] = rval;
  else if (strcmp (reg, "o4") == 0)
    sregs->r[(cwp + 12) & 0x7f] = rval;
  else if (strcmp (reg, "o5") == 0)
    sregs->r[(cwp + 13) & 0x7f] = rval;
  else if (strcmp (reg, "o6") == 0)
    sregs->r[(cwp + 14) & 0x7f] = rval;
  else if (strcmp (reg, "o7") == 0)
    sregs->r[(cwp + 15) & 0x7f] = rval;
  else if (strcmp (reg, "l0") == 0)
    sregs->r[(cwp + 16) & 0x7f] = rval;
  else if (strcmp (reg, "l1") == 0)
    sregs->r[(cwp + 17) & 0x7f] = rval;
  else if (strcmp (reg, "l2") == 0)
    sregs->r[(cwp + 18) & 0x7f] = rval;
  else if (strcmp (reg, "l3") == 0)
    sregs->r[(cwp + 19) & 0x7f] = rval;
  else if (strcmp (reg, "l4") == 0)
    sregs->r[(cwp + 20) & 0x7f] = rval;
  else if (strcmp (reg, "l5") == 0)
    sregs->r[(cwp + 21) & 0x7f] = rval;
  else if (strcmp (reg, "l6") == 0)
    sregs->r[(cwp + 22) & 0x7f] = rval;
  else if (strcmp (reg, "l7") == 0)
    sregs->r[(cwp + 23) & 0x7f] = rval;
  else if (strcmp (reg, "i0") == 0)
    sregs->r[(cwp + 24) & 0x7f] = rval;
  else if (strcmp (reg, "i1") == 0)
    sregs->r[(cwp + 25) & 0x7f] = rval;
  else if (strcmp (reg, "i2") == 0)
    sregs->r[(cwp + 26) & 0x7f] = rval;
  else if (strcmp (reg, "i3") == 0)
    sregs->r[(cwp + 27) & 0x7f] = rval;
  else if (strcmp (reg, "i4") == 0)
    sregs->r[(cwp + 28) & 0x7f] = rval;
  else if (strcmp (reg, "i5") == 0)
    sregs->r[(cwp + 29) & 0x7f] = rval;
  else if (strcmp (reg, "i6") == 0)
    sregs->r[(cwp + 30) & 0x7f] = rval;
  else if (strcmp (reg, "i7") == 0)
    sregs->r[(cwp + 31) & 0x7f] = rval;
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
      printf ("cannot set g0\n");
      break;
    default:
      break;
    }

}

static void
sparc_set_register (struct pstate *sregs, char *reg, uint32 rval, uint32 addr)
{
  if (reg == NULL)
    sparc_set_regi(sregs, addr, rval);
  else
    sparc_set_rega(sregs, reg, rval);
}

static void
disp_reg (sregs, reg)
     struct pstate *sregs;
     char *reg;
{
  if (strncmp (reg, "w", 1) == 0)
    sparc_disp_regs (sregs, VAL (&reg[1]));
}

/* Flush all register windows out to the stack.  Starting after the invalid
   window, flush all windows up to, and including the current window.  This
   allows GDB to do backtraces and look at local variables for frames that
   are still in the register windows.  Note that strictly speaking, this
   behavior is *wrong* for several reasons.  First, it doesn't use the window
   overflow handlers.  It therefore assumes standard frame layouts and window
   handling policies.  Second, it changes system state behind the back of the
   target program.  I expect this to mainly pose problems when debugging trap
   handlers.
*/

void
flush_windows (struct pstate *sregs)
{
  int invwin;
  int cwp;
  int win;
  int ws;

  /* Keep current window handy */

  cwp = sregs->psr & PSR_CWP;

  /* Calculate the invalid window from the wim. */

  for (invwin = 0; invwin <= PSR_CWP; invwin++)
    if ((sregs->wim >> invwin) & 1)
      break;

  /* Start saving with the window after the invalid window. */

  invwin = (invwin - 1) & PSR_CWP;

  for (win = invwin;; win = (win - 1) & PSR_CWP)
    {
      uint32 sp;
      int i;

      sp = sregs->r[(win * 16 + 14) & 0x7f];
#if 1
      if (sis_verbose > 2)
	{
	  uint32 fp = sregs->r[(win * 16 + 30) & 0x7f];
	  printf ("flush_window: win %d, sp %x, fp %x\n", win, sp, fp);
	}
#endif

      for (i = 0; i < 16; i++)
	ms->memory_write (sp + 4 * i, &sregs->r[(win * 16 + 16 + i) & 0x7f],
			  2, &ws);

      if (win == cwp)
	break;
    }
}

static void
sparc_display_fpu (struct pstate *sregs)
{
  int i, t;

  printf ("\n fsr: %08X\n\n", sregs->fsr);

  for (i = 0; i < 32; i++)
    {
#ifdef HOST_LITTLE_ENDIAN
      t = i ^ 1;
#else
      t = i;
#endif
      printf (" f%02d  %08x  %14e  ", i, sregs->fsi[t], sregs->fs[t]);
      if (!(i & 1))
	printf ("%14e\n", sregs->fd[i >> 1]);
      else
	printf ("\n");
    }
  printf ("\n");
}

static int
sparc_gdb_get_reg (char *buf)
{
  int i;

  for (i = 0; i < 72; i++)
    sparc_get_regi (&sregs[cpu], i, &buf[i * 4], 4);

  return (72 * 4);
}

/* op decoding */
#define FMT2 0
#define CALL 1
#define FMT3 2
#define LDST 3

/* -- OP2 codes (INST[31..30]) */
#define UNIMP    0
#define FBFCC    6
#define CBCCC    7

/*-- OP3 codes (INST[24..19]) */
#define IADD     0
#define ISUB     4
#define ANDN     5
#define ORN      6
#define ANDCC    0x11
#define ORCC     0x12
#define XORCC    0x13
#define ANDNCC   0x15
#define ORNCC    0x16
#define XNORCC   0x17
#define MULSCC   0x24
#define FPOP1    0x34
#define FPOP2    0x35
#define CPOP1    0x36
#define CPOP2    0x37
#define UMAC	 0x3e
#define SMAC	 0x3f

#define STC      0x34
#define STDCQ    0x36
#define STDC     0x37

/* -- BICC codes */
#define BA 8

/* -- FPOP1 */
#define FITOS    0xc4
#define FITOD    0xc8
#define FSTOI    0xd1
#define FDTOI    0xd2
#define FSTOD    0xc9
#define FDTOS    0xc6
#define FMOVS    0x1
#define FNEGS    0x5
#define FABSS    0x9
#define FSQRTS   0x29
#define FSQRTD   0x2a
#define FADDS    0x41
#define FADDD    0x42
#define FSUBS    0x45
#define FSUBD    0x46
#define FMULS    0x49
#define FMULD    0x4a
#define FSMULD   0x69
#define FDIVS    0x4d
#define FDIVD    0x4e

/* -- FPOP2 */
#define FCMPS    0x51
#define FCMPD    0x52
#define FCMPES   0x55
#define FCMPED   0x56

struct insn_type
{
  unsigned int op, op2, op3, opf, cond, annul;
  int rs1, rs2, rd, i, simm, asi, insn;
};


static void
regdec (char *st, int r)
{
  char regst[8], ch;

  if (r == 0x1e)
    strcat (st, "%fp");
  else if (r == 0xe)
    strcat (st, "%sp");
  else
    {
      switch ((r >> 3) & 3)
	{
	case 0:
	  ch = 'g';
	  break;
	case 1:
	  ch = 'o';
	  break;
	case 2:
	  ch = 'l';
	  break;
	case 3:
	  ch = 'i';
	}
      sprintf (regst, "%%%c%d", ch, r & 7);
      strcat (st, regst);
    }
}

static void
simm13dec (char *st, struct insn_type insn, int hex, int merge)
{
  char tmp[32];

  if (insn.i)
    {
      if (!hex)
	{
	  if (merge)
	    {
	      if (!insn.rs1)
		sprintf (tmp, "%d", insn.simm);
	      else
		sprintf (tmp, "%+d", insn.simm);
	    }
	  else
	    {
	      if (!insn.rs1)
		sprintf (tmp, "%d", insn.simm);
	      else
		sprintf (tmp, ", %d", insn.simm);
	    }
	}
      else
	{
	  if (merge)
	    {
	      if (insn.simm < 0)
		{
		  insn.simm = -insn.simm;
		  if (!insn.rs1)
		    sprintf (tmp, "-0x%x", insn.simm);
		  else
		    sprintf (tmp, " - 0x%x", insn.simm);
		}
	      else
		{
		  if (!insn.rs1)
		    sprintf (tmp, "0x%x", insn.simm);
		  else
		    sprintf (tmp, " + 0x%x", insn.simm);
		}
	    }
	  else
	    {
	      if (!insn.rs1)
		sprintf (tmp, "0x%x", insn.simm);
	      else
		sprintf (tmp, ", 0x%x", insn.simm);
	    }
	}
      strcat (st, tmp);
    }
}

static void
fregdec (char *st, int reg)
{
  char tmp[8];
  sprintf (tmp, "%%f%d", reg);
  strcat (st, tmp);
}

static void
cregdec (char *st, int reg)
{
  char tmp[8];
  sprintf (tmp, "%%c%d", reg);
  strcat (st, tmp);
}

static void
freg2 (char *st, struct insn_type insn)
{
  fregdec (st, insn.rs2);
  strcat (st, ", ");
  fregdec (st, insn.rd);
}

static void
freg3 (char *st, struct insn_type insn)
{
  fregdec (st, insn.rs1);
  strcat (st, ", ");
  fregdec (st, insn.rs2);
  strcat (st, ", ");
  fregdec (st, insn.rd);
}

static void
creg3 (char *st, struct insn_type insn)
{
  cregdec (st, insn.rs1);
  strcat (st, ", ");
  cregdec (st, insn.rs2);
  strcat (st, ", ");
  cregdec (st, insn.rd);
}

static void
fregc (char *st, struct insn_type insn)
{
  fregdec (st, insn.rs1);
  strcat (st, ", ");
  fregdec (st, insn.rs2);
}

static void
regimm (char *st, struct insn_type insn, int hex, int merge)
{

  if (!insn.i)
    {
      if (!insn.rs1)
	{
	  if (!insn.rs2)
	    {
	      strcat (st, "0");
	    }
	  else
	    {
	      regdec (st, insn.rs2);
	    }
	}
      else
	{
	  if (!insn.rs2)
	    {
	      regdec (st, insn.rs1);
	    }
	  else if (merge)
	    {
	      regdec (st, insn.rs1);
	      strcat (st, " + ");
	      regdec (st, insn.rs2);
	    }
	  else
	    {
	      regdec (st, insn.rs1);
	      strcat (st, ", ");
	      regdec (st, insn.rs2);
	    }
	}
    }
  else
    {
      if (!insn.rs1)
	{
	  simm13dec (st, insn, hex, merge);
	}
      else if (!insn.simm)
	{
	  regdec (st, insn.rs1);
	}
      else
	{
	  regdec (st, insn.rs1);
	  simm13dec (st, insn, hex, merge);
	}
    }
}

static void
regres (char *st, struct insn_type insn, int hex)
{
  regimm (st, insn, hex, 0);
  strcat (st, ", ");
  regdec (st, insn.rd);
}

static char brtbl[16][4] =
  { "n", "e", "le", "l", "lue", "cs", "neg", "vs", "a", "ne", "g", "ge", "gu",
"cc", "pos", "vc" };

static char fbrtbl[16][4] =
  { "n", "ne", "lg", "ul", "l", "ug", "g", "u", "a", "e", "ue", "ge", "uge",
"le", "ule", "o" };

char *
branchop (int insn)
{
  return brtbl[((insn >> 25) & 0x0f)];
}

char *
fbranchop (int insn)
{
  return fbrtbl[((insn >> 25) & 0x0f)];
}

static void
adec (char *st, struct insn_type insn)
{
  strcat (st, "[");
  regimm (st, insn, 1, 1);
  strcat (st, "]");
}

static void
adeca (char *st, struct insn_type insn)
{
  char tmp[32];

  adec (st, insn);
  sprintf (tmp, " 0x%x", insn.asi);
  strcat (st, tmp);
}

static void
ldparf (char *st, struct insn_type insn)
{
  adec (st, insn);
  strcat (st, ", ");
  fregdec (st, insn.rd);
}

static void
ldpar (char *st, struct insn_type insn)
{
  adec (st, insn);
  strcat (st, ", ");
  regdec (st, insn.rd);
}

static void
ldpara (char *st, struct insn_type insn)
{
  adeca (st, insn);
  strcat (st, ", ");
  regdec (st, insn.rd);
}

static void
stparx (char *st, struct insn_type insn)
{
  if (insn.rd)
    {
      regdec (st, insn.rd);
      strcat (st, ", ");
    }
  adec (st, insn);
}

static void
stparf (char *st, struct insn_type insn)
{
  fregdec (st, insn.rd);
  strcat (st, ", ");
  adec (st, insn);
}

static void
stparfq (char *st, struct insn_type insn)
{
  strcat (st, "fq, ");
  adec (st, insn);
}

static void
stparc (char *st, struct insn_type insn)
{
  cregdec (st, insn.rd);
  strcat (st, ", ");
  adec (st, insn);
}

static void
stparcq (char *st, struct insn_type insn)
{
  strcat (st, "cq, ");
  adec (st, insn);
}

static void
stpar (char *st, struct insn_type insn)
{
  regdec (st, insn.rd);
  strcat (st, ", ");
  adec (st, insn);
}

static void
stpara (char *st, struct insn_type insn)
{
  regdec (st, insn.rd);
  strcat (st, ", ");
  adeca (st, insn);
}

static void
sparc_disas (char *st, unsigned pc, unsigned int inst)
{
  struct insn_type insn;
  unsigned int addr;
  char tmp[32];

  insn.insn = inst;
  insn.op = (inst >> 30);
  insn.op2 = (inst >> 22) & 7;

  insn.op3 = (inst >> 19) & 0x3f;
  insn.opf = (inst >> 5) & 0x1ff;
  insn.cond = (inst >> 25) & 0x0f;
  insn.annul = (inst >> 29) & 1;
  insn.rs1 = (inst >> 14) & 0x1f;
  insn.rs2 = inst & 0x1f;
  insn.rd = (inst >> 25) & 0x1f;
  insn.i = (inst >> 13) & 1;
  insn.simm = (insn.insn << 19) >> 19;
  insn.asi = (inst >> 5) & 0xff;

  switch (insn.op)
    {
    case CALL:
      addr = pc + (inst << 2);
      sprintf (st, "call  0x%08x", addr);
      break;
    case FMT2:
      switch (insn.op2)
	{
	case UNIMP:
	  sprintf (st, "unimp");
	  break;
	case SETHI:
	  if (!insn.rd)
	    sprintf (st, "nop");
	  else
	    {
	      sprintf (st, "sethi  %%hi(0x%x), ", (inst << 10));
	      regdec (st, insn.rd);
	    }
	  break;
	case BICC:
	case FBFCC:
	  insn.simm = inst << 10;
	  insn.simm >>= 8;
	  addr = pc + insn.simm;
	  if (insn.op2 == BICC)
	    {
	      if ((inst >> 29) & 1)
		{
		  sprintf (st, "b%s,a  0x%08x", branchop (inst), addr);
		}
	      else
		{
		  sprintf (st, "b%s  0x%08x", branchop (inst), addr);
		}
	    }
	  else
	    {
	      if ((inst >> 29) & 1)
		{
		  sprintf (st, "fb%s,a  0x%08x", fbranchop (inst), addr);
		}
	      else
		{
		  sprintf (st, "fb%s  0x%08x", fbranchop (inst), addr);
		}
	    }
	  break;
	default:
	  sprintf (st, "unknown opcode: 0x%08x", inst);
	}
      break;
    case FMT3:
      switch (insn.op3)
	{
	case IAND:
	  strcpy (st, "and  ");
	  regres (st, insn, 1);
	  break;
	case IADD:
	  strcpy (st, "add  ");
	  regres (st, insn, 0);
	  break;
	case IOR:
	  if ((!insn.i) && (!insn.rs1) && (!insn.rs2))
	    {
	      strcpy (st, "clr  ");
	      regdec (st, insn.rd);
	    }
	  else if (((insn.i == '1') && (!insn.simm)) || (!insn.rs1))
	    {
	      strcpy (st, "mov  ");
	      regres (st, insn, 0);
	    }
	  else
	    {
	      strcpy (st, "or  ");
	      regres (st, insn, 1);
	    }
	  break;
	case IXOR:
	  strcpy (st, "xor  ");
	  regres (st, insn, 1);
	  break;
	case ISUB:
	  strcpy (st, "sub  ");
	  regres (st, insn, 0);
	  break;
	case ANDN:
	  strcpy (st, "andn  ");
	  regres (st, insn, 1);
	  break;
	case ORN:
	  strcpy (st, "orn  ");
	  regres (st, insn, 1);
	  break;
	case IXNOR:
	  if ((!insn.i) && ((insn.rs1 == insn.rd) || (!insn.rs2)))
	    {
	      strcpy (st, "not  ");
	      regdec (st, insn.rd);
	    }
	  else
	    {
	      strcpy (st, "xnor  ");
	      regdec (st, insn.rd);
	    }
	  break;
	case ADDX:
	  strcpy (st, "addx  ");
	  regres (st, insn, 0);
	  break;
	case SUBX:
	  strcpy (st, "subx  ");
	  regres (st, insn, 0);
	  break;
	case ADDCC:
	  strcpy (st, "addcc  ");
	  regres (st, insn, 0);
	  break;
	case ANDCC:
	  strcpy (st, "andcc  ");
	  regres (st, insn, 1);
	  break;
	case ORCC:
	  if ((insn.rs1) && (!insn.rd) && (!insn.i))
	    {
	      strcpy (st, "tst  ");
	      regdec (st, insn.rs2);
	    }
	  else
	    {
	      strcpy (st, "orcc  ");
	      regres (st, insn, 1);
	    }
	  break;
	case XORCC:
	  strcpy (st, "xorcc  ");
	  regres (st, insn, 1);
	  break;
	case SUBCC:
	  if (insn.rd)
	    {
	      strcpy (st, "subcc  ");
	      regres (st, insn, 0);
	    }
	  else
	    {
	      strcpy (st, "cmp  ");
	      regimm (st, insn, 0, 0);
	    }
	  break;
	case ANDNCC:
	  strcpy (st, "andncc  ");
	  regres (st, insn, 1);
	  break;
	case ORNCC:
	  strcpy (st, "orncc  ");
	  regres (st, insn, 1);
	  break;
	case XNORCC:
	  strcpy (st, "xnorcc  ");
	  regres (st, insn, 1);
	  break;
	case ADDXCC:
	  strcpy (st, "addxcc  ");
	  regres (st, insn, 1);
	  break;
	case UMAC:
	  strcpy (st, "umac  ");
	  regres (st, insn, 0);
	  break;
	case SMAC:
	  strcpy (st, "smac  ");
	  regres (st, insn, 0);
	  break;
	case UMUL:
	  strcpy (st, "umul  ");
	  regres (st, insn, 0);
	  break;
	case SMUL:
	  strcpy (st, "smul  ");
	  regres (st, insn, 0);
	  break;
	case UDIV:
	  strcpy (st, "udiv    ");
	  regres (st, insn, 0);
	  break;
	case SDIV:
	  strcpy (st, "sdiv    ");
	  regres (st, insn, 0);
	  break;
	case UMULCC:
	  strcpy (st, "umulcc  ");
	  regres (st, insn, 0);
	  break;
	case SMULCC:
	  strcpy (st, "smulcc  ");
	  regres (st, insn, 0);
	  break;
	case SUBXCC:
	  strcpy (st, "subxcc  ");
	  regres (st, insn, 0);
	  break;
	case UDIVCC:
	  strcpy (st, "udivcc  ");
	  regres (st, insn, 0);
	  break;
	case SDIVCC:
	  strcpy (st, "sdivcc  ");
	  regres (st, insn, 0);
	  break;
	case TADDCC:
	  strcpy (st, "taddcc  ");
	  regres (st, insn, 0);
	  break;
	case TSUBCC:
	  strcpy (st, "tsubcc  ");
	  regres (st, insn, 0);
	  break;
	case TADDCCTV:
	  strcpy (st, "taddcctv  ");
	  regres (st, insn, 0);
	  break;
	case TSUBCCTV:
	  strcpy (st, "tsubcctv  ");
	  regres (st, insn, 0);
	  break;
	case MULSCC:
	  strcpy (st, "mulscc  ");
	  regres (st, insn, 0);
	  break;
	case SLL:
	  strcpy (st, "sll  ");
	  regres (st, insn, 0);
	  break;
	case SRL:
	  strcpy (st, "srl  ");
	  regres (st, insn, 0);
	  break;
	case SRA:
	  strcpy (st, "sra  ");
	  regres (st, insn, 0);
	  break;
	case RDY:
	  if (insn.rs1)
	    {
	      sprintf (st, "mov  %%asr%d, ", insn.rs1);
	    }
	  else
	    {
	      strcpy (st, "mov  %y, ");
	    }
	  regdec (st, insn.rd);
	  break;
	case RDPSR:
	  strcpy (st, "mov  %psr, ");
	  regdec (st, insn.rd);
	  break;
	case RDWIM:
	  strcpy (st, "mov  %wim, ");
	  regdec (st, insn.rd);
	  break;
	case RDTBR:
	  strcpy (st, "mov  %tbr, ");
	  regdec (st, insn.rd);
	  break;
	case WRY:
	  if ((!insn.rs1) || (!insn.rs2))
	    {
	      if (insn.rd)
		{
		  strcpy (st, "mov  ");
		  regimm (st, insn, 1, 0);
		  sprintf (tmp, ", %%asr%d", insn.rd);
		  strcat (st, tmp);
		}
	      else
		{
		  strcpy (st, "mov  ");
		  regimm (st, insn, 1, 0);
		  strcat (st, ", %y");
		}
	    }
	  else
	    {
	      if (insn.rd)
		{
		  strcpy (st, "wr  ");
		  regimm (st, insn, 1, 0);
		  sprintf (tmp, ", %%asr%d", insn.rd);
		  strcat (st, tmp);
		}
	      else
		{
		  strcpy (st, "wr  ");
		  regimm (st, insn, 1, 0);
		  strcat (st, ", %y");
		}
	    }
	  break;
	case WRPSR:
	  if ((!insn.rs1) || (!insn.rs2))
	    {
	      strcpy (st, "mov  ");
	      regimm (st, insn, 1, 0);
	      strcat (st, ", %psr");
	    }
	  else
	    {
	      strcpy (st, "wr  ");
	      regimm (st, insn, 1, 0);
	      strcat (st, ", %psr");
	    }
	  break;
	case WRWIM:
	  if ((!insn.rs1) || (!insn.rs2))
	    {
	      strcpy (st, "mov  ");
	      regimm (st, insn, 1, 0);
	      strcat (st, ", %wim");
	    }
	  else
	    {
	      strcpy (st, "wr  ");
	      regimm (st, insn, 1, 0);
	      strcat (st, ", %wim");
	    }
	  break;
	case WRTBR:
	  if ((!insn.rs1) || (!insn.rs2))
	    {
	      strcpy (st, "mov  ");
	      regimm (st, insn, 1, 0);
	      strcat (st, ", %tbr");
	    }
	  else
	    {
	      strcpy (st, "wr  ");
	      regimm (st, insn, 1, 0);
	      strcat (st, ", %tbr");
	    }
	  break;
	case JMPL:
	  if (!insn.rd)
	    {
	      if ((insn.i) && (insn.simm == 8))
		{
		  if (insn.rs1 == 31)
		    {
		      strcpy (st, "ret  ");
		    }
		  else if (insn.rs1 == 15)
		    {
		      strcpy (st, "retl  ");
		    }
		  else
		    {
		      strcpy (st, "jmp  ");
		      regimm (st, insn, 1, 1);
		    }
		}
	      else
		{
		  strcpy (st, "jmp  ");
		  regimm (st, insn, 1, 1);
		}
	    }
	  else if (insn.rd == 15)
	    {
	      strcpy (st, "call  ");
	      regimm (st, insn, 1, 1);
	    }
	  else
	    {
	      strcpy (st, "jmpl  ");
	      regres (st, insn, 1);
	    }
	  break;
	case TICC:
	  sprintf (st, "t%s  ", branchop (inst));
	  regimm (st, insn, 1, 0);
	  break;
	case FLUSH:
	  strcpy (st, "flush  ");
	  regimm (st, insn, 1, 0);
	  break;
	case RETT:
	  strcpy (st, "rett  ");
	  regimm (st, insn, 0, 0);
	  break;
	case RESTORE:
	  if (!insn.rd)
	    strcpy (st, "restore  ");
	  else
	    {
	      strcpy (st, "restore  ");
	      regres (st, insn, 0);
	    }
	  break;
	case SAVE:
	  if (!insn.rd)
	    strcpy (st, "save  ");
	  else
	    {
	      strcpy (st, "save  ");
	      regres (st, insn, 0);
	    }
	  break;
	case FPOP1:
	  switch (insn.opf)
	    {
	    case FITOS:
	      strcpy (st, "fitos  ");
	      freg2 (st, insn);
	      break;
	    case FITOD:
	      strcpy (st, "fitod  ");
	      freg2 (st, insn);
	      break;
	    case FSTOI:
	      strcpy (st, "fstoi  ");
	      freg2 (st, insn);
	      break;
	    case FDTOI:
	      strcpy (st, "fdtoi  ");
	      freg2 (st, insn);
	      break;
	    case FSTOD:
	      strcpy (st, "fstod  ");
	      freg2 (st, insn);
	      break;
	    case FDTOS:
	      strcpy (st, "fdtos  ");
	      freg2 (st, insn);
	      break;
	    case FMOVS:
	      strcpy (st, "fmovs  ");
	      freg2 (st, insn);
	      break;
	    case FNEGS:
	      strcpy (st, "fnegs  ");
	      freg2 (st, insn);
	      break;
	    case FABSS:
	      strcpy (st, "fabss  ");
	      freg2 (st, insn);
	      break;
	    case FSQRTS:
	      strcpy (st, "fsqrts  ");
	      freg2 (st, insn);
	      break;
	    case FSQRTD:
	      strcpy (st, "fsqrtd  ");
	      freg2 (st, insn);
	      break;
	    case FADDS:
	      strcpy (st, "fadds  ");
	      freg3 (st, insn);
	      break;
	    case FADDD:
	      strcpy (st, "faddd  ");
	      freg3 (st, insn);
	      break;
	    case FSUBS:
	      strcpy (st, "fsubs  ");
	      freg3 (st, insn);
	      break;
	    case FSUBD:
	      strcpy (st, "fsubd  ");
	      freg3 (st, insn);
	      break;
	    case FMULS:
	      strcpy (st, "fmuls  ");
	      freg3 (st, insn);
	      break;
	    case FMULD:
	      strcpy (st, "fmuld  ");
	      freg3 (st, insn);
	      break;
	    case FSMULD:
	      strcpy (st, "fsmuld  ");
	      freg3 (st, insn);
	      break;
	    case FDIVS:
	      strcpy (st, "fdivs  ");
	      freg3 (st, insn);
	      break;
	    case FDIVD:
	      strcpy (st, "fdivd  ");
	      freg3 (st, insn);
	      break;
	    default:
	      sprintf (st, "unknown fpop:  %08x", insn.insn);
	      break;
	    }
	  break;
	case FPOP2:
	  switch (insn.opf)
	    {
	    case FCMPS:
	      strcpy (st, "fcmps  ");
	      fregc (st, insn);
	      break;
	    case FCMPD:
	      strcpy (st, "fcmpd  ");
	      fregc (st, insn);
	      break;
	    case FCMPES:
	      strcpy (st, "fcmpes  ");
	      fregc (st, insn);
	      break;
	    case FCMPED:
	      strcpy (st, "fcmped  ");
	      fregc (st, insn);
	      break;
	    default:
	      sprintf (st, "unknown fpop:  %08x", inst);
	      break;
	    }
	  break;
	default:
	  sprintf (st, "unknown opcode: 0x%08x", inst);
	}
      break;
    case LDST:
      switch (insn.op3)
	{
	case ST:
	  if (!insn.rd)
	    {
	      strcpy (st, "clr  ");
	      stparx (st, insn);
	      break;
	    }
	  else
	    {
	      strcpy (st, "st  ");
	      stpar (st, insn);
	      break;
	    }
	  break;
	case STB:
	  if (!insn.rd)
	    {
	      strcpy (st, "clrb  ");
	      stparx (st, insn);
	      break;
	    }
	  else
	    {
	      strcpy (st, "stb  ");
	      stpar (st, insn);
	      break;
	    }
	  break;
	case STH:
	  if (!insn.rd)
	    {
	      strcpy (st, "clrh  ");
	      stparx (st, insn);
	      break;
	    }
	  else
	    {
	      strcpy (st, "sth  ");
	      stpar (st, insn);
	      break;
	    }
	  break;
	case STC:
	  strcpy (st, "st  ");
	  stparc (st, insn);
	  break;
	case STDC:
	  strcpy (st, "std  ");
	  stparc (st, insn);
	  break;
	case STDCQ:
	  strcpy (st, "std  ");
	  stparcq (st, insn);
	  break;
	case STF:
	  strcpy (st, "st  ");
	  stparf (st, insn);
	  break;
	case STDF:
	  strcpy (st, "std  ");
	  stparf (st, insn);
	  break;
	case STDFQ:
	  strcpy (st, "std  ");
	  stparfq (st, insn);
	  break;
	case STFSR:
	  strcpy (st, "st  %fsr, ");
	  adec (st, insn);
	  break;
	case STD:
	  strcpy (st, "std  ");
	  stpar (st, insn);
	  break;
	case STA:
	  strcpy (st, "sta  ");
	  stpara (st, insn);
	  break;
	case STBA:
	  strcpy (st, "stba  ");
	  stpara (st, insn);
	  break;
	case STHA:
	  strcpy (st, "stha  ");
	  stpara (st, insn);
	  break;
	case STDA:
	  strcpy (st, "stda  ");
	  stpara (st, insn);
	  break;
	case LDF:
	  strcpy (st, "ld  ");
	  ldparf (st, insn);
	  break;
	case LDFSR:
	  strcpy (st, "ld  ");
	  adec (st, insn);
	  strcat (st, ", %fsr");
	  break;
	case LD:
	  strcpy (st, "ld  ");
	  ldpar (st, insn);
	  break;
	case LDUB:
	  strcpy (st, "ldub  ");
	  ldpar (st, insn);
	  break;
	case LDUH:
	  strcpy (st, "lduh  ");
	  ldpar (st, insn);
	  break;
	case LDDF:
	  strcpy (st, "ldd  ");
	  ldparf (st, insn);
	  break;
	case LDD:
	  strcpy (st, "ldd  ");
	  ldpar (st, insn);
	  break;
	case LDSB:
	  strcpy (st, "ldsb  ");
	  ldpar (st, insn);
	  break;
	case LDSH:
	  strcpy (st, "ldsh  ");
	  ldpar (st, insn);
	  break;
	case LDSTUB:
	  strcpy (st, "ldstub  ");
	  ldpar (st, insn);
	  break;
	case SWAP:
	  strcpy (st, "swap  ");
	  ldpar (st, insn);
	  break;
	case LDA:
	  strcpy (st, "lda  ");
	  ldpara (st, insn);
	  break;
	case LDUBA:
	  strcpy (st, "lduba  ");
	  ldpara (st, insn);
	  break;
	case LDUHA:
	  strcpy (st, "lduha  ");
	  ldpara (st, insn);
	  break;
	case LDDA:
	  strcpy (st, "ldda  ");
	  ldpara (st, insn);
	  break;
	case LDSBA:
	  strcpy (st, "ldsba  ");
	  ldpara (st, insn);
	  break;
	case LDSHA:
	  strcpy (st, "ldsha  ");
	  ldpara (st, insn);
	  break;
	case LDSTUBA:
	  strcpy (st, "ldstuba  ");
	  ldpara (st, insn);
	  break;
	case SWAPA:
	  strcpy (st, "swapa  ");
	  ldpara (st, insn);
	  break;
	case CASA:
	  strcpy (st, "casa  ");
	  ldpara (st, insn);
	  break;

	default:
	  sprintf (st, "unknown opcode: 0x%08x", inst);
	}
      break;
    default:
      sprintf (st, "unknown opcode: 0x%08x", inst);
    }
}


static void
sparc_print_insn (uint32 addr)
{
  char tmp[128];
  uint32 insn;
  uint32 hold;

  ms->memory_iread (addr, &insn, &hold);
  sparc_disas (tmp, addr, insn);
  printf (" %s", tmp);
}

const struct cpu_arch sparc = {
  3,
  sparc_dispatch_instruction,
  sparc_execute_trap,
  sparc_check_interrupts,
  sparc_print_insn,
  sparc_gdb_get_reg,
  sparc_set_register,
  sparc_display_registers,
  sparc_display_ctrl,
  sparc_display_special,
  sparc_display_fpu
};
