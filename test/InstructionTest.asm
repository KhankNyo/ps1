.org 0xBFC0_0000
SYS_WRITE = 0x0F00_0000
    ; load &Str into R1
    lui $1, Str >> 16
    ori $1, Str & 0xFFFF
    lui $2, SYS_WRITE >> 16 ; load sys write 
    sw $1, 0($2)            ; call sys_write(&Str)

loop: 
    beq $0, $0, loop
    sll $0, $0, 0

Data:
Str: 
    .db "Hello, world\n", 0

