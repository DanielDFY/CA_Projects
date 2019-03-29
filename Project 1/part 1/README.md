## Part1-design

> A detailed document that describes the debug process and solution.

At this part, the two instuctions to be implemented are `addOK` and `bitCount` .

First run the offered test case 1 by sim-fast :

```
$ simplesim-3.0/sim-fast test1
...
sim: ** starting *fast* functional simulation **
panic: attempted to execute a bogus opcode [sim_main:sim-fast.c, line 444]
...
```

Unrecognized instruction appeared. In order to find it out, use `sslittle-na-sstrix-objdump` to get its assembly code :

```
bin/sslittle-na-sstrix-objdump -x -d test1 > test1.asm
```

In the file `test.asm` , we first search `main` :

```assembly
00400340 <main>:
  400340:	43 00 00 00 	addiu $29,$29,-24
  400344:	e8 ff 1d 1d 
  400348:	34 00 00 00 	sw $31,16($29)
  40034c:	10 00 1f 1d 
  400350:	02 00 00 00 	jal 400548 <__main>
  400354:	52 01 10 00 
  400358:	43 00 00 00 	addiu $4,$0,1
  40035c:	01 00 04 00 
  400360:	43 00 00 00 	addiu $5,$0,-1
  400364:	ff ff 05 00 
  400368:	02 00 00 00 	jal 400240 <addOK>
  40036c:	90 00 10 00 
  400370:	a2 00 00 00 	lui $4,32768
  400374:	00 80 04 00 
  400378:	a2 00 00 00 	lui $5,32768
  40037c:	00 80 05 00 
  400380:	02 00 00 00 	jal 400240 <addOK>
  400384:	90 00 10 00 
  400388:	28 00 00 00 	lw $31,16($29)
  40038c:	10 00 1f 1d 
  400390:	43 00 00 00 	addiu $29,$29,24
  400394:	18 00 1d 1d 
  400398:	03 00 00 00 	jr $31
  40039c:	00 00 00 1f 
```

Now we know the unrecognized insrtuction is `addOK` . ` 0x00000061:10111300` , which can't be parsed by `objdump` , suggests the opcode of `addOK` is `0x61` .

```assembly
00400240 <addOK>:
  ...
  400268:	34 00 00 00 	sw $19,28($29)
  40026c:	1c 00 13 1d 
  400270:	61 00 00 00 	0x00000061:10111300
  400274:	00 13 11 10 
  ...
```

The same steps for `test2` :

```assembly
00400308 <bitCount>:
	...
	400350:	28 00 00 00 	lw $2,32($30)
  400354:	20 00 02 1e 
  400358:	62 00 00 00 	0x00000062:02030001
  40035c:	01 00 03 02 
  ...
  400370:	28 00 00 00 	lw $2,32($30)
  400374:	20 00 02 1e 
  400378:	62 00 00 00 	0x00000062:02030000
  40037c:	00 00 03 02 
  ...
```

`0x00000062:02030001` suggests the opcode of `bitCount` is `0x62` .

Because the format of `addOK` is the same as `add` , the format of `bitCount` is the same as `xori` , we can implement our new instructions based on them.



For `addOK` , we are offered macro `OVER(X,Y)` to check whether the sum of two operands is overflowed.

```c
#define ADDOK_IMPL																		\
	{																										\
  	SET_GPR(RD, !OVER(GPR(RS), GPR(RT));							\
	}
DEFINST(ADDOK, 0x61,
        "addOK", "d,s,t",
        IntALU, F_ICOMP,
        DGPR(RD), DNA, DGPR(RS), DGPR(RT), DNA)
```

For `bitCount` , we can use bitwise operation :

```c
#define BITCOUNT_IMPL										\
	{											\
    int mask1_tmp = (0x55 << 8) | 0x55;								\
		int mask1 = (mask1_tmp << 16) | mask1_tmp;					\
		int mask2_tmp = (0x33 << 8) | 0x33;						\
		int mask2 = (mask2_tmp << 16) | mask2_tmp;					\
		int mask3_tmp = (0x0f << 8) | 0x0f;						\
		int mask3 = (mask3_tmp << 16) | mask3_tmp;					\
		int mask4 = (0xff << 16) | 0xff;						\
		int mask5 = (0xff << 8) | 0xff;							\
		int c = GPR(RS);								\
		c=(c & mask1) + ((c >> 1) & mask1);						\
		c=(c & mask2) + ((c >> 2) & mask2);						\
		c=(c & mask3) + ((c >> 4) & mask3);						\
		c=(c & mask4) + ((c >> 8) & mask4);						\
		c=(c & mask5) + ((c >> 16) & mask5);						\
		if (UIMM) SET_GPR(RT, c);							\
    		else SET_GPR(RT, 32-c);								\
	}
DEFINST(BITCOUNT, 0x62,
       "bitCount", "t,s,u",
       IntALU, F_ICOMP|F_IMM,
       DGPR(RT), DNA, DGPR(RS), DNA, DNA)
```

Now rebuild the source code :

```
$ make clean
$ make config-pisa
$ make sim-fast
```

Then run the two tests again :

```
...
sim: ** starting *fast* functional simulation **
addOK(0x1, 0xffffffff)=1	Pass!
addOK(0x80000000, 0x80000000)=0	Pass!
...

...
sim: ** starting *fast* functional simulation **
bitCount(0x5, 1)=2	Pass!
bitCount(0x7, 1)=3	Pass!
bitCount(0x7, 0)=29	Pass!
...
```

