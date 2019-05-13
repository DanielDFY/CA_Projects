#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/* An implementation of 5-stage classic pipeline simulation */

/* don't count instructions flag, enabled by default, disable for inst count */
#undef NO_INSN_COUNT

#include "host.h"
#include "misc.h"
#include "machine.h"
#include "regs.h"
#include "memory.h"
#include "loader.h"
#include "syscall.h"
#include "dlite.h"
#include "sim.h"
#include "sim-pipe.h"

/* simulated registers */
static struct regs_t regs;

/* simulated memory */
static struct mem_t *mem = NULL;

/* register simulator-specific options */
void
sim_reg_options(struct opt_odb_t *odb)
{
  opt_reg_header(odb, 
"sim-pipe: This simulator implements based on sim-fast.\n"
		 );
}

/* check simulator-specific option values */
void
sim_check_options(struct opt_odb_t *odb, int argc, char **argv)
{
  if (dlite_active)
    fatal("sim-pipe does not support DLite debugging");
}

/* register simulator-specific statistics */
void
sim_reg_stats(struct stat_sdb_t *sdb)
{
#ifndef NO_INSN_COUNT
  stat_reg_counter(sdb, "sim_num_insn",
		   "total number of instructions executed",
		   &sim_num_insn, sim_num_insn, NULL);
#endif /* !NO_INSN_COUNT */
  stat_reg_int(sdb, "sim_elapsed_time",
	       "total simulation time in seconds",
	       &sim_elapsed_time, 0, NULL);
#ifndef NO_INSN_COUNT
  stat_reg_formula(sdb, "sim_inst_rate",
		   "simulation speed (in insts/sec)",
		   "sim_num_insn / sim_elapsed_time", NULL);
#endif /* !NO_INSN_COUNT */
  ld_reg_stats(sdb);
  mem_reg_stats(mem, sdb);
}


struct ifid_buf fd;
struct idex_buf de;
struct exmem_buf em;
struct memwb_buf mw;
struct wb_buf wb;
struct control_buf ctl;

#define DNA			(-1)

/* general register dependence decoders */
#define DGPR(N)			(N)
#define DGPR_D(N)		((N) &~1)

/* floating point register dependence decoders */
#define DFPR_L(N)		(((N)+32)&~1)
#define DFPR_F(N)		(((N)+32)&~1)
#define DFPR_D(N)		(((N)+32)&~1)

/* miscellaneous register dependence decoders */
#define DHI			(0+32+32)
#define DLO			(1+32+32)
#define DFCC		(2+32+32)
#define DTMP		(3+32+32)

/* initialize the simulator */
void
sim_init(void)
{
  /* allocate and initialize register file */
  regs_init(&regs);

  /* allocate and initialize memory space */
  mem = mem_create("mem");
  mem_init(mem);

  /* initialize stage latches*/
  sim_num_insn = 0;
  /* IF/ID */
  fd.inst.a = NOP;
  fd.PC = 0;
  fd.NPC = 0;
  /* ID/EX */
  de.inst.a = NOP;
  de.PC = 0;
  de.iflags = 0;
  de.func = 0;
  de.srcA = 0;
  de.srcB = 0;
  de.busA = 0;
  de.busB = 0;
  de.sw = 0;
  de.dstR = DNA;
  de.dstM = DNA;
  de.rwflag = 0;
  de.target = 0;
  /* EX/MEM */
  em.inst.a = NOP;
  em.PC = 0;
  em.alu = 0;
  em.sw = 0;
  em.dstR = DNA;
  de.dstM = DNA;
  em.rwflag = 0;
  em.target = 0;
  /* MEM/WB */
  mw.inst.a = NOP;
  mw.PC = 0;
  mw.memLoad = 0;
  mw.dstR = DNA;
  de.dstM = DNA;
  mw.rwflag = 0;
  /* WB */
  wb.inst.a = NOP;
  wb.PC = 0;
  /* CTL */
  ctl.ch = 0;
  ctl.cond = 0;
  ctl.dst = 0;
  ctl.dh = 0;
}

/* load program into simulated state */
void
sim_load_prog(char *fname,		/* program to load */
	      int argc, char **argv,	/* program arguments */
	      char **envp)		/* program environment */
{
  /* load program text and data, set up environment, memory, and regs */
  ld_load_prog(fname, argc, argv, envp, &regs, mem, TRUE);
}

/* print simulator-specific configuration information */
void
sim_aux_config(FILE *stream)
{  
	/* nothing currently */
}

/* dump simulator-specific auxiliary simulator statistics */
void
sim_aux_stats(FILE *stream)
{  /* nada */}

/* un-initialize simulator-specific state */
void 
sim_uninit(void)
{ /* nada */ }


/*
 * configure the execution engine
 */

/* next program counter */
#define SET_NPC(EXPR)		(regs.regs_NPC = (EXPR))

/* current program counter */
#define CPC			(regs.regs_PC)

/* general purpose registers */
#define GPR(N)			(regs.regs_R[N])
#define SET_GPR(N,EXPR)		(regs.regs_R[N] = (EXPR))
#define DECLARE_FAULT(EXP) 	{;}
#if defined(TARGET_PISA)

/* floating point registers, L->word, F->single-prec, D->double-prec */
#define FPR_L(N)		(regs.regs_F.l[(N)])
#define SET_FPR_L(N,EXPR)	(regs.regs_F.l[(N)] = (EXPR))
#define FPR_F(N)		(regs.regs_F.f[(N)])
#define SET_FPR_F(N,EXPR)	(regs.regs_F.f[(N)] = (EXPR))
#define FPR_D(N)		(regs.regs_F.d[(N) >> 1])
#define SET_FPR_D(N,EXPR)	(regs.regs_F.d[(N) >> 1] = (EXPR))

/* miscellaneous register accessors */
#define SET_HI(EXPR)		(regs.regs_C.hi = (EXPR))
#define HI			(regs.regs_C.hi)
#define SET_LO(EXPR)		(regs.regs_C.lo = (EXPR))
#define LO			(regs.regs_C.lo)
#define FCC			(regs.regs_C.fcc)
#define SET_FCC(EXPR)		(regs.regs_C.fcc = (EXPR))

#endif

/* precise architected memory state accessor macros */
#define READ_BYTE(SRC, FAULT)						\
  ((FAULT) = md_fault_none, MEM_READ_BYTE(mem, (SRC)))
#define READ_HALF(SRC, FAULT)						\
  ((FAULT) = md_fault_none, MEM_READ_HALF(mem, (SRC)))
#define READ_WORD(SRC, FAULT)						\
  ((FAULT) = md_fault_none, MEM_READ_WORD(mem, (SRC)))
#ifdef HOST_HAS_QWORD
#define READ_QWORD(SRC, FAULT)						\
  ((FAULT) = md_fault_none, MEM_READ_QWORD(mem, (SRC)))
#endif /* HOST_HAS_QWORD */

#define WRITE_BYTE(SRC, DST, FAULT)					\
  ((FAULT) = md_fault_none, MEM_WRITE_BYTE(mem, (DST), (SRC)))
#define WRITE_HALF(SRC, DST, FAULT)					\
  ((FAULT) = md_fault_none, MEM_WRITE_HALF(mem, (DST), (SRC)))
#define WRITE_WORD(SRC, DST, FAULT)					\
  ((FAULT) = md_fault_none, MEM_WRITE_WORD(mem, (DST), (SRC)))
#ifdef HOST_HAS_QWORD
#define WRITE_QWORD(SRC, DST, FAULT)					\
  ((FAULT) = md_fault_none, MEM_WRITE_QWORD(mem, (DST), (SRC)))
#endif /* HOST_HAS_QWORD */

/* system call handler macro */
#define SYSCALL(INST)	sys_syscall(&regs, mem_access, mem, INST, TRUE)

#ifndef NO_INSN_COUNT
#define INC_INSN_CTR()	sim_num_insn++
#else /* !NO_INSN_COUNT */
#define INC_INSN_CTR()	/* nada */
#endif /* NO_INSN_COUNT */


/* start simulation, program loaded, processor precise state initialized */
void
sim_main(void)
{
  fprintf(stderr, "sim: ** starting *pipe* functional simulation **\n");

  /* must have natural byte/word ordering */
  if (sim_swap_bytes || sim_swap_words)
    fatal("sim: *pipe* functional simulation cannot swap bytes or words");

  /* set up initial default next PC */
  regs.regs_NPC = regs.regs_PC + sizeof(md_inst_t);
  /* maintain $r0 semantics */
  regs.regs_R[MD_REG_ZERO] = 0;

  /* initalize PC */
  fd.PC = regs.regs_PC - sizeof(md_inst_t);

  while (TRUE)
  {
    ++sim_num_insn;
	  do_pipeline_ctl();
    do_wb();
    do_mem();
    do_ex();
    do_id();
    do_if();
    /* print current trace */
    do_log();
  }
}

void forward(int *val, int *src) {
  if(*src != DNA) {
    if(*src == em.dstR) {
      *val = em.alu;
    } else if(*src == mw.dstM) {
      *val = mw.memLoad;      
    } else {
      *val = GPR(*src);      
    }
  } else {
    *val = 0;
  }
}

void do_forward() {
  forward(&de.busA, &de.srcA);
  forward(&de.busB, &de.srcB);
  forward(&de.sw, &de.oprand.in1);
}

/* since load-use hazard can't be forwarding*/
void do_pipeline_ctl() {
  /* insert NOP for load hazard */
  if(ctl.dh) {
    fd.PC = de.PC;
    fd.inst = de.inst;
    de.inst.a = NOP;
  }
}

void do_if()
{
  if(ctl.ch) {
    fd.NPC = de.target;
    ctl.ch = FALSE;
  } else {
    fd.NPC = fd.PC + sizeof(md_inst_t);
  }
  /* instruction fetch */
  fd.PC = fd.NPC;
  MD_FETCH_INSTI(fd.inst, mem, fd.PC);
}

void do_id() {
    de.inst = fd.inst;
    de.PC = fd.PC;
    de.rwflag= 0;
    if(NOP == de.inst.a) return;
    MD_SET_OPCODE(de.opcode, de.inst);
    md_inst_t inst = de.inst;
#define DEFINST(OP,MSK,NAME,OPFORM,RES,FLAGS,O1,O2,I1,I2,I3)\
  if (OP==de.opcode){\
    de.iflags = FLAGS;\
    de.oprand.out1 = O1;\
    de.oprand.out2 = O2;\
    de.oprand.in1 = I1;\
    de.oprand.in2 = I2;\
    de.oprand.in3 = I3;\
    goto READ_OPRAND_VALUE;\
  }
#define DEFLINK(OP,MSK,NAME,MASK,SHIFT)
#define CONNECT(OP)
#include "machine.def"
READ_OPRAND_VALUE:
  /* check for stall */    
  if((de.oprand.in1 >= 0 && (ctl.dst&1<<de.oprand.in1)) || (de.oprand.in2 >= 0 && (ctl.dst&1<<de.oprand.in2))) {
    ctl.dh = TRUE;
    return;
  } else {
    ctl.dh = FALSE;
  }
  
  int oprA = GPR(de.oprand.in1);
  int oprB = GPR(de.oprand.in2);
  
  switch (de.opcode) {
      case ADD:
      case ADDU:
      case ADDIU:
      case LW:
      case SW:
      case LUI:
        de.func = ALU_ADD;
        break;
      case ANDI:
        de.func = ALU_AND;
        break;
      case SLL:
        de.func = ALU_SLL;
        break;
      case SLTI:
        de.func = ALU_SLT;
        break;
      case JUMP:
        ctl.ch = TRUE;
        de.target = (fd.PC & 0xf0000000) | ((de.inst.b & 0x3ffffff) << 2);
        de.func = ALU_NOP;
        break;
      case BNE:
        if (em.dstR != DNA) {
          if (em.dstR == de.oprand.in1)
            oprA = em.alu;
          else if (em.dstR == de.oprand.in2)
            oprB = em.alu;
        }
        if(oprA ^ oprB) {
          ctl.ch = TRUE;
          de.target = fd.PC + 8 + ((de.inst.b & 0xffff) << 2);
        }
        de.func = ALU_NOP;
        break;
      default:
        de.func = ALU_NOP;
        break;
  }
  /* src A*/
  if(de.iflags & F_DISP) {
    de.srcA = de.oprand.in2; 
  } else {
    de.srcA = de.oprand.in1; 
  }
  /* src B */
  de.srcB = de.oprand.in2;
  
  /* store */
  if(de.iflags&F_STORE) {
    de.rwflag |= 2;    
  }
  /* dst/load */ 
  if(de.iflags&F_LOAD) {
    de.rwflag |= 4;
    de.dstM = de.oprand.out1;
    de.dstR = DNA;
    /* write-in register */
    ctl.dst |= 1 << de.oprand.out1;
  } else {
    de.dstR = de.oprand.out1;
    de.dstM = DNA;
  }
  do_forward();
}

void do_ex() {
  em.inst = de.inst;
  em.PC = de.PC;
  em.dstR = de.dstR;
  em.dstM = de.dstM;  
  em.sw = de.sw;
  em.rwflag = de.rwflag;
  em.target = de.target;
  /* alu A */
  int aluA = de.busA;
  /* alu B */  
  int aluB;
  if(de.iflags&F_IMM || de.iflags&F_DISP) {
    aluB = (int)(short)(em.inst.b & 0xffff);
  } else if(de.func == ALU_SLL) {
    aluB = em.inst.b & 0xff;
  } else {
    aluB = de.busB; 
  }
  /* alu part */
  switch(de.func) {
    case ALU_ADD:
      em.alu = aluA + aluB;
      break;
    case ALU_SUB:
      em.alu = aluA - aluB;
      break;
    case ALU_AND:
      em.alu = aluA & aluB;
      break;
    case ALU_SLT:
      em.alu = aluA < aluB;      
      break;
    case ALU_SLL:
      em.alu = aluA << aluB;
      break;
    default:
      em.alu = 0;
      break;
  }
}

void do_mem() {
  enum md_fault_type _fault;
  
  mw.inst = em.inst;
  mw.dstR = em.dstR;
  mw.dstM = em.dstM;
  mw.alu = em.alu;
  mw.PC = em.PC;
  mw.rwflag = em.rwflag;
  if (mw.rwflag & 2) {
    /* store */
    WRITE_WORD(em.sw, mw.alu, _fault);
  } else if (mw.rwflag & 4) {
    /* load */
    mw.memLoad = READ_WORD(mw.alu, _fault);
    ctl.dst &= ~(1 << mw.dstM);
  }
}                                                                         

void do_wb() {
  wb.inst = mw.inst;
  wb.PC = mw.PC;
  wb.dstR = mw.dstR;
  wb.dstM = mw.dstM;
  wb.alu = mw.alu;
  wb.memLoad = mw.memLoad;
  if(mw.dstM != DNA) {
    SET_GPR(mw.dstM, mw.memLoad);
  }
  if(mw.dstR != DNA) {
    SET_GPR(mw.dstR, mw.alu);
  }
  if(wb.inst.a == SYSCALL){
    SYSCALL(wb.inst);
  }
}

void do_log()
{
  enum md_fault_type _fault;
	printf("[Cycle %3d]---------------------------------\n", sim_num_insn);
	printf("[IF]  ");md_print_insn(fd.inst, fd.PC, stdout);printf("\n");
	printf("[ID]  ");md_print_insn(de.inst, de.PC, stdout);printf("\n");
	printf("[EX]  ");md_print_insn(em.inst, em.PC, stdout);printf("\n");
	printf("[MEM] ");md_print_insn(mw.inst, mw.PC, stdout);printf("\n");
	printf("[WB]  ");md_print_insn(wb.inst, wb.PC, stdout);printf("\n");
	printf("[REGS]r2=%d r3=%d r4=%d r5=%d r6=%d mem = %d\n", GPR(2),GPR(3),GPR(4),GPR(5),GPR(6),READ_WORD(GPR(30)+16, _fault));
	printf("--------------------------------------------\n");
}