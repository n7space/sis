/*
 * This file is part of SIS.
 *
 * SIS, SPARC instruction simulator V2.5 Copyright (C) 1995 Jiri Gaisler,
 * European Space Agency
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Leon3 emulation, loosely based on erc32.c.
 */

#define ROM_START 	0x20000000
#define ROM_SIZE 	0x01000000
#define RAM_START 	0x80000000
#define RAM_SIZE 	0x04000000

#include <errno.h>
#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#ifdef HAVE_TERMIOS_H
#include <termios.h>
#endif
#include <sys/file.h>
#include <unistd.h>
#include "riscv.h"
#include "grlib.h"
#include "rv32dtb.h"

#define PLIC_START	0x0C000000
#define PLIC_MASK  	0xFFC
#define NS16550_START 	0x10000000
#define TESTSTART  	0x00100000

#define CLINT_START	0x02000000

/* APB registers */
#define APBSTART	0xC0000000

/* Memory exception waitstates.  */
#define MEM_EX_WS 	1


/* Forward declarations. */
static char *get_mem_ptr (uint32 addr, uint32 size);

/* One-time init. */

static void
init_sim (void)
{
  int i;

  for (i = 0; i < ncpu; i++)
    grlib_ahbm_add (&leon3s, 0);

  grlib_ahbs_add (&s5test, 0, TESTSTART, 0xFFF);
  grlib_ahbs_add (&clint, 0, CLINT_START, 0xFFF);
  grlib_ahbs_add (&plic, 0, PLIC_START, PLIC_MASK);
  grlib_ahbs_add (&ns16550, 0, NS16550_START, 0xFFF);
  grlib_ahbs_add (&srctrl, 0, ROM_START, ROM_MASKPP);
  grlib_ahbs_add (&sdctrl, 0, RAM_START, RAM_MASKPP);

  grlib_init ();
  memcpy (&romb[(ROM_END - 0x10000) & ROM_MASK], rv32_dtb, sizeof (rv32_dtb));
  ebase.ramstart = RAM_START;
}

/* Power-on reset init. */

static void
reset (void)
{
  grlib_reset ();
}

/* IU error mode manager. */

static void
error_mode (uint32 pc)
{

}

/* Flush ports when simulator stops. */

static void
sim_halt (void)
{
#ifdef FAST_UART
  apbuart_flush ();
#endif
}

static void
exit_sim (void)
{
  apbuart_close_port ();
}

/* Memory emulation.  */

static int
memory_read (uint32 addr, uint32 * data, int32 * ws)
{
  uint64 tmp;
  int32 mexc;
  int reg, cpuid;

  *ws = 0;
  /* bypass system bus decoding to speed-up RAM/ROM access */
  if ((addr >= RAM_START) && (addr < RAM_END))
    {
      memcpy (data, &ramb[addr & RAM_MASK], 4);
      return 0;
    }
  if ((addr >= ROM_START) && (addr < ROM_END))
    {
      memcpy (data, &romb[addr & ROM_MASK], 4);
      return 0;
    }

  /* regular system bus access */
  mexc = grlib_read (addr, data);
  *ws = 4;

  if (sis_verbose > 1)
    printf ("BUS read  a: %08x, d: %08x\n", addr, *data);

  if (sis_verbose && mexc)
    {
      printf ("Memory exception at %x (illegal address)\n", addr);
      *ws = MEM_EX_WS;
    }
  return mexc;
}

static int
memory_write (uint32 addr, uint32 * data, int32 sz, int32 * ws)
{
  uint32 waddr;
  int32 mexc;
  uint64 tmp;
  int reg, cpuid;

  mexc = 0;
  *ws = 0;
  if ((addr >= RAM_START) && (addr < RAM_END))
    {
      waddr = addr & RAM_MASK;
      grlib_store_bytes (ramb, waddr, data, sz);
      return 0;
    }
  else if ((addr >= ROM_START) && (addr < ROM_END))
    {
      grlib_store_bytes (romb, addr, data, sz);
      return 0;
    }
  else
    {
      mexc = grlib_write (addr, data, sz);
      *ws = 4;
    }

  if (sis_verbose > 0)
    printf ("AHB write a: %08x, d: %08x\n", addr, *data);
  if (sis_verbose && mexc)
    {
      printf ("Memory exception at %x (illegal address)\n", addr);
      *ws = MEM_EX_WS;
    }
  return mexc;
}

static char *
get_mem_ptr (uint32 addr, uint32 size)
{
  if ((addr >= RAM_START) && ((addr + size) < RAM_END))
    {
      return &ramb[addr & RAM_MASK];
    }
  else if ((addr >= ROM_START) && ((addr + size) < ROM_END))
    {
      return &romb[addr & ROM_MASK];
    }

  return NULL;
}

static int
sis_memory_write (uint32 addr, const char *data, uint32 length)
{
  char *mem;
  int32 ws;

  if ((mem = get_mem_ptr (addr, length)) != NULL)
    {
      memcpy (mem, data, length);
      return length;
    }
  else if (length == 4)
    memory_write (addr, (uint32 *) data, 2, &ws);
  return 0;
}

static int
sis_memory_read (uint32 addr, char *data, uint32 length)
{
  char *mem;
  int ws;

  if (length == 4)
    {
      memory_read (addr, (uint32 *) data, &ws);
      return 4;
    }

  if ((mem = get_mem_ptr (addr, length)) == NULL)
    return 0;

  memcpy (data, mem, length);
  return length;
}

static void
boot_init (void)
{

  int i;

  grlib_boot_init ();
  for (i = 0; i < ncpu; i++)
    {
      sregs[i].wim = 2;
      sregs[i].psr = 0xF30010e0;
      sregs[i].r[30] = RAM_END - (i * 0x20000);
      sregs[i].r[14] = sregs[i].r[30] - 96 * 4;
      sregs[i].cache_ctrl = 0x81000f;
      sregs[i].r[2] = sregs[i].r[30];	/* sp on RISCV-V */
      sregs[i].r[11] = ROM_END - 0x10000;	/* dtb on RISCV-V */
      sregs[i].pwd_mode = 0;
    }
}

const struct memsys rv32 = {
  init_sim,
  reset,
  error_mode,
  sim_halt,
  exit_sim,
  apbuart_init_stdio,
  apbuart_restore_stdio,
  memory_read,
  memory_read,
  memory_write,
  sis_memory_write,
  sis_memory_read,
  boot_init,
  get_mem_ptr,
  grlib_set_irq
};
