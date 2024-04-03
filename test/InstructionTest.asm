;.littleEndian

SYS_WRITESTR    = 0x7000_0000
SYS_WRITEHEX    = 0x7001_0000
SYS_CLRSCR      = 0x7100_0000
SYS_EXIT        = 0x7200_0000

RESET_VEC       = 0xBFC0_0000
START           = RESET_VEC + 0x0180
MEMORY_SIZE     = 4096


.org RESET_VEC
; prerequisite: 
;   bne, beq 
;   j,   jal, 
;   lui, ori
.jumpNop 0
.branchNop 0
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
    li $t1, SYS_WRITESTR
    sw $t0, 0($t1)
    beq $zero, $zero, ResetTrap
.jumpNop 1
.branchNop 1
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




; PrintStr: prints string in $a0 by writing its content to SYS_WRITESTR
; args:     const char *$a0
; destroys: $t0
PrintStr: 
    li $t0, SYS_WRITESTR
    sw $a0, 0($t0)          ; call sys_write(Str)
    ret 

; PrintHex: prints hexadecimal number in $a0
; args:     u32 $a0
; destroys: $t0
PrintHex:
    li $t0, SYS_WRITEHEX
    sw $a0, 0($t0)
    ret




; TestFailed: prints string pointed to by $a0 and $a1, prints he addr pointed to by $a2
; args:     const char *$a0, $a1; u32 $a2
; returns:  1 in $v0
; destroys: $t0..1, $a0, $v0
TestFailed:
    move $t1, $ra                   ; save return addr 
        jal PrintStr
        move $a0, $a1
        jal PrintStr
        move $a0, $a2
        jal PrintHex
    move $ra, $t1                   ; restore return addr 
    ori $v0, $zero, 1               ; returns test failed (not using jump delay slot here)
    ret




; TestBranch:       bltz, bgez, blez, bgtz, bltzal, bgezal
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
TestBranch_Bltz:
    la $a1, TestBranch_Bltz_Msg
    la $a2, TestBranch_Bltz
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
TestBranch_Bgez:
    la $a1, TestBranch_Bgez_Msg
    la $a2, TestBranch_Bgez
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
TestBranch_Blez:
    la $a1, TestBranch_Blez_Msg
    la $a2, TestBranch_Blez
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
TestBranch_Bgtz:
    la $a1, TestBranch_Bgtz_Msg
    la $a2, TestBranch_Bgtz
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
TestBranch_Bltzal:
    la $a3, TestBranch_Bltzal_Msg
    la $t0, TestBranch_BltzalRetAddr_Msg
    la $a2, TestBranch_Bltzal
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
        move $a1, $t0                   ; ret addr error msg
        bne $ra, $t6, TestFailed        ; assert ra == TestBranch_Bltzal_Addr0
            move $ra, $t5               ; restore return reg

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
        move $a1, $t0                   ; ret addr error msg (in delay slot)
            move $ra, $t5               ; restore return reg

    move $a1, $a3                       ; instruction error msg
    bltzal $t8, TestFailed              ; assert !( 0 < 0 ) 
        addiu $t7, 1
    TestBranch_Bltzal_Addr2:
        la $t6, TestBranch_Bltzal_Addr2
        move $a1, $t0                   ; ret addr error msg (in delay slot)
        bne $ra, $t6, TestFailed        ; assert ra == TestBranch_Bltzal_Addr2
            move $ra, $t5               ; restore return reg

    move $a1, $a3                       ; instruction error msg
    bltzal $t9, TestFailed              ; assert !( 1 < 0 )
        addiu $t7, 1
    TestBranch_Bltzal_Addr3:
        la $t6, TestBranch_Bltzal_Addr3
        move $a1, $t0                   ; ret addr error msg (in delay slot)
        bne $ra, $t6, TestFailed        ; assert ra == TestBranch_Bltzal_Addr3
            move $ra, $t5               ; restore return reg

    move $a1, $a3                       ; instruction error msg
    bltzal $t4, TestFailed              ; assert !( 1 < 0 ) 
        addiu $t7, 1
    TestBranch_Bltzal_Addr4:
        la $t6, TestBranch_Bltzal_Addr4
        move $a1, $t0                   ; ret addr error msg (in delay slot)
        bne $ra, $t6, TestFailed        ; assert ra == TestBranch_Bltzal_Addr4
            move $ra, $t5               ; restore return reg
    li $t6, 5
    la $a1, TestBranch_BltzalDelaySlot_Msg
    bne $t6, $t7, TestFailed            ; assert branch delay counter == 5
        move $ra, $t5                   ; restore return reg

    ; test bgezal
TestBranch_Bgezal:
    la $a3, TestBranch_Bgezal_Msg
    la $t0, TestBranch_BgezalRetAddr_Msg
    la $a2, TestBranch_Bgezal
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
        move $a1, $t0                   ; error msg
        bne $ra, $t6, TestFailed        ; assert ra == TestBranch_Bgezal_Addr0
            move $ra, $t5               ; restore return reg

    bgezal $t9, TestBranch_Bgezal_Ok1
        addiu $t7, 1
    TestBranch_Bgezal_Addr1:
        move $t6, $ra
        jal TestFailed
        move $a1, $a3
        move $ra, $t6
    TestBranch_Bgezal_Ok1:
        la $t6, TestBranch_Bgezal_Addr1
        move $a1, $t0                   ; error msg
        bne $ra, $t6, TestFailed        ; assert ra == TestBranch_Bgezal_Addr1
            move $ra, $t5               ; restore return reg
    
    bgezal $t4, TestBranch_Bgezal_Ok2
        addiu $t7, 1
    TestBranch_Bgezal_Addr2:
        move $t6, $ra
        jal TestFailed
        move $a1, $a3
        move $ra, $t6
    TestBranch_Bgezal_Ok2:
        la $t6, TestBranch_Bgezal_Addr2
        move $a1, $t0                   ; error msg
        bne $ra, $t6, TestFailed        ; assert ra == TestBranch_Bgezal_Addr2
            move $ra, $t5               ; restore return reg

    move $a1, $a3
    bgezal $t2, TestFailed
        addiu $t7, 1
    TestBranch_Bgezal_Addr3:
        la $t6, TestBranch_Bgezal_Addr3
        move $a1, $t0                   ; error msg
        bne $ra, $t6, TestFailed        ; assert ra == TestBranch_Bgezal_Addr3
            move $ra, $t5               ; restore return reg

    move $a1, $a3
    bgezal $t3, TestFailed
        addiu $t7, 1
    TestBranch_Bgezal_Addr4:
        la $t6, TestBranch_Bgezal_Addr4
        move $a1, $t0                   ; error msg
        bne $ra, $t6, TestFailed        ; assert ra == TestBranch_Bgezal_Addr4
            move $ra, $t5               ; restore return reg
    li $t6, 5
    la $a1, TestBranch_BgezalDelaySlot_Msg
    bne $t6, $t7, TestFailed            ; assert branch delay counter == 5
        move $ra, $t5                   ; restore return reg

    ret 
    nop
.branchNop 1
.jumpNop 1



; TestLoad: lb, lh, lw, lbu, lhu, lwl, lwr
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
TestLoad_Lb:
    la $a2, TestLoad_Lb
    la $a1, TestLoad_DelaySlotRead_Msg
    la $a0, TestLoad_Lb_Msg
    la $at, TestLoad_Byte

    move $t0, $t1
    lb $t0, 0($at)
        bne $t0, $t1, TestFailed    ; NOTE: in delay slot
        bne $t0, $t9, TestFailed    ; delay slot over, value should be loaded by now
    lb $t0, 1($at)
        bne $t0, $t9, TestFailed
        bne $t0, $t8, TestFailed
    lb $t0, 2($at)
        bne $t0, $t8, TestFailed
        bne $t0, $t7, TestFailed
    lb $t0, 3($at)
        bne $t0, $t7, TestFailed
        bne $t0, $t6, TestFailed
    lb $t0, 4($at)
        bne $t0, $t6, TestFailed
    bne $t0, $t5, TestFailed
        nop

    la $a1, TestLoad_DelaySlotWrite_Msg
    lb $t0, 0($at)
        or $t0, $t1, $zero          ; NOTE: despite delay slot, writes should be in order
    bne $t0, $t1, TestFailed
        nop

    ; test lh (signed)
TestLoad_Lh:
    la $a2, TestLoad_Lh
    la $a1, TestLoad_DelaySlotRead_Msg
    la $a0, TestLoad_Lh_Msg
    la $at, TestLoad_Half
    li $t7, -0x8000
    li $t6, 0x7FFF

    move $t0, $t1
    lh $t0, 0($at)
        bne $t0, $t1, TestFailed    ; NOTE: in delay slot
        bne $t0, $t9, TestFailed
    lh $t0, 2($at)
        bne $t0, $t9, TestFailed
        bne $t0, $t8, TestFailed
    lh $t0, 4($at)
        bne $t0, $t8, TestFailed
        bne $t0, $t7, TestFailed
    lh $t0, 6($at)
        bne $t0, $t7, TestFailed
    lh $t0, 8($at)
        bne $t0, $t6, TestFailed
    bne $t0, $t5, TestFailed
        nop
    la $a1, TestLoad_DelaySlotWrite_Msg
    lh $t0, 0($at)
        or $t0, $t1, $zero          ; NOTE: despite delay slot, writes should be in order
    bne $t0, $t1, TestFailed
        nop

    ; test lw (signed)
TestLoad_Lw:
    la $a2, TestLoad_Lw
    la $a1, TestLoad_DelaySlotRead_Msg
    la $a0, TestLoad_Lw_Msg
    la $at, TestLoad_Word
    li $t7, -0x8000_0000
    la $t6,  0x7FFF_FFFF

    move $t0, $t1
    lw $t0, 0($at)
        bne $t0, $t1, TestFailed    ; NOTE: in delay slot
        bne $t0, $t9, TestFailed
    lw $t0, 4($at)
        bne $t0, $t9, TestFailed
        bne $t0, $t8, TestFailed
    lw $t0, 8($at)
        bne $t0, $t8, TestFailed
        bne $t0, $t7, TestFailed
    lw $t0, 12($at)
        bne $t0, $t7, TestFailed
        bne $t0, $t6, TestFailed
    lw $t0, 16($at)
        bne $t0, $t6, TestFailed
    bne $t0, $t5, TestFailed
        nop
    la $a1, TestLoad_DelaySlotWrite_Msg
    lw $t0, 0($at)
        or $t0, $t1, $zero          ; NOTE: despite delay slot, writes should be in order
    bne $t0, $t1, TestFailed
        nop

    ; test lbu
TestLoad_Lbu:
    la $a2, TestLoad_Lbu
    la $a1, TestLoad_DelaySlotRead_Msg
    la $a0, TestLoad_Lbu_Msg
    la $at, TestLoad_Byte
    li $t6, 0x7F
    li $t7, 0x80
    li $t8, 0xFF

    lbu $t0, 0($at)
    lbu $t0, 1($at)
        bne $t0, $t9, TestFailed    ; NOTE: in delay slot
        bne $t0, $t8, TestFailed
    lbu $t0, 2($at)
        bne $t0, $t8, TestFailed
        bne $t0, $t7, TestFailed
    lbu $t0, 3($at)
        bne $t0, $t7, TestFailed
        bne $t0, $t6, TestFailed
    lbu $t0, 4($at)
        bne $t0, $t6, TestFailed
    bne $t0, $t5, TestFailed
        nop
    la $a1, TestLoad_DelaySlotWrite_Msg
    lbu $t0, 0($at)
        or $t0, $t1, $zero          ; NOTE: despite delay slot, writes should be in order
    bne $t0, $t1, TestFailed
        nop

    ; test lhu
TestLoad_Lhu:
    la $a2, TestLoad_Lhu
    la $a1, TestLoad_DelaySlotRead_Msg
    la $a0, TestLoad_Lhu_Msg
    la $at, TestLoad_Half
    li $t6, 0x7FFF
    li $t7, 0x8000
    li $t8, 0xFFFF

    move $t0, $t1
    lhu $t0, 0($at)
        bne $t0, $t1, TestFailed    ; NOTE: delay slot
        bne $t0, $t9, TestFailed
    lhu $t0, 2($at)
        bne $t0, $t9, TestFailed
        bne $t0, $t8, TestFailed
    lhu $t0, 4($at)
        bne $t0, $t8, TestFailed
        bne $t0, $t7, TestFailed
    lhu $t0, 6($at)
        bne $t0, $t7, TestFailed
        bne $t0, $t6, TestFailed
    lhu $t0, 8($at)
        bne $t0, $t6, TestFailed
    bne $t0, $t5, TestFailed
        nop
    la $a1, TestLoad_DelaySlotWrite_Msg
    lhu $t0, 0($at)
        or $t0, $t1, $zero          ; NOTE: despite delay slot, writes should be in order
    bne $t0, $t1, TestFailed
        nop

    ; test lwl
TestLoad_Lwl:
    la $a2, TestLoad_Lwl
    la $a1, TestLoad_DirFailed_Msg
    la $a0, TestLoad_Lwl_Msg
    la $at, TestLoad_WordDir

    la $t9, 0xdeadbeef
    la $t8, 0xadbeef77
    la $t7, 0xbeef7777
    la $t6, 0xef777777
    li $t0, 0x77777777
    move $t1, $t0
    lwl $t0, 0($at)
        bne $t0, $t1, TestFailed    ; NOTE: in delay slot 
        bne $t0, $t6, TestFailed
    lwl $t0, 1($at)
        bne $t0, $t6, TestFailed
        bne $t0, $t7, TestFailed
    lwl $t0, 2($at)
        bne $t0, $t7, TestFailed
        bne $t0, $t8, TestFailed
    lwl $t0, 3($at)
        bne $t0, $t8, TestFailed
    bne $t0, $t9, TestFailed
        nop

    la $a1, TestLoad_DelaySlotWrite_Msg
    li $t1, 0x1337C0DE
    lwl $t0, 2($at)
        or $t0, $zero, $t1          ; NOTE: should overwrite loaded content
    bne $t0, $t1, TestFailed
        nop

    ; test lwr
TestLoad_Lwr:
    la $a2, TestLoad_Lwl
    la $a1, TestLoad_DirFailed_Msg
    la $a0, TestLoad_Lwl_Msg
    la $at, TestLoad_WordDir

    la $t9, 0xdeadbeef
    la $t8, 0x77deadbe
    la $t7, 0x7777dead
    la $t6, 0x777777de
    li $t0, 0x77777777
    move $t1, $t0
    lwr $t0, 3($at)
        bne $t0, $t1, TestFailed    ; NOTE: in delay slot 
        bne $t0, $t6, TestFailed
    lwr $t0, 2($at)
        bne $t0, $t6, TestFailed
        bne $t0, $t7, TestFailed
    lwr $t0, 1($at)
        bne $t0, $t7, TestFailed
        bne $t0, $t8, TestFailed
    lwr $t0, 0($at)
        bne $t0, $t8, TestFailed
    bne $t0, $t9, TestFailed
        nop

    la $a1, TestLoad_DelaySlotWrite_Msg
    li $t1, 0x1337C0DE
    lwr $t0, 2($at)
        or $t0, $zero, $t1          ; NOTE: should overwrite loaded content
    bne $t0, $t1, TestFailed
        nop

    ; test lwl, lwr combo
TestLoad_Combo:
    la $a2, TestLoad_Combo
    la $a1, Test_Failed_Msg
    la $a0, TestLoad_Combo_Msg
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
        ; don't care about delay slot

    move $v0, $zero
    ret 
    nop
.loadNop 1
.branchNop 1




.branchNop 0
.loadNop 0
.jumpNop 0
TestStore:
    move $t9, $ra

    ; test sb:
    ; memcpy(FreeMemorySection, TestStore_ByteResult, TestStore_ByteSize)
TestStore_Sb:
    la $t1, FreeMemorySection
    la $t0, TestStore_ByteResult
    addiu $t2, $t0, TestStore_ByteSize
    TestStore_Bytes_Loop: 
        lb $t3, 0($t0)
        addiu $t0, 1
        sb $t3, 0($t1)
    bne $t0, $t2, TestStore_Bytes_Loop
        addiu $t1, 1                    ; NOTE: in branch delay slot

    ; memcmp(FreeMemorySection, TestStore_ByteResult, TestStore_ByteSize)
    la $a0, FreeMemorySection
    la $a1, TestStore_ByteResult
    jal Memcmp
        ori $a2, $zero, TestStore_ByteSize  ; NOTE: in jump delay slot
    la $a0, TestStore_Sb_Msg
    la $a1, Test_Failed_Msg
    la $a2, TestStore_Sb
    bez $v0, TestFailed                 ; fail if buffer not equal
        move $ra, $t9                   ; return while we're at it

    ; test sh:
TestStore_Sh:
    la $t1, FreeMemorySection
    la $t0, TestStore_HalfResult
    addiu $t2, $t0, TestStore_HalfSize
    TestStore_Half_Loop:
        lh $t3, 0($t0)
        addiu $t0, 2
        sh $t3, 0($t1)
    bne $t0, $t2, TestStore_Half_Loop
        addiu $t1, 2

    ; memcmp(FreeMemorySection, TestStore_HalfResult, TestStore_HalfSize)
    la $a0, FreeMemorySection
    la $a1, TestStore_HalfResult
    jal Memcmp
        ori $a2, $zero, TestStore_HalfSize ; NOTE: jump delay
    la $a0, TestStore_Sh_Msg
    la $a1, Test_Failed_Msg
    la $a2, TestStore_Sh
    bez $v0, TestFailed                 ; fail if buffer not equal
        move $ra, $t9                   ; return while we're at it

    ; test sw:
TestStore_Sw:
    la $t1, FreeMemorySection
    la $t0, TestStore_WordResult
    addiu $t2, $t0, TestStore_WordSize
    TestStore_Word_Loop:
        lw $t3, 0($t0)
        addiu $t0, 4
        sw $t3, 0($t1)
    bne $t0, $t2, TestStore_Word_Loop
        addiu $t1, 4

    ; memcmp(FreeMemorySection, TestStore_WordResult, TestStore_WordSize)
    la $a0, FreeMemorySection
    la $a1, TestStore_WordResult
    jal Memcmp
        ori $a2, $zero, TestStore_WordSize ; NOTE: jump delay
    la $a0, TestStore_Sw_Msg
    la $a1, Test_Failed_Msg
    la $a2, TestStore_Sw
    bez $v0, TestFailed                 ; fail if buffer not equal
        move $ra, $t9                   ; return while we're at it

    ; test swl
    li $t0, 0x44_33_22_11
    la $t1, 0x88_77_66_55
    la $t2, FreeMemorySection
TestStore_Swr:
    la $a0, TestStore_Swr_Msg
    la $a1, Test_Failed_Msg
    la $a2, TestStore_Swr

    la $t3, 0xAA_BB_CC_DD
    la $t6, 0xBB_CC_DD_11
    la $t7, 0xCC_DD_22_11
    la $t8, 0xDD_33_22_11

    sw $t1, 4($t2)
    sw $t0, 0($t2)                      ;  11 22 33 44  55 66 77 88

    swr $t3, 3($t2)                     ;  11 22 33[DD] 55 66 77 88
    lw $t4, 0($t2)
    lw $t5, 4($t2)
    bne $t4, $t8, TestFailed
        nop
    bne $t5, $t1, TestFailed
        ; delay slot doesn't matter

    swr $t3, 2($t2)                     ; 11 22[DD CC] 55 66 77 88
    lw $t4, 0($t2)
    lw $t5, 4($t2)
    bne $t4, $t7, TestFailed
        nop
    bne $t5, $t1, TestFailed
        ; delay slot doesn't matter
 
    swr $t3, 1($t2)                     ; 11[DD CC BB] 55 66 77 88
    lw $t4, 0($t2)
    lw $t5, 4($t2)
    bne $t4, $t6, TestFailed
        nop
    bne $t5, $t1, TestFailed
        ; delay slot doesn't matter
 
    swr $t3, 0($t2)                     ; [DD CC BB AA] 55 66 77 88
    lw $t4, 0($t2)
    lw $t5, 4($t2)
    bne $t4, $t3, TestFailed
        nop
    bne $t5, $t1, TestFailed
        nop

TestStore_Swl:
    la $a0, TestStore_Swl_Msg
    la $a1, Test_Failed_Msg
    la $a2, TestStore_Swl

    la $t3, 0xAA_BB_CC_DD
    la $t6, 0x88_AA_BB_CC
    la $t7, 0x88_77_AA_BB
    la $t8, 0x88_77_66_AA

    sw $t0, 0($t2)                      ; 11 22 33 44  55 66 77 88
    sw $t1, 4($t2)

    swl $t3, 1+3($t2)                   ; 11 22 33 44 [AA]66 77 88
    lw $t4, 0($t2)
    lw $t5, 4($t2)
    bne $t4, $t0, TestFailed
        nop
    bne $t5, $t8, TestFailed
        ; delay slot doesn't matter

    swl $t3, 2+3($t2)                   ; 11 22 33 44 [BB AA]77 88
    lw $t4, 0($t2)
    lw $t5, 4($t2)
    bne $t4, $t0, TestFailed
        nop
    bne $t5, $t7, TestFailed
        ; delay slot doesn't matter

    swl $t3, 3+3($t2)                   ; 11 22 33 44 [CC BB AA]88
    lw $t4, 0($t2)
    lw $t5, 4($t2)
    bne $t4, $t0, TestFailed
        nop
    bne $t5, $t6, TestFailed
        ; delay slot doesn't matter

    swl $t3, 4+3($t2)                     ; 11 22 33 44 [DD CC BB AA]
    lw $t4, 0($t2)
    lw $t5, 4($t2)
    bne $t4, $t0, TestFailed
        nop
    bne $t5, $t3, TestFailed
        ; delay slot doesn't matter

TestStore_Combo:
    ; https://student.cs.uwaterloo.ca/~cs350/common/r3000-manual.pdf
    ; wtf is there a mistake between appendix A-72 and A-74??
    ; it said swl reg, n(base); swr reg, n(base) can be used to load an unaligned word, 
    ; shouldn't it be swl reg, n+3(base); swr reg, n(base)???
    la $a0, TestStore_Combo_Msg
    la $a1, Test_Failed_Msg
    la $a2, TestStore_Combo

    li $t0, 0x1337C0DE
    la $t1, FreeMemorySection
    li $t2, 0x44_33_22_11

    ; offset by 1
    sw $t2, 0($t1)                      ; 11 22 33 44  11 22 33 44
    sw $t2, 4($t1)

    swr $t0, 1($t1)                     ; 11[DE C0 37] 11 22 33 44
    swl $t0, 1+3($t1)                   ; 11[DE C0 37  13]22 33 44

    lw $t3, 0($t1)
    li $t4, 0x37_C0DE_11
    bne $t3, $t4, TestFailed
    lw $t3, 4($t1)
    li $t4, 0x44_33_22_13
    bne $t3, $t4, TestFailed
        ; delay slot doesn't matter

    ; offset by 2
    sw $t2, 0($t1)                      ; 11 22 33 44  11 22 33 44
    sw $t2, 4($t1)

    swr $t0, 2($t1)                     ; 11 22[DE C0] 11 22 33 44
    swl $t0, 2+3($t1)                   ; 11 22[DE C0  37 13]33 44

    lw $t3, 0($t1)
    la $t4, 0xC0DE_22_11
    bne $t3, $t4, TestFailed
    lw $t3, 4($t1)
    la $t4, 0x44_33_1337
    bne $t3, $t4, TestFailed
        ; delay slot doesn't matter

    ; offset by 3
    sw $t2, 0($t1)                      ; 11 22 33 44  11 22 33 44
    sw $t2, 4($t1)

    swr $t0, 3($t1)                     ; 11 22 33[DE] 11 22 33 44
    swl $t0, 3+3($t1)                   ; 11 22 33[DE  C0 37 13]44

    lw $t3, 0($t1)
    la $t4, 0xDE_33_22_11
    bne $t3, $t4, TestFailed
    lw $t3, 4($t1)
    li $t4, 0x44_1337_C0
    bne $t3, $t4, TestFailed
        move $v0, $zero

    ret 
    nop
.jumpNop 1
.loadNop 1
.branchNop 1




; Memcmp:   compares 2 buffer byte for byte (slow!)
; args:     const Byte *$a0, $a1; u32 $a2
; returns:  $v0: 1 if buffer in $a0 is equal to buffer in $a1 byte for byte, otherwise 0
; destroys: $a0, $a1, $v0, $t0..2
.loadNop 0
.branchNop 0
.jumpNop 0
Memcmp:
    addu $t0, $a1, $a2              ; end = $a1 + $a2
Memcmp_Loop:
    lb $t1, 0($a0)                  ; tmp1 = [a0]
    lb $t2, 0($a1)                  ; tmp2 = [a1],  NOTE: in load delay slot
    addiu $a0, 1                    ; a0++,         NOTE: in load delay slot waiting for $t2 to be completely loaded
    beq $t1, $t2, Memcmp_Continue   ; if tmp1 == tmp2, continue
    addiu $a1, 1                    ; a1++,         NOTE: in branch delay slot, will always be executed
        ret                         ; else return 0
        move $v0, $zero
Memcmp_Continue:
    bne $t0, $a1, Memcmp_Loop       ; if end != a1, continue
        nop
    ret                             ; return 1
    ori $v0, $zero, 1
.jumpNop 1
.branchNop 1
.loadNop 1




; TestImmArith: tests arithmetic instructions involving immediate values: 
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
Test_Failed_Msg:                .db " failed.\n", 0

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
TestLoad_Combo_Msg:             .db "lwl and lwr", 0
TestLoad_DirFailed_Msg:         .db " old content didn't persist or delay slot failed.\n", 0
TestLoad_DelaySlotRead_Msg:     .db " read in delay slot failed.\n", 0
TestLoad_DelaySlotWrite_Msg:    .db " write in delay slot failed (via 'or' instruction).\n", 0
TestLoad_NoDelaySlot_Msg:       .db " delay slot should not exist.\n", 0

.align 4
TestLoad_WordDir:               .dw 0xdeadbeef, 0xbaadf00d
TestLoad_Word:                  .dw 1, -1, 0x8000_0000, 0x7FFF_FFFF, 0
TestLoad_Half:                  .dh 1, -1, 0x8000, 0x7FFF, 0
TestLoad_Byte:                  .db 1, -1, 0x80, 0x7F, 0

TestStore_Sb_Msg:               .db "sb", 0
TestStore_Sh_Msg:               .db "sh", 0
TestStore_Sw_Msg:               .db "sw", 0
TestStore_Swl_Msg:              .db "swl", 0
TestStore_Swr_Msg:              .db "swr", 0
TestStore_Combo_Msg:            .db "swl and swr", 0

.align 4
TestStore_WordResult:           .dw 1, -1, 0x7F69_1337, 0x8000_0000, 0x89AB_CDEF
TestStore_WordSize = 5*4
TestStore_HalfResult:           .dh 1, -1, 0x7FAB, 0x8000, 0xABCD
TestStore_HalfSize = 5*2
TestStore_ByteResult:           .db 1, -1, 0x7F, 0x80, 0x69
TestStore_ByteSize = 5

.align 4
FreeMemorySection:
.resv MEMORY_SIZE
FreeMemoryEnd:

