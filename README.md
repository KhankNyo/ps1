# PS1 Emulator (very WIP)
- This is a very bare-bone PS1 emulator. The only things working as of rn are the disassembler, debug emulator and assembler

# Build:
### Windows
- To build with gcc (or other compilers):
  ```
  .\build.bat
  ```
- To build with cl (MSVC compiler):
  ```
  .\build.bat cl
  ```
- remove built binaries:
  ```
  .\build.bat clean
  ```

# Debug emulator:
- When running, you can either press enter to execute an instruction, or enter the following commands
- ```setbp *address*```: sets a breakpoint at a given address, hex only. Example syntax:
```
setbp deadB33F
```
- ```cont```: continue running until a breakpoint is encountered. Example syntax:
```
cont
```

# Assembler:
- The assembler supports most basic Mips R3000 instructions (missing ones are LWCz and SWCz, FPU, and virtual memory instructions, since the PS1 does not have an FPU and virtual memory)
### Supported pseudo instructions:
- `la $reg, u32`: load an unsigned 32 bit number to $reg (gives an error if the number is out of range)
- `li $reg, i32`: load a signed 32 bit number to $reg (gives an error if the number is out of range)
- `move $rd, $rs`: load content of register $rs into register $rd
- `bra Label`: branch always
- `bez $reg, Label`: branch if $reg == 0
- `bnz $reg, Label`: branch if $reg != 0
- `nop`: no operation (literally does nothing but take up 4 bytes)
### Supported directives:
- `.db byte0, byte1, ...`: places a byte, bytes, or a string at the current location
- `.dh half0, half1, ...`: places a halfword or halfwords (16 bit value) at the current location
- `.dw word0, word1, ...`: places a word or words (32 bit values) at the current location
- `.dl long0, long1, ...`: places a longword or longwords (64 bit values) at the current location
- `.org address`: set the virtual PC to the given address (NOTE: does not move to a physical location)
- `.resv size`: reserve 'size' bytes (does move to a new location)
- `.loadNop value`: if value != 0: nop will be inserted after every load instruction (lw, lh, lb, lhu, lbu, lwl, lwr)
- `.branchNop value`: if value != 0: nop wil be inserted after every branch instruction (bltz, blez, bgtz, bgez, bgezal, bltzal, bne, beq)
- `.jumpNop value`: if value != 0: nop will be inserted after every jump instruction (j, jal, jr, jalr)
### Strings:
- Strings are NOT null terminated by default, the programmer must manually add a null terminator. Example:
```
.db "my string", 0
```
- Supported escape characters: `\n, \t, \r, \"`.
### General syntax:
- A comment starts with `;`. Anything after `;` will be a comment until the next line. Example syntax:
```
  addiu $1, $2  ; I am a comment
I_am_not_a_comment:
``` 
- The programmer can specify 3 or 2 operands for instructions that expect 3 operands. Example syntax:
  - `add $1, $1, $3 ` is equivalent to `add $1, $3`
- Registers must be prefixed with '$'. The assembler supports both fancy register name and register with number only. Example syntax:
  - `$0, $zero, $8, $t0`
- Variables are declared by writing its name followed by an equal sign. Example syntax:
  - `MyVariable = 1`
- Labels are declared by Writing its name followed by a colon. Example syntax:
```
MyLabel:
  bra MyLabel
```
- Note that instructions can use forward declared label or variable, but forward declared variables or label cannot be used in directives and other variable declaration. Example:
- - Legal:
```
  bra Next
  la $t0, Next
Next:
```
- - Illegal: 
```
  MyFirstValue = MyValue
  .org MyValue
MyValue = 100
```
