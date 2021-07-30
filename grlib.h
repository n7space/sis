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


/* Definitions for AMBA PNP in Gaisler Research GRLIB SOC */

/* Vendors */

#define VENDOR_GAISLER	1
#define VENDOR_PENDER	2
#define VENDOR_ESA	4
#define VENDOR_CONTRIB	9
#define VENDOR_DLR	10

/* Devices */

#define GAISLER_LEON3	0x003
#define GAISLER_APBMST	0x006
#define GAISLER_SRCTRL	0x008
#define GAISLER_SDCTRL	0x009
#define GAISLER_APBUART	0x00C
#define GAISLER_IRQMP	0x00D
#define GAISLER_GPTIMER	0x011
#define GAISLER_GRETH	0x01D
#define GAISLER_L2C	0x04B
#define ESA_MCTRL	0x00F
#define CONTRIB_NS16550	0x050
#define CONTRIB_CLINT	0x051
#define CONTRIB_PLIC	0x052
#define CONTRIB_S5TEST	0x053

/* How to build entries in the plug&play area */

#define GRLIB_PP_ID(v, d, x, i) ((v & 0xff) << 24) | ((d & 0x3ff) << 12) |\
			((x & 0x1f) << 5) | (i & 0x1f)
#define GRLIB_PP_AHBADDR(a, m, p, c, t) (a & 0xfff00000) | ((m & 0xfff) << 4) |\
			((p & 1) << 17) | ((c & 1) << 16) | (t & 0x3)
#define GRLIB_PP_APBADDR(a, m) ((a & 0xfff00)<< 12) | ((m & 0xfff) << 4) | 1

#define AHBPP_START	0xFFFFF000
#define AHBPP_END	0xFFFFFFFF
#define APBPP_START	0x800FF000
#define APBPP_END	0x800FFFFF

#define ROM_MASKPP	((~ROM_MASK >> 20) & 0xFFF)
#define RAM_MASKPP	((~RAM_MASK >> 20) & 0xFFF)

extern int grlib_apbpp_add (uint32 id, uint32 addr);
extern int grlib_ahbmpp_add (uint32 id);
extern int grlib_ahbspp_add (uint32 id, uint32 addr1, uint32 addr2,
			     uint32 addr3, uint32 addr4);
extern uint32 grlib_ahbpnp_read (uint32 addr);
extern uint32 grlib_apbpnp_read (uint32 addr);
extern void grlib_init ();
extern uint32 rvtimer_read (int address, int cpu);

struct grlib_ipcore
{
  void (*init) (void);
  void (*reset) (void);
  int (*read) (uint32 addr, uint32 * data);
  int (*write) (uint32 addr, uint32 * data, uint32 size);
  void (*add) (int irq, uint32 addr, uint32 mask);
};

struct grlib_buscore
{
  const struct grlib_ipcore *core;
  uint32 start;
  uint32 end;
  uint32 mask;
};

extern void grlib_ahbs_add (const struct grlib_ipcore *core, int irq,
			    uint32 addr, uint32 mask);
extern void grlib_ahbm_add (const struct grlib_ipcore *core, int irq);
extern void grlib_apb_add (const struct grlib_ipcore *core, int irq,
			   uint32 addr, uint32 mask);
extern int grlib_read (uint32 addr, uint32 * data);
extern int grlib_write (uint32 addr, uint32 * data, uint32 sz);
extern void grlib_set_irq (int32 level);
extern void grlib_store_bytes (char *mem, uint32 waddr, uint32 * data,
			       int32 sz);
extern void grlib_boot_init (void);
extern void grlib_reset (void);
extern void apbuart_init_stdio (void);
extern void apbuart_restore_stdio (void);
extern void apbuart_close_port (void);
extern void apbuart_flush (void);
extern const struct grlib_ipcore gptimer, irqmp, apbuart, apbmst,
  greth, l2c, leon3s, srctrl, ns16550, clint, plic, sdctrl, s5test;
