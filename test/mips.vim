" Vim syntax file
" Language:       Mips R3051
" Maintainer:     Khanh Ngo
" Last Change:    2024 Mar 26

if exists("b:current_syntax")
  finish
  endif

  runtime! syntax/asm.vim

  "case sensitive! 
  " syn case ignore
  syn case match
  "Mips opcodes
  syn match asmOpcode "\<addi\?u\?\>"
  syn match asmOpcode "\<andi\?\>"
  syn match asmOpcode "\<be[qz]\>"
  syn match asmOpcode "\<bg[et]z\>"
  syn match asmOpcode "\<bl[et]z\>"
  syn match asmOpcode "\<bgezal\>"
  syn match asmOpcode "\<bltzal\>"
  syn match asmOpcode "\<bn[ez]\>"
  syn match asmOpcode "\<break\>"
  syn match asmOpcode "\<bra\>"
  "TODO: C-group instructions
  "
  syn match asmOpcode "\<divu\?\>"
  syn match asmOpcode "\<multu\?\>"
  syn match asmOpcode "\<j\(al\)\?r\?\>"

  syn match asmOpcode "\<l[bh]u\?\>"
  syn match asmOpcode "\<lui\>"
  syn match asmOpcode "\<lwc[0-3]\>"
  syn match asmOpcode "\<lw[lr]\?\>"
  syn match asmOpcode "\<la\>"
  syn match asmOpcode "\<li\>"
  syn match asmOpcode "\<s[bhw]\>"
  syn match asmOpcode "\<sw[lr]\>"
  syn match asmOpcode "\<swc[0-3]\>"

  syn match asmOpcode "\<m[ft]c[0-3]\>"
  syn match asmOpcode "\<m[ft]hi\|m[ft]lo\>"
  syn match asmOpcode "\<move\>"

  syn match asmOpcode "\<nor\>"
  syn match asmOpcode "\<nop\>"
  syn match asmOpcode "\<ori\?\>"

  syn match asmOpcode "\<sllv\?\>"
  syn match asmOpcode "\<slti\?u\?\>"
  syn match asmOpcode "\<sr[al]v\?\>"
  syn match asmOpcode "\<subi\?u\?\>"
  syn match asmOpcode "\<syscall\>"

  syn match asmOpcode "\<xori\?\>"
  syn match asmOpcode "\<rfe\>"
  syn match asmOpcode "\<ret\>"

  syn match asmHexNumber "0x[_0-9a-fA-F]\+\>"
  syn match asmBinNumber "0b[_01]\+\>"
  syn match asmDecNumber "\<[_0-9]\+D\=\>"

  syn match asmLabel "^[a-zA-Z_][a-zA-Z0-9_]*:"

  syn match asmStringError "\"[ -~]*\""
  syn match asmStringError "\"[ -~]*$"
  syn match asmStringEscape display contained "\\[nrt\"]"
  syn region asmString   start="\"" skip="\\\"" end="\"" contains=asm68kCharError,asmStringEscape
  syn match asmCharError "[^ -~]" contained

  syn keyword asmDirective .org .db .dh .dw .dl .resv .branchNop .loadNop .jumpNop

  "Mips registers
  syn match asmRegister   "\$2[0-9]\|\$1[0-9]\|\$3[0-1]\|\$[0-9]"
  syn match asmRegister	  "\$v[01]"
  syn match asmRegister	  "\$a[0-3]"
  syn match asmRegister	  "\$t[0-9]"
  syn match asmRegister	  "\$s[0-7]"
  syn match asmRegister	  "\$k[0-1]"
  syn match asmRegister "\$zero\|$at\|$gp\|$sp\|\$fp\|\$ra"

  syn match asmComment ";.*"

  hi def link asmOpcode		Statement
  hi def link asmRegister	Identifier
  hi def link asmDirective	Special
  hi def link asmDecNumber	Number " Constant
  hi def link asmHexNumber	Number " Constant
  hi def link asmBinNumber	Number " Constant
  hi def link asmLabel		Type
  hi def link asmString		String " Constant
  hi def link asmStringError	Error
  hi def link asmCharError	Error
  hi def link asmComment	Comment


  let b:current_syntax = "mips"

  " vim: nowrap sw=2 sts=2 ts=8 noet
