/* This file is part of SIS (SPARC/RISCV instruction simulator)

   Copyright (C) 1995-2019 Free Software Foundation, Inc.
   Contributed by Jiri Gaisler, Sweden.

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

//#include "opcode/riscv.h"

#include "config.h"
#include "sis.h"

#define C_EXTENSION
#define FPU_ENABLED
#define FPU_D_ENABLED
#define T_JALR	2
#define T_BMISS 2
#define T_MUL	8
#define T_DIV	35
#define T_AMO	5

#define TRAP_IEXC   1
#define TRAP_ILLEG  2
#define TRAP_EBREAK 3
#define TRAP_LMALI  4
#define TRAP_LEXC   5
#define TRAP_SMALI  6
#define TRAP_SEXC   7
#define TRAP_ECALLU 8
#define TRAP_ECALLS 9
#define TRAP_ECALLM 11
#define TRAP_IPAGEF 13
#define TRAP_LPAGEF 13
#define TRAP_SPAGEF 15

#define FSR_TT		0x1C000
#define FP_IEEE		0x04000
#define FP_UNIMP	0x0C000
#define FP_SEQ_ERR	0x10000

#define	OP_LOAD		0x00
#define	OP_FLOAD	0x01
#define	OP_FENCE	0x03
#define	OP_IMM		0x04
#define	OP_AUIPC	0x05
#define	OP_STORE	0x08
#define	OP_FSW		0x09
#define	OP_AMO		0x0b
#define	OP_REG		0x0c
#define	OP_LUI		0x0d
#define	OP_FMADD	0x10
#define	OP_FMSUB	0x11
#define	OP_FNMSUB	0x12
#define	OP_FNMADD	0x13
#define	OP_FPU		0x14
#define	OP_BRANCH	0x18
#define	OP_JALR		0x19
#define	OP_JAL		0x1b
#define	OP_SYS		0x1c

#define	B_BE		0
#define	B_BNE		1
#define	B_BLT		4
#define	B_BGE		5
#define	B_BLTU		6
#define	B_BGEU		7

#define	ADD		0
#define	SLL		1
#define	SLT		2
#define	SLTU		3
#define	IXOR		4
#define	SRL		5
#define	IOR		6
#define	IAND		7

#define	SB		0
#define	SH		1
#define	SW		2
#define	LB		0
#define	LH		1
#define	LW		2
#define	LD		3
#define	LBU		4
#define	LHU		5

#define	AMOADD		0
#define	AMOSWAP		1
#define	LRQ		2
#define	SCQ		3
#define	AMOXOR		5
#define	AMOOR		8
#define	AMOAND		0x0C
#define	AMOMIN		0x10
#define	AMOMAX		0x14
#define	AMOMINU		0x18
#define	AMOMAXU		0x1C

#define	CSRRW		1
#define	CSRRS		2
#define	CSRRC		3
#define	CSRRWI		5
#define	CSRRSI		6
#define	CSRRCI		7

#define CADDI4SPN	0
#define CFLD		1
#define CLW		2
#define CFLW		3
#define CFSD		5
#define CSW		6
#define CFSW		7
#define CADDI		0
#define CJAL 		1
#define CLI 		2
#define CADDI16SP 	3
#define CARITH	 	4
#define CJNL 		5
#define CBEQZ 		6
#define CBNEZ 		7

#define SIGN_BIT 0x80000000

/* # of cycles overhead when a trap is taken */
#define TRAP_C  3

#define CSR_MSTATUS 0x300
#define CSR_MTVEC 0x305
#define CSR_MEPC 0x341
#define CSR_MIE 0x304
#define CSR_MIP 0x344
#define CSR_MSCRATCH 0x340
#define CSR_MCAUSE 0x342
#define CSR_FFLAGS 0x1
#define CSR_FRM 0x2
#define CSR_FCSR 0x3
#define CSR_TIMEH 0xc81
#define CSR_TIME 0xc01
#define CSR_MHARTID 0xf14
#define CSR_MISA 0x301
#define CSR_MTVAL 0x343

#define MSTATUS_MIE  0x08
#define MSTATUS_MPIE 0x80
#define MSTATUS_MPP  0x1800

#define MIP_MTI   0x080
#define MIP_MEIP  0x800
#define MIE_MTIE  0x080
#define MIE_MEIE  0x800

#define MSTATUS_MASK 0x1888
#define LDDM	0x7

#define RISCV_IMM_BITS 12

#define RV_IMM_SIGN(x) (-(((x) >> 31) & 1))
#define RV_X(x, s, n)  (((x) >> (s)) & ((1 << (n)) - 1))
#define EXTRACT_RVC_LW_IMM(x) \
  ((RV_X(x, 6, 1) << 2) | (RV_X(x, 10, 3) << 3) | (RV_X(x, 5, 1) << 6))

#define EXTRACT_RVC_ADDI4SPN_IMM(x) \
    ((RV_X(x, 6, 1) << 2) | (RV_X(x, 5, 1) << 3) | (RV_X(x, 11, 2) << 4) | (RV_X(x, 7, 4) << 6))

#define EXTRACT_RVC_LD_IMM(x) \
      ((RV_X(x, 10, 3) << 3) | (RV_X(x, 5, 2) << 6))
#define EXTRACT_RVC_IMM(x) \
        (RV_X(x, 2, 5) | (-RV_X(x, 12, 1) << 5))

#define EXTRACT_RVC_J_IMM(x) \
	  ((RV_X(x, 3, 3) << 1) | (RV_X(x, 11, 1) << 4) | (RV_X(x, 2, 1) << 5) | (RV_X(x, 7, 1) << 6) | (RV_X(x, 6, 1) << 7) | (RV_X(x, 9, 2) << 8) | (RV_X(x, 8, 1) << 10) | (-RV_X(x, 12, 1) << 11))

#define EXTRACT_RVC_ADDI16SP_IMM(x) \
	    ((RV_X(x, 6, 1) << 4) | (RV_X(x, 2, 1) << 5) | (RV_X(x, 5, 1) << 6) | (RV_X(x, 3, 2) << 7) | (-RV_X(x, 12, 1) << 9))
#define EXTRACT_RVC_LUI_IMM(x) \
	      (EXTRACT_RVC_IMM (x) << RISCV_IMM_BITS)
#define EXTRACT_RVC_B_IMM(x) \
	        ((RV_X(x, 3, 2) << 1) | (RV_X(x, 10, 2) << 3) | (RV_X(x, 2, 1) << 5) | (RV_X(x, 5, 2) << 6) | (-RV_X(x, 12, 1) << 8))
#define EXTRACT_RVC_J_IMM(x) \
		  ((RV_X(x, 3, 3) << 1) | (RV_X(x, 11, 1) << 4) | (RV_X(x, 2, 1) << 5) | (RV_X(x, 7, 1) << 6) | (RV_X(x, 6, 1) << 7) | (RV_X(x, 9, 2) << 8) | (RV_X(x, 8, 1) << 10) | (-RV_X(x, 12, 1) << 11))

#define EXTRACT_RVC_LWSP_IMM(x) \
		    ((RV_X(x, 4, 3) << 2) | (RV_X(x, 12, 1) << 5) | (RV_X(x, 2, 2) << 6))
#define EXTRACT_RVC_LDSP_IMM(x) \
		      ((RV_X(x, 5, 2) << 3) | (RV_X(x, 12, 1) << 5) | (RV_X(x, 2, 3) << 6))

#define EXTRACT_RVC_SWSP_IMM(x) \
		        ((RV_X(x, 9, 4) << 2) | (RV_X(x, 7, 2) << 6))
#define EXTRACT_RVC_SDSP_IMM(x) \
			  ((RV_X(x, 10, 3) << 3) | (RV_X(x, 7, 3) << 6))


#define EXTRACT_ITYPE_IMM(x) \
			    (RV_X(x, 20, 12) | (RV_IMM_SIGN(x) << 12))
#define EXTRACT_STYPE_IMM(x) \
			      (RV_X(x, 7, 5) | (RV_X(x, 25, 7) << 5) | (RV_IMM_SIGN(x) << 12))
#define EXTRACT_SBTYPE_IMM(x) \
			        ((RV_X(x, 8, 4) << 1) | (RV_X(x, 25, 6) << 5) | (RV_X(x, 7, 1) << 11) | (RV_IMM_SIGN(x) << 12))
#define EXTRACT_UTYPE_IMM(x) \
				  ((RV_X(x, 12, 20) << 12) | (RV_IMM_SIGN(x) << 32))
#define EXTRACT_UJTYPE_IMM(x) \
				    ((RV_X(x, 21, 10) << 1) | (RV_X(x, 20, 1) << 11) | (RV_X(x, 12, 8) << 12) | (RV_IMM_SIGN(x) << 20))

