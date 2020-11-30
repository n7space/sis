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
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#ifdef WIN32
#include <winsock.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#endif
#include <fcntl.h>
#include "sis.h"
#include <inttypes.h>
#include <sys/time.h>

/* set if UART device cannot handle attributes, terminal oriented IO by default */
int dumbio = 0;

/* set if UARTs are connected to a tty, enable by default */
int tty_setup = 1;

struct pstate sregs[NCPU];
struct estate ebase;
struct evcell evbuf[MAX_EVENT];

int ctrl_c = 0;
int sis_verbose = 0;
char *sis_version = PACKAGE_VERSION;
int nfp = 0;
int ift = 0;
int wrp = 0;
int rom8 = 0;
int uben = 0;
int termsave;
char uart_dev1[128] = "";
char uart_dev2[128] = "";
uint32 last_load_addr = 0;
int nouartrx = 0;
int port = 1234;
int sim_run = 0;
int sync_rt = 0;
char bridge[32] = "";

/* RAM and ROM for all systems */
char romb[MAX_ROM_SIZE];
char ramb[MAX_RAM_SIZE];
const struct memsys *ms = &erc32sys;
int cputype = 0;		/* 0 = erc32, 2 = leon2,3 = leon3, 5 = riscv */
int sis_gdb_break;
int cpu = 0;			/* active cpu */
int ncpu = 1;			/* number of cpus to emulate */
int delta = 50;			/* time slice for MP simulation */
const struct cpu_arch *arch = &sparc32;
uint32 daddr = 0;
/*
static bfd *abfd;
static asymbol **asymbols;
static int symsize = 0;
static int symcount = 0;
*/

/* Forward declarations */

static int batch (struct pstate *sregs, char *fname);
static void init_event (void);
static void disp_mem (uint32 addr, uint32 len);
static ssize_t mygetline (char **lineptr, size_t * n, FILE * stream);
static void symprint ();
static uint32 symtoaddr (char *s);

static int
batch (sregs, fname)
     struct pstate *sregs;
     char *fname;
{
  FILE *fp;
  char *lbuf = NULL;
  size_t len = 0;
  size_t slen;

  if ((fp = fopen (fname, "r")) == NULL)
    {
      fprintf (stderr, "couldn't open batch file %s\n", fname);
      return 0;
    }
  while (mygetline (&lbuf, &len, fp) > -1)
    {
      slen = strlen (lbuf);
      if (slen && (lbuf[slen - 1] == '\n'))
	{
	  lbuf[slen - 1] = 0;
	  printf ("sis> %s\n", lbuf);
	  exec_cmd (lbuf);
	}
    }
  free (lbuf);
  fclose (fp);
  return 1;
}

static uint64
limcalc (freq)
     float32 freq;
{
  uint64 unit, lim;
  double flim;
  char *cmd1, *cmd2;

  unit = 1;
  lim = -1;
  if ((cmd1 = strtok (NULL, " \t\n\r")) != NULL)
    {
      lim = VAL (cmd1);
      if ((cmd2 = strtok (NULL, " \t\n\r")) != NULL)
	{
	  if (strcmp (cmd2, "us") == 0)
	    unit = 1;
	  if (strcmp (cmd2, "ms") == 0)
	    unit = 1000;
	  if (strcmp (cmd2, "s") == 0)
	    unit = 1000000;
	}
      flim = (double) lim *(double) unit *(double) freq +
	(double) ebase.simtime;
      if (flim > ebase.simtime)
	{
	  lim = (uint64) flim;
	}
      else
	{
	  printf ("error in expression\n");
	  lim = 0;
	}
    }
  return lim;
}

int
exec_cmd (const char *cmd)
{
  char *cmd1, *cmd2;
  int32 stat, i;
  uint32 len, clen, j;
  char *cmdsave, *cmdsave2 = NULL;

  stat = OK;
  if (!cmd)
    return stat;
  cmdsave = strdup (cmd);
  cmdsave2 = strdup (cmd);
  if ((cmd1 = strtok (cmdsave2, " \t")) != NULL)
    {
      clen = strlen (cmd1);
      if (strncmp (cmd1, "bp", clen) == 0)
	{
	  for (i = 0; i < ebase.bptnum; i++)
	    {
	      printf ("  %d : 0x%08x\n", i + 1, ebase.bpts[i]);
	    }
	}
      else if ((strncmp (cmd1, "+bp", clen) == 0) ||
	       (strncmp (cmd1, "break", clen) == 0))
	{
	  if ((cmd1 = strtok (NULL, " \t\n\r")) != NULL)
	    {
	      if (isdigit (cmd1[0]))
		len = VAL (cmd1);
	      /*
	         else
	         len = symtoaddr(cmd1);
	       */
	      if (len)
		{
		  if (sim_set_watchpoint (len & ~1, 4, 1))
		    printf ("added breakpoint %d at 0x%08x\n",
			    ebase.bptnum, ebase.bpts[ebase.bptnum - 1]);
		}
	    }
	  else
	    {
	      for (i = 0; i < ebase.bptnum; i++)
		{
		  printf ("  %d : 0x%08x\n", i + 1, ebase.bpts[i]);
		}
	    }
	}
      else if ((strncmp (cmd1, "-bp", clen) == 0) ||
	       (strncmp (cmd1, "delete", clen) == 0))
	{
	  if ((cmd1 = strtok (NULL, " \t\n\r")) != NULL)
	    {
	      i = VAL (cmd1) - 1;
	      if ((i >= 0) && (i < ebase.bptnum))
		{
		  printf ("deleted breakpoint %d at 0x%08x\n", i + 1,
			  ebase.bpts[i]);
		  for (; i < ebase.bptnum - 1; i++)
		    {
		      ebase.bpts[i] = ebase.bpts[i + 1];
		    }
		  ebase.bptnum -= 1;
		}
	    }
	}
      else if (strncmp (cmd1, "batch", clen) == 0)
	{
	  if ((cmd1 = strtok (NULL, " \t\n\r")) == NULL)
	    {
	      printf ("no file specified\n");
	    }
	  else
	    {
	      batch (sregs, cmd1);
	    }
	}
      else if (strncmp (cmd1, "cont", clen) == 0)
	{
	  if ((cmd1 = strtok (NULL, " \t\n\r")) == NULL)
	    {
	      stat = run_sim (UINT64_MAX / 2, 0);
	    }
	  else
	    {
	      stat = run_sim (VAL (cmd1), 0);
	    }
	  daddr = sregs->pc;
	  ms->sim_halt ();
	}
      else if (strncmp (cmd1, "debug", clen) == 0)
	{
	  if ((cmd1 = strtok (NULL, " \t\n\r")) != NULL)
	    {
	      sis_verbose = VAL (cmd1);
	    }
	  printf ("Debug level = %d\n", sis_verbose);
	}
      else if (strncmp (cmd1, "disas", clen) == 0)
	{
	  if ((cmd1 = strtok (NULL, " \t\n\r")) != NULL)
	    {
	      daddr = VAL (cmd1);
	    }
	  if ((cmd2 = strtok (NULL, " \t\n\r")) != NULL)
	    {
	      len = VAL (cmd2);
	    }
	  else
	    len = 16;
	  printf ("\n");
	  daddr = dis_mem (daddr, len);
	  printf ("\n");
	}
      else if (strncmp (cmd1, "echo", clen) == 0)
	{
	  if ((cmd1 = strtok (NULL, " \t\n\r")) != NULL)
	    {
	      printf ("%s\n", (&cmdsave[clen + 1]));
	    }
	}
      else if (strncmp (cmd1, "float", clen) == 0)
	{
	  arch->display_fpu (sregs);
	}
      else if (strncmp (cmd1, "go", clen) == 0)
	{
	  if ((cmd1 = strtok (NULL, " \t\n\r")) == NULL)
	    {
	      len = last_load_addr;
	    }
	  else
	    {
	      len = VAL (cmd1);
	    }
	  for (i = 0; i < ncpu; i++)
	    {
	      sregs[i].pc = len & ~1;
	      sregs[i].npc = sregs->pc + 4;
	    }
	  if (ebase.simtime == 0)
	    ms->boot_init ();
	  printf ("resuming at 0x%08x\n", sregs->pc);
	  if ((cmd2 = strtok (NULL, " \t\n\r")) != NULL)
	    {
	      stat = run_sim (VAL (cmd2), 0);
	    }
	  else
	    {
	      stat = run_sim (UINT64_MAX / 2, 0);
	    }
	  daddr = sregs->pc;
	  ms->sim_halt ();
	}
      else if (strncmp (cmd1, "gdb", clen) == 0)
	{
	  if ((cmd1 = strtok (NULL, " \t\n\r")) != NULL)
	    {
	      port = VAL (cmd1);
	      if (port < 1024)
		port = 1024;
	    }
	  gdb_remote (port);
	}
      else if (strncmp (cmd1, "help", clen) == 0)
	{
	  gen_help ();
	}
      else if (strncmp (cmd1, "history", clen) == 0)
	{
	  if ((cmd1 = strtok (NULL, " \t\n\r")) != NULL)
	    {
	      ebase.histlen = VAL (cmd1);
	      for (i = 0; i < ncpu; i++)
		{
		  if (sregs[i].histbuf != NULL)
		    free (sregs[i].histbuf);
		  sregs[i].histbuf =
		    (struct histype *) calloc (ebase.histlen,
					       sizeof (struct histype));
		  sregs[i].histind = 0;
		}
	      printf ("trace history length = %d\n\r", ebase.histlen);

	    }
	  else
	    {
	      j = sregs[cpu].histind;
	      for (i = 0; i < ebase.histlen; i++)
		{
		  if (j >= ebase.histlen)
		    j = 0;
		  printf (" %8" PRIu64 " ", sregs[cpu].histbuf[j].time);
		  dis_mem (sregs[cpu].histbuf[j].addr, 1);
		  j++;
		}
	    }

	}
      else if (strncmp (cmd1, "load", clen) == 0)
	{
	  if ((cmd1 = strtok (NULL, " \t\n\r")) != NULL)
	    {
	      last_load_addr = elf_load (cmd1, 1);
	      daddr = last_load_addr;
	      while ((cmd1 = strtok (NULL, " \t\n\r")) != NULL)
		last_load_addr = elf_load (cmd1, 1);
	    }
	  else
	    {
	      printf ("load: no file specified\n");
	    }
	}
      else if (strncmp (cmd1, "mem", clen) == 0)
	{
	  if ((cmd1 = strtok (NULL, " \t\n\r")) != NULL)
	    daddr = VAL (cmd1);
	  if ((cmd2 = strtok (NULL, " \t\n\r")) != NULL)
	    len = VAL (cmd2);
	  else
	    len = 64;
	  disp_mem (daddr, len);
	  daddr += len;
	}
      else if (strncmp (cmd1, "cpu", clen) == 0)
	{
	  if ((cmd1 = strtok (NULL, " \t\n\r")) != NULL)
	    {
	      cpu = VAL (cmd1);
	      if (cpu > NCPU)
		cpu = NCPU;
	    }
	  printf ("active cpu: %d\n", cpu);
	}
      else if (strncmp (cmd1, "ncpu", clen) == 0)
	{
	  if ((cmd1 = strtok (NULL, " \t\n\r")) != NULL)
	    {
	      ncpu = VAL (cmd1);
	      if (ncpu > NCPU)
		ncpu = NCPU;
	    }
	  printf ("number of online cpus: %d\n", ncpu);
	}
      else if (strncmp (cmd1, "wmem", clen) == 0)
	{
	  if ((cmd1 = strtok (NULL, " \t\n\r")) != NULL)
	    daddr = VAL (cmd1);
	  if ((cmd2 = strtok (NULL, " \t\n\r")) != NULL)
	    len = VAL (cmd2);
	  ms->sis_memory_write (daddr, (char *) &len, 4);
	}
      else if (strncmp (cmd1, "perf", clen) == 0)
	{
	  cmd1 = strtok (NULL, " \t\n\r");
	  if ((cmd1 != NULL) && (strncmp (cmd1, "reset", strlen (cmd1)) == 0))
	    {
	      reset_stat (sregs);
	    }
	  else
	    show_stat (sregs);
	}
      else if (strncmp (cmd1, "quit", clen) == 0)
	{
	  stat = QUIT;
	}
      else if (strncmp (cmd1, "csr", clen) == 0)
	{
	  arch->display_special (&sregs[cpu]);
	}
      else if (strncmp (cmd1, "reg", clen) == 0)
	{
	  cmd1 = strtok (NULL, " \t\n\r");
	  cmd2 = strtok (NULL, " \t\n\r");
	  if (cmd2 != NULL)
	    arch->set_register (&sregs[cpu], cmd1, VAL (cmd2), 0);
	  /*
	     else if (cmd1 != NULL)
	     disp_reg(&sregs[cpu], cmd1);
	   */
	  else
	    {
	      arch->display_registers (&sregs[cpu]);
	      arch->display_ctrl (&sregs[cpu]);
	    }
	}
      else if (strncmp (cmd1, "reset", clen) == 0)
	{
	  ebase.simtime = 0;
	  ebase.simstart = 0;
	  reset_all ();
	  reset_stat (sregs);
	}
      else if (strncmp (cmd1, "run", clen) == 0)
	{
	  ebase.simtime = 0;
	  ebase.simstart = 0;
	  reset_all ();
	  reset_stat (sregs);
	  if (last_load_addr != 0)
	    {
	      for (i = 0; i < ncpu; i++)
		{
		  sregs[i].pc = last_load_addr & ~3;
		  sregs[i].npc = sregs[i].pc + 4;
		}
	    }
	  if (ebase.simtime == 0)
	    ms->boot_init ();
	  if ((cmd1 = strtok (NULL, " \t\n\r")) == NULL)
	    {
	      stat = run_sim (UINT64_MAX / 2, 0);
	    }
	  else
	    {
	      stat = run_sim (VAL (cmd1), 0);
	    }
	  daddr = sregs->pc;
	  ms->sim_halt ();
	}
      else if (strncmp (cmd1, "shell", clen) == 0)
	{
	  if ((cmd1 = strtok (NULL, " \t\n\r")) != NULL)
	    {
	      if (system (&cmdsave[clen]))
		{
		  /* Silence unused return value warning.  */
		}
	    }
	  /*
	     } else if (strncmp(cmd1, "sym", clen) == 0) {
	     symprint ();
	   */
	}
      else if (strncmp (cmd1, "step", clen) == 0)
	{
	  stat = run_sim (1, 1);
	  daddr = sregs->pc;
	  ms->sim_halt ();
	}
      else if (strncmp (cmd1, "tcont", clen) == 0)
	{
	  ebase.tlimit = limcalc (ebase.freq);
	  stat = run_sim (UINT64_MAX / 2, 0);
	  daddr = sregs->pc;
	  ms->sim_halt ();
	}
      else if (strncmp (cmd1, "tgo", clen) == 0)
	{
	  if ((cmd1 = strtok (NULL, " \t\n\r")) == NULL)
	    {
	      len = last_load_addr;
	    }
	  else
	    {
	      len = VAL (cmd1);
	      ebase.tlimit = limcalc (ebase.freq);
	    }
	  sregs->pc = len & ~1;
	  sregs->npc = sregs->pc + 4;
	  printf ("resuming at 0x%08x\n", sregs->pc);
	  stat = run_sim (UINT64_MAX / 2, 0);
	  daddr = sregs->pc;
	  ms->sim_halt ();
	}
      else if (strncmp (cmd1, "tlimit", clen) == 0)
	{
	  ebase.tlimit = limcalc (ebase.freq);
	  if (ebase.tlimit != (uint32) - 1)
	    if (sis_verbose)
	      printf ("simulation limit = %u (%.3f ms)\n",
		      (uint32) ebase.tlimit,
		      ebase.tlimit / ebase.freq / 1000);
	}
      else if (strncmp (cmd1, "tra", clen) == 0)
	{
	  if ((cmd1 = strtok (NULL, " \t\n\r")) == NULL)
	    {
	      stat = run_sim (UINT64_MAX / 2, 1);
	    }
	  else
	    {
	      stat = run_sim (VAL (cmd1), 1);
	    }
	  printf ("\n");
	  daddr = sregs->pc;
	  ms->sim_halt ();
	}
      else if (strncmp (cmd1, "trun", clen) == 0)
	{
	  ebase.simtime = 0;
	  ebase.simstart = 0;
	  reset_all ();
	  reset_stat (sregs);
	  if (last_load_addr != 0)
	    {
	      for (i = 0; i < ncpu; i++)
		{
		  sregs[i].pc = last_load_addr & ~3;
		  sregs[i].npc = sregs[i].pc + 4;
		}
	    }
	  if ((sregs->pc != 0) && (ebase.simtime == 0))
	    ms->boot_init ();
	  ebase.tlimit = limcalc (ebase.freq);
	  stat = run_sim (UINT64_MAX / 2, 0);
	  daddr = sregs->pc;
	  ms->sim_halt ();
	}
      else if (strncmp (cmd1, "wp", clen) == 0)
	{
	  for (i = 0; i < ebase.wprnum; i++)
	    {
	      printf ("  %d : 0x%08x (read)\n", i + 1, ebase.wprs[i]);
	    }
	  for (i = 0; i < ebase.wpwnum; i++)
	    {
	      printf ("  %d : 0x%08x (write)\n", i + 1, ebase.wpws[i]);
	    }
	}
      else if ((strncmp (cmd1, "+wpr", clen) == 0) ||
	       (strncmp (cmd1, "rwatch", clen) == 0))
	{
	  if ((cmd1 = strtok (NULL, " \t\n\r")) != NULL)
	    {
	      if (sim_set_watchpoint (VAL (cmd1) & ~0x3, 4, 3))
		printf ("added read watchpoint %d at 0x%08x\n",
			ebase.wprnum, ebase.wprs[ebase.wprnum - 1]);
	    }
	}
      else if (strncmp (cmd1, "-wpr", clen) == 0)
	{
	  if ((cmd1 = strtok (NULL, " \t\n\r")) != NULL)
	    {
	      i = VAL (cmd1) - 1;
	      if ((i >= 0) && (i < ebase.wprnum))
		{
		  printf ("deleted read watchpoint %d at 0x%08x\n", i + 1,
			  ebase.wprs[i]);
		  for (; i < ebase.wprnum - 1; i++)
		    {
		      ebase.wprs[i] = ebase.wprs[i + 1];
		    }
		  ebase.wprnum -= 1;
		}
	    }
	}
      else if ((strncmp (cmd1, "+wpw", clen) == 0) ||
	       (strncmp (cmd1, "watch", clen) == 0))
	{
	  if ((cmd1 = strtok (NULL, " \t\n\r")) != NULL)
	    {
	      if (sim_set_watchpoint (VAL (cmd1) & ~0x3, 4, 2))
		printf ("added write watchpoint %d at 0x%08x\n",
			ebase.wpwnum, ebase.wpws[ebase.wpwnum - 1]);
	    }
	  else
	    {
	      for (i = 0; i < ebase.wpwnum; i++)
		{
		  printf ("  %d : 0x%08x (write)\n", i + 1, ebase.wpws[i]);
		}
	    }
	}
      else if (strncmp (cmd1, "-wpw", clen) == 0)
	{
	  if ((cmd1 = strtok (NULL, " \t\n\r")) != NULL)
	    {
	      i = VAL (cmd1) - 1;
	      if ((i >= 0) && (i < ebase.wpwnum))
		{
		  printf ("deleted write watchpoint %d at 0x%08x\n", i + 1,
			  ebase.wpws[i]);
		  for (; i < ebase.wpwnum - 1; i++)
		    {
		      ebase.wpws[i] = ebase.wpws[i + 1];
		    }
		  ebase.wpwnum -= 1;
		}
	    }
	}
      else
	printf ("syntax error\n");
    }
  if (cmdsave2 != NULL)
    free (cmdsave2);
  if (cmdsave != NULL)
    free (cmdsave);
  return stat;
}


void
reset_stat (sregs)
     struct pstate *sregs;
{
  ebase.tottime = 0.0;
  sregs->pwdtime = 0;
  sregs->ninst = 0;
  sregs->fholdt = 0;
  sregs->holdt = 0;
  sregs->icntt = 0;
  sregs->finst = 0;
  sregs->nstore = 0;
  sregs->nload = 0;
  sregs->nbranch = 0;
  ebase.simstart = ebase.simtime;
  sregs->l1imiss = 0;
  sregs->l1dmiss = 0;

}

void
show_stat (sregs)
     struct pstate *sregs;
{
  uint64 iinst, ninst, pwdtime;
  uint64 stime, atime;
  int i;

  ninst = 0;
  pwdtime = 0;
  atime = 0;
  if (ebase.tottime == 0.0)
    ebase.tottime += 1E-6;
  for (i = 0; i < ncpu; i++)
    {
      if (sregs[i].pwd_mode)
	{
	  sregs[i].pwdtime += sregs[i].simtime - sregs[i].pwdstart;
	  sregs[i].pwdstart = sregs[i].simtime;
	}
      ninst += sregs[i].ninst;
      pwdtime += sregs[i].pwdtime;
    }
  stime = ebase.simtime - ebase.simstart;	/* Total simulated time */
  printf ("\n Frequency       : %4.1f MHz\n", ebase.freq);
  printf (" Cycles          : %" PRIu64 "\n", stime);
  printf (" Instructions    : %" PRIu64 "\n", ninst);
  printf (" Simulated time  : %.2f s\n",
	  (double) (stime) / 1000000.0 / ebase.freq);
  printf (" System perf.    : %.2f MOPS\n",
	  (double) ninst / ((double) (stime) / ebase.freq));
  printf (" Real-time perf. : %.2f %%\n",
	  100.0 / (ebase.tottime /
		   ((double) (stime) / (ebase.freq * 1.0E6))));
  printf (" Simulator perf. : %.2f MIPS\n",
	  (double) (ninst / ebase.tottime / 1E6));
  printf (" Wall time       : %.2f s\n\n", ebase.tottime);
  printf (" Core   MIPS   MFLOPS     CPI     Util"
#ifdef ENABLE_L1CACHE
	  "      IHit      DHit"
#endif
	  "\n");
  for (i = 0; i < ncpu; i++)
    {
#ifdef STAT
      iinst =
	sregs[i].ninst - sregs[i].finst - sregs[i].nload - sregs[i].nstore -
	sregs[i].nbranch;
#endif

      stime = sregs[i].simtime - ebase.simstart + 1;	/* Core simulated time */
      printf ("  %d    %5.2f    %5.2f    %5.2f    %5.2f%%"
#ifdef ENABLE_L1CACHE
	      "    %5.2f%%    %5.2f%%"
#endif
	      "\n", i,
	      ebase.freq * (double) (sregs[i].ninst - sregs[i].finst) /
	      (double) (stime - sregs[i].pwdtime),
	      ebase.freq * (double) sregs[i].finst / (double) (stime -
							       sregs
							       [i].pwdtime),
	      (double) (stime - sregs[i].pwdtime) / (double) (sregs[i].ninst +
							      1),
	      100.0 * (1.0 - ((double) sregs[i].pwdtime / (double) stime))
#ifdef ENABLE_L1CACHE
	      , (double) (sregs[i].ninst - sregs[i].l1imiss + 1) /
	      (double) (sregs[i].ninst + 1) * 100.0,
	      (double) (sregs[i].nload + sregs[i].nstore - sregs[i].l1dmiss +
			1) / (double) (sregs[i].nload + sregs[i].nstore +
				       1) * 100.0
#endif
	);
    }

#ifdef STAT
  printf ("   integer    : %9.2f %%\n",
	  100.0 * (double) iinst / (double) sregs[i].ninst);
  printf ("   load       : %9.2f %%\n",
	  100.0 * (double) sregs->nload / (double) sregs[i].ninst);
  printf ("   store      : %9.2f %%\n",
	  100.0 * (double) sregs->nstore / (double) sregs[i].ninst);
  printf ("   branch     : %9.2f %%\n",
	  100.0 * (double) sregs->nbranch / (double) sregs[i].ninst);
  printf ("   float      : %9.2f %%\n",
	  100.0 * (double) sregs->finst / (double) sregs[i].ninst);
  printf (" Integer CPI  : %9.2f\n",
	  ((double)
	   (stime - sregs[i].pwdtime - sregs[i].fholdt -
	    sregs[i].finst)) / (double) (sregs[i].ninst - sregs[i].finst));
  printf (" Float CPI    : %9.2f\n",
	  ((double) sregs[i].fholdt / (double) sregs[i].finst) + 1.0);
#endif
  printf ("\n");
}



void
init_bpt (sregs)
     struct pstate *sregs;
{
  int i;

  ebase.bptnum = 0;
  ebase.wprnum = 0;
  ebase.wpwnum = 0;
  ebase.histlen = 0;
  for (i = 0; i < ncpu; i++)
    {
      sregs[i].histind = 0;
      sregs[i].histbuf = NULL;
    }
  ebase.tlimit = 0;
}


/* support for catching ctrl-c */

#ifdef WIN32

BOOL WINAPI ConsoleHandler (DWORD);

void
init_signals ()
{
  if (!SetConsoleCtrlHandler ((PHANDLER_ROUTINE) ConsoleHandler, TRUE))
    {
      fprintf (stderr, "Unable to install ctrl-c handler!\n");
    }
}

BOOL WINAPI
ConsoleHandler (DWORD dwType)
{
  switch (dwType)
    {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
      ctrl_c = 1;
      break;
    default:
      break;
    }
  return TRUE;
}

#else

void
int_handler (int sig)
{

  int count;

  switch (sig)
    {
    case SIGINT:
      ctrl_c = 1;
      if (!sim_run)
	{
	  if (new_socket > 0)
	    close (new_socket);
	  else
	    exit (0);
	}
      break;
    default:
      printf ("\n\n Signal handler error  (%d)\n\n", sig);
    }
}

void
init_signals ()
{
  typedef void (*PFI) ();
  static PFI int_tab[2];

  int_tab[0] = signal (SIGTERM, int_handler);
  int_tab[1] = signal (SIGINT, int_handler);
}
#endif

void
print_insn_sis (uint32 addr)
{
  char i[4];

  ms->sis_memory_read (addr, i, 4);
  arch->disas (addr);
}

static void
disp_mem (addr, len)
     uint32 addr;
     uint32 len;
{

  uint32 i;
  union
  {
    char u8[4];
    uint32 u32;
  } data;
  uint32 mem[4], j;
  char *p;

  for (i = addr & ~3; i < ((addr + len) & ~3); i += 16)
    {
      printf ("\n %8X  ", i);
      for (j = 0; j < 4; j++)
	{
	  ms->sis_memory_read ((i + (j * 4)), data.u8, 4);
	  printf ("%08x  ", data.u32);
	  mem[j] = data.u32;
	}
      printf ("  ");
      p = (char *) mem;
      for (j = 0; j < 16; j++)
	{
	  if (isprint (p[j ^ arch->bswap]))
	    putchar (p[j ^ arch->bswap]);
	  else
	    putchar ('.');
	}
    }
  printf ("\n\n");
}

uint32
dis_mem (addr, len)
     uint32 addr;
     uint32 len;
{
  uint32 i, data;

  for (i = 0; i < len; i++)
    {
      ms->sis_memory_read (addr, (char *) &data, 4);
      if ((cputype == CPU_RISCV) && ((data & 3) != 3))
	{
	  data &= 0x0ffff;
	  printf (" %08x:  %04x      ", addr, data);
	}
      else
	printf (" %08x:  %08x  ", addr, data);
      print_insn_sis (addr);
      if (i >= 0xfffffffc)
	break;
      printf ("\n");
      if ((cputype == CPU_RISCV) && ((data & 3) != 3))
	addr += 2;
      else
	addr += 4;
    }
  return addr;
}

/* Add event to event queue */

void
event (cfunc, arg, delta)
     void (*cfunc) ();
     int32 arg;
     uint64 delta;
{
  struct evcell *ev1, *evins;

  if (ebase.freeq == NULL)
    {
      printf ("Error, too many events in event queue\n");
      return;
    }
  ev1 = &ebase.eq;
  delta += ebase.simtime;
  while ((ev1->nxt != NULL) && (ev1->nxt->time <= delta))
    {
      ev1 = ev1->nxt;
    }
  if (ev1->nxt == NULL)
    {
      ev1->nxt = ebase.freeq;
      ebase.freeq = ebase.freeq->nxt;
      ev1->nxt->nxt = NULL;
    }
  else
    {
      evins = ebase.freeq;
      ebase.freeq = ebase.freeq->nxt;
      evins->nxt = ev1->nxt;
      ev1->nxt = evins;
    }
  ev1->nxt->time = delta;
  ev1->nxt->cfunc = cfunc;
  ev1->nxt->arg = arg;
  ebase.evtime = ebase.eq.nxt->time;
}

/* remove event from event queue */
void
remove_event (cfunc, arg)
     void (*cfunc) ();
     int32 arg;
{
  struct evcell *ev1, *evdel;

  ev1 = &ebase.eq;
  while (ev1->nxt != NULL)
    {
      if ((ev1->nxt->cfunc == cfunc) && ((arg == ev1->nxt->arg) || (arg < 0)))
	{
	  evdel = ev1->nxt;
	  ev1->nxt = ev1->nxt->nxt;
	  evdel->nxt = ebase.freeq;
	  ebase.freeq = evdel;
	}
      ev1 = ev1->nxt;
    }
  ebase.evtime = ebase.eq.nxt->time;
}

static void
last_event (int32 arg)
{
  printf ("Warning: end of time ... exiting!\n");
  exit (0);
}

void
init_event ()
{
  int32 i;

  ebase.eq.nxt = NULL;
  ebase.freeq = evbuf;
  for (i = 0; i < MAX_EVENT; i++)
    {
      evbuf[i].nxt = &evbuf[i + 1];
    }
  evbuf[MAX_EVENT - 1].nxt = NULL;
  event (last_event, 0, UINT64_MAX);
}

/* Advance simulator time */

void
advance_time (endtime)
     uint64 endtime;
{

  struct evcell *evrem;
  void (*cfunc) ();
  uint32 arg;

  while (ebase.evtime <= endtime)
    {
      ebase.simtime = ebase.eq.nxt->time;
      cfunc = ebase.eq.nxt->cfunc;
      arg = ebase.eq.nxt->arg;
      evrem = ebase.eq.nxt;
      ebase.eq.nxt = ebase.eq.nxt->nxt;
      ebase.evtime = ebase.eq.nxt->time;
      evrem->nxt = ebase.freeq;
      ebase.freeq = evrem;
      cfunc (arg);
    }
  ebase.simtime = endtime;

}

uint32
now ()
{
  return (uint32) ebase.simtime;
}

void
pwd_enter (struct pstate *sregs)
{
  sregs->pwd_mode = 1;
  sregs->pwdstart = sregs->simtime;
  sregs->hold += delta;
}

void
rt_sync ()
{
  double walltime, realtime, dtime;
  int64 stime;
  stime = ebase.simtime - ebase.simstart;	/* Total simulated time */
  realtime = (double) ((stime) / 1000000.0 / ebase.freq);
  walltime = ebase.tottime + get_time () - ebase.starttime;
  dtime = (realtime - walltime);
  if (dtime > 0.001)
    {
      if (dtime > 1.0)
	dtime = 0.1;
      usleep ((useconds_t) (dtime * 1E6));
    }
}

int
check_bpt (sregs)
     struct pstate *sregs;
{
  int32 i;

  if (sregs->bphit)
    {
      sregs->bphit = 0;
      return 0;
    }
  for (i = 0; i < (int32) ebase.bptnum; i++)
    {
      if (sregs->pc == ebase.bpts[i])
	return BPT_HIT;
    }
  return 0;
}

int
check_wpr (struct pstate *sregs, int32 address, unsigned char mask)
{
  int32 i, msk;

  for (i = 0; i < ebase.wprnum; i++)
    {
      msk = ~(mask | ebase.wprm[i]);
      if (((address ^ ebase.wprs[i]) & msk) == 0)
	{
	  ebase.wpaddress = address;
	  if (ebase.wphit)
	    return (0);
	  ebase.wptype = 3;
	  return (WPT_HIT);
	}
    }
  return (0);
}

int
check_wpw (struct pstate *sregs, int32 address, unsigned char mask)
{
  int32 i, msk;

  for (i = 0; i < ebase.wpwnum; i++)
    {
      msk = ~(mask | ebase.wpwm[i]);
      if (((address ^ ebase.wpws[i]) & msk) == 0)
	{
	  ebase.wpaddress = ebase.wpws[i];
	  if (ebase.wphit)
	    return (0);
	  ebase.wptype = 2;
	  return (WPT_HIT);
	}
    }
  return (0);
}

void
reset_all ()
{
  init_event ();		/* Clear event queue */
  init_regs (sregs);
  ms->reset ();
}

void
sys_reset ()
{
  reset_all ();
  sregs[0].trap = 256;		/* Force fake reset trap */
}

void
sys_halt ()
{
  sregs[0].trap = 257;		/* Force fake halt trap */
}

/* simulate one core instruction-wise */

static int
run_sim_un (sregs, icount, dis)
     struct pstate *sregs;
     uint64 icount;
     int dis;
{
  int irq, mexc, deb;
  uint32 *inst;

  if (sregs->err_mode)
    icount = 0;
  deb = dis || ebase.histlen || ebase.bptnum;
  mexc = irq = 0;
  while (icount > 0)
    {
      if (sregs->pwd_mode)
	{
	  sregs->simtime = ebase.evtime;	/* skip forward to next event */
	  if (ext_irl[sregs->cpu])
	    irq = arch->check_interrupts (sregs);
	}
      else
	{
	  sregs->icnt = 1;
	  sregs->fhold = 0;
	  if (ext_irl[sregs->cpu])
	    irq = arch->check_interrupts (sregs);
	  if (!irq)
	    {
	      mexc = ms->memory_iread (sregs->pc, &sregs->inst, &sregs->hold);
	      if (mexc)
		{
		  sregs->trap = I_ACC_EXC;
		}
	      else
		{
		  if (deb)
		    {
		      if ((ebase.bptnum)
			  && (sregs->bphit = check_bpt (sregs)))
			icount = 0;
		      else
			{
			  if (ebase.histlen)
			    {
			      sregs->histbuf[sregs->histind].addr = sregs->pc;
			      sregs->histbuf[sregs->histind].time =
				ebase.simtime;
			      sregs->histind++;
			      if (sregs->histind >= ebase.histlen)
				sregs->histind = 0;
			    }
			  if (dis)
			    {
			      printf (" %8" PRIu64 " ", ebase.simtime);
			      dis_mem (sregs->pc, 1);
			    }
			  arch->dispatch_instruction (sregs);
			  icount--;
			  advance_time (sregs->simtime);
			}
		    }
		  else
		    {
		      arch->dispatch_instruction (sregs);
		      icount--;
		    }
		}
	    }
	  else
	    sregs->trap = irq + 16;
	  if (sregs->trap)
	    {
	      irq = 0;
	      if ((sregs->err_mode = arch->execute_trap (sregs)) == WPT_HIT)
		{
		  sregs->err_mode = 0;
		  sregs->trap = 0;
		  icount = 0;
		  ebase.bpcpu = sregs->cpu;
		  if (ebase.histlen)
		    {
		      sregs->histind--;
		      if (sregs->histind >= ebase.histlen)
			sregs->histind = ebase.histlen - 1;
		    }
		}
	      if (sregs->err_mode)
		{
		  ms->error_mode (sregs->pc);
		  icount = 0;
		  ebase.bpcpu = sregs->cpu;
		}
	    }
#ifdef STAT
	  sregs->fholdt += sregs->fhold;
	  sregs->holdt += sregs->hold;
	  sregs->icntt += sregs->icnt;
#endif
	  sregs->simtime += sregs->icnt + sregs->hold + sregs->fhold;
	}
      if (sregs->simtime >= ebase.evtime)
	advance_time (sregs->simtime);
      if (ctrl_c)
	{
	  icount = 0;
	}
    }
  advance_time (sregs->simtime);
  if (sregs->err_mode)
    return ERROR_MODE;
  if (sregs->bphit)
    return (BPT_HIT);
  if (ebase.wphit)
    return (WPT_HIT);
  if (ctrl_c)
    {
      return CTRL_C;
    }
  return TIME_OUT;
}

/* stop simulation after specified time */

static void
sim_timeout (int32 arg)
{
  ctrl_c = arg;
}

/* simulate one core time-wise */

static void
run_sim_core (sregs, ntime, deb, dis)
     struct pstate *sregs;
     uint64 ntime;
     int deb;
     int dis;
{
  int mexc, irq;
  mexc = irq = 0;
  if (sregs->pwd_mode == 0)
    while (ntime > sregs->simtime)
      {
	if (ext_irl[sregs->cpu])
	  irq = arch->check_interrupts (sregs);
	else
	  irq = 0;
	sregs->icnt = 1;
	mexc = ms->memory_iread (sregs->pc, &sregs->inst, &sregs->hold);
#ifdef ENABLE_L1CACHE
	if (sregs->l1itags[(sregs->pc >> L1ILINEBITS) & L1IMASK] !=
	    (sregs->pc >> L1ILINEBITS))
	  {
	    sregs->hold = T_L1IMISS;
	    sregs->l1itags[(sregs->pc >> L1ILINEBITS) & L1IMASK] =
	      (sregs->pc >> L1ILINEBITS);
	    sregs->l1imiss++;
	  }
#endif
	sregs->fhold = 0;
	if (!irq)
	  {
	    if (mexc)
	      {
		sregs->trap = I_ACC_EXC;
	      }
	    else
	      {
		if (deb)
		  {
		    if ((ebase.bptnum) && (sregs->bphit = check_bpt (sregs)))
		      {
			ntime = sregs->simtime;
			ctrl_c = 1;
			ebase.bpcpu = sregs->cpu;
			break;
		      }
		    if (ebase.histlen)
		      {
			sregs->histbuf[sregs->histind].addr = sregs->pc;
			sregs->histbuf[sregs->histind].time = sregs->simtime;
			sregs->histind++;
			if (sregs->histind >= ebase.histlen)
			  sregs->histind = 0;
		      }
		    if (dis)
		      {
			printf ("cpu %d  %8" PRIu64 " ", sregs->cpu,
				sregs->simtime);
			dis_mem (sregs->pc, 1);
		      }
		    arch->dispatch_instruction (sregs);
		  }
		else
		  {
		    arch->dispatch_instruction (sregs);
		  }
	      }
	  }
	else
	  sregs->trap = irq + 16;
	if (sregs->trap)
	  {
	    irq = 0;
	    if ((sregs->err_mode = arch->execute_trap (sregs)) == WPT_HIT)
	      {
		sregs->err_mode = 0;
		sregs->trap = 0;
		ntime = sregs->simtime;
		ctrl_c = 1;
		ebase.bpcpu = sregs->cpu;
		if (ebase.histlen)
		  {
		    sregs->histind--;
		    if (sregs->histind >= ebase.histlen)
		      sregs->histind = ebase.histlen - 1;
		  }
		break;
	      }
	    if (sregs->err_mode)
	      {
		ms->error_mode (sregs->pc);
		sregs->pwd_mode = 1;
		sregs->pwdstart = sregs->simtime;
		sregs->simtime = ntime;
		ctrl_c = 1;
		ebase.bpcpu = sregs->cpu;
		break;
	      }
	  }
#ifdef STAT
	sregs->fholdt += sregs->fhold;
	sregs->holdt += sregs->hold;
	sregs->icntt += sregs->icnt;
#endif
	sregs->simtime += sregs->icnt + sregs->hold + sregs->fhold;
      }
  else
    {
      sregs->simtime = ntime;
      if (ext_irl[sregs->cpu])
	irq = arch->check_interrupts (sregs);
    }
  sregs->lrq = 0;

}

/* time slice simulation of cpu cores in MP system */

static int
run_sim_mp (icount, dis)
     uint64 icount;
     int dis;
{
  uint64 ntime, etime;
  int deb, i, j;
  int err_mode, bphit, wphit, oldcpu;

  err_mode = bphit = wphit = 0;
  icount += ebase.simtime;
  for (i = 0; i < ncpu; i++)
    {
      if (sregs[i].err_mode)
	{
	  icount = 0;
	  err_mode = 1;
	}
    }
  while (icount > ebase.simtime)
    {
      ntime = ebase.simtime + delta;
      if (ntime > icount)
	ntime = icount;
      if (ntime > ebase.evtime)
	ntime = ebase.evtime;
      for (i = 0; i < ncpu; i++)
	{
	  deb = dis || ebase.histlen || ebase.bptnum;
	  etime = ntime;
	  run_sim_core (&sregs[i], ntime, deb, dis);
	  err_mode |= sregs[i].err_mode;
	  bphit |= sregs[i].bphit;
	  wphit |= ebase.wphit;
	  if (sregs[i].simtime < etime)
	    etime = sregs[i].simtime;
	}
      advance_time (etime);
      if (ctrl_c)
	{
	  icount = 0;
	}
    }

  oldcpu = cpu;
  cpu = ebase.bpcpu;
  if (err_mode == NULL_HIT)
    return NULL_HIT;
  if (err_mode)
    return ERROR_MODE;
  if (bphit)
    return (BPT_HIT);
  if (wphit)
    return (WPT_HIT);
  if (ctrl_c)
    {
      return CTRL_C;
    }
  cpu = oldcpu;
  return TIME_OUT;
}

int
run_sim (icount, dis)
     uint64 icount;
     int dis;
{
  int res;

  ctrl_c = 0;
  sim_run = 1;
  ebase.starttime = get_time ();
  ms->init_stdio ();
  if (ebase.tlimit > ebase.simtime)
    event (sim_timeout, 2, ebase.tlimit - ebase.simtime);
  if (ebase.coven)
    cov_start (sregs[0].pc);
  if ((ncpu == 1) || (icount == 1))
    res = run_sim_un (&sregs[cpu], icount, dis);
  else
    res = run_sim_mp (icount, dis);
  remove_event (sim_timeout, -1);
  ebase.tottime += get_time () - ebase.starttime;
  ms->restore_stdio ();
  if ((res == CTRL_C) && (ctrl_c == 2))
    printf ("\nTime-out limit reached\n");
  sim_run = 0;
  return res;
}

double
get_time (void)
{
  double usec;

  struct timeval tm;

  gettimeofday (&tm, NULL);
  usec = ((double) tm.tv_sec) * 1E6 + ((double) tm.tv_usec);
  return usec / 1E6;
}

/* Local version of getline() since not all systems supports it */

static const int line_size = 128;

static ssize_t
mygetdelim (char **lineptr, size_t * n, int delim, FILE * stream)
{
  int indx = 0;
  int c;

  /* Sanity checks.  */
  if (lineptr == NULL || n == NULL || stream == NULL)
    return -1;

  /* Allocate the line the first time.  */
  if (*lineptr == NULL)
    {
      *lineptr = malloc (line_size);
      if (*lineptr == NULL)
	return -1;
      *n = line_size;
    }

  /* Clear the line.  */
  memset (*lineptr, '\0', *n);

  while ((c = getc (stream)) != EOF)
    {
      /* Check if more memory is needed.  */
      if (indx >= *n)
	{
	  *lineptr = realloc (*lineptr, *n + line_size);
	  if (*lineptr == NULL)
	    {
	      return -1;
	    }
	  /* Clear the rest of the line.  */
	  memset (*lineptr + *n, '\0', line_size);
	  *n += line_size;
	}

      /* Push the result in the line.  */
      (*lineptr)[indx++] = c;

      /* Bail out.  */
      if (c == delim)
	{
	  break;
	}
    }
  return (c == EOF) ? -1 : indx;
}

static ssize_t
mygetline (char **lineptr, size_t * n, FILE * stream)
{
  return mygetdelim (lineptr, n, '\n', stream);
}

/* Coverage support */

#define COV_EXEC	1
#define COV_START	2
#define COV_JMP		4
#define COV_BT		8
#define COV_BNT		16

unsigned char covram[MAX_RAM_SIZE / 4];

void
cov_start (int address)
{
  covram[(address >> 2) & MAX_RAM_MASK] |= (COV_START | COV_EXEC);
}

void
cov_exec (int address)
{
  covram[(address >> 2) & MAX_RAM_MASK] |= COV_EXEC;
}


void
cov_bt (int address1, int address2)
{
  covram[(address1 >> 2) & MAX_RAM_MASK] |= (COV_BT | COV_EXEC);
  covram[(address2 >> 2) & MAX_RAM_MASK] |= (COV_START | COV_EXEC);
}

void
cov_bnt (int address)
{
  covram[(address >> 2) & MAX_RAM_MASK] |= (COV_BNT | COV_EXEC);
}

void
cov_jmp (int address1, int address2)
{
  covram[(address1 >> 2) & MAX_RAM_MASK] |= (COV_JMP | COV_EXEC);
  covram[(address2 >> 2) & MAX_RAM_MASK] |= (COV_START | COV_EXEC);
}

void
cov_save (char *name)
{
  FILE *fp;
  char filename[1024];
  int i, j, k, state;

  strcpy (filename, name);
  strcat (filename, ".cov");
  fp = fopen (filename, "w");
  state = 0;
  for (i = 0; i < MAX_RAM_SIZE / 4; i += 32)
    {
      k = 0;
      for (j = 0; j < 32; j++)
	{
	  k |= covram[i + j];
	}
      if (k || state)
	{
	  fprintf (fp, "%08x : ", ebase.ramstart + i * 4);
	  for (j = 0; j < 32; j++)
	    {
	      if (state)
		covram[i + j] |= COV_EXEC;
	      if (covram[i + j] & COV_START)
		state = 1;
	      if (covram[i + j] & (COV_JMP | COV_BT | COV_BNT))
		state = 0;
	      if ((ebase.ramstart + (i + j) * 4) == sregs[0].pc)
		state = 0;
	      covram[i + j] &= ~0x6;
	      fprintf (fp, "%x ", covram[i + j]);
	    }
	  fprintf (fp, "\n");
	}
    }
  printf ("\nsaved code coverage to %s\n", filename);
  fclose (fp);
}
