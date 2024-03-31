SYS_WRITE   = 0x7000_0000
SYS_CLRSCR  = 0x7100_0000
SYS_EXIT    = 0x7200_0000

RESET_VEC   = 0xBFC0_0000
START       = RESET_VEC + 0x0180
MEMORY_SIZE = 4096


.org RESET_VEC
; prerequisite: 
;   bne, beq 
;   j,   jal, 
;   lui, ori
.jumpNop 0
    j Reset1                    ; NOTE: jump to delay slot
Reset1:
    nop
    beq $zero, $zero, Reset2    ; NOTE: branch to delay slot
Reset2:
    nop
    jal Reset3                  ; NOTE: jump to delay slot, saving the addr of isntruction after 
Reset3:
    nop
Reset_LinkAddr:
    lui $t0, Reset_LinkAddr >> 16
    ori $t0, Reset_LinkAddr & 0xFFFF
    bne $t0, $ra, ResetTrap     ; assert $ra == Reset_LinkAddr
    nop
    j START                     ; START: testing other instructions
    nop
ResetTrap: 
    la $t0, TestPrerequisite_Failed_Msg
    li $t1, SYS_WRITE
    sw $t0, 0($t1)
    beq $zero, $zero, ResetTrap
.jumpNop 1
ResetEnd:
.resv START - ResetEnd




.org START
    jal TestBranch
    bnz $v0, Fail

    jal TestLoad
    bnz $v0, Fail           ; if (0 != arith test result) -> failed

    jal TestStore
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




; tests:            bltz, bgez, blez, bgtz, bltzal, bgezal, 
; assumes working:  beq, bne
; returns:          $v0: 0 for success, 1 for failure
; destroys:         $t0..9, $a0..1, $v0
TestBranch:
    move $v0, $zero                 ; assumes test is going to be successful
    la $a0, TestBranch_Failed_Msg

    ; t8 = 0 
    ; t9 = 1
    ; t2 = -1
    ; t3 = 0x8000_0000
    ; t4 = 0x7FFF_FFFF
    move $t8, $zero
    li $t9, 1
    addiu $t2, $zero, -1
    lui $t3, 0x8000
    addiu $t4, $t3, -1

    ; test bltz
    la $a1, TestBranch_Bltz_Msg
    bltz $t3, TestBranch_Bltz_Ok0
        bra TestFailed
TestBranch_Bltz_Ok0:
    bltz $t2, TestBranch_Bltz_Ok1
        bra TestFailed
TestBranch_Bltz_Ok1:
    bltz $t4, TestFailed
    bltz $t9, TestFailed
    bltz $t8, TestFailed

    ; test bgez
    la $a1, TestBranch_Bgez_Msg
    bgez $t8, TestBranch_Bgez_Ok0
        bra TestFailed
TestBranch_Bgez_Ok0:
    bgez $t9, TestBranch_Bgez_Ok1
        bra TestFailed
TestBranch_Bgez_Ok1:
    bgez $t4, TestBranch_Bgez_Ok2
        bra TestFailed
TestBranch_Bgez_Ok2:
    bgez $t2, TestFailed
    bgez $t3, TestFailed

    ; test blez
    la $a1, TestBranch_Blez_Msg
    blez $t8, TestBranch_Blez_Ok0
        bra TestFailed
TestBranch_Blez_Ok0:
    blez $t2, TestBranch_Blez_Ok1
        bra TestFailed
TestBranch_Blez_Ok1:
    blez $t3, TestBranch_Blez_Ok3
        bra TestFailed
TestBranch_Blez_Ok3:
    blez $t9, TestFailed
    blez $t4, TestFailed

    ; test bgtz
    la $a1, TestBranch_Bgtz_Msg
    bgtz $t9, TestBranch_Bgtz_Ok0
        bra TestFailed
TestBranch_Bgtz_Ok0:
    bgtz $t4, TestBranch_Bgtz_Ok1
        bra TestFailed
TestBranch_Bgtz_Ok1:
    bgtz $t8, TestFailed
    bgtz $t2, TestFailed
    bgtz $t3, TestFailed

.branchNop 0
.jumpNop 0
    ; test bltzal
    move $t5, $ra                       ; save return reg 
    la $a3, TestBranch_Bltzal_Msg
    la $a2, TestBranch_BltzalRetAddr_Msg

    bltzal $t2, TestBranch_Bltzal_Ok1
        move $t6, $ra                   ; save ret addr
    TestBranch_Bltzal_Addr0:
        jal TestFailed
        move $a1, $a3                   ; set instruction error msg
        move $ra, $t6                   ; restore ret addr
TestBranch_Bltzal_Ok1:
        la $t6, TestBranch_Bltzal_Addr0
        bne $ra, $t6, TestFailed        ; assert ra == TestBranch_Bltzal_Addr0
        move $a1, $a2                       ; set ret addr error msg

    bltzal $t3, TestBranch_Bltzal_Ok2
        move $t6, $ra
    TestBranch_Bltzal_Addr1:
        jal TestFailed
        move $a1, $a3                    ; instruction error msg
        move $ra, $t6
TestBranch_Bltzal_Ok2:
        la $t6, TestBranch_Bltzal_Addr1
        bne $ra, $t6, TestFailed        ; assert ra == TestBranch_Bltzal_Addr1
        move $a1, $a2                   ; ret addr error msg

    bltzal $t8, TestFailed
        move $a1, $a3                   ; instruction error msg
    TestBranch_Bltzal_Addr2:
        la $t6, TestBranch_Bltzal_Addr2
        bne $ra, $t6, TestFailed        ; assert ra == TestBranch_Bltzal_Addr2
        move $a1, $a2                   ; ret addr error msg

    bltzal $t9, TestFailed
        move $a1, $a3                   ; instruction error msg
    TestBranch_Bltzal_Addr3:
        la $t6, TestBranch_Bltzal_Addr3
        bne $ra, $t6, TestFailed        ; assert ra == TestBranch_Bltzal_Addr3
        move $a1, $a2                   ; ret addr error msg

    bltzal $t4, TestFailed
        move $a1, $a3                   ; instruction error msg
    TestBranch_Bltzal_Addr4:
        la $t6, TestBranch_Bltzal_Addr4
        bne $ra, $t6, TestFailed        ; assert ra == TestBranch_Bltzal_Addr4
        move $a1, $a2                   ; ret addr error msg

    ; test bgezal
    la $a3, TestBranch_Bgezal_Msg
    la $a2, TestBranch_BgezalRetAddr_Msg
    bgezal $t8, TestBranch_Bgezal_Ok0
        move $t6, $ra
    TestBranch_Bgezal_Addr0:
        jal TestFailed
        move $a1, $a3
        move $ra, $t6
TestBranch_Bgezal_Ok0:
        la $t6, TestBranch_Bgezal_Addr0
        bne $ra, $t6, TestFailed        ; assert ra == TestBranch_Bgezal_Addr0
        move $a1, $a2

    bgezal $t9, TestBranch_Bgezal_Ok1
        move $t6, $ra
    TestBranch_Bgezal_Addr1:
        jal TestFailed
        move $a1, $a3
        move $ra, $t6
TestBranch_Bgezal_Ok1:
        la $t6, TestBranch_Bgezal_Addr1
        bne $ra, $t6, TestFailed        ; assert ra == TestBranch_Bgezal_Addr1
        move $a1, $a2
    
    bgezal $t4, TestBranch_Bgezal_Ok2
        move $t6, $ra
    TestBranch_Bgezal_Addr2:
        jal TestFailed
        move $a1, $a3
        move $ra, $t6
TestBranch_Bgezal_Ok2:
        la $t6, TestBranch_Bgezal_Addr2
        bne $ra, $t6, TestFailed        ; assert ra == TestBranch_Bgezal_Addr2
        move $a1, $a2

    bgezal $t2, TestFailed
    move $a1, $a3
    TestBranch_Bgezal_Addr3:
        la $t6, TestBranch_Bgezal_Addr3
        bne $ra, $t6, TestFailed        ; assert ra == TestBranch_Bgezal_Addr3
        move $a1, $a2

    bgezal $t3, TestFailed
    move $a1, $a3
    TestBranch_Bgezal_Addr4:
        la $t6, TestBranch_Bgezal_Addr4
        bne $ra, $t6, TestFailed        ; assert ra == TestBranch_Bgezal_Addr4
        move $a1, $a2
.branchNop 1
.jumpNop 1

    move $ra, $t5                   ; restore return reg
    ret 




TestLoad:
    move $v0, $zero
    ret 




TestStore:
    move $v0, $zero
    ret 




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
TestPrerequisite_Failed_Msg:    .db "Test preqrequisite failed: jal.\n", 0
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
TestBranch_Bltz_Msg:            .db "bltz.\n", 0
TestBranch_Bgez_Msg:            .db "bgez.\n", 0
TestBranch_Blez_Msg:            .db "blez.\n", 0
TestBranch_Bgtz_Msg:            .db "bgtz.\n", 0
TestBranch_Bltzal_Msg:          .db "bltzal.\n", 0
TestBranch_Bgezal_Msg:          .db "bgezal.\n", 0
TestBranch_BltzalRetAddr_Msg:   .db "bltzal has incorrect return addr.\n", 0
TestBranch_BgezalRetAddr_Msg:   .db "bgezal has incorrect return addr.\n", 0



FreeMemorySection:
.resv MEMORY_SIZE
FreeMemoryEnd:

