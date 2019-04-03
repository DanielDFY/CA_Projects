## Instruction list

According to `loop.s` and `mechine.def` , all the instructions we need to implement are as follows:

| Instruction | OP Code | ALUSrcA | ALUSrcB |  ALUOp  | Dst  |  Branch   |
| :---------: | :-----: | :-----: | :-----: | :-----: | :--: | :-------: |
|     nop     |  0x00   |   DNA   |   DNA   | ALU_NOP | DNA  |     ×     |
|     add     |  0x40   |   in1   |   in2   | ALU_ADD | out1 |     ×     |
|    addu     |  0x42   |   in1   |   in2   | ALU_ADD | out1 |     ×     |
|    addiu    |  0x43   |   in1   |   imm   | ALU_ADD | out1 |     ×     |
|     lw      |  0x28   |   in2   |   imm   | ALU_ADD | out1 |     ×     |
|     sw      |  0x34   |   in2   |   imm   | ALU_ADD | DNA  |     ×     |
|    andi     |  0x4f   |   in1   |   imm   | ALU_AND | out1 |     ×     |
|     sll     |  0x55   |   in1   |  shamt  | ALU_SLL | out1 |     ×     |
|    slti     |  0x5c   |   in1   |   imm   | ALU_SLT | out1 |     ×     |
|     lui     |  0xa2   |   DNA   |   imm   | ALU_ADD | out1 |     ×     |
|      j      |  0x01   |   DNA   |   DNA   | ALU_NOP | DNA  |   jump    |
|     bne     |  0x06   |   in1   |   in2   | ALU_SUB | DNA  | not equal |
|   syscall   |  0xa0   |   DNA   |   DNA   | ALU_NOP | DNA  |     ×     |