/* Copyright (C) 1995-2017 Free Software Foundation, Inc.

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
#include <stdint.h>

#ifndef WORDS_BIGENDIAN
#define HOST_LITTLE_ENDIAN
#endif

#define	VAL(x)	strtoul(x,(char **)NULL,0)
#define SWAP_UINT16(x) (((x) >> 8) | ((x) << 8))
#define SWAP_UINT32(x) (((x) >> 24) | (((x) & 0x00FF0000) >> 8) | (((x) & 0x0000FF00) << 8) | ((x) << 24))

#define I_ACC_EXC 1
#define NWIN 8

/* Maximum events in event queue */
#define MAX_EVENT	256

/* Maximum # of floating point queue */
#define FPUQN	1

/* Maximum # of breakpoints and watchpoints */
#define BPT_MAX	256
#define WPR_MAX	256
#define WPW_MAX	256

/* Maximum number of cpus */
#define NCPU 4

/* size of simulated memory */
#define ROM_MASK  (ROM_SIZE - 1)
#define ROM_END   (ROM_START + ROM_SIZE)
#define RAM_MASK  (RAM_SIZE - 1)
#define RAM_END   (RAM_START + RAM_SIZE)
#define MAX_ROM_SIZE 0x01000000
#define MAX_RAM_SIZE 0x04000000
#define MAX_RAM_MASK (MAX_RAM_SIZE - 1)

/* cache config */

#define L1IBITS		12
#define L1ILINEBITS	5
#define L1ITAGBITS	(L1IBITS - L1ILINEBITS)
#define L1ITAGS		(1 << (L1ITAGBITS))
#define L1IMASK		(L1ITAGS -1)
#define L1DBITS		12
#define L1DLINEBITS	5
#define L1DTAGBITS	(L1DBITS - L1DLINEBITS)
#define L1DTAGS		(1 << (L1DTAGBITS))
#define L1DMASK		(L1DTAGS -1)
#define T_L1IMISS	17
#define T_L1DMISS	17

/* type definitions */

typedef short int int16;	/* 16-bit signed int */
typedef unsigned short int uint16;	/* 16-bit unsigned int */
typedef int int32;		/* 32-bit signed int */
typedef unsigned int uint32;	/* 32-bit unsigned int */
typedef float float32;		/* 32-bit float */
typedef double float64;		/* 64-bit float */

typedef uint64_t uint64;	/* 64-bit unsigned int */
typedef int64_t int64;		/* 64-bit signed int */

struct histype
{
  unsigned addr;
  uint64 time;
};

struct pstate
{

  float64 fd[32];		/* FPU registers */
  float32 *fs;
  int32 *fsi;
  uint32 fsr;
  int32 fpstate;
  uint32 fpq[FPUQN * 2];
  uint32 fpqn;
  uint32 ftime;
  uint32 flrd;
  uint32 frd;
  uint32 frs1;
  uint32 frs2;
  uint32 fpu_pres;		/* FPU present (0 = No, 1 = Yes) */

  uint32 psr;			/* IU registers */
  uint32 tbr;
  uint32 wim;
  uint32 g[8];
  uint32 r[128];
  uint32 y;
  uint32 asr17;			/* Single vector trapping */
  uint32 pc, npc;


  uint32 trap;			/* Current trap type */
  uint32 data;			/* Loaded data       */
  uint32 inst;			/* Current instruction */
  uint32 asi;			/* Current ASI */
  uint32 err_mode;		/* IU error mode */
  uint32 pwd_mode;		/* IU in power-down mode */
  uint32 breakpoint;

  uint32 ltime;			/* Load interlock time */
  uint32 hold;			/* IU hold cycles in current inst */
  uint32 fhold;			/* FPU hold cycles in current inst */
  uint32 icnt;			/* Instruction cycles in curr inst */

  uint32 histind;
  struct histype *histbuf;


  uint64 ninst;
  uint64 fholdt;
  uint64 holdt;
  uint64 icntt;
  uint64 finst;
  uint64 pwdtime;		/* Cycles in power-down mode */
  uint64 pwdstart;		/* Start of power-down mode */
  uint64 nstore;		/* Number of store instructions */
  uint64 nload;			/* Number of load instructions */
  uint64 nannul;		/* Number of annuled instructions */
  uint64 nbranch;		/* Number of branch instructions */
  uint32 ildreg;		/* Destination of last load instruction */
  uint64 ildtime;		/* Last time point for load dependency */

  int rett_err;			/* IU in jmpl/restore error state (Rev.0) */
  int jmpltime;
  int cpu;
  uint64 simtime;		/* local processor time */
  uint32 cache_ctrl;		/* Leon3 cache control register */
  void (*intack) ();		/* interrupt ack. callback */

  uint32 mip;
  uint32 mie;
  uint32 mpp;
  uint32 mode;
  uint32 mstatus;
  uint32 mtvec;
  uint32 epc;
  uint32 wpaddress;
  uint32 mcause;
  uint32 mtval;
  uint32 mscratch;
  uint64 mtimecmp;
  uint32 lrq;
  uint32 lrqa;

  uint32 bphit;
  uint32 l1itags[L1ITAGS];
  uint64 l1imiss;
  uint32 l1dtags[L1DTAGS];
  uint64 l1dmiss;

  uint32 sp[NWIN];
};

struct evcell
{
  void (*cfunc) ();
  int32 arg;
  uint64 time;
  struct evcell *nxt;
};

struct cpu_arch
{
  int bswap;
  int (*dispatch_instruction) (struct pstate * sregs);
  int (*execute_trap) (struct pstate * sregs);
  int (*check_interrupts) (struct pstate * sregs);
  void (*disas) (uint32 addr);
  int (*gdb_get_reg) (char *buf);
  void (*set_register) (struct pstate * sregs, char *reg, uint32 rval,
			uint32 addr);
  void (*display_registers) (struct pstate * sregs);
  void (*display_ctrl) (struct pstate * sregs);
  void (*display_special) (struct pstate * sregs);
  void (*display_fpu) (struct pstate * sregs);

};

struct estate
{
  struct evcell eq;
  struct evcell *freeq;
  uint64 simtime;		/* timestamp of last access to event queue */
  uint64 evtime;		/* timestamp of next event */
  float32 freq;			/* Simulated processor frequency */
  double starttime;
  double tottime;
  uint64 simstart;
  uint64 tlimit;		/* Simulation time limit */
  uint32 bptnum;
  uint32 bpts[BPT_MAX];		/* Breakpoints */
  uint32 bpsave[BPT_MAX];	/* Saved opcode */
  uint32 wprnum;
  uint32 wphit;
  uint32 wptype;
  uint32 wprs[WPR_MAX];		/* Read Watchpoints */
  unsigned char wprm[WPR_MAX];	/* Read Watchpoint masks */
  uint32 wpwnum;
  uint32 wpws[WPW_MAX];		/* Write Watchpoints */
  unsigned char wpwm[WPW_MAX];	/* Write Watchpoint masks */
  uint32 wpaddress;
  uint32 histlen;
  uint32 coven;			/* coverage enable */
  uint32 ramstart;		/* start of RAM */
  uint32 bpcpu;			/* cpu that hit breakpoint */
  uint32 bend;			/* cpu big endian */
  uint32 cpu;			/* cpu typefrom elf file */
};

extern const struct cpu_arch *arch;
extern const struct cpu_arch sparc32;
extern const struct cpu_arch riscv;

/* return values for run_sim */
#define OK 0
#define TIME_OUT 1
#define BPT_HIT 2
#define ERROR_MODE 3
#define CTRL_C 4
#define WPT_HIT 5
#define NULL_HIT 6
#define QUIT 10

/* special simulator trap types */
#define ERROR_TRAP 257
#define WPT_TRAP   258
#define NULL_TRAP   259

/* cpu type defines */
#define CPU_ERC32  1
#define CPU_LEON2  2
#define CPU_LEON3  3
#define CPU_RISCV  5

/* Prototypes  */

/* erc32.c */
extern const struct memsys erc32sys;

/* func.c */
extern char romb[];
extern char ramb[];
extern struct pstate sregs[];
extern struct estate ebase;
extern struct evcell evbuf[];
extern int nfp;
extern int ift;
extern int ctrl_c;
extern int sis_verbose;
extern char *sis_version;
extern uint32 last_load_addr;
extern int wrp;
extern int rom8;
extern int uben;
extern int irqpend;
extern int ext_irl[];
extern int termsave;
extern char uart_dev1[];
extern char uart_dev2[];
extern void set_regi (struct pstate *sregs, int32 reg, uint32 rval);
extern void get_regi (struct pstate *sregs, int32 reg, char *buf, int length);
extern int exec_cmd (const char *cmd);
extern void reset_stat (struct pstate *sregs);
extern void show_stat (struct pstate *sregs);
extern void init_bpt (struct pstate *sregs);
extern void init_signals (void);

void print_insn_sis (uint32 addr);
extern uint32 dis_mem (uint32 addr, uint32 len);
extern void event (void (*cfunc) (), int32 arg, uint64 delta);
extern uint32 now (void);
extern int check_bpt (struct pstate *sregs);
extern int check_wpr (struct pstate *sregs, int32 address,
		      unsigned char mask);
extern int check_wpw (struct pstate *sregs, int32 address,
		      unsigned char mask);

extern void reset_all (void);
extern void sys_reset (void);
extern void sys_halt (void);
extern int elf_load (char *fname, int load);
extern double get_time (void);
extern int nouartrx;
//extern                host_callback *sim_callback;
extern int dumbio;
extern int tty_setup;
extern int cputype;
extern int sis_gdb_break;
extern int cpu;			/* active debug cpu */
extern int ncpu;		/* number of online cpus */
extern int delta;		/* time slice for MP simulation */
extern void pwd_enter (struct pstate *sregs);
extern void remove_event (void (*cfunc) (), int32 arg);
extern int run_sim (uint64 icount, int dis);
void save_sp (struct pstate *sregs);
void cov_start (int address);
void cov_exec (int address);
void cov_bt (int address1, int address2);
void cov_bnt (int address);
void cov_jmp (int address1, int address2);
void cov_save (char *name);
extern int port;
extern int sim_run;
extern void int_handler (int sig);
extern uint32 daddr;
extern void l1data_update (uint32 address, uint32 cpu);
extern void l1data_snoop (uint32 address, uint32 cpu);
extern char bridge[];
extern int sync_rt;
extern void rt_sync();

/* exec.c */
extern void init_regs (struct pstate *sregs);
extern void mul64 (uint32 n1, uint32 n2, uint32 * result_hi,
		   uint32 * result_lo, int msigned);
extern void div64 (uint32 n1_hi, uint32 n1_low, uint32 n2,
		   uint32 * result, int msigned);

/* float.c */
extern int get_accex (void);
extern void clear_accex (void);
extern void set_fsr (uint32 fsr);

/* help.c */
extern void sis_usage (void);
extern void gen_help (void);

struct memsys
{
  void (*init_sim) (void);
  void (*reset) (void);
  void (*error_mode) (uint32 pc);
  void (*sim_halt) (void);
  void (*exit_sim) (void);
  void (*init_stdio) (void);
  void (*restore_stdio) (void);
  int (*memory_iread) (uint32 addr, uint32 * data, int32 * ws);
  int (*memory_read) (uint32 addr, uint32 * data, int32 * ws);
  int (*memory_write) (uint32 addr, uint32 * data, int32 sz, int32 * ws);
  int (*sis_memory_write) (uint32 addr, const char *data, uint32 length);
  int (*sis_memory_read) (uint32 addr, char *data, uint32 length);
  void (*boot_init) (void);
  char *(*get_mem_ptr) (uint32 addr, uint32 size);
  void (*set_irq) (int32 level);
};

extern const struct memsys *ms;

/* leon2.c */
extern const struct memsys leon2;

/* leon3.c */
extern const struct memsys leon3;

/* gr740.c */
extern const struct memsys gr740;

/* remote.c */

extern void gdb_remote (int port);
extern int simstat;
extern int new_socket;
extern void socket_poll ();

/* interf.c */

extern int sim_read (uint32 mem, char *buf, int length);
extern int sim_write (uint32 mem, const char *buf, int length);
extern void sim_create_inferior ();
extern void sim_resume (int step);
extern int sim_insert_swbreakpoint (uint32 addr, int len);
extern int sim_remove_swbreakpoint (uint32 addr, int len);
extern int sim_set_watchpoint (uint32 mem, int length, int type);
extern int sim_clear_watchpoint (uint32 mem, int length, int type);

/* sparc.c */
extern int gdb_sp_read (uint32 mem, char *buf, int length);

/* greth.c */
extern uint32 greth_read (uint32 address);
extern void greth_write (uint32 address, uint32 data);
extern void greth_rxready(unsigned char *buffer, int len);

/* tap.c */

extern int sis_tap_init (long unsigned emac);
extern int sis_tap_write (unsigned char *buffer, int len);

/* FPU timing based on Meiko */

#define T_FABSs		2
#define T_FADDs		4
#define T_FADDd		4
#define T_FCMPs		4
#define T_FCMPd		4
#define T_FDIVs		20
#define T_FDIVd		35
#define T_FMOVs		2
#define T_FMULs		5
#define T_FMULd		9
#define T_FNEGs		2
#define T_FSQRTs	37
#define T_FSQRTd	65
#define T_FSUBs		4
#define T_FSUBd		4
#define T_FdTOi		7
#define T_FdTOs		3
#define T_FiTOs		6
#define T_FiTOd		6
#define T_FsTOi		6
#define T_FsTOd		2
