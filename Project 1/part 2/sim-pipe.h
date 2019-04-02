#include "machine.h"

/* define values related to operands, all possible combinations are included */
typedef struct{
  int in1;			        /* input 1 register number */
  int in2;			        /* input 2 register number */
  int in3;			        /* input 3 register number */
  int out1;			        /* output 1 register number */
  int out2;			        /* output 2 register number */
}oprand_t;

/* define ALU func value */
typedef enum {
  ALU_NOP = 0,
  ALU_ADD,
  ALU_SUB,
  ALU_AND,
  ALU_SLT,
  ALU_SLL
} alu_func_t;

/*define buffer between fetch and decode stage*/
struct ifid_buf {
  md_inst_t inst;	      /* instruction that has been fetched */
  md_addr_t PC;	        /* pc value of current instruction */
  md_addr_t NPC;		    /* the next instruction to fetch */
};


/*define buffer between decode and execute stage*/
struct idex_buf {
  md_inst_t inst;		    /* instruction in ID stage */ 
  md_addr_t PC;         /* pc value of current instruction */
  int opcode;           /* operation number */
  oprand_t oprand;      /* operand */
  int iflags;           /* instruction flags */
  int srcA;             /* data 1 register, used for forwarding */
  int srcB;             /* data 2 register, used for forwarding */
  int func;             /* alu func code */
  int busA;             /* read data 1 */
  int busB;             /* read data 2 */
  int sw;               /* store mem address */
  int dstR;             /* write-in register */
  int dstM;             /* mem-write-in register */
  int rwflag;           /* read/write flag */
  int target;           /* jump target */
};

/*define buffer between execute and memory stage*/
struct exmem_buf{
  md_inst_t inst;		    /* instruction in EX stage */
  md_addr_t PC;         /* pc value of current instruction */
  int alu;              /* alu result */
  int sw;               /* store word address */
  int dstR;             /* write-in register */
  int dstM;             /* mem-write-in register */
  int rwflag;           /* read/write flag */
  int target;           /* jump target */
};

/*define buffer between memory and writeback stage*/
struct memwb_buf{
  md_inst_t inst;		    /* instruction in MEM stage */
  md_addr_t PC;         /* pc value of current instruction */
  int alu;              /* alu result */
  int memLoad;          /* value read from memory */
  int sw;               /* store word address */
  int dstR;             /* write-in register */
  int dstM;             /* mem-write-in register */
  int rwflag;           /* read/write flag */
};

/*used for trace printing*/
struct wb_buf{
  md_inst_t inst;       /* instruction in WB stage */
  md_addr_t PC;         /* pc value of current instruction */
  int alu;              /* alu result */
  int memLoad;          /* value read from memory */
  int dstR;             /* write-in register */
  int dstM;             /* mem-write-in register */
};  

/*define buffer for pipline control*/
struct control_buf {
  int ch;               /* check control hazard */
  int cond;             /* check branch */
  int dst;              /* store write-in dst of the last cycle */
  int dh;               /* check data hazard */
};

/*do fetch stage*/
void do_if();

/*do decode stage*/
void do_id();

/*do execute stage*/
void do_ex();

/*do memory stage*/
void do_mem();

/*do write_back to register*/
void do_wb();


#define MD_FETCH_INSTI(INST, MEM, PC)					\
  { INST.a = MEM_READ_WORD(mem, (PC));					\
    INST.b = MEM_READ_WORD(mem, (PC) + sizeof(word_t)); }

#define SET_OPCODE(OP, INST) ((OP) = ((INST).a & 0xff)) 

#define RSI(INST)		(INST.b >> 24& 0xff)		/* reg source #1 */
#define RTI(INST)		((INST.b >> 16) & 0xff)		/* reg source #2 */
#define RDI(INST)		((INST.b >> 8) & 0xff)		/* reg dest */

#define IMMI(INST)	((int)((/* signed */short)(INST.b & 0xffff)))	/*get immediate value*/
#define TARGI(INST)	(INST.b & 0x3ffffff)		/*jump target*/
