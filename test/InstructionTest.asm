SYS_WRITE = 0x7000_0000
SYS_CLRSCR = 0x7100_0000
RESET_VEC = 0xBFC0_0000

.org RESET_VEC
    ; print Str
    la $1, Str          ; load &Str
    li $2, SYS_WRITE    ; load sys_write 
    move $3, $1
    sw $1, 0($2)        ; call sys_write 

    ; clear screen
    li $2, SYS_CLRSCR
    sw $2, 0($2)        ; call sys_clrscr

    ; print Str2
    la $1, Str2         ; load &Str2
    li $2, SYS_WRITE    ; load sys_write
    sw $1, 0($2)        ; call sys_write

loop: 
    beq $0, $0, loop

Data:
Str: 
    .db "Hello, world\n", 0
Str2:
    .db "This is a newline", 0

