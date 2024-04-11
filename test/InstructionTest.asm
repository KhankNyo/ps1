;.littleEndian

SYS_WRITESTR    = 0x7000_0000
SYS_WRITEHEX    = 0x7001_0000
SYS_EXIT        = 0x7200_0000

RESET_VEC       = 0xBFC0_0000
START           = 0xBFC0_0200
BREAK_VEC       = 0x8000_0040
EXCEPTION_VEC   = 0x8000_0080
MEMORY_SIZE     = 4096


; prerequisite: 
;   bne, beq 
;   j,   jal, 
;   lui, ori
.jumpNop 0
.branchNop 0
.org RESET_VEC
    j Reset1                    ; NOTE: jump to delay slot
Reset1:
    nop
    beq $zero, $zero, Reset2    ; NOTE: branch to delay slot
Reset2:
    nop
    jal Reset3                  ; NOTE: jump to delay slot, saving the addr of isntruction after 
Reset3:
    lui $t0, Reset_LinkAddr >> 16
Reset_LinkAddr:
    ori $t0, Reset_LinkAddr & 0xFFFF
    bne $t0, $ra, ResetTrap     ; assert $ra == Reset_LinkAddr
    nop
    j START                     ; Start: testing other instructions
ResetTrap: 
    la $t0, TestPrerequisite_Failed_Msg
    li $t1, SYS_WRITESTR
    beq $zero, $zero, ResetTrap
    sw $t0, 0($t1)              ; the fail message will be printed repeatedly 
.jumpNop 1
.branchNop 1
ResetEnd:
.resv (BREAK_VEC - 0x8000_0000) - (ResetEnd - RESET_VEC)




; Break exception routine at 0x8000_0040, 2 instructions (8 bytes)
; destroys: none
.jumpNop 1
.org BREAK_VEC
    j EXCEPTION_VEC
    ; jump delay slot handled by assembler 
BreakEnd:
.jumpNop 1
.resv EXCEPTION_VEC - BreakEnd




; Exception handling routine at 0x8000_0080, 31 instructions (124 bytes)
; destroys k0, k1
.jumpNop 0
.branchNop 0
.loadNop 1
.org EXCEPTION_VEC
    ; see if exception is enabled
    la $k1, TestExcept_Enable
    lw $k1, 0($k1)
        mfc0 $k0, $cause                    ; load exception code while waiting for memory load to finish
    bez $k1, Exception_Trap                 ; infinite loop trap if exception has not been enabled
        ; delay slot

    ; get the exception code 
    andi $k0, 0x1F << 2  

    ; conveniently, the exception code starts at bit 2. Meaning that it is already multiplied by 4, 
    ; so we can just use it as an offset into the exception counter location 
    la $k1, TestExceptCounter_Base
    addu $k1, $k0

    ; increment the exception counter
    lw $k0, 0($k1)                          ; delay slot handled by the assembler 
    addiu $k0, 1
    sw $k0, 0($k1)

    ; now patch the opcode, check if it's in a branch delay slot
    mfc0 $k0, $cause
    bltz $k0, Exception_InBranchDelaySlot   ; BD is bit 31, which is the sign bit, Cause will be < 0 if BD is set
        mfc0 $k0, $epc                      ; get the faulting instruction addr
        j Exception_DonePatching
            sw $zero, 0($k0)                ; patch the instruction with a nop
Exception_InBranchDelaySlot:
    sw $zero, 4($k0)                        ; patch the ins in the delay slot with a nop
Exception_DonePatching:
    rfe                                     ; restore kernel and interrupt pending flags (untested)
    jr $k0
Exception_Trap:
    la $k0, PrintStr
    la $k1, PrintHex
    ; print unexpected exception msg
    lui $a0, Exception_Unexpected_Msg >> 16
    jr $k0
        ori $a0, Exception_Unexpected_Msg & 0xFFFF
    ; prints epc 
    jr $k1
        mfc0 $a0, $epc
Exception_TrapInf:
    bra Exception_TrapInf
        nop
.loadNop 1
.jumpNop 1
.branchNop 1
ExceptionEnd:
.resv (START - RESET_VEC) - (ExceptionEnd - 0x8000_0000)
    



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

    la $a0, TestStatus_Arith_Msg
    jal PrintStr
    jal TestArith
    bnz $v0, Fail
    la $a0, TestStatus_Ok_Msg
    jal PrintStr

    la $a0, TestStatus_HiLo_Msg
    jal PrintStr
    jal TestHiLo
    bnz $v0, Fail
    la $a0, TestStatus_Ok_Msg
    jal PrintStr

    la $a0, TestStatus_Exception_Msg
    jal PrintStr
    jal TestException
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
    move $v0, $zero
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
    lb $t0, 0($at)
        nop
        bne $t0, $t9, TestFailed    ; delay slot over, value should be loaded by now

    lb $t0, 1($at)
        bne $t0, $t9, TestFailed
    lb $t0, 1($at)
        nop
        bne $t0, $t8, TestFailed

    lb $t0, 2($at)
        bne $t0, $t8, TestFailed
    lb $t0, 2($at)
        nop
        bne $t0, $t7, TestFailed

    lb $t0, 3($at)
        bne $t0, $t7, TestFailed
    lb $t0, 3($at)
        nop
        bne $t0, $t6, TestFailed

    lb $t0, 4($at)
        bne $t0, $t6, TestFailed
    lb $t0, 4($at)
        nop
    bne $t0, $t5, TestFailed
        ; delay slot

TestLoad_LbDelaySlotWrite:
    lb $t0, 0($at)
    la $a1, TestLoad_DelaySlotWrite_Msg
    la $a2, TestLoad_LbDelaySlotWrite
        or $t0, $t1, $zero          ; NOTE: despite delay slot, writes should be in order
    bne $t0, $t1, TestFailed
        ; delay slot

    ; test lh (signed)
TestLoad_Lh:
    la $at, TestLoad_Half
    la $a2, TestLoad_Lh
    la $a1, TestLoad_DelaySlotRead_Msg
    la $a0, TestLoad_Lh_Msg
    li $t7, -0x8000
    li $t6, 0x7FFF

    move $t0, $t1
    lh $t0, 0($at)
        bne $t0, $t1, TestFailed    ; NOTE: in delay slot
    lh $t0, 0($at)
        nop 
        bne $t0, $t9, TestFailed

    lh $t0, 2($at)
        bne $t0, $t9, TestFailed
    lh $t0, 2($at)
        nop
        bne $t0, $t8, TestFailed

    lh $t0, 4($at)
        bne $t0, $t8, TestFailed
    lh $t0, 4($at)
        nop
        bne $t0, $t7, TestFailed

    lh $t0, 6($at)
        bne $t0, $t7, TestFailed
    lh $t0, 6($at)
        nop
        bne $t0, $t6, TestFailed

    lh $t0, 8($at)
        bne $t0, $t6, TestFailed
    lh $t0, 8($at)
        nop
    bne $t0, $t5, TestFailed
        nop

TestLoad_LhDelayWrite:
    la $a1, TestLoad_DelaySlotWrite_Msg
    la $a2, TestLoad_LhDelayWrite
    lh $t0, 0($at)
        or $t0, $t1, $zero          ; NOTE: despite delay slot, writes should be in order
    bne $t0, $t1, TestFailed
        ; delay slot

    ; test lw (signed)
TestLoad_Lw:
    la $at, TestLoad_Word
    la $a2, TestLoad_Lw
    la $a1, TestLoad_DelaySlotRead_Msg
    la $a0, TestLoad_Lw_Msg
    li $t7, -0x8000_0000
    la $t6,  0x7FFF_FFFF

    move $t0, $t1
    lw $t0, 0($at)
        bne $t0, $t1, TestFailed    ; NOTE: in delay slot
    lw $t0, 0($at)
        nop
        bne $t0, $t9, TestFailed

    lw $t0, 4($at)
        bne $t0, $t9, TestFailed
    lw $t0, 4($at)
        nop
        bne $t0, $t8, TestFailed

    lw $t0, 8($at)
        bne $t0, $t8, TestFailed
    lw $t0, 8($at)
        nop
        bne $t0, $t7, TestFailed

    lw $t0, 12($at)
        bne $t0, $t7, TestFailed
    lw $t0, 12($at)
        nop
        bne $t0, $t6, TestFailed

    lw $t0, 16($at)
        bne $t0, $t6, TestFailed
    lw $t0, 16($at)
        nop
    bne $t0, $t5, TestFailed
        nop

TestLoad_LwDelayWrite:
    la $a1, TestLoad_DelaySlotWrite_Msg
    la $a2, TestLoad_LwDelayWrite
    lw $t0, 0($at)
        or $t0, $t1, $zero          ; NOTE: despite delay slot, writes should be in order
    bne $t0, $t1, TestFailed
        ; delay slot

    ; test lbu
TestLoad_Lbu:
    li $t6, 0x7F
    la $a2, TestLoad_Lbu
    la $a1, TestLoad_DelaySlotRead_Msg
    la $a0, TestLoad_Lbu_Msg
    la $at, TestLoad_Byte
    li $t7, 0x80
    li $t8, 0xFF

    move $t0, $t1
    lbu $t0, 0($at)
        bne $t0, $t1, TestFailed    ; NOTE: in delay slot
    lbu $t0, 0($at)
        nop
        bne $t0, $t9, TestFailed
        
    lbu $t0, 1($at)
        bne $t0, $t9, TestFailed
    lbu $t0, 1($at)
        nop
        bne $t0, $t8, TestFailed

    lbu $t0, 2($at)
        bne $t0, $t8, TestFailed
    lbu $t0, 2($at)
        nop
        bne $t0, $t7, TestFailed

    lbu $t0, 3($at)
        bne $t0, $t7, TestFailed
    lbu $t0, 3($at)
        nop
        bne $t0, $t6, TestFailed

    lbu $t0, 4($at)
        bne $t0, $t6, TestFailed
    lbu $t0, 4($at)
        nop
    bne $t0, $t5, TestFailed

TestLoad_LbuDelaySlotWrite:
    lbu $t0, 0($at)
    la $a1, TestLoad_DelaySlotWrite_Msg
    la $a2, TestLoad_LbuDelaySlotWrite
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
    lhu $t0, 0($at)
        nop
        bne $t0, $t9, TestFailed

    lhu $t0, 2($at)
        bne $t0, $t9, TestFailed
    lhu $t0, 2($at)
        nop
        bne $t0, $t8, TestFailed

    lhu $t0, 4($at)
        bne $t0, $t8, TestFailed
    lhu $t0, 4($at)
        nop
        bne $t0, $t7, TestFailed

    lhu $t0, 6($at)
        bne $t0, $t7, TestFailed
    lhu $t0, 6($at)
        nop
        bne $t0, $t6, TestFailed

    lhu $t0, 8($at)
        bne $t0, $t6, TestFailed
    lhu $t0, 8($at)
        nop
    bne $t0, $t5, TestFailed
        ; delay slot

TestLoad_LhDelaySlotWrite:
    lhu $t0, 0($at)
    la $a1, TestLoad_DelaySlotWrite_Msg
    la $a2, TestLoad_LhDelaySlotWrite
        or $t0, $t1, $zero          ; NOTE: despite delay slot, writes should be in order
    bne $t0, $t1, TestFailed
        ; delay slot

    ; test lwl
TestLoad_Lwl:
    la $at, TestLoad_WordDir
    la $a2, TestLoad_Lwl
    la $a1, TestLoad_DirFailed_Msg
    la $a0, TestLoad_Lwl_Msg

    la $t9, 0xdeadbeef
    la $t8, 0xadbeef77
    la $t7, 0xbeef7777
    la $t6, 0xef777777
    li $t0, 0x77777777
    move $t1, $t0
    lwl $t0, 0($at)
        bne $t0, $t1, TestFailed    ; NOTE: in delay slot 
    lwl $t0, 0($at)
        nop
        bne $t0, $t6, TestFailed

    lwl $t0, 1($at)
        bne $t0, $t6, TestFailed
    lwl $t0, 1($at)
        nop
        bne $t0, $t7, TestFailed

    lwl $t0, 2($at)
        bne $t0, $t7, TestFailed
    lwl $t0, 2($at)
        nop
        bne $t0, $t8, TestFailed

    lwl $t0, 3($at)
        bne $t0, $t8, TestFailed
    lwl $t0, 3($at)
        nop
    bne $t0, $t9, TestFailed
        ; nop

TestLoad_LwlDelaySlotWrite:
    li $t1, 0x1337C0DE
    la $a1, TestLoad_DelaySlotWrite_Msg
    la $a2, TestLoad_LwlDelaySlotWrite
    lwl $t0, 2($at)
        or $t0, $zero, $t1          ; NOTE: should overwrite loaded content
    bne $t0, $t1, TestFailed
        ; delay slot

    ; test lwr
TestLoad_Lwr:
    la $a0, TestLoad_Lwl_Msg
    la $at, TestLoad_WordDir
    la $a2, TestLoad_Lwl
    la $a1, TestLoad_DirFailed_Msg

    la $t9, 0xdeadbeef
    la $t8, 0x77deadbe
    la $t7, 0x7777dead
    la $t6, 0x777777de
    li $t0, 0x77777777
    move $t1, $t0
    lwr $t0, 3($at)
        bne $t0, $t1, TestFailed    ; NOTE: in delay slot 
    lwr $t0, 3($at)
        nop
        bne $t0, $t6, TestFailed

    lwr $t0, 2($at)
        bne $t0, $t6, TestFailed
    lwr $t0, 2($at)
        nop
        bne $t0, $t7, TestFailed

    lwr $t0, 1($at)
        bne $t0, $t7, TestFailed
    lwr $t0, 1($at)
        nop
        bne $t0, $t8, TestFailed

    lwr $t0, 0($at)
        bne $t0, $t8, TestFailed
    lwr $t0, 0($at)
        nop
    bne $t0, $t9, TestFailed
        ; delay slot

TestLoad_LwrDelaySlotWrite:
    li $t1, 0x1337C0DE
    la $a1, TestLoad_DelaySlotWrite_Msg
    la $a2, TestLoad_LwrDelaySlotWrite
    lwr $t0, 2($at)
        or $t0, $zero, $t1          ; NOTE: should overwrite loaded content
    bne $t0, $t1, TestFailed
        ; delay slot

    ; test lwl, lwr combo
TestLoad_Combo:
    la $t1, TestLoad_WordDir
    la $a2, TestLoad_Combo
    la $a1, Test_Failed_Msg
    la $a0, TestLoad_Combo_Msg
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
        swr $t3, 0($t2)                 ; [DD CC BB AA] 55 66 77 88
    bne $t5, $t1, TestFailed
        ; delay slot doesn't matter
 
    lw $t4, 0($t2)
    lw $t5, 4($t2)
    bne $t4, $t3, TestFailed
        nop
    bne $t5, $t1, TestFailed
        ; delay slot 

TestStore_Swl:
    la $t3, 0xAA_BB_CC_DD
    la $t6, 0x88_AA_BB_CC
    la $t7, 0x88_77_AA_BB
    la $t8, 0x88_77_66_AA

    la $a0, TestStore_Swl_Msg
    la $a1, Test_Failed_Msg
    la $a2, TestStore_Swl

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



.branchNop 0
TestArith:
    ; ori
TestArith_Ori:
    la $a2, TestArith_Ori
    la $a1, TestArith_ZeroExtend_Msg
    la $a0, TestArith_Ori_Msg
    ori $t0, $zero, 0x8000              ; 0x0000_8000
    blez $t0, TestFailed                ; assert t0 > 0
        ; delay slot shouldn't matter
    ori $t0, $t0, 0x7FFF                ; 0x0000_FFFF
    la $a1, Test_Failed_Msg
    ori $t1, $zero, 0xFFFF              ; 0x0000_FFFF
    bne $t0, $t1, TestFailed            ; assert equal 
        ; delay slot doesn't matter
TestArith_Ori_R0:
    ori $zero, $t0, 0xFFFF
    la $a2, TestArith_Ori_R0
    la $a1, Test_R0_Msg
    li $t0, 0
    bne $zero, $t0, TestFailed
        ; delay slot doesn't matter

    ; lui
TestArith_Lui:
    ori $t0, $zero, 0xFFFF              ; 0x0000_FFFF
    la $a2, TestArith_Lui
    la $a1, TestArith_ClearLowerFailed_Msg
    la $a0, TestArith_Lui_Msg
    lui $t0, 0                          ; 0x0000_0000
    bnz $t0, TestFailed
        ; delay slot doesn't matter 
    lui $t0, 0x8000                     ; 0x8000_0000
    la $a1, TestArith_SignExtend_Msg
    bgez $t0, TestFailed                ; assert sign bit 
        ; delay slot doesn't matter
TestArith_Lui_R0:
    lui $zero, 0xFFFF
    la $a2, TestArith_Lui_R0
    la $a1, Test_R0_Msg
    li $t0, 0
    bne $zero, $t0, TestFailed
        ; delay slot doesn't matter

    ; addiu
TestArith_Addiu:
    lui $t0, 0xFFFF                     ; 0xFFFF_0000
    la $a0, TestArith_Addiu_Msg
    la $a1, TestArith_ZeroExtend_Msg
    la $a2, TestArith_Addiu
    addiu $t0, $zero, 0x7FFF            ; 0x0000_7FFF
    blez $t0, TestFailed
        ; delay slot shouldn't matter
    addiu $t0, $zero, -0x8000           ; 0xFFFF_8000
    la $a1, TestArith_SignExtend_Msg
    bgez $t0, TestFailed
        ; delay slot shouldn't matter
TestArith_Addiu_R0:
    addiu $zero, $t0, 1
    la $a2, TestArith_Addiu_R0
    la $a1, Test_R0_Msg
    li $t0, 0
    bne $zero, $t0, TestFailed
        ; delay slot doesn't matter

    ; addu
TestArith_Addu:
    li $t0, -0x8000
    move $t4, $t0
    ori $t1, $zero, 0x8420              ; 0x0000_8420
    move $t5, $t1
    la $a0, TestArith_Addu_Msg
    la $a1, Test_Failed_Msg
    la $a2, TestArith_Addu
    addu $t2, $t1, $t0                  ; t2 = 0x0420, t1 stays, t0 stays
    ori $t3, $zero, 0x0420
    bne $t2, $t3, TestFailed
        nop
    bne $t1, $t5, TestFailed
        nop
    bne $t0, $t4, TestFailed
        ; delay slot shouldn't matter
TestArith_Addu_R0:
    addu $zero, $t1, $t1
    la $a2, TestArith_Addu_R0
    la $a1, Test_R0_Msg
    li $t0, 0
    bne $zero, $t0, TestFailed
        ; delay slot shouldn't matter

    ; subu
TestArith_Subu:
    la $a0, TestArith_Subu_Msg
    la $a1, Test_Failed_Msg
    la $a2, TestArith_Subu
    li $t0, -0x8000_0000
    li $t1, 1
    li $t5, 0x7FFF_FFFF
    move $t3, $t0
    move $t4, $t1
    subu $t2, $t0, $t1
    bne $t2, $t5, TestFailed
        nop
    bne $t0, $t3, TestFailed
        nop
    bne $t1, $t4, TestFailed
TestArith_Subu_R0:
    subu $zero, $t0, $t1
    la $a1, Test_R0_Msg
    la $a2, TestArith_Subu_R0
    li $t0, 0
    bne $zero, $t0, TestFailed

    ; andi
TestArith_Andi:
    ori $t1, $zero, 0xFFFF
    li $t0, -1
    andi $t0, $t0, 0xFFFF               ; 0x0000_FFFF
    la $a0, TestArith_Andi_Msg
    la $a1, TestArith_ZeroExtend_Msg
    la $a2, TestArith_Andi
    bne $t0, $t1, TestFailed
        ; delay slot shouldn't matter
TestArith_Andi_R0:
    ori $t0, $zero, 0xFFFF
    andi $zero, $t0, 0x0FF0
    la $a1, Test_R0_Msg
    la $a2, TestArith_Andi_R0
    li $t0, 0
    bne $zero, $t0, TestFailed
        ; delay slot shouldn't matter
    
    ; and 
TestArith_And:
    la $t0, 0xF0F0F23F
    move $t3, $t0
    la $t1, 0x10200001
    move $t4, $t1
    and $t2, $t1, $t0
    li $t5, 0x10200001
    la $a0, TestArith_And_Msg
    la $a1, Test_Failed_Msg
    la $a2, TestArith_And
    bne $t5, $t2, TestFailed
        nop 
    bne $t4, $t1, TestFailed
        nop
    bne $t3, $t0, TestFailed
        ; delay slot shouldn't matter
TestArith_And_R0:
    li $t0, 0x0FF0F0F0
    li $t1, -1
    and $zero, $t0, $t1
    la $a1, Test_R0_Msg
    la $a2, TestArith_And_R0
    li $t0, 0
    bne $zero, $t0, TestFailed
        ; delay slot shoudn't matter

    ; xori  
TestArith_Xori:
    la $t0, 0x1234FF00
    xori $t1, $t0, 0xFFFF               ; 0x123400FF
    la $t2, 0x123400FF
    la $a0, TestArith_Xori_Msg
    la $a1, TestArith_ZeroExtend_Msg
    la $a2, TestArith_Xori
    bne $t1, $t2, TestFailed
        ; delay slot shouldn't matter
TestArith_Xori_R0:
    xori $zero, $zero, 0xFFFF
    la $a1, Test_R0_Msg
    la $a2, TestArith_Xori_R0
    li $t0, 0
    bne $zero, $t0, TestFailed
        ; delay slot shouldn't matter

    ; xor
TestArith_Xor:
    li $t0, -1
    li $t1, 0
    move $t3, $t0
    move $t4, $t1
    xor $t2, $t0, $t1                   ; 0xFFFF_FFFF ^ 0x0000_0000
    li $t5, -1
    la $a0, TestArith_Xor_Msg
    la $a1, Test_Failed_Msg
    la $a2, TestArith_Xor
    bne $t5, $t2, TestFailed
        nop
    bne $t4, $t1, TestFailed
        nop
    bne $t3, $t0, TestFailed
        ; delay slot shouldn't matter
TestArith_Xor_R0:
    li $t0, -1
    li $t1, 0
    xor $zero, $t0, $t1
    la $a1, Test_R0_Msg
    la $a2, TestArith_Xor_R0
    li $t0, 0
    bne $zero, $t0, TestFailed
        ; delay slot shouldn't matter

    ; or
TestArith_Or:
    la $a0, TestArith_Or_Msg
    la $a1, Test_Failed_Msg
    la $a2, TestArith_Or
    la $t0, 0xFF00_0F0F
    la $t1, 0x0F0F_F0F0
    move $t3, $t0
    move $t4, $t1
    or $t2, $t1, $t0
    la $t5, 0xFF0F_FFFF
    bne $t2, $t5, TestFailed
        nop
    bne $t1, $t4, TestFailed
        nop
    bne $t0, $t3, TestFailed
        ; delay slot shouldn't matter
TestArith_Or_R0:
    or $zero, $t0, $t1
    la $a1, Test_R0_Msg
    la $a2, TestArith_Or_R0
    li $t0, 0
    bne $zero, $t0, TestFailed
        ; delay slot shouldn't matter

TestArith_Nor:
    la $t0, 0xFF00_FF00
    la $t1, 0x0FF0_0FF0
    move $t3, $t0
    move $t4, $t1
    la $t5, 0x000F_000F
    nor $t2, $t1, $t0
    la $a0, TestArith_Nor_Msg
    la $a1, Test_Failed_Msg
    la $a2, TestArith_Nor
    bne $t2, $t5, TestFailed
        nop
    bne $t1, $t4, TestFailed
        nop
    bne $t0, $t3, TestFailed
        ; delay slot shouldn't matter
TestArith_Nor_R0:
    nor $zero, $t0, $t1
    la $a1, Test_R0_Msg
    la $a2, TestArith_Nor_R0
    li $t0, 0
    bne $zero, $t0, TestFailed
        ; delay slot shouldn't matter

    ; slt*
    ; 0x800_0000, 0xFFFF_FFFF, 0, 1
    li $t0, -0x8000
    li $t1, -1
    li $t2, 0
    li $t3, 1
TestArith_Sltiu:
    la $a0, TestArith_Sltiu_Msg
    la $a1, Test_Failed_Msg
    la $a2, TestArith_Sltiu
    sltiu $t4, $t0, -0x8000     ; 0x8000 < 0x8000?
    bnz $t4, TestFailed         ; no
    sltiu $t4, $t0, -1          ; 0x8000 < 0xFFFF?
    bez $t4, TestFailed         ; yes
    sltiu $t4, $t0, 0           ; 0x8000 < 0?
    bnz $t4, TestFailed         ; no
    sltiu $t4, $t0, 1           ; 0x8000 < 1?
    bnz $t4, TestFailed         ; no

    sltiu $t4, $t1, -0x8000     ; 0xFFFF < 0x8000?
    bnz $t4, TestFailed         ; no
    sltiu $t4, $t1, -1          ; 0xFFFF < 0xFFFF?
    bnz $t4, TestFailed         ; no
    sltiu $t4, $t1, 0           ; 0xFFFF < 0?
    bnz $t4, TestFailed         ; no
    sltiu $t4, $t1, 1           ; 0xFFFF < 1?
    bnz $t4, TestFailed         ; no

    sltiu $t4, $t2, -0x8000     ; 0 < 0x8000?
    bez $t4, TestFailed         ; yes
    sltiu $t4, $t2, -1          ; 0 < 0xFFFF?
    bez $t4, TestFailed         ; yes
    sltiu $t4, $t2, 0           ; 0 < 0?
    bnz $t4, TestFailed         ; no
    sltiu $t4, $t2, 1           ; 0 < 1?
    bez $t4, TestFailed         ; yes

    sltiu $t4, $t3, -0x8000     ; 1 < 0x8000?
    bez $t4, TestFailed         ; yes
    sltiu $t4, $t3, -1          ; 1 < 0xFFFF?
    bez $t4, TestFailed         ; yes
    sltiu $t4, $t3, 0           ; 1 < 0?
    bnz $t4, TestFailed         ; no
    sltiu $t4, $t3, 1           ; 1 < 1?
    bnz $t4, TestFailed         ; no
        ; delay slot shouldn't matter
TestArith_Sltiu_R0:
    sltiu $zero, $zero, 1
    la $a1, Test_R0_Msg
    la $a2, TestArith_Sltiu_R0
    li $t5, 0
    bne $zero, $t5, TestFailed
        ; delay slot shouldn't matter

TestArith_Slti:
    slti $t4, $t0, -0x8000      ; -0x8000 < -0x8000?
    la $a0, TestArith_Slti_Msg
    la $a1, Test_Failed_Msg
    la $a2, TestArith_Slti
    bnz $t4, TestFailed         ; no
    slti $t4, $t0, -1           ; -0x8000 < -1?
    bez $t4, TestFailed         ; yes
    slti $t4, $t0, 0            ; -0x8000 < 0?
    bez $t4, TestFailed         ; yes
    slti $t4, $t0, 1            ; -0x8000 < 1?
    bez $t4, TestFailed         ; yes

    slti $t4, $t1, -0x8000      ; -1 < -0x8000?
    bnz $t4, TestFailed         ; no
    slti $t4, $t1, -1           ; -1 < -1?
    bnz $t4, TestFailed         ; no
    slti $t4, $t1, 0            ; -1 < 0?
    bez $t4, TestFailed         ; yes
    slti $t4, $t1, 1            ; -1 < 1?
    bez $t4, TestFailed         ; yes

    slti $t4, $t2, -0x8000      ; 0 < -0x8000?
    bnz $t4, TestFailed         ; no
    slti $t4, $t2, -1           ; 0 < -1?
    bnz $t4, TestFailed         ; no
    slti $t4, $t2, 0            ; 0 < 0?
    bnz $t4, TestFailed         ; no
    slti $t4, $t2, 1            ; 0 < 1?
    bez $t4, TestFailed         ; yes

    slti $t4, $t3, -0x8000      ; 1 < -0x8000?
    bnz $t4, TestFailed         ; no
    slti $t4, $t3, -1           ; 1 < -1?
    bnz $t4, TestFailed         ; no
    slti $t4, $t3, 0            ; 1 < 0?
    bnz $t4, TestFailed         ; no
    slti $t4, $t3, 1            ; 1 < 1?
    bnz $t4, TestFailed         ; no
        ; delay slot shouldn't matter
TestArith_Slti_R0:
    slti $zero, $zero, 1
    la $a1, Test_R0_Msg
    la $a2, TestArith_Slti_R0
    li $t5, 0
    bne $zero, $t5, TestFailed
        ; delay slot shouldn't matter

TestArith_Sltu:
    sltu $t4, $t0, $t0          ; 0x8000 < 0x8000?
    la $a0, TestArith_Sltu_Msg
    la $a1, Test_Failed_Msg
    la $a2, TestArith_Sltu
    bnz $t4, TestFailed         ; no
    sltu $t4, $t0, $t1          ; 0x8000 < 0xFFFF?
    bez $t4, TestFailed         ; yes
    sltu $t4, $t0, $t2          ; 0x8000 < 0?
    bnz $t4, TestFailed         ; no
    sltu $t4, $t0, $t3          ; 0x8000 < 1?
    bnz $t4, TestFailed         ; no

    sltu $t4, $t1, $t0          ; 0xFFFF < 0x8000?
    bnz $t4, TestFailed         ; no
    sltu $t4, $t1, $t1          ; 0xFFFF < 0xFFFF?
    bnz $t4, TestFailed         ; no
    sltu $t4, $t1, $t2          ; 0xFFFF < 0?
    bnz $t4, TestFailed         ; no
    sltu $t4, $t1, $t3          ; 0xFFFF < 1?
    bnz $t4, TestFailed         ; no

    sltu $t4, $t2, $t0          ; 0 < 0x8000?
    bez $t4, TestFailed         ; yes
    sltu $t4, $t2, $t1          ; 0 < 0xFFFF?
    bez $t4, TestFailed         ; yes
    sltu $t4, $t2, $t2          ; 0 < 0?
    bnz $t4, TestFailed         ; no
    sltu $t4, $t2, $t3          ; 0 < 1?
    bez $t4, TestFailed         ; yes

    sltu $t4, $t3, $t0          ; 1 < 0x8000?
    bez $t4, TestFailed         ; yes
    sltu $t4, $t3, $t1          ; 1 < 0xFFFF?
    bez $t4, TestFailed         ; yes
    sltu $t4, $t3, $t2          ; 1 < 0?
    bnz $t4, TestFailed         ; no
    sltu $t4, $t3, $t3          ; 1 < 1?
    bnz $t4, TestFailed         ; no
        ; delay slot shouldn't matter
TestArith_Sltu_R0:
    sltu $zero, $zero, $t3
    la $a1, Test_R0_Msg
    la $a2, TestArith_Sltu_R0
    li $t5, 0
    bne $zero, $t5, TestFailed
        ; delay slot shouldn't matter

TestArith_Slt:
    slt $t4, $t0, $t0           ; -0x8000 < -0x8000?
    la $a0, TestArith_Slt_Msg
    la $a1, Test_Failed_Msg
    la $a2, TestArith_Slt
    bnz $t4, TestFailed         ; no
    slt $t4, $t0, $t1           ; -0x8000 < -1?
    bez $t4, TestFailed         ; yes
    slt $t4, $t0, $t2           ; -0x8000 < 0?
    bez $t4, TestFailed         ; yes
    slt $t4, $t0, $t3           ; -0x8000 < 1?
    bez $t4, TestFailed         ; yes

    slt $t4, $t1, $t0           ; -1 < -0x8000?
    bnz $t4, TestFailed         ; no
    slt $t4, $t1, $t1           ; -1 < -1?
    bnz $t4, TestFailed         ; no
    slt $t4, $t1, $t2           ; -1 < 0?
    bez $t4, TestFailed         ; yes
    slt $t4, $t1, $t3           ; -1 < 1?
    bez $t4, TestFailed         ; yes

    slt $t4, $t2, $t0           ; 0 < -0x8000?
    bnz $t4, TestFailed         ; no
    slt $t4, $t2, $t1           ; 0 < -1?
    bnz $t4, TestFailed         ; no
    slt $t4, $t2, $t2           ; 0 < 0?
    bnz $t4, TestFailed         ; no
    slt $t4, $t2, $t3           ; 0 < 1?
    bez $t4, TestFailed         ; yes

    slt $t4, $t3, $t0           ; 1 < -0x8000?
    bnz $t4, TestFailed         ; no
    slt $t4, $t3, $t1           ; 1 < -1?
    bnz $t4, TestFailed         ; no
    slt $t4, $t3, $t2           ; 1 < 0?
    bnz $t4, TestFailed         ; no
    slt $t4, $t3, $t3           ; 1 < 1?
    bnz $t4, TestFailed         ; no
        ; delay slot shouldn't matter
TestArith_Slt_R0:
    sltu $zero, $zero, $t3
    la $a1, Test_R0_Msg
    la $a2, TestArith_Slt_R0
    li $t5, 0
    bne $zero, $t5, TestFailed
        ; delay slot shouldn't matter

    ; sll
TestArith_Sll:
    li $t0, 1
    sll $t1, $t0, 1             ; should be 2
    la $a0, TestArith_Sll_Msg
    la $a1, Test_Failed_Msg
    la $a2, TestArith_Sll
    li $t2, 2                   ; check rd
    bne $t1, $t2, TestFailed
    li $t2, 1                   ; check rs
    bne $t0, $t2, TestFailed
    sll $t1, 31                 ; should be 0
    bnz $t1, TestFailed
        ; delay slot 
TestArith_Sll_R0:
    sll $zero, $t0, 1
    la $a1, Test_R0_Msg
    la $a2, TestArith_Sll_R0
    li $t0, 0
    bne $zero, $t0, TestFailed
        ; delay slot

    ; srl
TestArith_Srl:
    lui $t0, 0x8000
    srl $t1, $t0, 1             ; should be 0x4000_0000
    la $a0, TestArith_Srl_Msg
    la $a1, Test_Failed_Msg
    la $a2, TestArith_Srl
    lui $t2, 0x4000              ; check rd
    bne $t1, $t2, TestFailed
    lui $t2, 0x8000              ; check rs
    bne $t0, $t2, TestFailed
    srl $t1, 31                 ; should be 0
    bnz $t1, TestFailed
        ; delay slot
TestArith_Srl_R0:
    srl $zero, $t0, 1
    la $a1, Test_R0_Msg
    la $a2, TestArith_Srl_R0
    li $t0, 0
    bne $zero, $t0, TestFailed
        ; delay slot

    ; sra 
TestArith_Sra:
    lui $t0, 0x8000
    sra $t1, $t0, 1             ; should be 0xC000_0000
    la $a0, TestArith_Sra_Msg
    la $a1, Test_Failed_Msg
    la $a2, TestArith_Sra
    lui $t2, 0xC000
    bne $t1, $t2, TestFailed
    lui $t2, 0x8000
    bne $t0, $t2, TestFailed
    sra $t1, 31                 ; should be -1
    li $t2, -1
    bne $t1, $t2, TestFailed
        ; delay slot
TestArith_Sra_R0:
    sra $zero, $t0, 1
    la $a1, Test_R0_Msg
    la $a2, TestArith_Sra_R0
    li $t0, 0
    bne $zero, $t0, TestFailed
        ; delay slot

    ; sllv
TestArith_Sllv:
    li $t0, 1
    li $t1, 33
    sllv $t2, $t0, $t1          ; should be 2
    la $a0, TestArith_Sllv_Msg
    la $a1, Test_Failed_Msg
    la $a2, TestArith_Sllv
    li $t3, 2
    bne $t3, $t2, TestFailed
    li $t3, 1
    bne $t3, $t0, TestFailed
    li $t1, -1                  ; 31
    sllv $t2, $t1               ; should be 0
    bnz $t2, TestFailed
        ; delay slot
TestArith_Sllv_R0:
    li $t1, 1
    sllv $zero, $t0, $t1
    la $a1, Test_R0_Msg
    la $a2, TestArith_Sllv_R0
    li $t0, 0
    bne $zero, $t0, TestFailed
        ; delay slot

    ; srlv
TestArith_Srlv:
    lui $t0, 0x8000
    li $t1, 1
    srlv $t2, $t0, $t1          ; should be 0x4000_0000
    la $a0, TestArith_Srlv_Msg
    la $a1, Test_Failed_Msg
    la $a2, TestArith_Srlv
    lui $t3, 0x4000
    bne $t2, $t3, TestFailed
    lui $t3, 0x8000
    bne $t0, $t3, TestFailed
    li $t1, 63
    srlv $t2, $t1               ; should be 0
    bnz $t2, TestFailed
        ; delay slot
TestArith_Srlv_R0:
    srlv $zero, $t0, $t1
    la $a1, Test_R0_Msg
    la $a2, TestArith_Srlv_R0
    li $t0, 0
    bne $zero, $t0, TestFailed
        ; delay slot

    ; srav
TestArith_Srav:
    lui $t0, 0x8000
    li $t1, 1
    srav $t2, $t0, $t1          ; should be 0xC000_0000
    la $a0, TestArith_Srav_Msg
    la $a1, Test_Failed_Msg
    la $a2, TestArith_Srav
    lui $t3, 0xC000
    bne $t2, $t3, TestFailed
    li $t1, -1
    srav $t2, $t1               ; should be -1
    li $t3, -1
    bne $t2, $t3, TestFailed
        ; delay slot
TestArith_Srav_R0:
    li $t1, 1
    srav $zero, $t0, $t1
    la $a1, Test_R0_Msg
    la $a2, TestArith_Srav_R0
    li $t0, 0
    bne $zero, $t0, TestFailed
        ; delay slot

    move $v0, $zero
    ret
.branchNop 1


.branchNop 0
TestHiLo:
TestHiLo_MoveHi:
    la $a0, TestHiLo_MoveHi_Msg
    la $a1, Test_Failed_Msg
    la $a2, TestHiLo_MoveHi
    la $t0, 0x11223344
    mthi $t0
    mfhi $t1
    bne $t1, $t0, TestFailed
        ; delay slot
TestHiLo_MoveHi_R0:
    mfhi $zero
    la $a1, Test_R0_Msg
    la $a2, TestHiLo_MoveHi_R0
    li $t0, 0
    bne $zero, $t0, TestFailed
        ; delay slot

TestHiLo_MoveLo:
    la $a0, TestHiLo_MoveLo_Msg
    la $a1, Test_Failed_Msg
    la $a2, TestHiLo_MoveLo
    la $t1, 0x55667788
    mtlo $t0
    mflo $t1
    bne $t0, $t1, TestFailed
        ; delay slot
TestHiLo_MoveLo_R0:
    mflo $zero
    la $a1, Test_R0_Msg
    la $a2, TestHiLo_MoveLo_R0
    li $t0, 0
    bne $zero, $t0, TestFailed
        ; delay slot

TestHiLo_Mult:
    la $a0, TestHiLo_Mult_Msg
    la $a1, Test_Failed_Msg
    la $a2, TestHiLo_Mult
    li $t0, -2
    li $t1, 3
    mult $t0, $t1               ; -2*3
    mfhi $t2
    li $t3, -1
    bne $t3, $t2, TestFailed    ; bits 63..32 = -1
        mflo $t2
    li $t3, -6
    bne $t3, $t2, TestFailed    ; bits 31..0 = -6

    li $t0, -2
    li $t1, -3
    mult $t0, $t1               ; -2*-3
    mfhi $t2
    bnz $t2, TestFailed         ; bits 63..32 = 0
        mflo $t2
    li $t3, 6
    bne $t3, $t2, TestFailed    ; bits 31..0 = 6

    li $t0, 0x7FFF_FFFF
    mult $t0, $t0               ; 0x7FFF_FFFF^2
    mfhi $t1
    li $t2, (0x7FFF_FFFF*0x7FFF_FFFF) >> 32
    bne $t1, $t2, TestFailed
        mflo $t1
    la $t2, (0x7FFF_FFFF*0x7FFF_FFFF) & 0xFFFF_FFFF
    bne $t2, $t1, TestFailed
        ; delay slot

TestHiLo_Multu:
    lui $t0, 0x8000
    la $a0, TestHiLo_Multu_Msg
    la $a1, Test_Failed_Msg
    la $a2, TestHiLo_Multu
    multu $t0, $t0
    mfhi $t1
    la $t2, (0x8000_0000 * 0x8000_0000) >> 32
    bne $t2, $t1, TestFailed
        mflo $t1
    la $t2, (0x8000_0000 * 0x8000_0000) & 0xFFFF_FFFF
    bne $t2, $t1, TestFailed
        ; delay slot

TestHiLo_Div:
    li $t0, 100
    la $a0, TestHiLo_Div_Msg
    la $a1, Test_Failed_Msg
    la $a2, TestHiLo_Div

    li $t1, -10
    div $t0, $t1                ; 100 / -10
    mfhi $t2
    bnz $t2, TestFailed
        mflo $t2
    li $t4, -10
    bne $t2, $t4, TestFailed
        ; delay slot
        
    li $t0, 69
    li $t1, 420
    div $t0, $t1                ; 69 / 420
    mflo $t2
    bnz $t2, TestFailed         ; assert result == 0
        mfhi $t2
    bne $t2, $t0, TestFailed    ; assert remainder == 69
        ; delay slot

    la $t0, 0x7FFF_FFFF
    div $t0, $zero              ; 0x7FFF_FFFF / 0
    la $a1, TestHiLo_Div0Positive_Msg
    li $t2, -1
    mflo $t1
    bne $t1, $t2, TestFailed    ; assert Lo == -1
        mfhi $t1
    bne $t1, $t0, TestFailed    ; assert Hi == Rs
        ; delay slot

    li $t0, -0x8000_0000
    div $t0, $zero             ; -0x8000_0000 / 0
    li $t1, 1
    mflo $t2
    bne $t1, $t2, TestFailed    ; assert lo == 1
        mfhi $t2
    bne $t2, $t0, TestFailed    ; assert hi == src
        ; delay slot

    li $t1, -1
    div $t0, $t1                ; -0x8000_0000 / -1
    la $a1, TestHiLo_Div0Negative_Msg
    mflo $t2
    bne $t2, $t0, TestFailed    ; assert Lo == -0x8000_0000 
        mfhi $t2
    bnz $t2, TestFailed         ; assert Hi == 0
        ; delay slot


TestHiLo_Divu:
    la $t0, 0x8000_0001
    la $a0, TestHiLo_Divu_Msg
    la $a1, Test_Failed_Msg
    la $a2, TestHiLo_Divu

    la $t1, 0x4000_0000
    divu $t0, $t1               ; 0x8000_0001 / 0x4000_0000
    li $t3, 2
    mflo $t2
    bne $t2, $t3, TestFailed    ; assert result == 2
        li $t3, 1
    mfhi $t2
    bne $t2, $t3, TestFailed    ; assert remainder == 1
        ; delay slot
    
    lui $t0, 0x8000
    la $a1, TestHiLo_DivuZeroFailed_Msg
    divu $t0, $zero             ; 0x8000_0000 / 0 
    li $t2, -1
    mflo $t1
    bne $t1, $t2, TestFailed    ; assert result == -1
        mfhi $t1
    bne $t1, $t0, TestFailed    ; assert remainder == rs
        ; delay slot

    li $t0, -1
    divu $t0, $zero             ; 0xFFFF_FFFF / 0
    mflo $t1
    bne $t0, $t1, TestFailed    ; assert result == 0xFFFF_FFFF
        mfhi $t1
    bne $t0, $t1, TestFailed    ; assert remainder == rs
        ; delay slot 

    move $v0, $zero
    ret
.branchNop 1


.branchNop 0
.jumpNop 0
.loadNop 0
TestException: 
    move $t9, $ra                   ; save return addr

    ; enable exception testing 
    li $t0, 1
    la $t1, TestExcept_Enable
    sw $t0, 0($t1)

    ; exception: arith overflow 
    la $a0, TestExcept_Overflow_Msg
    jal PrintStr
        ; delay slot 

    ; test exception: addi
    la $a2, TestException_Arith_Addi
    la $a0, TestArith_Addi_Msg
TestException_Arith_Addi:
    li $t0, 0x7FFF_FFFF
    li $t1, 0x7FFF_FFFF
    move $t5, $zero
    addi $t0, 1                     ; exception should trigger here 
    addiu $t5, 1
    la $a1, TestExcept_PreserveFailed_Msg
    bne $t0, $t1, TestFailed
        move $ra, $t9               ; restore retaddr from the PrintStr call (in delay slot)
    la $t3, TestExceptCounter_Base + TestExceptCounter_Overflow
    lw $t2, 0($t3)
    li $t4, 1
    la $a1, TestExcept_Wrong_Msg
    bne $t2, $t4, TestFailed        ; assert arith exception counter updated
        ; delay slot
    la $a1, TestExcept_DoubleExec_Msg
    bne $t5, $t4, TestFailed        ; assert instruction after exception not being executed (addi)
        sw $zero, 0($t3)            ; restore arith exception counter

    ; test exception: add
TestException_Arith_Add:
        add $t0, $t1                ; exception: NOTE also in branch delay slot (branch cond = false)
    la $a0, TestArith_Add_Msg
    la $a1, TestExcept_PreserveFailed_Msg
    la $a2, TestException_Arith_Add
    bne $t0, $t1, TestFailed        ; assert result not updated
        ; delay slot
    lw $t2, 0($t3)                  ; load arith exception counter
    li $t4, 1
    bne $t2, $t4, TestFailed        ; assert arith exception counter updated
        sw $zero, 0($t3)

    ; test exception: sub (and delay slot)
TestException_Arith_Sub:
    la $a0, TestArith_Sub_Msg
    la $a1, TestExcept_BranchDelay_Msg
    la $a2, TestException_Arith_Sub

    li $t0, 1
    la $t1, 0x8000_0000
    move $t2, $t1
    bra TestException_Arith_BranchLocation
        sub $t1, $t0                ; exception
        bra TestFailed              ; assert delay slot exception
            nop
TestException_Arith_BranchLocation:
    lui $a1, TestExcept_PreserveFailed_Msg >> 16
    bne $t1, $t2, TestFailed        ; assert $t1 being preserved
        ori $a1, TestExcept_PreserveFailed_Msg & 0xFFFF
    j TestException_Arith_JumpLocation
        sub $t1, $t0                ; exception 
        bra TestFailed              ; assert branch delay slot exception
            ; delay slot
TestException_Arith_JumpLocation:
    ; NOTE: not in a branch delay slot, even though the ins before it is a branch
    ; https://devblogs.microsoft.com/oldnewthing/20180416-00/?p=98515
    sub $t1, $t0                    ; exception
    lw $t0, 0($t3)                  ; load exception counter
    li $t1, 3
    la $a1, TestExcept_Wrong_Msg
    bne $t0, $t1, TestFailed        ; assert 3 arith overflow exceptions
        sw $zero, 0($t3)            ; restore arith exception counter
    
    ; print ok msg 
    lui $a0, TestStatus_Ok_Msg >> 16
    jal PrintStr
        ori $a0, TestStatus_Ok_Msg & 0xFFFF

    ; print addr load test msg
    lui $a0, TestExcept_AddrLoad_Msg >> 16
    jal PrintStr
        ori $a0, TestExcept_AddrLoad_Msg & 0xFFFF

    ; test exception: lw
    la $a0, TestLoad_Lw_Msg
    la $a2, TestException_LwUnaligned
TestException_LwUnaligned:
    la $t0, FreeMemorySection                                   ; $t0: load addr
    la $t1, TestExceptCounter_Base + TestExceptCounter_AddrLoad ; $t1: exception counter
    la $t7, TestException_Lw_Ins        ; repatch addr
    lw $t8, 0($t7)                      ; load the instruction to be repatched

    sw $zero, 0($t0)                    ; zero the location
    sw $zero, 4($t0)
    li $t6, 3
TestException_LwUnaligned_Again:
        li $t2, 0x420
        move $t3, $t2
        move $t4, $zero
TestException_Lw_Ins:
        lw $t2, 1($t0)                  ; exception here
            addiu $t4, 1                ; should only be executed once
        lw $t5, 0($t1)                  ; load counter
        li $t6, 1
        la $a1, TestExcept_Wrong_Msg
        bne $t6, $t5, TestFailed        ; assert counter updated
            move $ra, $t9               ; restore addr from PrintStr call (in delay slot)
        la $a1, TestExcept_PreserveFailed_Msg
        bne $t3, $t2, TestFailed        ; assert value not being loaded
            li $t5, 1
        la $a1, TestExcept_DoubleExec_Msg
        bne $t4, $t5, TestFailed        ; assert next instruction not being executed
            ; delay slot
        addiu $t0, 1                    ; else inc load addr 
        sw $t8, 0($t7)                  ; repatch the instruction (patched out by the exception handler)
        addiu $t6, -1
    bnz $t6, TestException_LwUnaligned_Again
        sw $zero, 0($t1)                ; reset load exception counter (branch delay slot)

    ; test exception: lh
    la $a0, TestLoad_Lh_Msg
    la $a2, TestException_LhUnaligned
TestException_LhUnaligned:
    la $t0, FreeMemorySection                                   ; $t0: load addr
    ; la $t1, TestExceptCounter_Base + TestExceptCounter_AddrLoad ; $t1: exception counter, same as lw 

    sw $zero, 0($t0)                ; zero the location
    li $t2, 0x420
    move $t3, $t2
    move $t4, $zero
    lh $t2, 1($t0)                  ; exception here
        addiu $t4, 1                ; should only be executed once
    lw $t5, 0($t1)                  ; load exception counter
    li $t6, 1
    la $a1, TestExcept_Wrong_Msg
    bne $t6, $t5, TestFailed        ; assert counter updated by exception routine 
        move $ra, $t9               ; restore addr from PrintStr call (in delay slot)
    la $a1, TestExcept_PreserveFailed_Msg
    bne $t3, $t2, TestFailed        ; assert value not being loaded
        li $t5, 1
    la $a1, TestExcept_DoubleExec_Msg
    bne $t4, $t5, TestFailed        ; assert next instruction not being executed
        sw $zero, 0($t1)            ; reset load exception counter

    ; test exception: lhu 
    la $a1, TestLoad_Lhu_Msg
    la $a2, TestException_LhuUnaligned
TestException_LhuUnaligned:
    ; la $t0, FreeMemorySection                                   ; $t0: load addr, same as lh
    ; la $t1, TestExceptCounter_Base + TestExceptCounter_AddrLoad ; $t1: exception counter, same as lh

    sw $zero, 0($t0)                ; zero the location
    li $t2, 0x420
    move $t3, $t2
    move $t4, $zero
    lhu $t2, 1($t0)                 ; exception here
        addiu $t4, 1                ; should only be executed once
    lw $t5, 0($t1)                  ; load exception counter
    li $t6, 1
    la $a1, TestExcept_Wrong_Msg
    bne $t6, $t5, TestFailed        ; assert counter updated by exception routine 
        move $ra, $t9               ; restore addr from PrintStr call (in delay slot)
    la $a1, TestExcept_PreserveFailed_Msg
    bne $t3, $t2, TestFailed        ; assert value not being loaded
        ; delay slot
    li $t5, 1
    la $a1, TestExcept_DoubleExec_Msg
    bne $t4, $t5, TestFailed        ; assert next instruction not being executed
        sw $zero, 0($t0)            ; reset load exception counter

    ; print Ok msg
    lui $a0, TestStatus_Ok_Msg >> 16
    jal PrintStr
        ori $a0, TestStatus_Ok_Msg & 0xFFFF

    ; print test store exception
    lui $a0, TestExcept_AddrStore_Msg >> 16
    jal PrintStr
        ori $a0, TestExcept_AddrStore_Msg & 0xFFFF

    ; test exception: sw
    la $t0, FreeMemorySection                                     ; $t0: load/store addr, same as lhu
TestException_SwUnaligned:
    la $t1, TestExceptCounter_Base + TestExceptCounter_AddrStore    ; $t1: store exception counter 
    la $a0, TestStore_Sw_Msg
    la $a2, TestException_SwUnaligned

    ; zero the location 
    sw $zero, 0($t0)
    sw $zero, 4($t0)

    li $t6, 3
    la $t8, TestException_Sw_Ins
    lw $t7, 0($t8)                      ; load the instruction to patch
TestException_Sw_Again:
        li $t2, 0x6969
        move $t3, $zero
TestException_Sw_Ins:
        sw $t2, 1($t0)                  ; exception here
            addiu $t3, 1
        lw $t4, 0($t0)
        la $a1, TestExcept_PreserveFailed_Msg
        bnz $t4, TestFailed             ; assert no memory write
            move $ra, $t9               ; restore return addr from PrintStr in delay slot
        lw $t4, 4($t0)
            li $t5, 1                   ; preload testing value while waiting in delay slot
        bnz $t4, TestFailed             ; assert no memory write 
            ; delay slot
        lw $t4, 0($t1)                  ; preload the exception counter
        la $a1, TestExcept_DoubleExec_Msg
        bne $t3, $t5, TestFailed        ; assert next instruction only executed once
            ; delay slot
        li $t3, 1                       ; expected exception counter value
        la $a1, TestExcept_Wrong_Msg
        bne $t3, $t4, TestFailed        ; assert exception counter updated
            ; delay slot
        addiu $t7, 1                    ; add 1 to the sw instruction's offset field
        sw $t7, 0($t8)                  ; patch the sw instruction (patched out by exception handler)
        addiu $t6, -1
    bnz $t6, TestException_Sw_Again     ; loop until all 3 unaligned offsets are tested
        sw $zero, 0($t1)                 ; reset exception counter

    ; test exception: sh
TestException_ShUnaligned:
    la $a0, TestStore_Sh_Msg
    la $a2, TestException_ShUnaligned
    la $t1, TestExceptCounter_Base + TestExceptCounter_AddrStore    ; $t1: store exception counter 

    sw $zero, 0($t0)                ; zero the location 

    li $t2, 0x1EE7
    move $t3, $zero
    sh $t2, 1($t0)                  ; exception here
        addiu $t3, 1
    lw $t4, 0($t0)
    la $a1, TestExcept_PreserveFailed_Msg
    bnz $t4, TestFailed             ; assert no memory write occured
        move $ra, $t9               ; restore return addr from PrintStr
    lw $t4, 4($t0)
        li $t5, 1                   ; preload testing value for the instruction right after the exception while waiting in delay slot
    bnz $t4, TestFailed             ; assert no memory write 
        lw $t4, 0($t1)              ; preload the exception counter
    la $a1, TestExcept_DoubleExec_Msg
    bne $t3, $t5, TestFailed        ; assert next instruction only executed once
        li $t3, 1                   ; expected exception counter value
    lui $a1, TestExcept_Wrong_Msg >> 16
    bne $t3, $t4, TestFailed        ; assert exception counter updated
        ori $a1, TestExcept_Wrong_Msg & 0xFFFF

    ret
    move $v0, $zero
.branchNop 1
.jumpNop 1
.loadNop 1




DataSection:
TestStatus_Load_Msg:            .db "Load instructions: ", 0
TestStatus_Store_Msg:           .db "Store instructions: ", 0
TestStatus_Branch_Msg:          .db "Branch instructions: ", 0
TestStatus_Arith_Msg:           .db "Alu instructions: ", 0
TestStatus_HiLo_Msg:            .db "Hi and Lo instructions: ", 0
TestStatus_Exception_Msg:       .db "Testing Exceptions: \n", 0
TestStatus_Ok_Msg:              .db "OK\n", 0
TestFinished_Msg:               .db "All tests passed.\n", 0
Test_Failed_Msg:                .db " failed.\n", 0
Test_R0_Msg:                    .db " modified the zero register\n", 0

TestPrerequisite_Failed_Msg:    .db "Test preqrequisite failed: jal.\n", 0

TestArith_Lui_Msg:              .db "lui", 0
TestArith_Ori_Msg:              .db "ori", 0
TestArith_Or_Msg:               .db "or", 0
TestArith_Nor_Msg:              .db "nor", 0
TestArith_Andi_Msg:             .db "andi", 0
TestArith_And_Msg:              .db "and", 0
TestArith_Xori_Msg:             .db "xori", 0
TestArith_Xor_Msg:              .db "xor", 0
TestArith_Addiu_Msg:            .db "addiu", 0
TestArith_Addi_Msg:             .db "addi", 0
TestArith_Addu_Msg:             .db "addu", 0
TestArith_Add_Msg:              .db "add", 0
TestArith_Subu_Msg:             .db "subu", 0
TestArith_Sub_Msg:              .db "sub", 0
TestArith_Sltiu_Msg:            .db "sltiu", 0
TestArith_Slti_Msg:             .db "slti", 0
TestArith_Sltu_Msg:             .db "sltu", 0
TestArith_Slt_Msg:              .db "slt", 0
TestArith_Sll_Msg:              .db "sll", 0
TestArith_Srl_Msg:              .db "srl", 0
TestArith_Sra_Msg:              .db "sra", 0
TestArith_Sllv_Msg:             .db "sllv", 0
TestArith_Srlv_Msg:             .db "srlv", 0
TestArith_Srav_Msg:             .db "srav", 0
TestArith_ZeroExtend_Msg:       .db " failed to zero extend.\n", 0
TestArith_SignExtend_Msg:       .db " failed to sign extend.\n", 0
TestArith_ClearLowerFailed_Msg: .db " failed to clear bottom 16 bits.\n", 0

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

TestLoad_Lb_Msg:                .db "lb", 0
TestLoad_Lbu_Msg:               .db "lbu", 0
TestLoad_Lh_Msg:                .db "lh", 0
TestLoad_Lhu_Msg:               .db "lhu", 0
TestLoad_Lw_Msg:                .db "lw", 0
TestLoad_Lwl_Msg:               .db "lwl", 0
TestLoad_Lwr_Msg:               .db "lwr", 0
TestLoad_Combo_Msg:             .db "lwl and lwr", 0
TestLoad_DirFailed_Msg:         .db ": old content didn't persist or delay slot failed.\n", 0
TestLoad_DelaySlotRead_Msg:     .db ": read in delay slot failed.\n", 0
TestLoad_DelaySlotWrite_Msg:    .db ": write in delay slot failed (via 'or' instruction).\n", 0
TestLoad_NoDelaySlot_Msg:       .db ": delay slot should not exist.\n", 0

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

TestHiLo_MoveHi_Msg:            .db "mfhi and mthi", 0
TestHiLo_MoveLo_Msg:            .db "mflo and mtlo", 0
TestHiLo_Mult_Msg:              .db "mult", 0
TestHiLo_Multu_Msg:             .db "multu", 0
TestHiLo_Div_Msg:               .db "div", 0
TestHiLo_Divu_Msg:              .db "divu", 0
TestHiLo_Div0Positive_Msg:      .db " by 0 and a positive number must return -1 in Lo, Rs in Hi.\n"   
TestHiLo_DivuZeroFailed_Msg:    .db " by 0 must return -1 in Lo, Rs in Hi.\n"
TestHiLo_Div0Negative_Msg:      .db " by 0 and a negative number must return 1 in Lo, Rs in Hi.\n"
TestHiLo_Div80000000_Msg:       .db " between 0x80000000 and -1 must return 0x80000000 in Lo, 0 in Hi.\n"


Exception_Unexpected_Msg:       .db "Unexpected exception, EPC = ", 0
.align 4
TestExcept_Enable:              .dw 0
TestExceptCounter_Base:
.resv 32*4
TestExceptCounter_Interrupt = 0
TestExceptCounter_AddrLoad = 4*4
TestExceptCounter_AddrStore = 5*4
TestExceptCounter_InsBusErr = 6*4
TestExceptCounter_DataBusErr = 7*4
TestExceptCounter_Syscall = 8*4
TestExceptCounter_BreakPoint = 9*4
TestExceptCounter_ResvIns = 10*4
TestExceptCounter_CopUnusable = 11*4
TestExceptCounter_Overflow = 12*4

TestExcept_Interrupt_Msg:       .db "  Interrupt Exception: ", 0
TestExcept_AddrLoad_Msg:        .db "  Addr Load Exception: ", 0
TestExcept_AddrStore_Msg:       .db "  Addr Store Exception: ", 0
TestExcept_InsBusErr_Msg:       .db "  IBE Exception: ", 0
TestExcept_DataBusErr_Msg:      .db "  DBE Exception: ", 0
TestExcept_Syscall_Msg:         .db "  Syscall Exception: ", 0
TestExcept_BreakPoint_Msg:      .db "  Breakpoint Exception: ", 0
TestExcept_ResvIns_Msg:         .db "  Reserved Ins Exception: ", 0
TestExcept_CopUnusable_Msg:     .db "  Coprocessor Unusable Exception: ", 0
TestExcept_Overflow_Msg:        .db "  Arith Overflow Exception: ", 0           ; tested

TestExcept_PreserveFailed_Msg:  .db ": result should not be written back.\n", 0
TestExcept_Wrong_Msg:           .db ": excode field of CP0's Cause is not correct.\n", 0
TestExcept_DoubleExec_Msg:      .db ": instruction after exception shouldn't be executed.\n", 0
TestExcept_BranchDelay_Msg:     .db ": EPC must point at branch instruction for exception in delay slot.\n", 0



.align 4
FreeMemorySection:
.resv MEMORY_SIZE
FreeMemoryEnd:

