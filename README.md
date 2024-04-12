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
- ```cont```: continue running until a breakpoint is encountered
```
cont
``` 
