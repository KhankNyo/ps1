SYS_WRITE   = 0x7000_0000
SYS_CLRSCR  = 0x7100_0000
SYS_EXIT    = 0x7200_0000

RESET_VEC   = 0xBFC0_0000
START       = RESET_VEC + 0x0180
MEMORY_SIZE = 4096


.org RESET_VEC
    j START
ResetEnd:
.resv START - ResetEnd


.org START
    jal TestLoad
    bnz $v0, Fail           ; if (0 != arith test result) -> failed

    jal TestStore
    bnz $v0, Fail

    jal TestBranch
    bnz $v0, Fail

    jal TestArith
    bnz $v0, Fail           

    la $a0, TestFinished_Msg ; else success, print msg 
    jal PrintStr
Fail:
    li $t0, SYS_EXIT
    sw $zero, 0($t0)        ; call sys_exit 
InfLoop: 
    bra InfLoop


; prints string in $a0 by writing its content to SYS_WRITE
; args:     const char *$a0
; destroys: $t0
PrintStr: 
    li $t0, SYS_WRITE
    sw $a0, 0($t0)          ; call sys_write(Str)
    ret 

; prints string in $a0 and then string in $a1
; args:     const char *$a0, $a1
; returns:  1 in $v0
; destroys: $t0, $t1, $v0
TestFailed:
    move $t1, $ra                   ; save return addr 
        jal PrintStr
        move $a0, $a1
        jal PrintStr
    move $ra, $t1                   ; restore return addr 
    ori $v0, $zero, 1               ; returns test failed
    ret


TestLoad:
    move $v0, $zero
    ret 


TestStore:
    move $v0, $zero
    ret 


.jumpNop 0
.branchNop 0
.loadNop 0
TestBranch:
    la $a0, TestBranch_Failed_Msg

    ; test jump delay slot
    la $a1, TestBranch_J_Msg
    move $t0, $zero
    j TestBranch_Jump_Target
        addiu $t0, 1
    TestBranch_Jump_Target:
    bez $t0, TestFailed
    nop

    ; test beq:
    la $a1, TestBranch_Beq_Msg
    move $t0, $zero
    addiu $t0, 1                            ; t0 = 1
    beq $t0, $zero, TestFailed              ; assert t0 != 0
        addiu $t0, -1                       ; t0 = 0 at jump target
    la $a1, TestBranch_BeqDelaySlot_Msg
    beq $t0, $zero, TestBranch_BeqDone      ; assert t0 == 0
        nop
        bra TestFailed
TestBranch_BeqDone:
    bra TestFailed
        nop

    ; test bne:
    la $a1, TestBranch_Bne_Msg
    li $t1, 1                               ; t1 = 1
    li $t0, -1                              ; t0 = -1
    addiu $t0, 1                            ; t0 = 0 right before branch instruction 
    bne $t0, $zero, TestFailed              ; assert t0 == 0
        addiu $t0, 1
    la $a1, TestBranch_BneDelaySlot_Msg
    bne $t0, $t1, TestFailed                ; assert t0 == t1
    nop

    ret 
    move $v0, $zero                 ; returns 0, success
.loadNop 1
.jumpNop 1
.branchNop 1



; tests arithmetic instructions involving immediate values: 
;   addiu, sltiu, slti, andi, ori, xori, lui
; destroys: $a0, $a1, $t0, $t1, $t2, $v0
.jumpNop 0 
TestArith:
    la $a0, TestArith_Failed_Msg

    ; test r0:
    la $a1, TestArith_R0_Msg
    move $1, $zero
    move $2, $zero
    addiu $0, $0, 10                ; r0 = 0 (should not be 10)
    addu $1, $0, $0                 ; r1 = 0 (should not be 20)
    bne $1, $0, TestFailed          ; assert r1 == r0

    ; test addiu: sign extension
    la $a1, TestArith_AddiuSex_Msg
    addiu $t0, $zero, -1            ; t0 =  0xFFFF_FFFF
    addiu $t0, 1                    ; t0 += 0x0000_0001
    bne $t0, $zero, TestFailed      ; assert t0 == 0

    ; test ori: zero extension
    la $a1, TestArith_OriZex_Msg
    ori $t0, $zero, 0xFFFF          ; t0 = 0x0000_FFFF
    addiu $t1, $zero, -1            ; t1 = 0xFFFF_FFFF
    beq $t0, $t1, TestFailed        ; assert t0 != t1 

    ; test lui, andi: 
    la $a1, TestArith_LuiAndi_Msg
    ori $t0, $zero, 0xFFFF          ; t0 =  0x0000_FFFF
    lui $t0, 0xFFFF                 ; t0 =  0xFFFF_0000
    andi $t0, $t0, 0xFFFF           ; t0 &= 0x0000_FFFF
    bne $t0, $zero, TestFailed      ; assert t0 == 0

    ; test xori:
    la $a1, TestArith_Xori_Msg       
    addiu $t0, $zero, -1            ; t0 =  0xFFFF_FFFF
    xori $t0, 0xF00F                ; t0 ^= 0x0000_F00F
    la $t1, 0xFFFF_0FF0             ; expected 
    bne $t1, $t0, TestFailed        ; assert t0 == 0xFFFF_0FF0

    ; test slti: true case 
    la $a1, TestArith_Slti_Msg
    slti $t0, $zero, 1              ; t1 = 0 < 1
    bez $t0, TestFailed             ; assert condition true

    ; test slti: false case, negative
    la $a1, TestArith_SltiNeg_Msg
    slti $t0, $zero, -1             ; t0 = 0 < -1
    bnz $t0, TestFailed             ; assert condition false

    ; test sltiu: true case, unsigned
    la $a1, TestArith_SltiuUnsigned_Msg
    sltiu $t0, $zero, -1            ; t0 = 0 < 0xFFFF_FFFF
    bez $t0, TestFailed             ; assert condition true

    ; test sltiu: false case, sign extension
    la $a1, TestArith_SltiuSex_Msg
    li $t1, -1
    sltiu $t0, $t1, (-1 << 15)      ; t0 = 0xFFFF_FFFF < 0xFFFF_8000
    bnz $t0, TestFailed             ; assert condition false

    ret
    move $v0, $zero                 ; returns test success 
.jumpNop 1


DataSection:
TestFinished_Msg:               .db "All tests passed.\n", 0

TestArith_Failed_Msg:           .db "Arithmetic test failed: ", 0
TestArith_R0_Msg:               .db "R0 should always be zero.\n", 0
TestArith_AddiuSex_Msg:         .db "addiu should sign extend its immediate value.\n", 0
TestArith_OriZex_Msg:           .db "ori should zero extend its immediate value.\n", 0
TestArith_LuiAndi_Msg:          .db "lui, andi.\n", 0
TestArith_Xori_Msg:             .db "xori should zero extend its immediate value.\n", 0
TestArith_Slti_Msg:             .db "slti.\n", 0
TestArith_SltiNeg_Msg:          .db "slti failed on negative values.\n", 0
TestArith_SltiuUnsigned_Msg:    .db "Operands of sltiu should be treated as unsigned.\n", 0
TestArith_SltiuSex_Msg:         .db "Immediate of sltiu must be sign extended.\n", 0

TestBranch_Failed_Msg:          .db "Branch test failed: ", 0
TestBranch_J_Msg:               .db "'j' instruction.\n", 0
TestBranch_Beq_Msg:             .db "beq.\n", 0
TestBranch_BeqDelaySlot_Msg:    .db "beq delay slot.\n", 0
TestBranch_Bne_Msg:             .db "bne.\n", 0
TestBranch_BneDelaySlot_Msg:    .db "bne delay slot.\n", 0
    


FreeMemorySection:
.resv MEMORY_SIZE
FreeMemoryEnd:

