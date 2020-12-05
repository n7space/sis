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
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#include "sis.h"

#define PSR_CWP 0x7
#define SIM_DESC int
#define SIM_ADDR uint32
#define SIM_RC int
#define SIM_RC_FAIL 0
#define SIM_RC_OK 1
#define EBREAK 0x00100073
#define CEBREAK 0x90002

static int
run_sim_gdb (icount, dis)
     uint64 icount;
     int dis;
{
  int res;

  if ((sregs[cpu].pc != 0) && (ebase.simtime == 0))
    ms->boot_init ();
  res = run_sim (icount, dis);
  ms->sim_halt ();
  clearerr (stdin);
  return res;
}

void
sim_close (sd, quitting)
     SIM_DESC sd;
     int quitting;
{

  ms->exit_sim ();
#if defined(F_GETFL) && defined(F_SETFL)
  fcntl (0, F_SETFL, termsave);
#endif

};

void
sim_create_inferior ()
{
  int i;

  if (sis_verbose)
    printf ("interf: sim_create_inferior()");
  ebase.simtime = 0;
  ebase.simstart = 0;
  reset_all ();
  reset_stat (sregs);
  for (i = 0; i < ncpu; i++)
    {
      sregs[i].pc = last_load_addr & ~1;
      sregs[i].npc = sregs[i].pc + 4;
    }
}

int
sim_write (uint32 mem, const char *buf, int length)
{
  int i, len;

  for (i = 0; i < length; i++)
    {
      ms->sis_memory_write ((mem + i) ^ arch->bswap, &buf[i], 1);
    }
  return length;
}

int
sim_read (uint32 mem, char *buf, int length)
{
  int i, len;

  if (sis_gdb_break && (archtype == CPU_SPARC) && (length >= 4))
    {
      if (gdb_sp_read (mem, buf, length))
	return length;
    }
  for (i = 0; i < length; i++)
    {
      ms->sis_memory_read ((mem + i) ^ arch->bswap, &buf[i], 1);
    }
  return length;
}

void
sim_info (sd, verbose)
     SIM_DESC sd;
     int verbose;
{
  show_stat (&sregs[cpu]);
}

int simstat = OK;

void
sim_resume (int step)
{
  if (step)
    simstat = run_sim_gdb (1, 0);
  else
    {
      socket_poll ();
      simstat = run_sim_gdb (UINT64_MAX / 2, 0);
      remove_event (socket_poll, -1);
    }

  if (sis_gdb_break && (cputype != CPU_RISCV))
    save_sp (&sregs[cpu]);
}

int
sim_stop (SIM_DESC sd)
{
  ctrl_c = 1;
  return 1;
}

static int
sis_insert_hw_breakpoint (int addr)
{
  if (ebase.wprnum < BPT_MAX)
    {
      ebase.bpts[ebase.bptnum] = addr;
      ebase.bptnum++;
      if (sis_verbose)
	printf ("inserted hw breakpoint at %x\n", addr);
      return SIM_RC_OK;
    }
  else
    return SIM_RC_FAIL;
}

static int
sis_remove_hw_breakpoint (int addr)
{
  int i = 0;

  if (!ebase.bptnum)
    return 1;
  while ((i < ebase.bptnum) && (ebase.bpts[i] != addr))
    i++;
  if (addr == ebase.bpts[i])
    {
      for (; i < ebase.bptnum - 1; i++)
	ebase.wprs[i] = ebase.bpts[i + 1];
      ebase.bptnum -= 1;
      if (sis_verbose)
	printf ("removed hw breakpoint at %x\n", addr);
    }
  return 1;
}

static int
sis_insert_watchpoint_read (int addr, unsigned char mask)
{
  if (ebase.wprnum < WPR_MAX)
    {
      ebase.wprs[ebase.wprnum] = addr;
      ebase.wprm[ebase.wprnum] = mask;
      ebase.wprnum++;
      if (sis_verbose)
	printf ("inserted read watchpoint at %x\n", addr);
      return SIM_RC_OK;
    }
  else
    return SIM_RC_FAIL;
}

static int
sis_remove_watchpoint_read (int addr)
{
  int i = 0;

  if (!ebase.wprnum)
    return 1;
  while ((i < ebase.wprnum) && (ebase.wprs[i] != addr))
    i++;
  if (addr == ebase.wprs[i])
    {
      for (; i < ebase.wprnum - 1; i++)
	ebase.wprs[i] = ebase.wprs[i + 1];
      ebase.wprnum -= 1;
      if (sis_verbose)
	printf ("removed read watchpoint at %x\n", addr);
    }
  return 1;
}

static int
sis_insert_watchpoint_write (int32 addr, unsigned char mask)
{
  if (ebase.wpwnum < WPR_MAX)
    {
      ebase.wpws[ebase.wpwnum] = addr;
      ebase.wpwm[ebase.wpwnum] = mask;
      ebase.wpwnum++;
      if (sis_verbose)
	printf ("sim_insert_watchpoint_write: 0x%08x : %x\n", addr, mask);
      return SIM_RC_OK;
    }
  else
    return SIM_RC_FAIL;
}

static int
sis_remove_watchpoint_write (int addr)
{
  int i = 0;

  if (!ebase.wpwnum)
    return 1;
  while ((i < ebase.wpwnum) && (ebase.wpws[i] != addr))
    i++;
  if (addr == ebase.wpws[i])
    {
      for (; i < ebase.wpwnum - 1; i++)
	ebase.wpws[i] = ebase.wpws[i + 1];
      ebase.wpwnum -= 1;
      if (sis_verbose)
	printf ("removed write watchpoint at %x\n", addr);
    }
  return SIM_RC_OK;
}

int
sim_can_use_hw_breakpoint (SIM_DESC sd, int type, int cnt, int othertype)
{
  if (type == 2)		/* bp_hardware_breakpoint not supported */
    return 0;
  else
    return 1;
}


int
sim_set_watchpoint (uint32 mem, int length, int type)
{
  int res;
  unsigned char mask;

  if (!length)
    return 1;			/* used by gdb for probing of watchpoints */

  mask = length - 1;

  switch (type)
    {
    case 0:
      res = sim_insert_swbreakpoint (mem, length);
      break;
    case 1:
      res = sis_insert_hw_breakpoint (mem);
      break;
    case 2:
      res = sis_insert_watchpoint_write (mem, mask);
      break;
    case 3:
      res = sis_insert_watchpoint_read (mem, mask);
      break;
    case 4:
      if ((res = sis_insert_watchpoint_write (mem, mask)) == SIM_RC_OK)
	res = sis_insert_watchpoint_read (mem, mask);
      if (res == SIM_RC_FAIL)
	sis_remove_watchpoint_read (mem);
      break;
    default:
      res = 0;
    }
  return (res);
}


int
sim_clear_watchpoint (uint32 mem, int length, int type)
{
  int res;

  if (!length)
    return 1;

  switch (type)
    {
    case 0:
      res = sim_remove_swbreakpoint (mem, length);
      break;
    case 1:
      res = sis_remove_hw_breakpoint (mem);
      break;
    case 2:
      res = sis_remove_watchpoint_write (mem);
      break;
    case 3:
      res = sis_remove_watchpoint_read (mem);
      break;
    case 4:
      if ((res = sis_remove_watchpoint_write (mem)) == SIM_RC_OK)
	res = sis_remove_watchpoint_read (mem);
      else
	sis_remove_watchpoint_read (mem);
      break;
    default:
      res = 0;
    }
  return (res);
}

int
sim_stopped_by_watchpoint (SIM_DESC sd)
{
  if (sis_verbose)
    printf ("sim_stopped_by_watchpoint %x\n", ebase.wphit);
  return ((ebase.wphit != 0));
}

int
sim_watchpoint_address (SIM_DESC sd)
{
  if (sis_verbose)
    printf ("sim__watchpoint_address %x\n", ebase.wpaddress);
  return (ebase.wpaddress);
}

int
sim_insert_swbreakpoint (uint32 addr, int len)
{
  uint32 breakinsn;

  if (ebase.bptnum < BPT_MAX)
    {
      ebase.bpts[ebase.bptnum] = addr;
      ms->sis_memory_read (addr, (char *) &ebase.bpsave[ebase.bptnum], len);
      if (len == 4)
	{
	  breakinsn = EBREAK;
	  ms->sis_memory_write (addr, (char *) &breakinsn, 4);
	}
      else
	{
	  breakinsn = CEBREAK;
	  ms->sis_memory_write (addr, (char *) &breakinsn, 2);
	}
      if (sis_verbose > 1)
	printf ("sim_insert_swbreakpoint: added breakpoint %d at 0x%08x\n",
		ebase.bptnum + 1, addr);
      ebase.bptnum += 1;
      return 1;
    }
  return 0;			/* Too many breakpoints */
}

int
sim_remove_swbreakpoint (uint32 addr, int len)
{
  int i;

  /* find breakpoint to remove */
  for (i = 0; i < ebase.bptnum; i++)
    {
      if (ebase.bpts[i] == addr)
	break;
    }
  if (ebase.bpts[i] == addr)
    {
      /* write back saved opcode */
      ms->sis_memory_write (addr, (char *) &ebase.bpsave[i], len);
      if (sis_verbose > 1)
	printf ("sim_remove_swbreakpoint: remove breakpoint %d at 0x%08x\n",
		i, addr);
      /* shift down remaining breakpoints */
      for (; i < ebase.bptnum; i++)
	{
	  ebase.bpts[i] = ebase.bpts[i + 1];
	}
      ebase.bptnum -= 1;
      return 1;
    }
  return 0;			/* breakpoint not found */
}
