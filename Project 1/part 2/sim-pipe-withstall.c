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
  de.busA = DNA;
  de.busB = DNA;
  de.wr = 0;
  de.dst = DNA;
  de.rwflag = 0;
  de.target = 0;
  /* EX/MEM */
  em.inst.a = NOP;
  em.PC = 0;
  em.alu = 0;
  em.wr = 0;
  em.dst = DNA;
  em.rwflag = 0;
  em.target = 0;
  /* MEM/WB */
  mw.inst.a = NOP;
  mw.PC = 0;
  mw.memLoad = 0;
  mw.dst = DNA;
  mw.fromMem = 0;
  /* WB */
  wb.inst.a = NOP;
  wb.PC = 0;
  /* CTL */
  ctl.flag = FALSE;
  ctl.cond = 0;
  ctl.dst = 0;
  ctl.stall = FALSE;
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
    do_if();
    do_id();
    do_ex();
    do_mem();
    do_wb();
    /* print current trace */
    do_log();
  }
}

void do_pipeline_ctl()
{
  /* check prediction, insert a NOP to wait for the result */
	if (ctl.flag) {
    fd.PC = de.PC;
    fd.inst.a = NOP;
  }
  /* repeatedly decode the same instruction while keep increasing NPC, until hazard solved */
  if (ctl.stall) {
    fd.PC = de.PC;
    fd.inst = de.inst;
    de.inst.a = NOP;
  }
}

void do_if()
{
  if (ctl.cond) {
    /* if branch */ 
    if (ctl.cond & 2){
      /* address from memory value */
      fd.NPC = em.target;
    } else {
      /* address from register */
      fd.NPC = de.target;
    }
    /* reset */
    ctl.flag = FALSE;
    ctl.cond = 0;
  } else {
    fd.NPC = fd.PC + sizeof(md_inst_t);
  }
  /* instruction fetch */
  MD_FETCH_INSTI(fd.inst, mem, fd.PC);
  fd.PC = fd.NPC;
}

void do_id()
{
  /* if inst is nop, simply return */
  if(NOP == fd.inst.a) return;
  de.PC = fd.PC;
  de.inst = fd.inst;
  MD_SET_OPCODE(de.opcode, de.inst);
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
  if((de.oprand.in1 >= 0 && (ctl.dst & de.oprand.in1)) || (de.oprand.in2 >= 0 && (ctl.dst & de.oprand.in2))) {
    ctl.stall = TRUE;
    return;
  } else {
    ctl.stall = FALSE;
  }
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
        ctl.flag = TRUE;
        ctl.cond = 1;
        de.target = (fd.PC & 0xf0000000) | ((de.inst.b & 0x3ffffff) << 2);
        break;
      case BNE:
        ctl.flag = TRUE;
        de.func = ALU_SUB;
        de.target = fd.PC + 8 + ((de.inst.b & 0xffff) << 2);
        break;
      default:
        de.func = ALU_NOP;
        break;
  }
  /* read data 1 */
  if (de.iflags & F_DISP) {
    de.busA = de.oprand.in2 != DNA ? GPR(de.oprand.in2) : 0;
  } else {
    de.busA = de.oprand.in1 != DNA ? GPR(de.oprand.in1) : 0;
  }
  /* read data 2 */
  if (de.iflags & F_IMM || de.iflags & F_DISP) {
    de.busB = (int)(short)(de.inst.b & 0xffff);
  } else if (de.func == ALU_SLL) {
    de.busB = de.inst.b & 0xff;
  } else {
    de.busB = de.oprand.in2 != DNA?GPR(de.oprand.in2):0; 
  }
  /* record write-in register, set write flag */
  if (de.iflags & F_STORE) {
    de.wr = GPR(de.oprand.in1);
    de.rwflag |= 1;
  } else {
    de.rwflag &= ~1;
  }
  /* set read flag */
  if (de.iflags & F_LOAD) {
    de.rwflag |= 2;
  } else {
    de.rwflag &= ~2;
  }
  /* record dst */
  if (de.oprand.out1 >= 0) {
    ctl.dst ^= de.oprand.out1;
  }
}

void do_ex()
{
  em.inst = de.inst;
  em.PC = de.PC;
  em.dst = de.dst;
  em.rwflag = de.rwflag;
  em.target = de.target;
  switch (de.func) {
    case ALU_ADD:
      em.alu = de.busA + de.busB;
      break;
    case ALU_SUB:
      em.alu = de.busA - de.busB;
      break;
    case ALU_AND:
      em.alu = de.busA & de.busB;
      break;
    case ALU_SLL:
      em.alu = de.busA << de.busB;
      break;
    case ALU_SLT:
      em.alu = de.busA < de.busB;
      break;
    default:
      em.alu = 0;
      break;
  }
  if (ctl.flag) {
    if (em.alu) {
      ctl.cond = 2;
    } else {
      ctl.cond = 0;
    }
  }
}

void do_mem()
{
  enum md_fault_type _fault;
  mw.inst = em.inst;
  mw.PC = em.PC;
  if (em.rwflag & 1) {
    WRITE_WORD(em.wr, em.alu, _fault);
  } else if (em.rwflag & 2) {
    mw.memLoad = READ_WORD(em.alu, _fault);
    mw.fromMem = TRUE;
  }
  if(wb.inst.a == SYSCALL){
    SYSCALL(wb.inst);
  }
}                                                                                        

void do_wb()
{
	wb.inst = mw.inst;
  wb.PC = mw.PC;
  ctl.dst ^= mw.wr;
  if (mw.fromMem) {
    SET_GPR(mw.wr, mw.memLoad);
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
	printf("----------------------------------------------\n");
}