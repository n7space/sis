/*
 * This file is part of SIS.
 *
 * SIS, SPARC instruction simulator. Copyright (C) 2014 Jiri Gaisler
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
 */

#include "riscv.h"
#include <stdbool.h>
#include <stdio.h>
#include <inttypes.h>
#ifdef HAVE_TERMIOS_H
#include <termios.h>
#endif
#include <sys/file.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <string.h>
#include "grlib.h"

#include "timer.h"
#include "uart.h"

/* APB PNP */

static uint32 apbppmem[32 * 2];	/* 32-entry APB PP AREA */
static int apbppindex;

int
grlib_apbpp_add (uint32 id, uint32 addr)
{
  apbppmem[apbppindex++] = id;
  apbppmem[apbppindex++] = addr;
  if (apbppindex >= (32 * 2))
    apbppindex = 0;		/* prevent overflow of area */
  return apbppindex;
}

uint32
grlib_apbpnp_read (uint32 addr)
{
  uint32 read_data;
  addr &= 0xff;
  read_data = apbppmem[addr >> 2];

  return read_data;
}

/* AHB PNP */

static uint32 ahbppmem[128 * 8];	/* 128-entry AHB PP AREA */
static int ahbmppindex;
static int ahbsppindex = 64 * 8;

int
grlib_ahbmpp_add (uint32 id)
{
  ahbppmem[ahbmppindex] = id;
  ahbmppindex += 8;
  if (ahbmppindex >= (64 * 8))
    ahbmppindex = 0;		/* prevent overflow of area */
  return ahbmppindex;
}

int
grlib_ahbspp_add (uint32 id, uint32 addr1, uint32 addr2,
		  uint32 addr3, uint32 addr4)
{
  ahbppmem[ahbsppindex] = id;
  ahbsppindex += 4;
  ahbppmem[ahbsppindex++] = addr1;
  ahbppmem[ahbsppindex++] = addr2;
  ahbppmem[ahbsppindex++] = addr3;
  ahbppmem[ahbsppindex++] = addr4;
  if (ahbsppindex >= (128 * 8))
    ahbsppindex = 64 * 8;	/* prevent overflow of area */
  return ahbsppindex;
}

uint32
grlib_ahbpnp_read (uint32 addr)
{
  uint32 read_data;

  addr &= 0xfff;
  read_data = ahbppmem[addr >> 2];
  return read_data;

}

static struct grlib_buscore ahbmcores[16];
static struct grlib_buscore ahbscores[16];
static struct grlib_buscore apbcores[16];
static int ahbmi;
static int ahbsi;
static int apbi;

void
grlib_init ()
{
  int i;

  for (i = 0; i < ahbmi; i++)
    if (ahbmcores[i].core->init)
      ahbmcores[i].core->init ();
  for (i = 0; i < ahbsi; i++)
    if (ahbscores[i].core->init)
      ahbscores[i].core->init ();
}

void
grlib_reset ()
{
  int i;

  for (i = 0; i < ahbmi; i++)
    if (ahbmcores[i].core->reset)
      ahbmcores[i].core->reset ();
  for (i = 0; i < ahbsi; i++)
    if (ahbscores[i].core->reset)
      ahbscores[i].core->reset ();
}


void
grlib_ahbm_add (const struct grlib_ipcore *core, int irq)
{
  ahbmcores[ahbmi].core = core;
  if (core->add)
    core->add (irq, 0, 0);
  ahbmi++;
}

void
grlib_ahbs_add (const struct grlib_ipcore *core, int irq,
		uint32 addr, uint32 mask)
{
  ahbscores[ahbsi].core = core;
  if (core->add)
    {
      ahbscores[ahbsi].start = addr;
      ahbscores[ahbsi].end = addr + ~(mask << 24) + 1;
      ahbscores[ahbsi].mask = ~(mask << 24);
      core->add (irq, addr, mask);
    }
  ahbsi++;
}

int
grlib_read (uint32 addr, uint32 * data)
{
  int i;
  int res = 0;

  for (i = 0; i < ahbsi; i++)
    if ((addr >= ahbscores[i].start) && (addr < ahbscores[i].end))
      {
	if (ahbscores[i].core->read)
	  res = ahbscores[i].core->read (addr & ahbscores[i].mask, data);
	else
	  res = 1;
	return !res;
      }

  if (!res && ((addr >= AHBPP_START) && (addr <= AHBPP_END)))
    {
      *data = grlib_ahbpnp_read (addr);
      if (sis_verbose > 1)
	printf ("AHB PP read a: %08x, d: %08x\n", addr, *data);
      return 0;
    }

  return !res;
}

int
grlib_write (uint32 addr, uint32 * data, uint32 sz)
{
  int i;
  int res = 0;

  for (i = 0; i < ahbsi; i++)
    if ((addr >= ahbscores[i].start) && (addr < ahbscores[i].end))
      {
	if (ahbscores[i].core->write)
	  res = ahbscores[i].core->write (addr & ahbscores[i].mask, data, sz);
	else
	  res = 1;
	if (sis_verbose > 2)
	  printf ("AHB write a: %08x, d: %08x\n", addr, *data);
	break;
      }
  return !res;
}

void
grlib_apb_add (const struct grlib_ipcore *core, int irq,
	       uint32 addr, uint32 mask)
{
  apbcores[apbi].core = core;
  if (core->add)
    {
      apbcores[apbi].start = addr & (mask << 8);
      apbcores[apbi].end =
	(apbcores[apbi].start + ~(mask << 8) + 1) & APB_CORES_ADDRESS_MASK;
      apbcores[apbi].mask = ~(mask << 8) & APB_CORES_ADDRESS_MASK;
      core->add (irq, addr, mask);
    }
  apbi++;
}

/* ------------------- GRETH -----------------------*/

extern int greth_irq;

static int
grlib_greth_read (uint32 addr, uint32 * data)
{
  *data = greth_read (addr);
}

static int
grlib_greth_write (uint32 addr, uint32 * data, uint32 size)
{
  greth_write (addr, *data);
}

static void
greth_add (int irq, uint32 addr, uint32 mask)
{
  grlib_ahbmpp_add (GRLIB_PP_ID (VENDOR_GAISLER, GAISLER_GRETH, 0, 0));
  grlib_apbpp_add (GRLIB_PP_ID (VENDOR_GAISLER, GAISLER_GRETH, 0, irq),
		   GRLIB_PP_APBADDR (addr, mask));
  greth_irq = irq;
  if (sis_verbose)
    printf (" GRETH 10/100 Mbit Ethernet core    0x%08x   %d\n", addr, irq);
}

const struct grlib_ipcore greth = {
  NULL, NULL, grlib_greth_read, grlib_greth_write, greth_add
};

/* ------------------- L2C -----------------------*/

static int
grlib_l2c_read (uint32 addr, uint32 * data)
{
  uint32 res;

  switch (addr & 0xFC)
    {
    case 4:
      /* Status */
      res = 0x00502803;
      break;
    default:
      res = 0;
    }

  *data = res;
}

static void
l2c_add (int irq, uint32 addr, uint32 mask)
{
  grlib_ahbmpp_add (GRLIB_PP_ID (VENDOR_GAISLER, GAISLER_L2C, 0, 0));
  grlib_apbpp_add (GRLIB_PP_ID (VENDOR_GAISLER, GAISLER_L2C, 0, 0),
		   GRLIB_PP_APBADDR (addr, mask));
  if (sis_verbose)
    printf(" Level 2 Cache controller           0x%08x\n", addr);
}

const struct grlib_ipcore l2c = {
  NULL, NULL, grlib_l2c_read, NULL, l2c_add
};


/* ------------------- LEON3 -----------------------*/

static void
leon3_add ()
{
  grlib_ahbmpp_add (GRLIB_PP_ID (VENDOR_GAISLER, GAISLER_LEON3, 0, 0));
  if (sis_verbose)
    printf (" LEON3 SPARC V8 processor                      \n");
}

const struct grlib_ipcore leon3s = {
  NULL, NULL, NULL, NULL, leon3_add
};

/* ------------------- APBMST ----------------------*/

static void
apbmst_init ()
{
  int i;

  for (i = 0; i < apbi; i++)
    if (apbcores[i].core->init)
      apbcores[i].core->init ();
}

static void
apbmst_reset ()
{
  int i;

  for (i = 0; i < apbi; i++)
    if (apbcores[i].core->reset)
      apbcores[i].core->reset ();
}

static int
apbmst_read (uint32 addr, uint32 * data)
{
  int i;
  int res = 0;

  for (i = 0; i < apbi; i++)
    {
      if ((addr >= apbcores[i].start) && (addr < apbcores[i].end))
	{
	  res = 1;
	  if (apbcores[i].core->read)
	    apbcores[i].core->read (addr & apbcores[i].mask, data);
	  break;
	}
    }
  if (!res && (addr >= 0xFF000))
    {
      *data = grlib_apbpnp_read (addr);
      if (sis_verbose > 1)
	printf ("APB PP read a: %08x, d: %08x\n", addr, *data);
    }
  return 1;
}

static int
apbmst_write (uint32 addr, uint32 * data, uint32 size)
{
  int i;

  for (i = 0; i < apbi; i++)
    if ((addr >= apbcores[i].start) && (addr < apbcores[i].end))
      {
	if (apbcores[i].core->write)
	  apbcores[i].core->write (addr & apbcores[i].mask, data, size);
	break;
      }
  return 1;
}

static void
apbmst_add (int irq, uint32 addr, uint32 mask)
{
  grlib_ahbspp_add (GRLIB_PP_ID (VENDOR_GAISLER, GAISLER_APBMST, 0, 0),
		    GRLIB_PP_AHBADDR (addr, mask, 0, 0, 2), 0, 0, 0);
  if (sis_verbose)
    printf (" AHB/APB Bridge                     0x%08x\n", addr);
}

const struct grlib_ipcore apbmst = {
  apbmst_init, apbmst_reset, apbmst_read, apbmst_write, apbmst_add
};

/* ------------------- IRQMP -----------------------*/

#define IRQMP_IPR	0x04
#define IRQMP_IFR 	0x08
#define IRQMP_ICR 	0x0C
#define IRQMP_ISR 	0x10
#define IRQMP_IBR 	0x14
#define IRQMP_IMR 	0x40
#define IRQMP_IMR1	0x44
#define IRQMP_IMR2	0x48
#define IRQMP_IMR3	0x4C
#define IRQMP_IFR0 	0x80
#define IRQMP_IFR1 	0x84
#define IRQMP_IFR2 	0x88
#define IRQMP_IFR3 	0x8C
#define IRQMP_PEXTACK0 	0xC0
#define IRQMP_PEXTACK1 	0xC4
#define IRQMP_PEXTACK2 	0xC8
#define IRQMP_PEXTACK3 	0xCC

static void irqmp_intack (int level, int cpu);
static void chk_irq (void);

/* IRQMP registers.  */

static uint32 irqmp_ipr;
static uint32 irqmp_ibr;
static uint32 irqmp_imr[NCPU];
static uint32 irqmp_ifr[NCPU];
static uint32 irqmp_pextack[NCPU];

/* Mask with the supported interrupts */
static uint32 irqmp_mask;

/* The extended interrupt line (a zero value disables the feature) */
int irqmp_extirq;

static void
irqmp_init (void)
{
  int i;

  for (i = 0; i < NCPU; i++)
    {
      sregs[i].intack = irqmp_intack;
    }

  if (irqmp_extirq)
    irqmp_mask = 0xfffffffe;
  else
    irqmp_mask = 0x0000fffe;
}

static void
irqmp_reset (void)
{
  int i;

  irqmp_ipr = 0;
  for (i = 0; i < NCPU; i++)
    {
      irqmp_imr[i] = 0;
      irqmp_ifr[i] = 0;
      irqmp_pextack[i] = 0;
    }
}

static void
irqmp_intack (int level, int cpu)
{
  int bit = 1 << level;

  if (sis_verbose > 2)
    printf ("%8" PRIu64 " cpu %d interrupt %d acknowledged\n",
	    ebase.simtime, cpu, level);

  irqmp_pextack[cpu] = 0;
  if (level == irqmp_extirq)
    {
      int i;

      for (i = 16; i < 32; ++i)
	if ((irqmp_ipr & (1 << i)) & irqmp_imr[cpu])
	  {
	    if (sis_verbose > 2)
	      printf ("%8" PRIu64 " cpu %d set extended interrupt "
		      "acknowledge to %i\n", ebase.simtime, cpu, i);
	    irqmp_ipr &= ~(1 << i);
	    irqmp_pextack[cpu] = i;
	    break;
	  }
    }

  if (irqmp_ifr[cpu] & bit)
    irqmp_ifr[cpu] &= ~bit;
  else
    irqmp_ipr &= ~bit;
  chk_irq ();
}

static void
chk_irq ()
{
  int32 i, cpu;
  uint32 itmp;
  int old_irl;

  for (cpu = 0; cpu < ncpu; cpu++)
    {
      old_irl = ext_irl[cpu];
      itmp = ((irqmp_ipr | irqmp_ifr[cpu]) & irqmp_imr[cpu]) & irqmp_mask;
      if (itmp & 0xffff0000)
	{
	  if (sis_verbose > 2)
	    printf ("%8" PRIu64 " cpu %d forward extended interrupt\n",
		    ebase.simtime, cpu);
	  itmp |= 1 << irqmp_extirq;
	}
      ext_irl[cpu] = 0;
      if (itmp != 0)
	{
	  for (i = 15; i > 0; i--)
	    {
	      if (((itmp >> i) & 1) != 0)
		{
		  if ((sis_verbose > 2) && (i != old_irl))
		    printf ("%8" PRIu64 " cpu %d irl: %d\n",
			    ebase.simtime, cpu, i);
		  ext_irl[cpu] = i;
		  break;
		}
	    }
	}
    }
}

void
grlib_set_irq (int32 level)
{
  int i;

  if ((irqmp_ibr >> level) & 1)
    for (i = 0; i < ncpu; i++)
      irqmp_ifr[i] |= (1 << level);
  else
    irqmp_ipr |= (1 << level);
  chk_irq ();
}

static int
irqmp_read (uint32 addr, uint32 * data)
{
  int i;

  switch (addr & 0xff)
    {
    case IRQMP_IPR:		/* 0x04 */
      *data = irqmp_ipr;
      break;

    case IRQMP_IFR:		/* 0x08 */
      *data = irqmp_ifr[0];
      break;

    case IRQMP_ISR:		/* 0x10 */
      *data = ((ncpu - 1) << 28) | (irqmp_extirq << 16);
      for (i = 0; i < ncpu; i++)
	*data |= (sregs[i].pwd_mode << i);
      break;

    case IRQMP_IBR:		/* 0x14 */
      *data = irqmp_ibr;
      break;

    case IRQMP_IMR:		/* 0x40 */
      *data = irqmp_imr[0];
      break;

    case IRQMP_IMR1:		/* 0x44 */
      *data = irqmp_imr[1];
      break;

    case IRQMP_IMR2:		/* 0x48 */
      *data = irqmp_imr[2];
      break;

    case IRQMP_IMR3:		/* 0x4C */
      *data = irqmp_imr[3];
      break;

    case IRQMP_IFR0:		/* 0x80 */
      *data = irqmp_ifr[0];
      break;

    case IRQMP_IFR1:		/* 0x84 */
      *data = irqmp_ifr[1];
      break;

    case IRQMP_IFR2:		/* 0x88 */
      *data = irqmp_ifr[2];
      break;

    case IRQMP_IFR3:		/* 0x8C */
      *data = irqmp_ifr[3];
      break;

    case IRQMP_PEXTACK0:	/* 0xC0 */
      *data = irqmp_pextack[0];
      break;

    case IRQMP_PEXTACK1:	/* 0xC4 */
      *data = irqmp_pextack[1];
      break;

    case IRQMP_PEXTACK2:	/* 0xC8 */
      *data = irqmp_pextack[2];
      break;

    case IRQMP_PEXTACK3:	/* 0xCC */
      *data = irqmp_pextack[3];
      break;

    default:
      *data = 0;
    }
}

static int
irqmp_write (uint32 addr, uint32 * data, uint32 size)
{
  int i;

  switch (addr & 0xff)
    {

    case IRQMP_IPR:		/* 0x04 */
      irqmp_ipr = *data & irqmp_mask;
      chk_irq ();
      break;

    case IRQMP_IFR:		/* 0x08 */
      irqmp_ifr[0] = *data & 0xfffe;
      chk_irq ();
      break;

    case IRQMP_ICR:		/* 0x0C */
      irqmp_ipr &= ~*data & irqmp_mask;
      chk_irq ();
      break;

    case IRQMP_ISR:		/* 0x10 */
      for (i = 0; i < ncpu; i++)
	{
	  if ((*data >> i) & 1)
	    {
	      if (sregs[i].pwd_mode)
		{
		  sregs[i].simtime = ebase.simtime;
		  if (sis_verbose > 1)
		    printf ("%8" PRIu64 " cpu %d starting\n", ebase.simtime,
			    i);
		  sregs[i].pwdtime += ebase.simtime - sregs[i].pwdstart;
		}
	      sregs[i].pwd_mode = 0;
	    }
	}
      break;

    case IRQMP_IBR:		/* 0x14 */
      irqmp_ibr = *data & 0xfffe;
      break;

    case IRQMP_IMR:		/* 0x40 */
      irqmp_imr[0] = *data & irqmp_mask;
      chk_irq ();
      break;

    case IRQMP_IMR1:		/* 0x44 */
      irqmp_imr[1] = *data & irqmp_mask;
      chk_irq ();
      break;

    case IRQMP_IMR2:		/* 0x48 */
      irqmp_imr[2] = *data & irqmp_mask;
      chk_irq ();
      break;

    case IRQMP_IMR3:		/* 0x4C */
      irqmp_imr[3] = *data & irqmp_mask;
      chk_irq ();
      break;

    case IRQMP_IFR0:		/* 0x80 */
      irqmp_ifr[0] = *data & 0xfffe;
      chk_irq ();
      break;

    case IRQMP_IFR1:		/* 0x84 */
      irqmp_ifr[1] = *data & 0xfffe;
      chk_irq ();
      break;

    case IRQMP_IFR2:		/* 0x88 */
      irqmp_ifr[2] = *data & 0xfffe;
      chk_irq ();
      break;

    case IRQMP_IFR3:		/* 0x8C */
      irqmp_ifr[3] = *data & 0xfffe;
      chk_irq ();
      break;
    }
}

static void
irqmp_add (int irq, uint32 addr, uint32 mask)
{
  grlib_apbpp_add (GRLIB_PP_ID (VENDOR_GAISLER, GAISLER_IRQMP, 2, 0),
		   GRLIB_PP_APBADDR (addr, mask));
  if (sis_verbose)
    printf (" IRQMP Interrupt controller         0x%08x\n", addr);
}

const struct grlib_ipcore irqmp = {
  irqmp_init, irqmp_reset, irqmp_read, irqmp_write, irqmp_add
};

/* ------------------- GPTIMER -----------------------*/

gp_timer_apbctrl1 gptimer1;
gp_timer_apbctrl2 gptimer2;

static void
gptimer_apbctrl1_intr (int32 arg)
{
  // unused parameter
  (void)(arg);

  gptimer_update (&gptimer1.core, gptimer1.timers, GPTIMER_APBCTRL1_SIZE);
  uint32_t separate_irq_flag = gptimer_get_flag (gptimer1.core.configuration_register, GPT_SI);

  for (int i = 0; i < GPTIMER_APBCTRL1_SIZE; i++)
  {
    if (gptimer_get_flag (gptimer1.timers[i].control_register, GPT_IP))
    {
      int32_t irq_flag = separate_irq_flag ? GPTIMER_APBCTRL1_INTERRUPT_BASE_NR + i : GPTIMER_APBCTRL1_INTERRUPT_BASE_NR;
      grlib_set_irq (irq_flag);
      gptimer_reset_flag (&gptimer1.timers[i].control_register, GPT_IP);
    }
  }

  event (gptimer_apbctrl1_intr, 0, 1);
}

static void
gptimer_apbctrl2_intr (int32 arg)
{
  // unused parameter
  (void)(arg);

  gptimer_update (&gptimer2.core, gptimer2.timers, GPTIMER_APBCTRL2_SIZE);

  bool timers_latched = false;
  uint32_t latch_configuration_register = gptimer_read_core_register (&gptimer2.core, GPTIMER_LATCH_CONFIGURATION_REGISTER_ADDRESS);
  
  uint32_t irq_pending = 0;
  uint32_t irq_force = 0;
  irqmp_read (IRQMP_IPR, &irq_pending);
  irqmp_read (IRQMP_IFR, &irq_force);
  uint32_t irq_vector = irq_pending | irq_force;

  if (gptimer_get_flag (gptimer2.core.configuration_register, GPT_EL))
  {
    for (int i = 0; i < 32; i++)
    {
      if (((irq_vector >> i) & (latch_configuration_register >> i)) & GPTIMER_FLAG_MASK)
      {
        timers_latched = true;
        gptimer_reset_flag (&gptimer2.core.configuration_register, GPT_EL);
        break;
      }
    }
  }

  for (int i = 0; i < GPTIMER_APBCTRL2_SIZE; i++)
  {
    if (gptimer_get_flag (gptimer2.timers[i].control_register, GPT_IP))
    {
      grlib_set_irq (GPTIMER_APBCTRL2_INTERRUPT_BASE_NR);
      gptimer_reset_flag (&gptimer2.timers[i].control_register, GPT_IP);
    }
    
    if (timers_latched && gptimer_get_flag (gptimer2.timers[i].control_register, GPT_EN))
    {
      gptimer_write_timer_register (&gptimer2.timers[i], GPTIMER_TIMER_LATCH_REGISTER_ADDRESS, NULL);
    }
  }

  event (gptimer_apbctrl2_intr, 0, 1);
}

static void
gptimer_apbctrl1_init (void)
{
  gptimer_apbctrl1_timer_reset ();
}

static void
gptimer_apbctrl2_init (void)
{
  gptimer_apbctrl2_timer_reset ();
}

static void
gptimer_add (int irq, uint32 addr, uint32 mask)
{
  grlib_apbpp_add (GRLIB_PP_ID (VENDOR_GAISLER, GAISLER_GPTIMER, 0, irq),
		   GRLIB_PP_APBADDR (addr, mask));
  if (sis_verbose)
    printf (" GPTIMER timer unit                 0x%08x   %d\n", addr, irq);
}

static void
gptimer_apbctrl1_add (int irq, uint32 addr, uint32 mask)
{
  gptimer_add (irq, addr, mask);
}

static void
gptimer_apbctrl2_add (int irq, uint32 addr, uint32 mask)
{
  gptimer_add (irq, addr, mask);
}

static void
gptimer_apbctrl1_reset (void)
{
  gptimer_apbctrl1_timer_reset ();

  remove_event (gptimer_apbctrl1_intr, -1);
  event (gptimer_apbctrl1_intr, 0, 1);

  if (sis_verbose)
  {
    printf ("GPT started (period %d)\n\r", gptimer1.core.scaler_register);
  }
}

static void
gptimer_apbctrl2_reset (void)
{
  gptimer_apbctrl2_timer_reset ();

  remove_event (gptimer_apbctrl2_intr, -1);
  event (gptimer_apbctrl2_intr, 0, 1);

  if (sis_verbose)
  {
    printf ("GPT started (period %d)\n\r", gptimer2.core.scaler_register);
  }
}

static int
gptimer_read (gp_timer_core *core, gp_timer *timers, uint32 addr, uint32 * data)
{
  uint32_t address_masked = addr & GPTIMER_REGISTERS_MASK;
  switch (address_masked & GPTIMER_OFFSET_MASK)
  {
    case CORE_OFFSET:
    {
      *data = gptimer_read_core_register (core, address_masked);
      break;
    }
    case GPTIMER1_OFFSET:
    {
      *data = gptimer_read_timer_register (&timers[0], address_masked);
      break;
    }
    case GPTIMER2_OFFSET:
    {
      *data = gptimer_read_timer_register (&timers[1], address_masked);
      break;
    }
    case GPTIMER3_OFFSET:
    {
      *data = gptimer_read_timer_register (&timers[2], address_masked);
      break;
    }
    case GPTIMER4_OFFSET:
    {
      *data = gptimer_read_timer_register (&timers[3], address_masked);
      break;
    }
    default:
    {
      *data = 0;
    } 
  }
}

static int 
gptimer_apbctrl1_read (uint32 addr, uint32 * data)
{
  gptimer_read (&gptimer1.core, gptimer1.timers, addr, data);
}

static int 
gptimer_apbctrl2_read (uint32 addr, uint32 * data)
{
  gptimer_read (&gptimer2.core, gptimer2.timers, addr, data);
}

static int
gptimer_timer_write (gp_timer *timers, uint32 addr, uint32 * data)
{
  switch (addr & GPTIMER_OFFSET_MASK)
  {
    case GPTIMER1_OFFSET:
    {
      gptimer_write_timer_register (&timers[0], addr, data);
      break;
    }
    case GPTIMER2_OFFSET:
    {
      gptimer_write_timer_register (&timers[1], addr, data);
      break;
    }
    case GPTIMER3_OFFSET:
    {
      gptimer_write_timer_register (&timers[2], addr, data);
      break;
    }
    case GPTIMER4_OFFSET:
    {
      gptimer_write_timer_register (&timers[3], addr, data);
      break;
    }
    default:
    {
      *data = 0;
    } 
  }
}

static int
gptimer_apbctrl1_write (uint32 addr, uint32 * data, uint32 sz)
{
  if ((addr & GPTIMER_OFFSET_MASK) == CORE_OFFSET)
  {
    gptimer_apbctrl1_write_core_register (addr, data);
  }
  else
  {
    gptimer_timer_write (gptimer1.timers, addr, data);
  }
}

static int
gptimer_apbctrl2_write (uint32 addr, uint32 * data, uint32 sz)
{
  if ((addr & GPTIMER_OFFSET_MASK) == CORE_OFFSET)
  {
    gptimer_apbctrl2_write_core_register (addr, data);
  }
  else
  {
    gptimer_timer_write (gptimer2.timers, addr, data);
  }
}

const struct grlib_ipcore gptimer_apbctrl1 = {
  gptimer_apbctrl1_init, gptimer_apbctrl1_reset, gptimer_apbctrl1_read, gptimer_apbctrl1_write, gptimer_apbctrl1_add
};

const struct grlib_ipcore gptimer_apbctrl2 = {
  gptimer_apbctrl2_init, gptimer_apbctrl2_reset, gptimer_apbctrl2_read, gptimer_apbctrl2_write, gptimer_apbctrl2_add
};

/* APBUART.  */

#ifndef O_NONBLOCK
#define O_NONBLOCK 0
#endif

apbuart_type uarts[APBUART_NUM];
int uart_dumbio;
int uart_nouartrx;
int uart_sis_verbose;
int uart_tty_setup;

void
apbuart_init_stdio (void)
{
  uart_init_stdio(&uarts[0]);
  uart_init_stdio(&uarts[1]);
  uart_init_stdio(&uarts[2]);
  uart_init_stdio(&uarts[3]);
  uart_init_stdio(&uarts[4]);
  uart_init_stdio(&uarts[5]);
}

void
apbuart_restore_stdio (void)
{
  uart_restore_stdio(&uarts[0]);
  uart_restore_stdio(&uarts[1]);
  uart_restore_stdio(&uarts[2]);
  uart_restore_stdio(&uarts[3]);
  uart_restore_stdio(&uarts[4]);
  uart_restore_stdio(&uarts[5]);
}

static void
apbuart0_init (void)
{
  uart_init(&uarts[0]);
}

static void
apbuart1_init (void)
{
  uart_init(&uarts[1]);
}

static void
apbuart2_init (void)
{
  uart_init(&uarts[2]);
}

static void
apbuart3_init (void)
{
  uart_init(&uarts[3]);
}

static void
apbuart4_init (void)
{
  uart_init(&uarts[4]);
}

static void
apbuart5_init (void)
{
  uart_init(&uarts[5]);
}

static void
uart_rx_event (int32 arg)
{
  apbuart_type *uart = get_uart_by_irq (arg);

  if (uart != NULL)
  {
    if (apbuart_read_event (uart) && apbuart_get_flag (uart->control_register, APBUART_RI))
    {
      grlib_set_irq (uart->irq);
    }

    event (uart_rx_event, uart->irq, UART_RX_TIME);
  }
}

static void
fast_uart_rx_event (int32 arg)
{
  apbuart_type *uart = get_uart_by_irq (arg);

  if (uart != NULL)
  {
    if (apbuart_fast_read_event (uart) && apbuart_get_flag (uart->control_register, APBUART_RI))
    {
      grlib_set_irq (uart->irq);
    }

    event (fast_uart_rx_event, uart->irq, UART_RX_TIME);
  }
}

int
uart_read (apbuart_type *uart, uint32_t addr, uint32_t *data)
{
  int result = 1;

  switch (addr & APBUART_REGISTER_TYPE_MASK)
  {
    case APBUART_DATA_REGISTER_ADDRESS:
      apbuart_reset_flag(&uart->status_register, APBUART_DR);
      *data = uart->uart_io.in.buffer[uart->uart_io.in.buffer_index];
#ifdef FAST_UART
      fast_uart_rx_event (uart->irq);
#endif
      result = 0;
      break;

    case APBUART_STATUS_REGISTER_ADDRESS:
      *data = uart->status_register;
      result = 0;
      break;

    case APBUART_CONTROL_REGISTER_ADDRESS:
      *data = uart->control_register;
      result = 0;
      break;
    default:
      if (sis_verbose)
      {
        printf ("Read from unimplemented UART register (%x)\n", uart->address);
      }
  }

  return result;
}

static int
apbuart0_read (uint32 addr, uint32 * data)
{
  return uart_read (&uarts[0], addr, data);
}

static int
apbuart1_read (uint32 addr, uint32 * data)
{
  return uart_read (&uarts[1], addr, data);
}

static int
apbuart2_read (uint32 addr, uint32 * data)
{
  return uart_read (&uarts[2], addr, data);
}

static int
apbuart3_read (uint32 addr, uint32 * data)
{
  return uart_read (&uarts[3], addr, data);
}

static int
apbuart4_read (uint32 addr, uint32 * data)
{
  return uart_read (&uarts[4], addr, data);
}

static int
apbuart5_read (uint32 addr, uint32 * data)
{
  return uart_read (&uarts[5], addr, data);
}

static void 
uart_tx_event (int32_t arg)
{
  apbuart_type *uart = get_uart_by_irq(arg);

  if (uart != NULL)
  {
    if (apbuart_write_event (uart) && apbuart_get_flag (uart->control_register, APBUART_TI))
    {
      grlib_set_irq (uart->irq);
    }

    event (uart_tx_event, uart->irq, UART_TX_TIME);
  }  
}

static void
fast_uart_tx_event (int32 arg)
{
  apbuart_type *uart = get_uart_by_irq (arg);

  if (uart != NULL)
  {
    if (apbuart_fast_write_event (uart) && apbuart_get_flag (uart->control_register, APBUART_TI))
    {
      grlib_set_irq (uart->irq);
    }

    event (fast_uart_tx_event, uart->irq, UART_FLUSH_TIME);
  }
}

int
uart_write (apbuart_type *uart, uint32_t addr, uint32_t * data, uint32_t sz)
{
  int result = 1;

  switch (addr & APBUART_REGISTER_TYPE_MASK)
  {
    case APBUART_DATA_REGISTER_ADDRESS:
    {
#ifdef FAST_UART
      if (apbuart_fast_write_to_uart_buffer (uart, data) && apbuart_get_flag (uart->control_register, APBUART_TI))
      {
        grlib_set_irq (uart->irq);
      }    
#else
      uart->uart_io.out.buffer[0] = (unsigned char) *data;
      uart->uart_io.out.buffer_size = 1;
      apbuart_reset_flag(&uart->status_register, APBUART_TS);
#endif
      result = 0;
      break;
    }
    case APBUART_STATUS_REGISTER_ADDRESS:
    {
      break;
    }
    case APBUART_CONTROL_REGISTER_ADDRESS:
    {
      uart->control_register = (*data & APBUART_CONTROL_REGISTER_WRITE_MASK);
      result = 0;
      break;
    }
    default:
    {
      if (sis_verbose)
      {
        printf ("Write to unimplemented UART register (%x)\n", uart->address);
      }
    }
  }

  return result;
}

static int
apbuart0_write (uint32 addr, uint32 * data, uint32 sz)
{
  return uart_write (&uarts[0], addr, data, sz);
}

static int
apbuart1_write (uint32 addr, uint32 * data, uint32 sz)
{
  return uart_write (&uarts[1], addr, data, sz);
}

static int
apbuart2_write (uint32 addr, uint32 * data, uint32 sz)
{
  return uart_write (&uarts[2], addr, data, sz);
}

static int
apbuart3_write (uint32 addr, uint32 * data, uint32 sz)
{
  return uart_write (&uarts[3], addr, data, sz);
}

static int
apbuart4_write (uint32 addr, uint32 * data, uint32 sz)
{
  return uart_write (&uarts[4], addr, data, sz);
}

static int
apbuart5_write (uint32 addr, uint32 * data, uint32 sz)
{
  return uart_write (&uarts[5], addr, data, sz);
}

static void
uart_event_start (int uart_irq)
{
#ifdef FAST_UART
  event (fast_uart_rx_event, uart_irq, UART_RX_TIME);
  event (fast_uart_tx_event, uart_irq, UART_FLUSH_TIME);
#else
#ifndef _WIN32
  event (uart_rx_event, uart_irq, UART_RX_TIME);
  event (uart_tx_event, uart_irq, UART_TX_TIME);
#endif
#endif
}

static void
apbuart0_reset (void)
{
  uart_reset(&uarts[0]);
  uart_event_start (uarts[0].irq);
}

static void
apbuart1_reset (void)
{
  uart_reset(&uarts[1]);
  uart_event_start (uarts[1].irq);
}

static void
apbuart2_reset (void)
{
  uart_reset(&uarts[2]);
  uart_event_start (uarts[2].irq);
}

static void
apbuart3_reset (void)
{
  uart_reset(&uarts[3]);
  uart_event_start (uarts[3].irq);
}

static void
apbuart4_reset (void)
{
  uart_reset(&uarts[4]);
  uart_event_start (uarts[4].irq);
}

static void
apbuart5_reset (void)
{
  uart_reset(&uarts[5]);
  uart_event_start (uarts[5].irq);
}

int
uart_add (apbuart_type *uart)
{
  int result = 0;

  result = grlib_apbpp_add (GRLIB_PP_ID (VENDOR_GAISLER, GAISLER_APBUART, 1, uart->irq),
        GRLIB_PP_APBADDR (uart->address, APBUART_ADDR_MASK));
  if (sis_verbose)
    printf (" APBUART serial port                0x%08x   %d\n", uart->address, uart->irq);

  return result;
}

static void
apbuart_add (int irq, uint32 addr, uint32 mask)
{
  apbuart_type *uart = get_uart_by_address (addr);

  if (uart != NULL)
  {
    if (strcmp (uart->uart_io.device.device_path, "") != 0)
    {
      uart->address = addr;
      uart->irq = irq;

      uart_add (uart);
    }
  }
}

const struct grlib_ipcore apbuart0 = {
  apbuart0_init, apbuart0_reset, apbuart0_read, apbuart0_write, apbuart_add
};

const struct grlib_ipcore apbuart1 = {
  apbuart1_init, apbuart1_reset, apbuart1_read, apbuart1_write, apbuart_add
};

const struct grlib_ipcore apbuart2 = {
  apbuart2_init, apbuart2_reset, apbuart2_read, apbuart2_write, apbuart_add
};

const struct grlib_ipcore apbuart3 = {
  apbuart3_init, apbuart3_reset, apbuart3_read, apbuart3_write, apbuart_add
};

const struct grlib_ipcore apbuart4 = {
  apbuart4_init, apbuart4_reset, apbuart4_read, apbuart4_write, apbuart_add
};

const struct grlib_ipcore apbuart5 = {
  apbuart5_init, apbuart5_reset, apbuart5_read, apbuart5_write, apbuart_add
};

/* ------------------- SDCTRL -----------------------*/

/* Store data in host byte order.  MEM points to the beginning of the
   emulated memory; WADDR contains the index the emulated memory,
   DATA points to words in host byte order to be stored.  SZ contains log(2)
   of the number of bytes to retrieve, and can be 0 (1 byte), 1 (one half-word),
   2 (one word), or 3 (two words); WS should return the number of wait-states. */

void
grlib_store_bytes (char *mem, uint32 waddr, uint32 * data, int32 sz)
{
  if (sz == 2)
    memcpy (&mem[waddr], data, 4);
  else
    switch (sz)
      {
      case 0:
	waddr ^= arch->bswap;
	mem[waddr] = *data & 0x0ff;
	break;
      case 1:
	waddr ^= arch->bswap & 2;
	*((uint16 *) & mem[waddr]) = (*data & 0x0ffff);
	break;
      case 3:
	memcpy (&mem[waddr], data, 8);
	break;
      }
}

static int
sdctrl_write (uint32 addr, uint32 * data, uint32 sz)
{
  grlib_store_bytes (ramb, addr, data, sz);
  return 1;
}

static int
sdctrl_read (uint32 addr, uint32 * data)
{
  memcpy (data, &ramb[addr & ~0x3], 4);
  return 4;
}

static void
sdctrl_add (int irq, uint32 addr, uint32 mask)
{
  grlib_ahbspp_add (GRLIB_PP_ID (VENDOR_GAISLER, GAISLER_SDCTRL, 0, 0),
		    GRLIB_PP_AHBADDR (addr, mask, 1, 1, 2), 0, 0, 0);
  if (sis_verbose)
    printf (" SDRAM controller %d M              0x%08x\n",
	    (~(mask << 20) + 1) >> 20, addr);
}

const struct grlib_ipcore sdctrl = {
  NULL, NULL, sdctrl_read, sdctrl_write, sdctrl_add
};

/* ------------------- srctrl -----------------------*/
/* used only for ROM access */

static int
srctrl_write (uint32 addr, uint32 * data, uint32 sz)
{
  grlib_store_bytes (romb, addr, data, sz);
  return 1;
}

static int
srctrl_read (uint32 addr, uint32 * data)
{
  memcpy (data, &romb[addr & ~0x3], 4);
  return 4;
}

static void
srctrl_add (int irq, uint32 addr, uint32 mask)
{
  grlib_ahbspp_add (GRLIB_PP_ID (VENDOR_GAISLER, GAISLER_SRCTRL, 0, 0),
		    GRLIB_PP_AHBADDR (addr, mask, 1, 1, 2), 0, 0, 0);
  if (sis_verbose)
    printf (" PROM controller %d M               0x%08x\n",
	    (~(mask << 20) + 1) >> 20, addr);
}

const struct grlib_ipcore srctrl = {
  NULL, NULL, srctrl_read, srctrl_write, srctrl_add
};

/* ------------------- boot init --------------------*/
void
grlib_boot_init (void)
{
  uint32 tmp;
  tmp = ebase.freq - 1;

  gptimer_apbctrl1_write (GPTIMER_SCALER_RELOAD_VALUE_REGISTER_ADDRESS, &tmp, 2);
  tmp = -1;
  gptimer_apbctrl1_write (GPTIMER1_OFFSET | GPTIMER_TIMER_COUNTER_VALUE_REGISTER_ADDRESS, &tmp, 2);
  gptimer_apbctrl1_write (GPTIMER1_OFFSET | GPTIMER_TIMER_RELOAD_VALUE_REGISTER_ADDRESS, &tmp, 2);
  tmp = 7;
  gptimer_apbctrl1_write (GPTIMER1_OFFSET | GPTIMER_TIMER_CONTROL_REGISTER_ADDRESS, &tmp, 2);
}

/* ------------------- ns16550 -----------------------*/
static void plic_irq (int irq);
static int32 uart_lcr, uart_ie, uart_mcr, ns16550_irq, uart_txctrl;

static void
ns16550_add (int irq, uint32 addr, uint32 mask)
{
  grlib_ahbspp_add (GRLIB_PP_ID (VENDOR_CONTRIB, CONTRIB_NS16550, 0, 0),
		    GRLIB_PP_AHBADDR (addr, mask, 0, 0, 2), 0, 0, 0);
  ns16550_irq = irq;
  if (sis_verbose)
    printf (" NS16550 UART                       0x%08x   %d\n", addr, irq);
}

static int
ns16550_write (uint32 addr, uint32 * data, uint32 sz)
{
  switch (addr & 0xff)
    {
    case 0:
      if (!uart_lcr)
	{
	  putchar (*data & 0xff);
	}
      break;
    case 4:
      if (!uart_lcr)
	uart_ie = *data & 0xff;
      break;
    case 0x08:
      uart_txctrl = *data & 0xff;
      break;
    case 0x0c:
      uart_lcr = *data & 0x80;
      break;
    case 0x10:
      uart_mcr = *data & 0xff;
      break;
    }

  if ((uart_ie & 0x2) && (uart_mcr & 0x8))
    plic_irq (ns16550_irq);
  return 1;
}

static int
ns16550_read (uint32 addr, uint32 * data)
{
  *data = 0;
  switch (addr & 0xff)
    {
    case 0x04:
      *data = uart_ie;
      break;
    case 0x08:
      *data = uart_txctrl;
      break;
    case 0x10:
      *data = uart_mcr;		//0x03;
      break;
    case 0x14:
      *data = 0x60;
      break;
    }
  return 4;
}

static void
ns16550_reset (void)
{
  uart_lcr = 0;
  uart_ie = 0;
  uart_mcr = 0;
}

const struct grlib_ipcore ns16550 = {
  NULL, ns16550_reset, ns16550_read, ns16550_write, ns16550_add
};

/* ------------------- clint -------------------------*/

static void
clint_add (int irq, uint32 addr, uint32 mask)
{
  grlib_ahbspp_add (GRLIB_PP_ID (VENDOR_CONTRIB, CONTRIB_CLINT, 0, 0),
		    GRLIB_PP_AHBADDR (addr, mask, 0, 0, 2), 0, 0, 0);
  if (sis_verbose)
    printf (" CLINT Interrupt controller         0x%08x   %d\n", addr, irq);
}

#define CLINTSTART  	0x00000
#define CLINTEND  	0x10000
#define CLINT_TIMECMP	0x04000
#define CLINT_TIMEBASE	0x0BFF8
static int
clint_read (uint32 addr, uint32 * data)
{
  uint64 tmp;
  int reg, cpuid;

  reg = (addr >> 2) & 1;
  cpuid = ((addr >> 3) % NCPU);
  if ((addr >= CLINT_TIMEBASE) && (addr < CLINTEND))
    {
      tmp = ebase.simtime >> 32;
      if (reg)
	*data = tmp & 0xffffffff;
      else
	*data = ebase.simtime & 0xffffffff;
    }
  else if ((addr >= CLINT_TIMECMP) && (addr < CLINT_TIMEBASE))
    {
      tmp = sregs[cpuid].mtimecmp >> 32;
      if (reg)
	*data = tmp & 0xffffffff;
      else
	*data = sregs[cpuid].mtimecmp & 0xffffffff;
    }
  else if ((addr >= 0) && (addr < CLINT_TIMECMP))
    {
      cpuid = ((addr >> 2) % NCPU);
      *data = ((sregs[cpuid].mip & MIP_MSIP) >> 4) & 1;
    }

  return 4;
}

static void
set_mtip (int32 arg)
{
  sregs[arg].mip |= MIP_MTIP;
  rv32_check_lirq (arg);
}

static int
clint_write (uint32 addr, uint32 * data, uint32 sz)
{
  uint64 tmp;
  int reg, cpuid;

  if ((addr >= CLINTSTART) && (addr < CLINTEND))
    {
      reg = (addr >> 2) & 1;
      if ((addr >= CLINT_TIMECMP) && (addr <= CLINT_TIMEBASE))
	{
	  cpuid = ((addr >> 3) % NCPU);
	  if (reg)
	    {
	      tmp = sregs[cpuid].mtimecmp & 0xffffffff;
	      sregs[cpuid].mtimecmp = *data;
	      sregs[cpuid].mtimecmp <<= 32;
	      sregs[cpuid].mtimecmp |= tmp;
	    }
	  else
	    {
	      tmp = sregs[cpuid].mtimecmp >> 32;
	      sregs[cpuid].mtimecmp = (tmp << 32) | *data;
	    }
	  remove_event (set_mtip, cpuid);
	  sregs[cpuid].mip &= ~MIP_MTIP;
	  if (sregs[cpuid].mtimecmp <= sregs[cpuid].simtime)
	    sregs[cpuid].mip |= MIP_MTIP;
	  else
	    event (set_mtip, cpuid,
		   sregs[cpuid].mtimecmp - sregs[cpuid].simtime);
	}
      else if ((addr >= CLINTSTART) && (addr <= CLINT_TIMECMP))
	{
	  cpuid = ((addr >> 2) % NCPU);
	  if ((*data & 1) == 1)
	    sregs[cpuid].mip |= MIP_MSIP;
	  else
	    sregs[cpuid].mip &= ~MIP_MSIP;
	  rv32_check_lirq (cpuid);
	}
    }

  return 1;
}

const struct grlib_ipcore clint = {
  NULL, NULL, clint_read, clint_write, clint_add
};

/* ------------------- plic --------------------------*/
/* simplified functionality supported for now */

#define PLIC_PRIO 0
#define PLIC_IPEND 0x1000
#define PLIC_IENA  0x2000
#define PLIC_THRES 0x200000
#define PLIC_CLAIM 0x200004
#define PLIC_MASK1 0xFC

static unsigned char plic_prio[64];
static uint32 plic_ie[NCPU][2];
static uint32 plic_ip[2];
static uint32 plic_thres[NCPU];
static uint32 plic_claim[NCPU];

static void
plic_check_irq (uint32 hart)
{
  int i, irq;
  if (irq = (plic_ie[hart][0] & plic_ip[0]))
    {
      for (i = 1; i < 32; i++)
	{
	  if ((irq >> i) & 1)
	    plic_claim[hart] = i;
	}
      sregs[hart].mip |= MIP_MEIP;
    }
}

static void
plic_irq (int irq)
{
  int i;
  plic_ip[0] |= (1 << irq);
  for (i = 0; i < NCPU; i++)
    {
      plic_check_irq (i);
      rv32_check_lirq (i);
    }
}

static int
plic_read (uint32 addr, uint32 * data)
{
  int hart;
  if (addr >= PLIC_THRES)
    {
      hart = ((addr >> 12) & 0x0F) % NCPU;
      if ((addr & PLIC_MASK1) == 0)
	*data = plic_thres[hart];	// irq threshold, not used for now
      else
	{
	  *data = plic_claim[hart];
	  plic_claim[hart] = 0;
	  plic_ip[0] &= ~(1 << *data);
	}
    }
  else if (addr >= PLIC_IENA)
    {
      hart = ((addr >> 7) & 0x0F) % NCPU;
      if (addr & 4)
	*data = plic_ie[hart][1];	// irq enable
      else
	*data = plic_ie[hart][0];
    }
  else if (addr >= PLIC_IPEND)
    {
      hart = ((addr >> 7) & 0x0F) % NCPU;
      if (addr & 4)
	*data = plic_ip[1];	// irq pending
      else
	*data = plic_ip[0];
    }
  else if (addr < PLIC_IPEND)
    {
      *data = plic_prio[(addr & 0x0ff) >> 2];	// irq priority, not used for now
    }
  if (sis_verbose)
    printf (" PLIC read          0x%08x   %d\n", addr, *data);
  return 4;
}

static int
plic_write (uint32 addr, uint32 * data, uint32 sz)
{
  int hart;
  if (addr >= PLIC_THRES)
    {
      hart = ((addr >> 12) & 0x0F) % NCPU;
      if ((addr & PLIC_MASK1) == 0)
	plic_thres[hart] = *data;	// irq threshold, not used for now
      else
	plic_check_irq (hart);	// irq completion
    }
  else if (addr >= PLIC_IENA)
    {
      hart = ((addr >> 7) & 0x0F) % NCPU;
      if (addr & 4)
	plic_ie[hart][1] = *data;	// irq enable
      else
	plic_ie[hart][0] = *data;
    }
  else if (addr < PLIC_IPEND)
    {
      plic_prio[(addr & 0x0ff) >> 2] = *data;	// irq priority, not used for now
    }
  return 1;
}

static void
plic_add (int irq, uint32 addr, uint32 mask)
{
  grlib_ahbspp_add (GRLIB_PP_ID (VENDOR_CONTRIB, CONTRIB_PLIC, 0, 0),
		    GRLIB_PP_AHBADDR (addr, mask, 0, 0, 2), 0, 0, 0);
  if (sis_verbose)
    printf (" PLIC Interrupt controller          0x%08x   %d\n", addr, irq);
}

const struct grlib_ipcore plic = {
  NULL, NULL, plic_read, plic_write, plic_add
};

/* ------------------- sifive test module --------------*/
/* used to halt processor */

static int
s5test_write (uint32 addr, uint32 * data, uint32 sz)
{
  int i;

  switch (addr & 0xff)
    {
    case 0:
      if (*data == 0x5555)
	{
	  printf ("Power-off issued, exiting ...\n");
	  for (i = 0; i < ncpu; i++)
	    sregs[i].trap = ERROR_TRAP;
	}
      break;
    }

  return 1;
}

static void
s5test_add (int irq, uint32 addr, uint32 mask)
{
  grlib_ahbspp_add (GRLIB_PP_ID (VENDOR_CONTRIB, CONTRIB_S5TEST, 0, 0),
		    GRLIB_PP_AHBADDR (addr, mask, 0, 0, 2), 0, 0, 0);
  if (sis_verbose)
    printf (" S5 Test module                     0x%08x   %d\n", addr, irq);
}

const struct grlib_ipcore s5test = {
  NULL, NULL, NULL, s5test_write, s5test_add
};
