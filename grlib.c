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
      ahbscores[ahbsi].end = addr + ~(mask << 20) + 1;
      ahbscores[ahbsi].mask = ~(mask << 20);
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
	(apbcores[apbi].start + ~(mask << 8) + 1) & 0x0fffff;
      apbcores[apbi].mask = ~(mask << 8) & 0x0fffff;
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
    printf(" GRETH 10/100 Mbit Ethernet core    0x%08x   %d\n", addr, irq);
}

const struct grlib_ipcore greth = {
  NULL, NULL, grlib_greth_read, grlib_greth_write, greth_add
};


/* ------------------- LEON3 -----------------------*/

static void
leon3_add ()
{
  grlib_ahbmpp_add (GRLIB_PP_ID (VENDOR_GAISLER, GAISLER_LEON3, 0, 0));
  if (sis_verbose)
    printf(" LEON3 SPARC V8 processor                      \n");
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
    printf(" AHB/APB Bridge                     0x%08x\n", addr);
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

static void irqmp_intack (int level, int cpu);
static void chk_irq (void);

/* IRQMP registers.  */

static uint32 irqmp_ipr;
static uint32 irqmp_ibr;
static uint32 irqmp_imr[NCPU];
static uint32 irqmp_ifr[NCPU];

static void
irqmp_init (void)
{
  int i;

  for (i = 0; i < NCPU; i++)
    {
      sregs[i].intack = irqmp_intack;
    }
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
    }
}

static void
irqmp_intack (int level, int cpu)
{
  int irq_test;

  if ((sis_verbose > 2) && (level != 10))
    printf ("%8" PRIu64 " cpu %d interrupt %d acknowledged\n",
	    ebase.simtime, cpu, level);
  if (irqmp_ifr[cpu] & (1 << level))
    irqmp_ifr[cpu] &= ~(1 << level);
  else
    irqmp_ipr &= ~(1 << level);
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
      itmp = ((irqmp_ipr | irqmp_ifr[cpu]) & irqmp_imr[cpu]) & 0x0fffe;
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
      *data = ((ncpu - 1) << 28);
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

    case IRQMP_IFR:		/* 0x08 */
      irqmp_ifr[0] = *data & 0xfffe;
      chk_irq ();
      break;

    case IRQMP_ICR:		/* 0x0C */
      irqmp_ipr &= ~*data & 0x0fffe;
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
      irqmp_imr[0] = *data & 0x7ffe;
      chk_irq ();
      break;

    case IRQMP_IMR1:		/* 0x44 */
      irqmp_imr[1] = *data & 0x7ffe;
      chk_irq ();
      break;

    case IRQMP_IMR2:		/* 0x48 */
      irqmp_imr[2] = *data & 0x7ffe;
      chk_irq ();
      break;

    case IRQMP_IMR3:		/* 0x4C */
      irqmp_imr[3] = *data & 0x7ffe;
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
    printf(" IRQMP Interrupt controller         0x%08x\n", addr);
}

const struct grlib_ipcore irqmp = {
  irqmp_init, irqmp_reset, irqmp_read, irqmp_write, irqmp_add
};

/* ------------------- GPTIMER -----------------------*/

#define GPTIMER_SCALER  0x00
#define GPTIMER_SCLOAD  0x04
#define GPTIMER_CONFIG  0x08
#define GPTIMER_TIMER1 	0x10
#define GPTIMER_RELOAD1	0x14
#define GPTIMER_CTRL1 	0x18
#define GPTIMER_TIMER2 	0x20
#define GPTIMER_RELOAD2	0x24
#define GPTIMER_CTRL2 	0x28

#define NGPTIMERS  2

static uint32 gpt_irq;
static uint32 gpt_scaler;
static uint64 gpt_scaler_start;
static uint32 gpt_counter[NGPTIMERS];
static uint32 gpt_reload[NGPTIMERS];
static uint64 gpt_counter_start[NGPTIMERS];
static uint32 gpt_ctrl[NGPTIMERS];

static void gpt_intr (int32 arg);

static void
gpt_add_intr (int i)
{
  if (gpt_ctrl[i] & 1)
    {
      event (gpt_intr, i,
	     (uint64) (gpt_scaler + 1) * (uint64) ((uint64) gpt_counter[i] +
						   (uint64) 1));
      gpt_counter_start[i] = now ();
    }
}

static void
gpt_intr (int32 i)
{
  gpt_counter[i] = -1;
  if (gpt_ctrl[i] & 1)
    {
      if (gpt_ctrl[i] & 2)
	{
	  gpt_counter[i] = gpt_reload[i];
	}
      if (gpt_ctrl[i] & 8)
	{
	  grlib_set_irq (gpt_irq + i);
	}
      gpt_add_intr (i);
    }
}

static void
gpt_init (void)
{
}

static void
gpt_add (int irq, uint32 addr, uint32 mask)
{
  grlib_apbpp_add (GRLIB_PP_ID (VENDOR_GAISLER, GAISLER_GPTIMER, 0, irq),
		   GRLIB_PP_APBADDR (addr, mask));
  gpt_irq = irq;
  if (sis_verbose)
    printf(" GPTIMER timer unit                 0x%08x   %d\n", addr, irq);
}

static void
gpt_reset (void)
{
  gpt_counter[0] = 0xffffffff;
  gpt_reload[0] = 0xffffffff;
  gpt_scaler = 0xffff;
  gpt_ctrl[0] = 0;
  gpt_ctrl[1] = 0;
  remove_event (gpt_intr, -1);
  gpt_scaler_start = now ();
  if (sis_verbose)
    printf ("GPT started (period %d)\n\r", gpt_scaler + 1);
}

static void
gpt_add_intr_all ()
{
  int i;

  for (i = 0; i < NGPTIMERS; i++)
    {
      gpt_add_intr (i);
    }
}

static void
gpt_scaler_set (uint32 val)
{
  /* Mask for 16-bit scaler. */
  if (gpt_scaler != (val & 0x0ffff))
    {
      gpt_scaler = val & 0x0ffff;
      remove_event (gpt_intr, -1);
      gpt_scaler_start = now ();
      gpt_add_intr_all ();
    }
}

static void
gpt_ctrl_write (uint32 val, int i)
{
  if (val & 4)
    {
      /* Reload.  */
      gpt_counter[i] = gpt_reload[i];
    }
  if (val & 1)
    {
      gpt_ctrl[i] = val & 0xb;
      remove_event (gpt_intr, i);
      gpt_add_intr (i);
    }
  gpt_ctrl[i] = val & 0xb;
}

static uint32
gpt_counter_read (int i)
{
  if (gpt_ctrl[i] & 1)
    return gpt_counter[i] -
      ((now () - gpt_counter_start[i]) / (gpt_scaler + 1));
  else
    return gpt_counter[i];
}

static uint32
gpt_scaler_read ()
{
  return gpt_scaler - ((now () - gpt_scaler_start) % (gpt_scaler + 1));
}

static int
gpt_read (uint32 addr, uint32 * data)
{
  int i;

  switch (addr & 0xff)
    {
    case GPTIMER_SCALER:	/* 0x00 */
      *data = gpt_scaler_read ();
      break;

    case GPTIMER_SCLOAD:	/* 0x04 */
      *data = gpt_scaler;
      break;

    case GPTIMER_CONFIG:	/* 0x08 */
      *data = 0x100 | (gpt_irq << 3) | NGPTIMERS;
      break;

    case GPTIMER_TIMER1:	/* 0x10 */
      *data = gpt_counter_read (0);
      break;

    case GPTIMER_RELOAD1:	/* 0x14 */
      *data = gpt_reload[0];
      break;

    case GPTIMER_CTRL1:	/* 0x18 */
      *data = gpt_ctrl[0];
      break;

    case GPTIMER_TIMER2:	/* 0x20 */
      *data = gpt_counter_read (1);
      break;

    case GPTIMER_RELOAD2:	/* 0x24 */
      *data = gpt_reload[1];
      break;

    case GPTIMER_CTRL2:	/* 0x28 */
      *data = gpt_ctrl[1];
      break;

    default:
      *data = 0;
    }
}

static int
gpt_write (uint32 addr, uint32 * data, uint32 sz)
{
  int i;

  switch (addr & 0xff)
    {
    case GPTIMER_SCLOAD:	/* 0x04 */
      gpt_scaler_set (*data);
      break;

    case GPTIMER_TIMER1:	/* 0x10 */
      gpt_counter[0] = *data;
      remove_event (gpt_intr, 0);
      gpt_add_intr (0);
      break;

    case GPTIMER_RELOAD1:	/* 0x14 */
      gpt_reload[0] = *data;
      break;

    case GPTIMER_CTRL1:	/* 0x18 */
      gpt_ctrl_write (*data, 0);
      break;

    case GPTIMER_TIMER2:	/* 0x20 */
      gpt_counter[1] = *data;
      remove_event (gpt_intr, 1);
      gpt_add_intr (1);
      break;

    case GPTIMER_RELOAD2:	/* 0x24 */
      gpt_reload[1] = *data;
      break;

    case GPTIMER_CTRL2:	/* 0x28 */
      gpt_ctrl_write (*data, 1);
      break;

    }
}

const struct grlib_ipcore gptimer = {
  gpt_init, gpt_reset, gpt_read, gpt_write, gpt_add
};


/* APBUART.  */

#define APBUART_RXTX	0x00
#define APBUART_STATUS  0x04
#define APBUART_CTRL    0x08

/* Size of UART buffers (bytes).  */
#define UARTBUF	1024

/* Number of simulator ticks between flushing the UARTS.  */
/* For good performance, keep above 1000.  */
#define UART_FLUSH_TIME	  5000

/* New uart defines.  */
#define UART_TX_TIME	1000
#define UART_RX_TIME	1000
#define UARTA_DR	0x1
#define UARTA_SRE	0x2
#define UARTA_HRE	0x4
#define UARTA_OR	0x10

/* UART support variables.  */

/* File descriptor for input file.  */
static int32 fd1, fd2;

/* UART status register */
static int32 Ucontrol;

static unsigned char aq[UARTBUF], bq[UARTBUF];
static int32 anum, aind = 0;
static int32 bnum, bind = 0;
static char wbufa[UARTBUF], wbufb[UARTBUF];
static unsigned wnuma;
static unsigned wnumb;
static FILE *f1in, *f1out;
#ifdef HAVE_TERMIOS_H
static struct termios ioc1, ioc2, iocold1, iocold2;
#endif
#ifndef O_NONBLOCK
#define O_NONBLOCK 0
#endif

static int f1open = 0;

static char uarta_sreg, uarta_hreg;
static uint32 uart_stat_reg;
static uint32 uarta_data;

static int ifd1 = -1, ofd1 = -1;

void
apbuart_init_stdio (void)
{
  if (dumbio)
    return;
#ifdef HAVE_TERMIOS_H
  if (ifd1 == 0 && f1open)
    {
      tcsetattr (0, TCSANOW, &ioc1);
      tcflush (ifd1, TCIFLUSH);
    }
#endif
}

void
apbuart_restore_stdio (void)
{
  if (dumbio)
    return;
#ifdef HAVE_TERMIOS_H
  if (ifd1 == 0 && f1open && tty_setup)
    tcsetattr (0, TCSANOW, &iocold1);
#endif
}

#define DO_STDIO_READ( _fd_, _buf_, _len_ )          \
		( dumbio || nouartrx ? (0) : read( _fd_, _buf_, _len_ ) )

static void
apbuart_init (void)
{
  f1in = stdin;
  f1out = stdout;
  if (uart_dev1[0] != 0)
    {
      if ((fd1 = open (uart_dev1, O_RDWR | O_NONBLOCK)) < 0)
	{
	  printf ("Warning, couldn't open output device %s\n", uart_dev1);
	}
      else
	{
	  if (sis_verbose)
	    printf ("serial port A on %s\n", uart_dev1);
	  f1in = f1out = fdopen (fd1, "r+");
	  setbuf (f1out, NULL);
	  f1open = 1;
	}
    }
  if (f1in)
    ifd1 = fileno (f1in);
  if (ifd1 == 0)
    {
      if (sis_verbose)
	printf ("serial port A on stdin/stdout\n");
      if (!dumbio)
	{
#ifdef HAVE_TERMIOS_H
	  tcgetattr (ifd1, &ioc1);
	  if (tty_setup)
	    {
	      iocold1 = ioc1;
	      ioc1.c_lflag &= ~(ICANON | ECHO);
	      ioc1.c_cc[VMIN] = 0;
	      ioc1.c_cc[VTIME] = 0;
	    }
#endif
	}
      f1open = 1;
    }

  if (f1out)
    {
      ofd1 = fileno (f1out);
      if (!dumbio && tty_setup && ofd1 == 1)
	setbuf (f1out, NULL);
    }

  wnuma = 0;
}

static int
apbuart_read (uint32 addr, uint32 * data)
{
  unsigned tmp = 0;

  switch (addr & 0xff)
    {

    case 0x00:			/* UART 1 RX/TX */
#ifndef _WIN32
#ifdef FAST_UART

      if (aind < anum)
	{
	  if ((aind + 1) < anum)
	    grlib_set_irq (3);
	  *data = (uint32) aq[aind++];
	  return 0;
	}
      else
	{
	  if (f1open)
	    anum = DO_STDIO_READ (ifd1, aq, UARTBUF);
	  else
	    anum = 0;
	  if (anum > 0)
	    {
	      aind = 0;
	      if ((aind + 1) < anum)
		grlib_set_irq (3);
	      *data = (uint32) aq[aind++];
	      return 0;
	    }
	  else
	    {
	      *data = (uint32) aq[aind];
	      return 0;
	    }
	}
#else
      tmp = uarta_data;
      uarta_data &= ~UART_DR;
      uart_stat_reg &= ~UARTA_DR;
      *data = tmp;
      return 0;
#endif
#else
      *data = 0;
      return 0;
#endif
      break;

    case 0x04:			/* UART status register  */
#ifndef _WIN32
#ifdef FAST_UART

      Ucontrol = 0;
      if (aind < anum)
	{
	  Ucontrol |= 0x00000001;
	}
      else
	{
	  if (f1open)
	    anum = DO_STDIO_READ (ifd1, aq, UARTBUF);
	  else
	    anum = 0;
	  if (anum > 0)
	    {
	      Ucontrol |= 0x00000001;
	      aind = 0;
	      grlib_set_irq (3);
	    }
	}
      Ucontrol |= 0x00000006;
      *data = Ucontrol;
      return 0;
#else
      *data = uart_stat_reg;
      return 0;
#endif
#else
      *data = 0x00060006;
      return 0;
#endif
      break;
    case 0x08:			/* UART control register  */
      *data = 3;
      return 0;
      break;
    default:
      if (sis_verbose)
	printf ("Read from unimplemented UART register (%x)\n", addr);
    }

  *data = 0;
  return 0;
}

static int
apbuart_write (uint32 addr, uint32 * data, uint32 sz)
{
  unsigned char c;

  c = (unsigned char) *data;
  switch (addr & 0xff)
    {

    case 0x00:			/* UART A */
#ifdef FAST_UART
      if (f1open)
	{
	  if (wnuma < UARTBUF)
	    wbufa[wnuma++] = c;
	  else
	    {
	      while (wnuma)
		{
		  wnuma -= fwrite (wbufa, 1, wnuma, f1out);
		}
	      wbufa[wnuma++] = c;
	    }
	}
      grlib_set_irq (3);
#else
      if (uart_stat_reg & UARTA_SRE)
	{
	  uarta_sreg = c;
	  uart_stat_reg &= ~UARTA_SRE;
	  event (uarta_tx, 0, UART_TX_TIME);
	}
      else
	{
	  uarta_hreg = c;
	  uart_stat_reg &= ~UARTA_HRE;
	}
#endif
      break;

    case 0x04:			/* UART status register */
#ifndef FAST_UART
      uart_stat_reg &= 1;
#endif
      break;
    case 0x08:			/* UART control register  */
      break;
    default:
      if (sis_verbose)
	printf ("Write to unimplemented UART register (%x)\n", addr);
    }
}

void
apbuart_flush (void)
{
  while (wnuma && f1open)
    {
      wnuma -= fwrite (wbufa, 1, wnuma, f1out);
    }
}

static void
uarta_tx (void)
{
  while (f1open)
    {
      while (fwrite (&uarta_sreg, 1, 1, f1out) != 1)
	continue;
    }
  if (uart_stat_reg & UARTA_HRE)
    {
      uart_stat_reg |= UARTA_SRE;
    }
  else
    {
      uarta_sreg = uarta_hreg;
      uart_stat_reg |= UARTA_HRE;
      event (uarta_tx, 0, UART_TX_TIME);
    }
  grlib_set_irq (3);
}

static void
uart_rx (int32 arg)
{
  char rxd;
  int32 rsize = 0;


  if (f1open)
    rsize = DO_STDIO_READ (ifd1, &rxd, 1);
  else
    rsize = 0;
  if (rsize > 0)
    {
      uarta_data = rxd;
      if (uart_stat_reg & UARTA_DR)
	{
	  uart_stat_reg |= UARTA_OR;
	}
      uart_stat_reg |= UARTA_DR;
      grlib_set_irq (3);
    }
  event (uart_rx, 0, UART_RX_TIME);
}

static void
uart_intr (int32 arg)
{
  uint32 tmp;
  /* Check for UART interrupts every 1000 clk.  */
  apbuart_read (APBUART_STATUS, &tmp);
  apbuart_flush ();
  event (uart_intr, 0, UART_FLUSH_TIME);
}


static void
uart_irq_start (void)
{
#ifdef FAST_UART
  event (uart_intr, 0, UART_FLUSH_TIME);
#else
#ifndef _WIN32
  event (uart_rx, 0, UART_RX_TIME);
#endif
#endif
}

static void
apbuart_reset (void)
{
  wnuma = wnumb = 0;
  anum = aind = bnum = bind = 0;
  uart_stat_reg = UARTA_SRE | UARTA_HRE;

  uart_irq_start ();
}

void
apbuart_close_port (void)
{
  if (f1open && f1in != stdin)
    fclose (f1in);
}

static void
apbuart_add (int irq, uint32 addr, uint32 mask)
{
  grlib_apbpp_add (GRLIB_PP_ID (VENDOR_GAISLER, GAISLER_APBUART, 1, irq),
		   GRLIB_PP_APBADDR (addr, mask));
  if (sis_verbose)
    printf(" APBUART serial port                0x%08x   %d\n", addr, irq);
}

const struct grlib_ipcore apbuart = {
  apbuart_init, apbuart_reset, apbuart_read, apbuart_write, apbuart_add
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
    printf(" SDRAM controller %d M              0x%08x\n", 
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
    printf(" PROM controller %d M               0x%08x\n", 
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
  gpt_write (GPTIMER_SCLOAD, &tmp, 2);
  tmp = -1;
  gpt_write (GPTIMER_TIMER1, &tmp, 2);
  gpt_write (GPTIMER_RELOAD1, &tmp, 2);
  tmp = 7;
  gpt_write (GPTIMER_CTRL1, &tmp, 2);
}

/* ------------------- ns16550 -----------------------*/

static int32 uart_lcr;

static void
ns16550_add (int irq, uint32 addr, uint32 mask)
{
  grlib_ahbspp_add (GRLIB_PP_ID (VENDOR_CONTRIB, CONTRIB_NS16550, 0, 0),
		    GRLIB_PP_AHBADDR (addr, mask, 0, 0, 2), 0, 0, 0);
  if (sis_verbose)
    printf(" NS16550 UART                       0x%08x   %d\n", addr, irq);
}

static int
ns16550_write (uint32 addr, uint32 * data, uint32 sz)
{
      switch (addr & 0xff)
	{
	case 0:
	  if (!uart_lcr)
	    putchar (*data & 0xff);
	  break;
	case 0x0c:
	  uart_lcr = *data & 0x80;
	  break;
	}

  return 1;
}

static int
ns16550_read (uint32 addr, uint32 * data)
{
      *data = 0;
      switch (addr & 0xff)
	{
	case 0x10:
	  *data = 0x03;
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
    printf(" CLINT Interrupt controller         0x%08x   %d\n", addr, irq);
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
/* no functionality supported for now */

static void
plic_add (int irq, uint32 addr, uint32 mask)
{
  grlib_ahbspp_add (GRLIB_PP_ID (VENDOR_CONTRIB, CONTRIB_PLIC, 0, 0),
		    GRLIB_PP_AHBADDR (addr, mask, 0, 0, 2), 0, 0, 0);
  if (sis_verbose)
    printf(" PLIC Interrupt controller          0x%08x   %d\n", addr, irq);
}

const struct grlib_ipcore plic = {
  NULL, NULL, NULL, NULL, plic_add
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
	      for (i=0; i< ncpu; i++)
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
    printf(" S5 Test module                     0x%08x   %d\n", addr, irq);
}

const struct grlib_ipcore s5test = {
  NULL, NULL, NULL, s5test_write, s5test_add
};
