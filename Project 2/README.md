# Cache Design

> A single-level four-way set-associative cache

### Data Structures

```c
struct cache_line {
  unsigned int data[4];             /* 16 bytes */
  unsigned int tag:27;              /* tag bits of the line */
  unsigned int dirty:1;             /* if the line is dirty */
  unsigned int valid:1;             /* if the line is valid */
  unsigned int ref_count:19;        /* times the line has been referred */
  struct cache_line* next;          /* pointer to the next line */
};
```

The cache line size is *16 bytes*. Valid bit and dirty bit are used to indicate whether the data in the cache line is currently valid and whether needs to be written back.



```c
struct cache_set {
  struct cache_line* head;          /* the head of the queue */
  struct cache_line* tail;          /* the tail of the queue */
  unsigned int n;                   /* the number of lines in the queue */
};
```

We are supposed to implement the *FIFO* replacement algorithm. Each time  a new line is added into the set, it will be placed at the head of the queue, and the member line pointed by the tail pointer will be removed if the set is full and needs a replacement.



```c
struct cache {
  struct cache_set sets[16];        /* 16 sets */
  unsigned int isEnabled;           /* if the cache is enabled */
  unsigned int accessCounter;       /* times of cache access */
  unsigned int hitCounter;          /* times of cache hit */
  unsigned int missCounter;         /* times of cache miss */
  unsigned int replaceCounter;      /* times of cache line replacement */
  unsigned int wbCounter;           /* times of write back */
};
```

The cache is supposed to have *16 sets*. Counters are used to record important statistic data.



### Cache Access Functions

```c
void enque_cache_set(struct cache_set* sp, struct cache_line* lp) {
  if (sp->tail != NULL) {
    sp->tail->next = lp;
  }
  sp->tail = lp;
  if (sp->head == NULL) {
    sp->head = lp;
  }
  ++sp->n;
}

void deque_cache_set(struct cache_set* sp) {
  if (sp->n == 0) {
    return;
  }
  struct cache_line* head = sp->head;
  sp->head = sp->head->next;
  --sp->n;
  if (sp->n == 0) {
    sp->tail == NULL;
  }
  free(head);
}
```

Basic functions for *enque* and *edeque* implementation.



```c
void cache_do_read(struct cache_line* lp, unsigned int offset, word_t* dst) {
  memcpy(dst, (void *)(&lp->data)+offset, sizeof(word_t));   
}

void cache_do_write(struct cache_line* lp, unsigned int offset, word_t* src) {
  memcpy((void *)(&lp->data)+offset, src, sizeof(word_t));
  lp->dirty = 1;
}
```

Access functions which use given tool function `memcpy` to read data into *dst* or write data from *src* into cache. 



```c
unsigned int cache_access(struct cache* cp, md_addr_t addr, word_t* wp, cache_func func) {
  unsigned int tag = ADDR_TAG(addr);
  unsigned int idx = ADDR_IDX(addr);
  unsigned int offset = ADDR_OFFSET(addr);
  
  struct cache_set* sp = &cp->sets[idx];
  struct cache_line* lp = NULL;
  md_addr_t align_addr = addr & (~0xF);

  unsigned int cycles = HIT_LATENCY;
  unsigned int miss = 1;
  ++cp->accessCounter;

  for (lp = sp->head; lp != NULL; lp = lp->next) {
    if (lp->valid && tag == lp->tag) {
      miss = 0;
      ++lp->ref_count;
      ++cp->hitCounter;
      func(lp, offset, wp);
      break;
    }
  }
  
  if (miss) {
    cycles = MISS_LATENCY;
    ++cp->missCounter;
    lp = malloc_cache_line(align_addr);
    add_cache_line(sp, idx, lp);
    func(lp, offset, wp);
  }
  return cycles;
}

unsigned int cache_read(struct cache* cp, md_addr_t addr, word_t* wp) {
  return cache_access(cp, addr, wp, cache_do_read);
}

unsigned int cache_write(struct cache* cp, md_addr_t addr, word_t* wp) {
  return cache_access(cp, addr, wp, cache_do_write);
}
```

Go through the cache. If hit, do read/write. If miss, set cycle penalty and fetch the data into the new cache line.



```c
struct cache_line* malloc_cache_line(md_addr_t addr) {
  struct cache_line* lp = malloc(sizeof(struct cache_line));
  enum md_fault_type _fault;
  int i;
  for (i = 0; i < SET_WAYS; ++i) {
    lp->data[i] = READ_WORD(addr + (i * 4), _fault);
  }
  lp->ref_count = 0;
  lp->tag = ADDR_TAG(addr);
  lp->valid = 1;
  lp->dirty = 0;
  lp->next = NULL;
  return lp;
}
```

Create and initialize the new cache line.



```c
void cache_write_back(struct cache_line* lp, unsigned int idx) {
  md_addr_t addr = (lp->tag << 8) | (idx << 4);
  enum md_fault_type _fault;
  int i;
  for (i = 0; i < SET_WAYS; ++i) {
    WRITE_WORD(lp->data[i], addr + (i * 4), _fault);
  }
  lp->dirty = 0;
}

void add_cache_line(struct cache_set* sp, unsigned int idx, struct cache_line* lp) {
  if (sp->n >= SET_WAYS) {
    if (sp->head->dirty) {
      ++cache.wbCounter;
      cache_write_back(sp->head, idx);
    }
    ++cache.replaceCounter;
    deque_cache_set(sp);
  }
  enque_cache_set(sp, lp);
}
```

If a cache set is already full and the line to be replaced (*FIFO*) is dirty, the data in this line should be written into memory.



```c
unsigned int cache_flush(struct cache* cp) {
  struct cache_set* sp;
  struct cache_line* lp;
  int i;
  for (i = 0; i < SET_NUM; ++i) {
    sp = &cp->sets[i];
    for (lp = sp->head; lp != NULL; lp = lp->next) {
      if (lp->dirty)
      cache_write_back(lp, i);
    }
  }
}
```

At the end of the program, all the data in the cache should be written into the memory (*cache flush*).



### Statistics

* Hit latency: 1 cycle
* Miss latency: 10 cycles
* The cache can be set enabled `cache.isEnable = 1` and disabled `cache.isEnable = 0` at the cache initializaion. When it's turned off, all memory access latency is 10 cycles.



