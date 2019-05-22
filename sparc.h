#include "config.h"
#include "sis.h"

/* Load/store interlock delay */
#define FLSTHOLD 1

/* Load delay (delete if unwanted - speeds up simulation) */
#define LOAD_DEL 1

#define T_LD	2
#define T_LDD	3
#define T_ST	3
#define T_STD	4
#define T_LDST	4
#define T_JMPL	2
#define T_RETT	2
#define T_MUL	5
#define T_DIV	35

#define FSR_QNE 	0x2000
#define FP_EXE_MODE 0
#define	FP_EXC_PE   1
#define FP_EXC_MODE 2

#define	FBA	8
#define	FBN	0
#define	FBNE	1
#define	FBLG	2
#define	FBUL	3
#define	FBL 	4
#define	FBUG	5
#define	FBG 	6
#define	FBU 	7
#define FBA	8
#define FBE	9
#define FBUE	10
#define FBGE	11
#define FBUGE	12
#define FBLE	13
#define FBULE	14
#define FBO	15

#define	FCC_E 	0
#define	FCC_L 	1
#define	FCC_G 	2
#define	FCC_U 	3

#define PSR_ET 0x20
#define PSR_EF 0x1000
#define PSR_PS 0x40
#define PSR_S  0x80
#define PSR_N  0x0800000
#define PSR_Z  0x0400000
#define PSR_V  0x0200000
#define PSR_C  0x0100000
#define PSR_CC 0x0F00000
#define PSR_CWP 0x7
#define PSR_PIL 0x0f00

#define ICC_N	(icc >> 3)
#define ICC_Z	(icc >> 2)
#define ICC_V	(icc >> 1)
#define ICC_C	(icc)

#define FP_PRES	(sregs->fpu_pres)

#define TRAP_IEXC 1
#define TRAP_UNIMP 2
#define TRAP_PRIVI 3
#define TRAP_FPDIS 4
#define TRAP_WOFL 5
#define TRAP_WUFL 6
#define TRAP_UNALI 7
#define TRAP_FPEXC 8
#define TRAP_DEXC 9
#define TRAP_TAG 10
#define TRAP_DIV0 0x2a

#define FSR_TT		0x1C000
#define FP_IEEE		0x04000
#define FP_UNIMP	0x0C000
#define FP_SEQ_ERR	0x10000

#define	BICC_BN		0
#define	BICC_BE		1
#define	BICC_BLE	2
#define	BICC_BL		3
#define	BICC_BLEU	4
#define	BICC_BCS	5
#define	BICC_NEG	6
#define	BICC_BVS	7
#define	BICC_BA		8
#define	BICC_BNE	9
#define	BICC_BG		10
#define	BICC_BGE	11
#define	BICC_BGU	12
#define	BICC_BCC	13
#define	BICC_POS	14
#define	BICC_BVC	15

#define INST_SIMM13 0x1fff
#define INST_RS2    0x1f
#define INST_I	    0x2000
#define ADD 	0x00
#define ADDCC 	0x10
#define ADDX 	0x08
#define ADDXCC 	0x18
#define TADDCC 	0x20
#define TSUBCC  0x21
#define TADDCCTV 0x22
#define TSUBCCTV 0x23
#define IAND 	0x01
#define IANDCC 	0x11
#define IANDN 	0x05
#define IANDNCC	0x15
#define MULScc 	0x24
#define DIVScc 	0x1D
#define SMUL	0x0B
#define SMULCC	0x1B
#define UMUL	0x0A
#define UMULCC	0x1A
#define SDIV	0x0F
#define SDIVCC	0x1F
#define UDIV	0x0E
#define UDIVCC	0x1E
#define IOR 	0x02
#define IORCC 	0x12
#define IORN 	0x06
#define IORNCC 	0x16
#define SLL 	0x25
#define SRA 	0x27
#define SRL 	0x26
#define SUB 	0x04
#define SUBCC 	0x14
#define SUBX 	0x0C
#define SUBXCC 	0x1C
#define IXNOR 	0x07
#define IXNORCC	0x17
#define IXOR 	0x03
#define IXORCC 	0x13
#define SETHI 	0x04
#define BICC 	0x02
#define FPBCC 	0x06
#define RDY 	0x28
#define RDPSR 	0x29
#define RDWIM 	0x2A
#define RDTBR 	0x2B
#define SCAN 	0x2C
#define WRY	0x30
#define WRPSR	0x31
#define WRWIM	0x32
#define WRTBR	0x33
#define JMPL 	0x38
#define RETT 	0x39
#define TICC 	0x3A
#define SAVE 	0x3C
#define RESTORE 0x3D
#define LDD	0x03
#define LDDA	0x13
#define LD	0x00
#define LDA	0x10
#define LDF	0x20
#define LDDF	0x23
#define LDSTUB	0x0D
#define LDSTUBA	0x1D
#define LDUB	0x01
#define LDUBA	0x11
#define LDSB	0x09
#define LDSBA	0x19
#define LDUH	0x02
#define LDUHA	0x12
#define LDSH	0x0A
#define LDSHA	0x1A
#define LDFSR	0x21
#define ST	0x04
#define STA	0x14
#define STB	0x05
#define STBA	0x15
#define STD	0x07
#define STDA	0x17
#define STF	0x24
#define STDFQ	0x26
#define STDF	0x27
#define STFSR	0x25
#define STH	0x06
#define STHA	0x16
#define SWAP	0x0F
#define SWAPA	0x1F
#define FLUSH	0x3B
#define CASA	0x3C

/* # of cycles overhead when a trap is taken */
#define TRAP_C  3

/* Forward declarations */

static int fpexec (uint32 op3, uint32 rd, uint32 rs1, uint32 rs2,
		   struct pstate *sregs);
