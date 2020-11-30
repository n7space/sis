/*
 * This file is part of SIS.
 *
 * SIS, SPARC/RISCV instruction simulator. Copyright (C) 2020 Jiri Gaisler
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


/* Emulation of GRETH 10/100 Mbit network interface */
/* Based on grlib-gpl-2018.1-b4217/doc/grip.pdf */
/* Multicast not supported for now ... */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/file.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "sis.h"

#define MDIO_WRITE 	1
#define MDIO_READ 	2
#define CTRL_SPEED	0x80
#define CTRL_RST	0x40
#define CTRL_RI		8
#define CTRL_TI		4
#define CTRL_RE		2
#define CTRL_TE		1
#define STATUS_TI	8
#define STATUS_RI	4

#define BASE_PNT	0x3f8
#define DESC_EN		(1 << 11)
#define DESC_WRAP	(1 << 12)
#define DESC_IE		(1 << 13)

static uint32 greth_ctrl;
static uint32 greth_status;
static uint32 greth_macmsb;
static uint32 greth_maclsb;
static uint32 greth_mdio;
static uint32 greth_txbase;
static uint32 greth_txdesc;
static uint32 greth_txbuf;
static unsigned char *greth_txbufptr;
static uint32 greth_rxbase;
static uint32 greth_rxdesc;
static uint32 greth_rxbuf;
static unsigned char *greth_rxbufptr;
static unsigned char greth_mac[6];
static long unsigned mac;
static const char broadcast[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

/* Simple emulation of Microchip KSZ8041NL/RNL PHY */

static uint32
mdio_read (uint32 address)
{
  uint32 res;

  switch (address & 0x1F)
    {
    case 0:
      res = 0x3100;
      break;
    case 1:
      res = 0x7865;
      break;
    case 2:
      res = 0x0022;
      break;
    case 3:
      res = 0x1512;
      break;
    case 4:
      res = 0x01ef;
      break;
    case 5:
      res = 0x41e1;
      break;
    default:
      res = 0;
    }

  if (sis_verbose > 1)
    printf ("%8lu cpu %d MDIO read a: %02x, d: %04x\n",
	    ebase.simtime, cpu, address, res);
  return res;
}

static void
mdio_write (uint32 address, uint32 data)
{
  if (sis_verbose > 1)
    printf ("%8lu cpu %d MDIO write a: %02x, d: %04x\n",
	    ebase.simtime, cpu, address, data);
}

static void
greth_tx (void)
{
  int32 ws;
  uint32 tmpdesc;
  unsigned char buffer[2048];
  int len, wlen, i;

  if (greth_ctrl & CTRL_TE)
    {
      ms->memory_read (greth_txbase, &greth_txdesc, &ws);
      if (greth_txdesc & DESC_EN)
	{
	  ms->memory_read (greth_txbase + 4, &greth_txbuf, &ws);
	  greth_txbufptr = ms->get_mem_ptr (greth_txbuf, 1536);
	  len = greth_txdesc & 0x7ff;
	  /* endian swap on host/target endian mismatch */
	  if (arch->bswap)
	    {
	      wlen = (len + 3) & ~3;	// align up to 32-bit word
	      for (i = 0; i < wlen; i++)
		buffer[i] = greth_txbufptr[arch->bswap ^ i];
	      sis_tap_write ((unsigned char *) buffer, len);
	    }
	  else
	    sis_tap_write (greth_txbufptr, greth_txdesc & 0x7ff);
	  greth_status |= STATUS_TI;
	  if ((greth_ctrl & CTRL_TI) && (greth_txdesc & DESC_IE))
	    ms->set_irq (6);
	  if (sis_verbose)
	    printf ("packet transmitted, len %d, desc %d\n",
		    greth_txdesc & 0x7ff, (greth_txbase & BASE_PNT) >> 3);
	  tmpdesc = greth_txdesc & 0x7ff;
	  ms->memory_write (greth_txbase, &tmpdesc, 2, &ws);
	  if ((greth_txdesc & DESC_WRAP) ||
	      ((greth_txbase & BASE_PNT) == BASE_PNT))
	    greth_txbase &= ~BASE_PNT;
	  else
	    greth_txbase += 8;
	  ms->memory_read (greth_txbase, &greth_txdesc, &ws);
	}
    }
  event (greth_tx, 1, 5000);
}

/* Write GRETH APB registers */

void
greth_write (uint32 address, uint32 data)
{
  int32 ws;

  switch (address & 0xFC)
    {
    case 0:
      if (data & CTRL_RST)
	{
	  greth_ctrl = CTRL_SPEED;
	}
      else
	{
	  if ((data & CTRL_RE) && !(greth_ctrl & CTRL_RE) && !mac)
	    {
	      mac = greth_macmsb;
	      mac <<= 32;
	      mac |= greth_maclsb;
	      sis_tap_init (mac);
	      mac = 1;
	      event (greth_tx, 1, 100);
	      sync_rt = 1;
	    }
	  greth_ctrl = data;
	}
      break;
    case 4:
      greth_status &= ~data;
      break;
    case 8:
      greth_macmsb = data & 0x0ffff;
      greth_mac[0] = (data >> 8) & 0x0ff;
      greth_mac[1] = data & 0x0ff;
      break;
    case 0x0c:
      greth_maclsb = data;
      greth_mac[2] = (data >> 24) & 0x0ff;
      greth_mac[3] = (data >> 16) & 0x0ff;
      greth_mac[4] = (data >> 8) & 0x0ff;
      greth_mac[5] = data & 0x0ff;
      break;
    case 0x10:
      greth_mdio = data & 0xfffffff0;
      if (data & MDIO_WRITE)
	{
	  mdio_write ((data >> 6) & 0x1f, data >> 16);
	}
      else if (data & MDIO_READ)
	{
	  greth_mdio = mdio_read ((data >> 6) & 0x1f) << 16;
	}
      break;
    case 0x14:
      greth_txbase = data & 0xfffffff8;
      break;
    case 0x18:
      greth_rxbase = data & 0xfffffff8;
      break;
    }
  if (sis_verbose > 1)
    printf ("%8lu cpu %d APB write a: %08x, d: %08x\n",
	    ebase.simtime, cpu, address, data);
}

/* Read GRETH APB registers */

uint32
greth_read (uint32 address)
{
  uint32 res;

  switch (address & 0xFC)
    {
    case 0:
      res = greth_ctrl;
      break;
    case 4:
      res = greth_status;
      break;
    case 8:
      res = greth_macmsb;
      break;
    case 0x0c:
      res = greth_maclsb;
      break;
    case 0x10:
      res = greth_mdio;
      break;
    case 0x14:
      res = greth_txbase;
      break;
    case 0x18:
      res = greth_rxbase;
      break;
    default:
      res = 0;
    }
  return res;
}

void
greth_rxready (unsigned char *buffer, int len)
{
  uint32 tmpdesc, ws;
  int i, wlen;

  if (sis_verbose > 1)
    {
      printf ("net: read %d bytes from device\n", len);
      for (i = 0; i < len; i++)
	printf ("%02x", buffer[i]);
      printf ("\n");
    }
  /* accept only unicast or broadcast packets */
  if (((strncmp (greth_mac, buffer, 6) == 0) ||
       (strncmp (buffer, broadcast, 6) == 0)) && (greth_ctrl & CTRL_RE))
    {
      ms->memory_read (greth_rxbase, &greth_rxdesc, &ws);
      if (greth_rxdesc & DESC_EN)
	{
	  ms->memory_read (greth_rxbase + 4, &greth_rxbuf, &ws);
	  greth_rxbufptr = ms->get_mem_ptr (greth_rxbuf, 1536);
	  /* endian swap on host/target endian mismatch */
	  if (arch->bswap)
	    {
	      wlen = (len + 3) & ~3;	// align up to 32-bit word
	      for (i = 0; i < wlen; i++)
		greth_rxbufptr[i] = buffer[arch->bswap ^ i];
	    }
	  else
	    memcpy (greth_rxbufptr, buffer, len);
	  tmpdesc = len & 0x7ff;
	  ms->memory_write (greth_rxbase, &tmpdesc, 2, &ws);
	  greth_status |= STATUS_RI;
	  if ((greth_rxdesc & DESC_WRAP)
	      || ((greth_rxbase & BASE_PNT) == BASE_PNT))
	    greth_rxbase &= ~BASE_PNT;
	  else
	    greth_rxbase += 8;

	  if ((greth_ctrl & CTRL_RI) && (greth_rxdesc & DESC_IE))
	    ms->set_irq (6);
	}
      else if (sis_verbose > 1)
	printf ("net: received packet dropped!\n");
    }
}
