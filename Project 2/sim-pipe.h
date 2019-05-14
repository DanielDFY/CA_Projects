#include "machine.h"

/* define values related to operands, all possible combinations are included */
typedef struct {
  int in1;			        /* input 1 register number */
  int in2;			        /* input 2 register number */
  int in3;			        /* input 3 register number */
  int out1;			        /* output 1 register number */
  int out2;			        /* output 2 register number */
} oprand_t;

/* define ALU func value */
typedef enum {
  ALU_NOP = 0,
  ALU_ADD,
  ALU_SUB,
  ALU_AND,
  ALU_SLT,
  ALU_SLL,
  ALU_MULT
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
  int sw;               /* store word value */
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
  int sw;               /* store word value */
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
  int sw;               /* store word value */
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

/* cache part */

#define SET_WAYS 4     /* 4-way set-associative cache */
#define SET_NUM 16     /* the cache has 16 sets */
#define HIT_LATENCY 1     /* cache hit latency is 1 cycle */
#define MISS_LATENCY 10     /* cache miss latency is 10 cycle */
#define ADDR_TAG(ADDR) (((unsigned int) ADDR) >> 8)     /* get tag bits */
#define ADDR_IDX(ADDR) ((((unsigned int) ADDR) & 0xF0) >> 4)      /* get index bits */
#define ADDR_OFFSET(ADDR) ((((unsigned int) ADDR) & 0xF))       /* get offset bits */

struct cache_line {
  unsigned int data[4];             /* 16 bytes */
  unsigned int tag:27;              /* tag bits of the line */
  unsigned int dirty:1;             /* if the line is dirty */
  unsigned int valid:1;             /* if the line is valid */
  unsigned int ref_count:19;        /* times the line has been referred */
  struct cache_line* next;          /* pointer to the next line */
};

struct cache_set {
  struct cache_line* head;          /* the head of the queue */
  struct cache_line* tail;          /* the tail of the queue */
  unsigned int n;                   /* the number of lines in the queue */
};

struct cache {
  struct cache_set sets[16];        /* 16 sets */
  unsigned int isEnabled;           /* if the cache is enabled */
  unsigned int accessCounter;       /* times of cache access */
  unsigned int hitCounter;          /* times of cache hit */
  unsigned int missCounter;         /* times of cache miss */
  unsigned int replaceCounter;      /* times of cache line replacement */
  unsigned int wbCounter;           /* times of write back */
};

/* enque a line into the queue of a cache set */
void enque_cache_set(struct cache_set*, struct cache_line*);

/* deque a line out of the queue of a cache set */
void deque_cache_set(struct cache_set*);

/* function pointer to cache read/write function */
typedef void (*cache_func)(struct cache_line*, unsigned int, word_t*);

/* cache read function */
void cache_do_read(struct cache_line*, unsigned int, word_t*);

/* cache write function */
void cache_do_write(struct cache_line*, unsigned int, word_t*);

/* access the cache (read/write based on the function pointer) */
unsigned int cache_access(struct cache*, md_addr_t, word_t*, cache_func);

/* read data from given address into destination */
unsigned int cache_read(struct cache*, md_addr_t, word_t*);

/* write data into given address */ 
unsigned int cache_write(struct cache*, md_addr_t, word_t*);

/* allocate space to new cache line, return pointer of the line */
struct cache_line* malloc_cache_line(md_addr_t);

/* write back if all lines in the set are dirty */
void cache_write_back(struct cache_line*, unsigned int);

/* add a line into given cache set */
void add_cache_line(struct cache_set*, unsigned int, struct cache_line*);

/* write all dirty line back */
unsigned int cache_flush(struct cache*);