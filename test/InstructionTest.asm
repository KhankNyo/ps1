;.littleEndian

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
    la $a0, TestStatus_Load_Msg
    jal PrintStr
    jal TestLoad
    bnz $v0, Fail           ; if (0 != arith test result) -> failed
    la $a0, TestStatus_Ok_Msg
    jal PrintStr

    la $a0, TestStatus_Store_Msg
    jal PrintStr
    jal TestStore
    bnz $v0, Fail
    la $a0, TestStatus_Ok_Msg
    jal PrintStr

    la $a0, TestStatus_Branch_Msg
    jal PrintStr
    jal TestBranch
    bnz $v0, Fail
    la $a0, TestStatus_Ok_Msg
    jal PrintStr

    la $a0, TestStatus_ImmArith_Msg
    jal PrintStr
    jal TestImmArith
    bnz $v0, Fail           
    la $a0, TestStatus_Ok_Msg
    jal PrintStr

    la $a0, TestFinished_Msg ; else success, print msg 
    jal PrintStr
Fail:
    li $t0, SYS_EXIT
    sw $zero, 0($t0)        ; call sys_exit 

InfLoop: 
    bra InfLoop




; prints string in $a0 by writing its content to SYS_WRITE
; args:     const char *$a0
; destroys: $t0, $ra
PrintStr: 
    li $t0, SYS_WRITE
    sw $a0, 0($t0)          ; call sys_write(Str)
    ret 




; prints string in $a0 and then string in $a1
; args:     const char *$a0, $a1
; returns:  1 in $v0
; destroys: $t0, $t1, $v0, $ra
TestFailed:
    move $t1, $ra                   ; save return addr 
        jal PrintStr
        move $a0, $a1
        jal PrintStr
    move $ra, $t1                   ; restore return addr 
    ori $v0, $zero, 1               ; returns test failed (not using jump delay slot here)
    ret




; tests:            bltz, bgez, blez, bgtz, bltzal, bgezal, 
; assumes working:  beq, bne
; returns:          $v0: 0 for success, 1 for failure
; destroys:         $t0..9, $a0..1, $v0, $ra
.branchNop 0
.jumpNop 0
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
    move $t7, $zero                 ; branch delday counter 
    bltz $t3, TestBranch_Bltz_Ok0   ; assert (i32)(0x8000_0000) < 0
        addiu $t7, 1
        bra TestFailed
        nop
TestBranch_Bltz_Ok0:
    bltz $t2, TestBranch_Bltz_Ok1   ; assert (i32)0xFFFF_FFFF < 0
        addiu $t7, 1
        bra TestFailed
        nop
TestBranch_Bltz_Ok1:
    bltz $t4, TestFailed            ; assert !( (i32)0x7FFF_FFFF < 0 )
        addiu $t7, 1
    bltz $t9, TestFailed            ; assert !( 1 < 0 )
        addiu $t7, 1
    bltz $t8, TestFailed            ; assert !( 0 < 0 )
        addiu $t7, 1
    li $t6, 5
    la $a1, TestBranch_BltzDelaySlot_Msg
    bne $t7, $t6, TestFailed        ; assert branch delay counter == 5
        nop

    ; test bgez
    la $a1, TestBranch_Bgez_Msg
    move $t7, $zero
    bgez $t8, TestBranch_Bgez_Ok0   ; assert 0 >= 0
        addiu $t7, 1
        bra TestFailed
        nop
TestBranch_Bgez_Ok0:
    bgez $t9, TestBranch_Bgez_Ok1   ; assert 1 >= 0
        addiu $t7, 1
        bra TestFailed
        nop
TestBranch_Bgez_Ok1:
    bgez $t4, TestBranch_Bgez_Ok2   ; assert (i32)0x7FFF_FFFF >= 0
        addiu $t7, 1
        bra TestFailed
        nop
TestBranch_Bgez_Ok2:
    bgez $t2, TestFailed            ; assert !( (i32)0xFFFF_FFFF >= 0 )
        addiu $t7, 1
    bgez $t3, TestFailed            ; assert !( (i32)0x8000_0000 >= 0 )
        addiu $t7, 1
    li $t6, 5
    la $a1, TestBranch_BgezDelaySlot_Msg
    bne $t7, $t6, TestFailed        ; assert branch delay counter == 5
        nop

    ; test blez
    la $a1, TestBranch_Blez_Msg
    move $t7, $zero
    blez $t8, TestBranch_Blez_Ok0   ; assert 0 <= 0
        addiu $t7, 1
        bra TestFailed
        nop
TestBranch_Blez_Ok0:
    blez $t2, TestBranch_Blez_Ok1   ; assert (i32)0xFFFF_FFFF <= 1
        addiu $t7, 1
        bra TestFailed
        nop
TestBranch_Blez_Ok1:
    blez $t3, TestBranch_Blez_Ok3   ; assert (i32)0x8000_0000 <= 0
        addiu $t7, 1
        bra TestFailed
        nop
TestBranch_Blez_Ok3:
    blez $t9, TestFailed            ; assert !( 1 <= 0 )
        addiu $t7, 1
    blez $t4, TestFailed            ; assert !( (i32)0x7FFF_FFFF <= 0 )
        addiu $t7, 1
    li $t6, 5
    la $a1, TestBranch_BlezDelaySlot_Msg
    bne $t7, $t6, TestFailed        ; assert branch delay counter == 5


    ; test bgtz
    la $a1, TestBranch_Bgtz_Msg
    move $t7, $zero                 ; branch delay counter
    bgtz $t9, TestBranch_Bgtz_Ok0   ; assert 1 > 0
        addiu $t7, 1
        bra TestFailed
        nop
TestBranch_Bgtz_Ok0:
    bgtz $t4, TestBranch_Bgtz_Ok1   ; assert (i32)0x7FFF_FFFF > 0
        addiu $t7, 1
        bra TestFailed
        nop
TestBranch_Bgtz_Ok1:
    bgtz $t8, TestFailed            ; assert !( 0 > 0 )
        addiu $t7, 1
    bgtz $t2, TestFailed            ; assert !( (i32)0xFFFF_FFFF > 0 )
        addiu $t7, 1
    bgtz $t3, TestFailed            ; assert !( (i32)0x8000_0000 < 0 )
        addiu $t7, 1
    li $t6, 5
    la $a1, TestBranch_BgtzDelaySlot_Msg
    bne $t7, $t6, TestFailed        ; assert branch delay counter == 5

    move $t5, $ra                       ; save return reg 
    ; test bltzal
    la $a3, TestBranch_Bltzal_Msg
    la $a2, TestBranch_BltzalRetAddr_Msg
    move $t7, $zero                     ; branch delay counter
    bltzal $t2, TestBranch_Bltzal_Ok1   ; assert (i32)0xFFFF_FFFF < 0
        addiu $t7, 1
    TestBranch_Bltzal_Addr0:
        move $t6, $ra
        jal TestFailed
        move $a1, $a3                   ; set instruction error msg (in delay slot)
        move $ra, $t6
TestBranch_Bltzal_Ok1:
        la $t6, TestBranch_Bltzal_Addr0
        bne $ra, $t6, TestFailed        ; assert ra == TestBranch_Bltzal_Addr0
        move $a1, $a2

    bltzal $t3, TestBranch_Bltzal_Ok2   ; assert (i32)0x8000_0000 < 0
        addiu $t7, 1
    TestBranch_Bltzal_Addr1:
        move $t6, $ra
        jal TestFailed
        move $a1, $a3                   ; NOTE: arg in delay slot
        move $ra, $t6
TestBranch_Bltzal_Ok2:
        la $t6, TestBranch_Bltzal_Addr1
        bne $ra, $t6, TestFailed        ; assert ra == TestBranch_Bltzal_Addr1
        move $a1, $a2                   ; ret addr error msg

    move $a1, $a3                       ; instruction error msg
    bltzal $t8, TestFailed              ; assert !( 0 < 0 ) 
        addiu $t7, 1
    TestBranch_Bltzal_Addr2:
        la $t6, TestBranch_Bltzal_Addr2
        bne $ra, $t6, TestFailed        ; assert ra == TestBranch_Bltzal_Addr2
        move $a1, $a2                   ; ret addr error msg (in delay slot)

    move $a1, $a3                       ; instruction error msg
    bltzal $t9, TestFailed              ; assert !( 1 < 0 )
        addiu $t7, 1
    TestBranch_Bltzal_Addr3:
        la $t6, TestBranch_Bltzal_Addr3
        bne $ra, $t6, TestFailed        ; assert ra == TestBranch_Bltzal_Addr3
        move $a1, $a2                   ; ret addr error msg (in delay slot

    move $a1, $a3                       ; instruction error msg
    bltzal $t4, TestFailed              ; assert !( 1 < 0 ) 
        addiu $t7, 1
    TestBranch_Bltzal_Addr4:
        la $t6, TestBranch_Bltzal_Addr4
        bne $ra, $t6, TestFailed        ; assert ra == TestBranch_Bltzal_Addr4
        move $a1, $a2                   ; ret addr error msg (in delay slot)
    li $t6, 5
    la $a1, TestBranch_BltzalDelaySlot_Msg
    bne $t6, $t7, TestFailed            ; assert branch delay counter == 5
        nop

    ; test bgezal
    la $a3, TestBranch_Bgezal_Msg
    la $a2, TestBranch_BgezalRetAddr_Msg
    move $t7, $zero
    bgezal $t8, TestBranch_Bgezal_Ok0
        addiu $t7, 1
    TestBranch_Bgezal_Addr0:
        move $t6, $ra
        jal TestFailed
        move $a1, $a3
        move $ra, $t6
TestBranch_Bgezal_Ok0:
        la $t6, TestBranch_Bgezal_Addr0
        bne $ra, $t6, TestFailed        ; assert ra == TestBranch_Bgezal_Addr0
        move $a1, $a2

    bgezal $t9, TestBranch_Bgezal_Ok1
        addiu $t7, 1
    TestBranch_Bgezal_Addr1:
        move $t6, $ra
        jal TestFailed
        move $a1, $a3
        move $ra, $t6
TestBranch_Bgezal_Ok1:
        la $t6, TestBranch_Bgezal_Addr1
        bne $ra, $t6, TestFailed        ; assert ra == TestBranch_Bgezal_Addr1
        move $a1, $a2
    
    bgezal $t4, TestBranch_Bgezal_Ok2
        addiu $t7, 1
    TestBranch_Bgezal_Addr2:
        move $t6, $ra
        jal TestFailed
        move $a1, $a3
        move $ra, $t6
TestBranch_Bgezal_Ok2:
        la $t6, TestBranch_Bgezal_Addr2
        bne $ra, $t6, TestFailed        ; assert ra == TestBranch_Bgezal_Addr2
        move $a1, $a2

    move $a1, $a3
    bgezal $t2, TestFailed
        addiu $t7, 1
    TestBranch_Bgezal_Addr3:
        la $t6, TestBranch_Bgezal_Addr3
        bne $ra, $t6, TestFailed        ; assert ra == TestBranch_Bgezal_Addr3
        move $a1, $a2

    move $a1, $a3
    bgezal $t3, TestFailed
        addiu $t7, 1
    TestBranch_Bgezal_Addr4:
        la $t6, TestBranch_Bgezal_Addr4
        bne $ra, $t6, TestFailed        ; assert ra == TestBranch_Bgezal_Addr4
        move $a1, $a2
    li $t6, 5
    la $a1, TestBranch_BgezalDelaySlot_Msg
    bne $t6, $t7, TestFailed            ; assert branch delay counter == 5
        nop
.branchNop 1
.jumpNop 1

    move $ra, $t5                   ; restore return reg
    ret 



; tests lb, lh, lw, lbu, lhu, lwl, lwr instructions
; returns:  $v0: 1 for failure, 0 for success
; destroys: $t0..9, $a0, $a1, $at, $ra, $v0
.loadNop 0
.branchNop 0
TestLoad:
    li $t9, 1
    li $t8, -1
    li $t7, -0x80
    li $t6, 0x7F
    li $t1, 0x1337C0DE
    move $t5, $zero

    ; test lb (signed)
    la $a1, TestLoad_DelaySlotRead_Msg
    la $a0, TestLoad_Lb_Msg
    la $at, TestLoad_Byte

    move $t0, $t1
    lb $t0, 0($at)
        bne $t0, $t1, TestFailed    ; NOTE: in delay slot
    lb $t0, 1($at)
        bne $t0, $t9, TestFailed
    lb $t0, 2($at)
        bne $t0, $t8, TestFailed
    lb $t0, 3($at)
        bne $t0, $t7, TestFailed
    lb $t0, 4($at)
        bne $t0, $t6, TestFailed
        nop
    bne $t0, $t5, TestFailed
        nop

    la $a1, TestLoad_DelaySlotWrite_Msg
    lb $t0, 0($at)
        or $t0, $t1, $zero          ; NOTE: despite delay slot, writes should be in order
    bne $t0, $t1, TestFailed
        nop

    ; test lh (signed)
    la $a0, TestLoad_Lh_Msg
    la $a1, TestLoad_DelaySlotRead_Msg
    la $at, TestLoad_Half
    li $t7, -0x8000
    li $t6, 0x7FFF

    move $t0, $t1
    lh $t0, 0($at)
        bne $t0, $t1, TestFailed    ; NOTE: in delay slot
    lh $t0, 2($at)
        bne $t0, $t9, TestFailed
    lh $t0, 4($at)
        bne $t0, $t8, TestFailed
    lh $t0, 6($at)
        bne $t0, $t7, TestFailed
    lh $t0, 8($at)
        bne $t0, $t6, TestFailed
        nop
    bne $t0, $t5, TestFailed
        nop
    la $a1, TestLoad_DelaySlotWrite_Msg
    lh $t0, 0($at)
        or $t0, $t1, $zero          ; NOTE: despite delay slot, writes should be in order
    bne $t0, $t1, TestFailed
        nop

    ; test lw (signed)
    la $a0, TestLoad_Lw_Msg
    la $a1, TestLoad_DelaySlotRead_Msg
    la $at, TestLoad_Word
    li $t7, -0x8000_0000
    la $t6,  0x7FFF_FFFF

    move $t0, $t1
    lw $t0, 0($at)
        bne $t0, $t1, TestFailed    ; NOTE: in delay slot
    lw $t0, 4($at)
        bne $t0, $t9, TestFailed
    lw $t0, 8($at)
        bne $t0, $t8, TestFailed
    lw $t0, 12($at)
        bne $t0, $t7, TestFailed
    lw $t0, 16($at)
        bne $t0, $t6, TestFailed
        nop
    bne $t0, $t5, TestFailed
        nop
    la $a1, TestLoad_DelaySlotWrite_Msg
    lw $t0, 0($at)
        or $t0, $t1, $zero          ; NOTE: despite delay slot, writes should be in order
    bne $t0, $t1, TestFailed
        nop

    ; test lbu
    li $t6, 0x7F
    li $t7, 0x80
    li $t8, 0xFF
    la $a1, TestLoad_DelaySlotRead_Msg
    la $a0, TestLoad_Lbu_Msg
    la $at, TestLoad_Byte

    lbu $t0, 0($at)
    lbu $t0, 1($at)
        bne $t0, $t9, TestFailed    ; NOTE: in delay slot
    lbu $t0, 2($at)
        bne $t0, $t8, TestFailed
    lbu $t0, 3($at)
        bne $t0, $t7, TestFailed
    lbu $t0, 4($at)
        bne $t0, $t6, TestFailed
        nop
    bne $t0, $t5, TestFailed
        nop
    la $a1, TestLoad_DelaySlotWrite_Msg
    lbu $t0, 0($at)
        or $t0, $t1, $zero          ; NOTE: despite delay slot, writes should be in order
    bne $t0, $t1, TestFailed
        nop

    ; test lhu
    li $t6, 0x7FFF
    li $t7, 0x8000
    li $t8, 0xFFFF
    la $a1, TestLoad_DelaySlotRead_Msg
    la $a0, TestLoad_Lhu_Msg
    la $at, TestLoad_Half

    lhu $t0, 0($at)
    lhu $t0, 2($at)
        bne $t0, $t9, TestFailed    ; NOTE: in delay slot
    lhu $t0, 4($at)
        bne $t0, $t8, TestFailed
    lhu $t0, 6($at)
        bne $t0, $t7, TestFailed
    lhu $t0, 8($at)
        bne $t0, $t6, TestFailed
        nop
    bne $t0, $t5, TestFailed
        nop
    la $a1, TestLoad_DelaySlotWrite_Msg
    lhu $t0, 0($at)
        or $t0, $t1, $zero          ; NOTE: despite delay slot, writes should be in order
    bne $t0, $t1, TestFailed
        nop

    ; test lwl
    la $at, TestLoad_WordDir
    la $a0, TestLoad_Lwl_Msg
    la $a1, TestLoad_DirFailed_Msg

    la $t9, 0xdeadbeef
    la $t8, 0xadbeef77
    la $t7, 0xbeef7777
    la $t6, 0xef777777
    li $t0, 0x77777777
    move $t1, $t0
    lwl $t0, 0($at)
        bne $t0, $t1, TestFailed    ; NOTE: in delay slot 
    lwl $t0, 1($at)
        bne $t0, $t6, TestFailed
    lwl $t0, 2($at)
        bne $t0, $t7, TestFailed
    lwl $t0, 3($at)
        bne $t0, $t8, TestFailed
        nop
    bne $t0, $t9, TestFailed
        nop

    la $a1, TestLoad_DelaySlotWrite_Msg
    li $t1, 0x1337C0DE
    lwl $t0, 2($at)
        or $t0, $zero, $t1          ; NOTE: should overwrite loaded content
    bne $t0, $t1, TestFailed
        nop

    ; test lwr
    la $at, TestLoad_WordDir
    la $a0, TestLoad_Lwl_Msg
    la $a1, TestLoad_DirFailed_Msg

    la $t9, 0xdeadbeef
    la $t8, 0x77deadbe
    la $t7, 0x7777dead
    la $t6, 0x777777de
    li $t0, 0x77777777
    move $t1, $t0
    lwr $t0, 3($at)
        bne $t0, $t1, TestFailed    ; NOTE: in delay slot 
    lwr $t0, 2($at)
        bne $t0, $t6, TestFailed
    lwr $t0, 1($at)
        bne $t0, $t7, TestFailed
    lwr $t0, 0($at)
        bne $t0, $t8, TestFailed
        nop
    bne $t0, $t9, TestFailed
        nop

    la $a1, TestLoad_DelaySlotWrite_Msg
    li $t1, 0x1337C0DE
    lwr $t0, 2($at)
        or $t0, $zero, $t1          ; NOTE: should overwrite loaded content
    bne $t0, $t1, TestFailed
        nop

    ; test lwl, lwr combo
    la $a0, TestLoad_Combo_Msg
    la $a1, TestLoad_Failed_Msg
    la $t1, TestLoad_WordDir
    la $t2, 0x0D_DEAD_BE
    la $t3, 0xF00D_DEAD_
    la $t4, 0xAD_F00D_DE

    lwl $t0, 4($t1)                 ; 0x0d__-____
    lwr $t0, 1($t1)                 ; 0x__de-adbe
        nop
    bne $t0, $t2, TestFailed
        nop
    lwl $t0, 5($t1)                 ; 0xf00d-____
    lwr $t0, 2($t1)                 ; 0x____-dead
        nop
    bne $t0, $t3, TestFailed
        nop
    lwl $t0, 6($t1)                 ; 0xadf0-0d__
    lwr $t0, 3($t1)                 ; 0x____-__de
        nop
    bne $t0, $t4, TestFailed
        nop

    move $v0, $zero
    ret 
.loadNop 1
.branchNop 1




TestStore:
    move $v0, $zero
    ret 




; tests arithmetic instructions involving immediate values: 
;   addiu, sltiu, slti, andi, ori, xori, lui
; destroys: $a0, $a1, $t0, $t1, $t2, $v0, $ra
.jumpNop 0 
TestImmArith:
    ; test r0:
    la $a1, TestImmArith_R0_Msg
    move $1, $zero
    move $2, $zero
    addiu $0, $0, 10                ; r0 = 0 (should not be 10)
    addu $1, $0, $0                 ; r1 = 0 (should not be 20)
    bne $1, $0, TestFailed          ; assert r1 == r0

    ; test addiu: sign extension
    la $a1, TestImmArith_AddiuSex_Msg
    addiu $t0, $zero, -1            ; t0 =  0xFFFF_FFFF
    addiu $t0, 1                    ; t0 += 0x0000_0001
    bne $t0, $zero, TestFailed      ; assert t0 == 0

    ; test ori: zero extension
    la $a1, TestImmArith_OriZex_Msg
    ori $t0, $zero, 0xFFFF          ; t0 = 0x0000_FFFF
    addiu $t1, $zero, -1            ; t1 = 0xFFFF_FFFF
    beq $t0, $t1, TestFailed        ; assert t0 != t1 

    ; test lui, andi: 
    la $a1, TestImmArith_LuiAndi_Msg
    ori $t0, $zero, 0xFFFF          ; t0 =  0x0000_FFFF
    lui $t0, 0xFFFF                 ; t0 =  0xFFFF_0000
    andi $t0, $t0, 0xFFFF           ; t0 &= 0x0000_FFFF
    bne $t0, $zero, TestFailed      ; assert t0 == 0

    ; test xori:
    la $a1, TestImmArith_Xori_Msg       
    addiu $t0, $zero, -1            ; t0 =  0xFFFF_FFFF
    xori $t0, 0xF00F                ; t0 ^= 0x0000_F00F
    la $t1, 0xFFFF_0FF0             ; expected 
    bne $t1, $t0, TestFailed        ; assert t0 == 0xFFFF_0FF0

    ; test slti: true case 
    la $a1, TestImmArith_Slti_Msg
    slti $t0, $zero, 1              ; t1 = 0 < 1
    bez $t0, TestFailed             ; assert condition true

    ; test slti: false case, negative
    la $a1, TestImmArith_SltiNeg_Msg
    slti $t0, $zero, -1             ; t0 = 0 < -1
    bnz $t0, TestFailed             ; assert condition false

    ; test sltiu: true case, unsigned
    la $a1, TestImmArith_SltiuUnsigned_Msg
    sltiu $t0, $zero, -1            ; t0 = 0 < 0xFFFF_FFFF
    bez $t0, TestFailed             ; assert condition true

    ; test sltiu: false case, sign extension
    la $a1, TestImmArith_SltiuSex_Msg
    li $t1, -1
    sltiu $t0, $t1, (-1 << 15)      ; t0 = 0xFFFF_FFFF < 0xFFFF_8000
    bnz $t0, TestFailed             ; assert condition false

    ret
    move $v0, $zero                 ; returns test success 
.jumpNop 1


DataSection:
TestStatus_Load_Msg:            .db "Load instructions: ", 0
TestStatus_Store_Msg:           .db "Store instructions: ", 0
TestStatus_Branch_Msg:          .db "Branch instructions: ", 0
TestStatus_ImmArith_Msg:        .db "Alu with immediate instructions: ", 0
TestStatus_Ok_Msg:              .db "OK\n", 0
TestFinished_Msg:               .db "All tests passed.\n", 0

TestPrerequisite_Failed_Msg:    .db "Test preqrequisite failed: jal.\n", 0

TestImmArith_Failed_Msg:        .db "Arithmetic test failed: ", 0
TestImmArith_R0_Msg:            .db "R0 should always be zero.\n", 0
TestImmArith_AddiuSex_Msg:      .db "addiu should sign extend its immediate value.\n", 0
TestImmArith_OriZex_Msg:        .db "ori should zero extend its immediate value.\n", 0
TestImmArith_LuiAndi_Msg:       .db "lui, andi.\n", 0
TestImmArith_Xori_Msg:          .db "xori should zero extend its immediate value.\n", 0
TestImmArith_Slti_Msg:          .db "slti.\n", 0
TestImmArith_SltiNeg_Msg:       .db "slti failed on negative values.\n", 0
TestImmArith_SltiuUnsigned_Msg: .db "Operands of sltiu should be treated as unsigned.\n", 0
TestImmArith_SltiuSex_Msg:      .db "Immediate of sltiu must be sign extended.\n", 0

TestBranch_Failed_Msg:          .db "Branch test failed: ", 0
TestBranch_Bltz_Msg:            .db "bltz.\n", 0
TestBranch_Bgez_Msg:            .db "bgez.\n", 0
TestBranch_Blez_Msg:            .db "blez.\n", 0
TestBranch_Bgtz_Msg:            .db "bgtz.\n", 0
TestBranch_Bltzal_Msg:          .db "bltzal.\n", 0
TestBranch_Bgezal_Msg:          .db "bgezal.\n", 0
TestBranch_BltzalRetAddr_Msg:   .db "bltzal has incorrect return addr.\n", 0
TestBranch_BgezalRetAddr_Msg:   .db "bgezal has incorrect return addr.\n", 0

TestBranch_BltzDelaySlot_Msg:   .db "bltz delay slot.\n", 0
TestBranch_BgezDelaySlot_Msg:   .db "bgezu delay slot.\n", 0
TestBranch_BlezDelaySlot_Msg:   .db "bltez delay slot.\n", 0
TestBranch_BgtzDelaySlot_Msg:   .db "bgtz delay slot.\n", 0
TestBranch_BltzalDelaySlot_Msg: .db "bltzal delay slot.\n", 0
TestBranch_BgezalDelaySlot_Msg: .db "bgezal delay slot.\n", 0

TestLoad_Lb_Msg:                .db "lb:", 0
TestLoad_Lbu_Msg:               .db "lbu:", 0
TestLoad_Lh_Msg:                .db "lh:", 0
TestLoad_Lhu_Msg:               .db "lhu:", 0
TestLoad_Lw_Msg:                .db "lw:", 0
TestLoad_Lwl_Msg:               .db "lwl:", 0
TestLoad_Lwr_Msg:               .db "lwr:", 0
TestLoad_Combo_Msg:             .db "lwl and lwr ", 0
TestLoad_Failed_Msg:            .db "failed.\n", 0
TestLoad_DirFailed_Msg:         .db " old content didn't persist or delay slot failed.\n", 0
TestLoad_DelaySlotRead_Msg:     .db " read in delay slot failed.\n", 0
TestLoad_DelaySlotWrite_Msg:    .db " write in delay slot failed (via 'or' instruction).\n", 0
TestLoad_NoDelaySlot_Msg:       .db " delay slot should not exist.\n", 0

.align 4
TestLoad_WordDir:   .dw 0xdeadbeef, 0xbaadf00d
TestLoad_Word:      .dw 1, -1, 0x8000_0000, 0x7FFF_FFFF, 0
TestLoad_Half:      .dh 1, -1, 0x8000, 0x7FFF, 0
TestLoad_Byte:      .db 1, -1, 0x80, 0x7F, 0


FreeMemorySection:
.resv MEMORY_SIZE
FreeMemoryEnd:

