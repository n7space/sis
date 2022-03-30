/* This file is part of SIS (SPARC/RISCV instruction simulator)

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
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#include <stdio.h>
#include <fcntl.h>
#include "sis.h"
#include <inttypes.h>
#include <libgen.h>

#if HAVE_READLINE
#include "readline/readline.h"
#include "readline/history.h"
#else
/* Linenoise as a readline library replacement
   https://github.com/antirez/linenoise */
#include "linenoise.h"
#endif

/* Command history buffer length - MUST be binary */
#define HIST_LEN 256

int main(argc, argv) int argc;
char** argv;
{
    int cont = 1;
    int stat = 1;
    int freq = 0;
    int copt = 0;

    char *cfile, *bacmd;
    char* cmdq[HIST_LEN];
    int cmdi = 0;
    int i;
    int lfile = 0;
    char tlim[64] = "";
    int run = 0;
    char prompt[8];
    int gdb = 0;

    printf("\n SIS - SPARC/RISCV instruction simulator %s,  copyright Jiri "
           "Gaisler 2020\n",
           sis_version);
    printf(" Bug-reports to jiri@gaisler.se\n\n");

    /* initialize history buffer */
    for(i = 0; i < HIST_LEN; i++)
        cmdq[i] = 0;

    /* if binary name starts with riscv, force RISCV emulation */
    strncpy(uart_dev0, argv[0], 128);
    cfile = basename(uart_dev0);
    if(strncmp(cfile, "riscv", 5) == 0)
        archtype = CPU_RISCV;
    if(strncmp(cfile, "sparc", 5) == 0)
        archtype = CPU_SPARC;

    cfile = 0;
    uart_dev0[0] = 0;

    /* parse start-up switches */
    while(stat < argc) {
        if(argv[stat][0] == '-') {
            if(strcmp(argv[stat], "-v") == 0) {
                sis_verbose += 1;
            } else if(strcmp(argv[stat], "-c") == 0) {
                if((stat + 1) < argc) {
                    copt = 1;
                    cfile = argv[++stat];
                }
            } else if(strcmp(argv[stat], "-bridge") == 0) {
                if((stat + 1) < argc)
                    strncpy(bridge, argv[++stat], 31);
            } else if(strcmp(argv[stat], "-cov") == 0)
                ebase.coven = 1;
            else if(strcmp(argv[stat], "-nfp") == 0)
                nfp = 1;
            else if(strcmp(argv[stat], "-ift") == 0)
                ift = 1;
            else if(strcmp(argv[stat], "-wrp") == 0)
                wrp = 1;
            else if(strcmp(argv[stat], "-rom8") == 0)
                rom8 = 1;
            else if(strcmp(argv[stat], "-uben") == 0)
                uben = 1;
            else if(strcmp(argv[stat], "-uart1") == 0) {
                if((stat + 1) < argc)
                    strcpy(uart_dev0, argv[++stat]);
            } else if(strcmp(argv[stat], "-uart2") == 0) {
                if((stat + 1) < argc)
                    strcpy(uart_dev1, argv[++stat]);
            } else if(strcmp(argv[stat], "-freq") == 0) {
                if((stat + 1) < argc)
                    freq = VAL(argv[++stat]);
            } else if(strcmp(argv[stat], "-dumbio") == 0) {
                dumbio = 1;
            } else if(strcmp(argv[stat], "-gdb") == 0) {
                gdb = 1;
            } else if(strcmp(argv[stat], "-port") == 0) {
                if((stat + 1) < argc)
                    port = VAL(argv[++stat]);
            } else if(strcmp(argv[stat], "-nouartrx") == 0) {
                nouartrx = 1;
            } else if(strcmp(argv[stat], "-r") == 0) {
                run = 1;
            } else if(strcmp(argv[stat], "-rt") == 0) {
                sync_rt = 1;
            } else if(strcmp(argv[stat], "-erc32") == 0) {
                cputype = CPU_ERC32;
                archtype = CPU_SPARC;
            } else if(strcmp(argv[stat], "-leon2") == 0) {
                cputype = CPU_LEON2;
                archtype = CPU_SPARC;
            } else if(strcmp(argv[stat], "-leon3") == 0) {
                cputype = CPU_LEON3;
                archtype = CPU_SPARC;
            } else if(strcmp(argv[stat], "-gr740") == 0) {
                cputype = CPU_LEON4;
                archtype = CPU_SPARC;
            } else if(strcmp(argv[stat], "-riscv") == 0) {
                archtype = CPU_RISCV;
            } else if(strcmp(argv[stat], "-griscv") == 0) {
                cputype = CPU_LEON3;
                archtype = CPU_RISCV;
            } else if(strcmp(argv[stat], "-rv32") == 0) {
                cputype = CPU_RISCV;
                archtype = CPU_RISCV;
            } else if(strcmp(argv[stat], "-tlim") == 0) {
                if((stat + 2) < argc) {
                    strcpy(tlim, "tlim ");
                    strcat(tlim, argv[++stat]);
                    strcat(tlim, " ");
                    strcat(tlim, argv[++stat]);
                }
            } else if(strcmp(argv[stat], "-m") == 0) {
                if((stat + 1) < argc)
                    ncpu = VAL(argv[++stat]);
                if((ncpu < 1) || (ncpu > NCPU))
                    ncpu = 1;
            } else if(strcmp(argv[stat], "-d") == 0) {
                if((stat + 1) < argc)
                    delta = VAL(argv[++stat]);
                if(delta <= 0)
                    delta = 25;
            } else {
                printf("unknown option %s\n", argv[stat]);
                sis_usage();
                exit(1);
            }
        } else {
            lfile = stat;
        }
        stat++;
    }

    if(lfile) {
        last_load_addr = elf_load(argv[lfile], 0);
    }

    if(!archtype) {
        if(!ebase.arch) {
            archtype = CPU_SPARC;
            cputype = CPU_ERC32;
        } else {
            archtype = ebase.arch;
            cputype = ebase.cpu;
        }
        if(!cputype)
            cputype = CPU_LEON3;
    } else if(!cputype) {
        cputype = ebase.cpu;
        if(!cputype) {
            if(archtype == CPU_SPARC)
                cputype = CPU_ERC32;
            else
                cputype = CPU_LEON3;
        }
    }

    switch(cputype) {
        case CPU_ERC32:
            printf(" ERC32 emulation enabled\n");
            cputype = CPU_ERC32;
            ms = &erc32sys;
            if(!freq)
                freq = 14;
            break;
        case CPU_LEON2:
            printf(" LEON2 emulation enabled\n");
            ms = &leon2;
            if(!freq)
                freq = 50;
            break;
        case CPU_LEON3:
            if(archtype == CPU_SPARC)
                printf(" LEON3 emulation enabled, %d cpus online, delta %d "
                       "clocks\n",
                       ncpu,
                       delta);
            else {
                printf(" RISCV/GRLIB emulation enabled, %d cpus online, delta "
                       "%d clocks\n",
                       ncpu,
                       delta);
                arch = &riscv;
            }
            ms = &leon3;
            if(!freq)
                freq = 50;
            break;
        case CPU_LEON4:
            printf(" GR740/LEON4 emulation enabled, %d cpus online, delta %d "
                   "clocks\n",
                   ncpu,
                   delta);
            ms = &gr740;
            if(!freq)
                freq = 50;
            break;
        case CPU_RISCV:
            //      if (delta == 50)        delta = 25;   // 25 clock delta
            //      works better with the CLINT
            printf(" RISCV/CLINT emulation enabled, %d cpus online, delta %d "
                   "clocks\n",
                   ncpu,
                   delta);
            ms = &rv32;
            arch = &riscv;
            if(!freq)
                freq = 50;
            break;
        default:
            printf(" ERC32 emulation enabled\n");
            cputype = CPU_ERC32;
            ms = &erc32sys;
            if(!freq)
                freq = 14;
    }

#ifdef ENABLE_L1CACHE
    if(ncpu > 1)
        printf(" L1 cache: %dK/%dK, %d bytes/line \n",
               (1 << (L1IBITS - 10)),
               (1 << (L1DBITS - 10)),
               (1 << L1ILINEBITS));
#endif

    if(nfp)
        printf(" FPU disabled\n");
    ebase.freq = freq;
    printf("\n");

#ifdef F_GETFL
    termsave = fcntl(0, F_GETFL, 0);
#endif
#if HAVE_READLINE
    using_history();
#else
    linenoiseHistorySetMaxLen(HIST_LEN);
#endif
    init_signals();
    ebase.simtime = 0;
    ebase.simstart = 0;
    reset_all();
    init_bpt(sregs);
    ms->init_sim();
    if(lfile) {
        last_load_addr = elf_load(argv[lfile], 1);
        daddr = last_load_addr;
    }
#ifdef STAT
    reset_stat(&sregs);
#endif

    if(copt) {
        bacmd = (char*)malloc(256);
        strcpy(bacmd, "batch ");
        strcat(bacmd, cfile);
        exec_cmd(bacmd);
    }
    if(tlim[0]) {
        exec_cmd(tlim);
    }

    if(gdb) {
        sregs->pc = last_load_addr;
        gdb_remote(port);
    }

    while(cont) {
        if(cmdq[cmdi] != 0) {
#if HAVE_READLINE
            free(cmdq[cmdi]);
#else
            linenoiseFree(cmdq[cmdi]);
#endif
            cmdq[cmdi] = 0;
        }
        if(run) {
            stat = exec_cmd("run");
            cont = 0;
        } else {
            if(ncpu > 1)
                sprintf(prompt, "cpu%d> ", cpu);
            else
                sprintf(prompt, "sis> ");
#if HAVE_READLINE
            cmdq[cmdi] = readline(prompt);
#else
            cmdq[cmdi] = linenoise(prompt);
#endif
            if(cmdq[cmdi] && *cmdq[cmdi])
#if HAVE_READLINE
                add_history(cmdq[cmdi]);
#else
                linenoiseHistoryAdd(cmdq[cmdi]);
#endif
            if(cmdq[cmdi])
                stat = exec_cmd(cmdq[cmdi]);
            else {
                puts("\n");
                exit(0);
            }
        }
        switch(stat) {
            case OK:
                break;
            case CTRL_C:
                printf("\b\bInterrupt!\n");
            case TIME_OUT:
                printf(" Stopped at time %" PRIu64 " (%.3f ms)\n",
                       ebase.simtime,
                       ((double)ebase.simtime / (double)ebase.freq) / 1000.0);
                break;
            case BPT_HIT:
                printf("cpu %d breakpoint at 0x%08x reached\n",
                       ebase.bpcpu,
                       sregs[ebase.bpcpu].pc);
                break;
            case ERROR_MODE:
                printf("cpu %d in error mode (tt = 0x%02x)\n",
                       ebase.bpcpu,
                       sregs[ebase.bpcpu].trap);
                stat = 0;
                printf(" %8" PRIu64 " ", ebase.simtime);
                dis_mem(sregs[cpu].pc, 1);
                break;
            case WPT_HIT:
                printf("cpu %d watchpoint at 0x%08x reached, pc = 0x%08x\n",
                       ebase.bpcpu,
                       ebase.wpaddress,
                       sregs[ebase.bpcpu].pc);
                ebase.wphit = 1;
                break;
            case NULL_HIT:
                printf("segmentation error, cpu %d halted\n", ebase.bpcpu);
                stat = 0;
                printf(" %8" PRIu64 " ", ebase.simtime);
                dis_mem(sregs[cpu].pc, 1);
                break;
            case QUIT:
                cont = 0;
                break;
            default:
                break;
        }
        ctrl_c = 0;
        stat = OK;

        cmdi = (cmdi + 1) & (HIST_LEN - 1);
    }
    if(ebase.coven)
        cov_save(argv[lfile]);
    return 0;
}
