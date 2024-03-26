#ifndef R3051_ASSEMBLER_C
#define R3051_ASSEMBLER_C

#include "Common.h"

/* returns true on success, false on failure */
typedef void (*AsmErrorLoggingFn)(void *Context, const char *ErrorMessage, uint MessageSizeBytes);
/* 
 * if OldPtr == NULL, should acts like malloc, 
 * if SizeBytes == NULL, should act like free (never used)
 * else should act like realloc */
typedef void *(*AsmAllocatorFn)(void *Context, void *OldPtr, u32 SizeBytes);

typedef struct ButeBuffer 
{
    u8 *Ptr;
    iSize Size;
} ByteBuffer;

/* if failed, Ptr is NULL */
ByteBuffer R3051_Assemble(
    const char *Source, 
    void *LoggerContext, 
    AsmErrorLoggingFn Logger,
    void *EmitterContext, 
    AsmAllocatorFn Allocator
);



/*==================================================================================
 *
 *                              IMPLEMENTATION
 *
 *==================================================================================*/

#include <inttypes.h> /* PRIi64 */
#include <stdarg.h> 

typedef enum AsmTokenType 
{
    TOK_ERR = 0,
    TOK_EOF,

    TOK_INT, 
    TOK_IDENTIFIER,
    TOK_REG,
    TOK_GTE_REG,

    TOK_PLUS, 
    TOK_MINUS, 
    TOK_STAR, 
    TOK_SLASH,
    TOK_PERCENT,
    TOK_AMPERSAND,
    TOK_BAR,
    TOK_CARET,

    TOK_GREATER_GREATER, 
    TOK_LESS_LESS, 
    TOK_GREATER_MINUS,

    TOK_EQUAL,
    TOK_COLON, 
    TOK_COMMA,

    TOK_LPAREN,
    TOK_RPAREN,

    TOK_INS_I_TYPE_ALU,
    TOK_INS_I_TYPE_RT,
    TOK_INS_I_TYPE_MEM,
    TOK_INS_I_TYPE_BR1,
    TOK_INS_I_TYPE_BR2,

    TOK_INS_J_TYPE,

    TOK_INS_R_TYPE_3,
    TOK_INS_R_TYPE_RSRT,
    TOK_INS_R_TYPE_RDRS,
    TOK_INS_R_TYPE_RTRD,
    TOK_INS_R_TYPE_RD,
    TOK_INS_R_TYPE_RS,
    TOK_INS_R_TYPE_SHAMT,

    TOK_INS_NO_ARG,

    TOK_COUNT,
} AsmTokenType;

typedef struct AsmKeyword 
{
    u8 Len;
    const char Str[15];
    AsmTokenType Type;
    u32 Opcode;
} AsmKeyword;

typedef struct AsmToken 
{
    StringView Str;
    int Offset;
    i32 Line;
    AsmTokenType Type;
    union {
        u64 Int;
        u32 Opcode;
        uint Reg;
        const char *Str;
    } As;
} AsmToken;

typedef struct AsmExpression 
{
    StringView Str;
    int Offset;
    i32 Line;
    Bool8 Incomplete;
    u64 Value;
} AsmExpression;

typedef struct AsmLabel 
{
    AsmToken Token;
    u64 Value;
} AsmLabel;

typedef struct AsmIncompleteLabel 
{
    AsmToken Token;
    AsmExpression Expr;
} AsmIncompleteLabel;

typedef enum AsmPatchType 
{
    PATCH_U32, 
    PATCH_U16, 
    PATCH_SHAMT,
    PATCH_JUMP,
    PATCH_BRANCH,
} AsmPatchType;

typedef struct AsmIncompleteExpr 
{
    AsmExpression Expr;
    u32 Location;
    AsmPatchType PatchType;
} AsmIncompleteExpr;

typedef struct Assembler
{
    const char *Source;
    const char *Begin;
    const char *End;
    const char *LineStart;

    void *LoggerContext;
    AsmErrorLoggingFn Logger;
    void *AllocatorContext;
    AsmAllocatorFn Allocator;
    u32 CurrentVirtualLocation;

    u8 *Buffer;
    iSize BufferSize;
    iSize BufferCap;

    Bool8 Panic;
    Bool8 Error;
    Bool8 FatalError;
    Bool8 ErrorOnUndefinedLabel;

    AsmToken CurrentToken, NextToken;
    i32 Line;

    AsmLabel Labels[2048];
    uint LabelCount;
    AsmIncompleteLabel IncompleteLabels[1024];
    uint IncompleteLabelCount;
    AsmIncompleteExpr IncompleteExprs[1024];
    uint IncompleteExprCount;
} Assembler;



static Bool8 AsmIsAlpha(char Ch)
{
    return '_' == Ch || IN_RANGE('a', Ch, 'z') || IN_RANGE('A', Ch, 'Z');
}

static Bool8 AsmIsAtEnd(const Assembler *Asm)
{
    return '\0' == *Asm->End;
}

static char AsmConsumeChar(Assembler *Asm)
{
    if (AsmIsAtEnd(Asm))
        return 0;
    return *++Asm->End;
}

static Bool8 AsmConsumeIfNextCharIs(Assembler *Asm, char Ch)
{
    if (*Asm->End == Ch)
    {
        AsmConsumeChar(Asm);
        return true;
    }
    return false;
}

static char AsmSkipSpace(Assembler *Asm)
{
    char Ch = *Asm->End;
    while (!AsmIsAtEnd(Asm))
    {
        Asm->Begin = Asm->End;
        switch (Ch)
        {
        case ' ':
        case '\t':
        {
        } break;

        case '\n':
        {
            Asm->Line++;
            Asm->LineStart = Asm->End;
        } break;
        case ';': 
        {
            while (!AsmIsAtEnd(Asm) && '\n' != AsmConsumeChar(Asm))
            { /* do nothing */ }

            Asm->Line++;
            Asm->LineStart = Asm->End;
        } break;
        default: goto Out;
        }

        Ch = AsmConsumeChar(Asm);
    }
Out:
    return Ch;
}

static AsmToken AsmCreateToken(Assembler *Asm, AsmTokenType Type)
{
    AsmToken Token = {
        .Str.Ptr = Asm->Begin,
        .Str.Len = Asm->End - Asm->Begin,
        .Line = Asm->Line,
        .Offset = Asm->Begin - Asm->LineStart + 1,

        .Type = Type,
        .As.Int = 0,
    };
    Asm->Begin = Asm->End;
    return Token;
}

static AsmToken AsmErrorToken(Assembler *Asm, const char *ErrorMessage)
{
    AsmToken Token = AsmCreateToken(Asm, TOK_ERR);
    Token.As.Str = ErrorMessage;
    return Token;
}

static AsmToken AsmConsumeNumber(Assembler *Asm)
{
    u64 Integer = 0;
    char Ch = 0;
    /* hex */
    if (*Asm->Begin == '0' && AsmConsumeIfNextCharIs(Asm, 'x'))
    {
        /* example syntax: 0x12Bc_dEF5 */
        Ch = *Asm->End;
        do {
            if (IN_RANGE('0', Ch, '9'))
            {
                Integer *= 16;
                Integer += Ch - '0';
            }
            else if (IN_RANGE('a', Ch, 'f'))
            {
                Integer *= 16;
                Integer += Ch - 'a' + 10;
            }
            else if (IN_RANGE('A', Ch, 'F'))
            {
                Integer *= 16;
                Integer += Ch - 'A' + 10;
            }
            else if ('_' == Ch)
            {
                /* do nothing */
            }
            else break;
            Ch = AsmConsumeChar(Asm);
        } while (!AsmIsAtEnd(Asm));

        if (IN_RANGE('g', Ch, 'z') || IN_RANGE('G', Ch, 'Z'))
            return AsmErrorToken(Asm, "Invalid character in hexadecimal number");
    }
    /* binary */
    else if (*Asm->Begin == '0' && AsmConsumeIfNextCharIs(Asm, 'b'))
    {
        /* example syntax: 0b1100_1010 */
        Ch = *Asm->End;
        do {
            if (Ch == '1' || Ch == '0')
            {
                Integer *= 2;
                Integer += (Ch == '1');
            }
            else if ('_' == Ch)
            {
                /* do nothing */
            }
            else break;
            Ch = AsmConsumeChar(Asm);
        } while (!AsmIsAtEnd(Asm));

        if (AsmIsAlpha(Ch) || IN_RANGE('2', Ch, '9'))
            return AsmErrorToken(Asm, "Invalid character in binary number");
    }
    /* decimal */
    else 
    {
        /* example syntax: 101_999 */
        Ch = *Asm->Begin;
        Asm->End = Asm->Begin;
        do {
            if (IN_RANGE('0', Ch, '9'))
            {
                Integer *= 10;
                Integer += Ch - '0';
            }
            else if ('_' == Ch)
            {
                /* do nothing */
            }
            else break;
            Ch = AsmConsumeChar(Asm);
        } while (!AsmIsAtEnd(Asm));

        if (AsmIsAlpha(Ch))
            return AsmErrorToken(Asm, "Invalid character in number");
    }

    AsmToken Token = AsmCreateToken(Asm, TOK_INT);
    Token.As.Int = Integer;
    return Token;
}


static Bool8 AsmStrEqual(const char *A, const char *B, iSize Len)
{
    while (Len > 0)
    {
        if (*A != *B)
            return false;
        A++;
        B++;
        Len--;
    }
    return true;
}

static const AsmKeyword *AsmGetKeywordInfo(const char *Iden, iSize Len)
{
    if (Len < 1)
        return NULL;

#define INS(Mne, Typ, Opc) {\
        .Len = sizeof(Mne) - 1, \
        .Str = Mne,\
        .Type = TOK_INS_##Typ,\
        .Opcode = Opc\
    }
    static const AsmKeyword Keywords[128][20] = {
        ['a'] = {
            INS("add",      R_TYPE_3,       0x00000020),
            INS("addi",     I_TYPE_ALU,     0x20000000),
            INS("addiu",    I_TYPE_ALU,     0x24000000),
            INS("addu",     R_TYPE_3,       0x00000021),
            INS("and",      R_TYPE_3,       0x00000024),
            INS("andi",     I_TYPE_ALU,     0x30000000),
        },
        ['b'] = {
            INS("beq",      I_TYPE_BR2,     0x10000000),
            INS("bgez",     I_TYPE_BR1,     0x04010000),
            INS("bgezal",   I_TYPE_BR1,     0x04110000),
            INS("bgtz",     I_TYPE_BR1,     0x1C000000),
            INS("blez",     I_TYPE_BR1,     0x18000000),
            INS("bltz",     I_TYPE_BR1,     0x04000000),
            INS("bltzal",   I_TYPE_BR1,     0x04100000),
            INS("bne",      I_TYPE_BR2,     0x14000000),
            INS("break",    NO_ARG,         0x0000000D),
        },
        ['c'] = {
            /* TODO: CFCz */
            /* TODO: COPz */
            /* TODO: CTCz */
            0
        },
        ['d'] = {   
            INS("div",      R_TYPE_RSRT,    0x0000001A),
            INS("divu",     R_TYPE_RSRT,    0x0000001B),
        },
        ['j'] = {
            INS("j",        J_TYPE,         0x08000000),
            INS("jal",      J_TYPE,         0x0C000000),
            INS("jalr",     R_TYPE_RDRS,    0x00000009),
            INS("jr",       R_TYPE_RS,      0x00000008),
        },
        ['l'] = {
            INS("lb",       I_TYPE_MEM,     0x80000000),
            INS("lbu",      I_TYPE_MEM,     0x90000000),
            INS("lh",       I_TYPE_MEM,     0x84000000),
            INS("lhu",      I_TYPE_MEM,     0x94000000),
            INS("lui",      I_TYPE_RT,      0x3C000000),
            INS("lw",       I_TYPE_MEM,     0x8C000000),
            /* TODO: LWCz */
            INS("lwl",      I_TYPE_MEM,     0x88000000),
            INS("lwr",      I_TYPE_MEM,     0x98000000),
        },
        ['m'] = {
            INS("mfc0",     R_TYPE_RTRD,    0x40000000),
            INS("mfc1",     R_TYPE_RTRD,    0x44000000),
            INS("mfc2",     R_TYPE_RTRD,    0x48000000),
            INS("mfc3",     R_TYPE_RTRD,    0x4C000000),

            INS("mfhi",     R_TYPE_RD,      0x00000010),
            INS("mflo",     R_TYPE_RD,      0x00000012),

            INS("mtc0",     R_TYPE_RTRD,    0x40800000),
            INS("mtc1",     R_TYPE_RTRD,    0x44800000),
            INS("mtc2",     R_TYPE_RTRD,    0x48800000),
            INS("mtc3",     R_TYPE_RTRD,    0x4C800000),

            INS("mthi",     R_TYPE_RS,      0x00000011),
            INS("mtlo",     R_TYPE_RS,      0x00000013),

            INS("mult",     R_TYPE_RSRT,    0x00000018),
            INS("multu",    R_TYPE_RSRT,    0x00000019),
        },
        ['n'] = {
            INS("nor",      R_TYPE_3,       0x00000027),
        },
        ['o'] = {
            INS("or",       R_TYPE_3,       0x00000025),
            INS("ori",      I_TYPE_ALU,     0x34000000),
        },
        ['s'] = {
            INS("sb",       I_TYPE_MEM,     0xA0000000),
            INS("sh",       I_TYPE_MEM,     0xA4000000),
            INS("sw",       I_TYPE_MEM,     0xAC000000),
            /* TODO: SWCz */
            INS("swl",      I_TYPE_MEM,     0xA8000000),
            INS("swr",      I_TYPE_MEM,     0xB8000000),

            INS("sll",      R_TYPE_SHAMT,   0x00000000),
            INS("sllv",     R_TYPE_3,       0x00000004),
            INS("sra",      R_TYPE_SHAMT,   0x00000003),
            INS("srav",     R_TYPE_3,       0x00000007),
            INS("srl",      R_TYPE_SHAMT,   0x00000002),
            INS("srlv",     R_TYPE_3,       0x00000006),

            INS("sub",      R_TYPE_3,       0x00000022),
            INS("subu",     R_TYPE_3,       0x00000023),

            INS("slt",      R_TYPE_3,       0x0000002A),
            INS("slti",     I_TYPE_ALU,     0x28000000),
            INS("sltiu",    I_TYPE_ALU,     0x2C000000),
            INS("sltu",     R_TYPE_3,       0x0000002B),

            INS("syscall",  NO_ARG,         0x0000000C),
        },
        ['x'] = {
            INS("xor",      R_TYPE_3,       0x00000026),
            INS("xori",     I_TYPE_ALU,     0x38000000),
        },
        ['r'] = {
            INS("rfe",      NO_ARG,         0x42000010),
        },
    };
#undef INS

    if (!IN_RANGE('a', Iden[0], 'z'))
        return NULL;

    unsigned char Key = Iden[0] % STATIC_ARRAY_SIZE(Keywords);
    const AsmKeyword *PotentialKeywords = Keywords[Key];
    for (int i = 0; i < (int)STATIC_ARRAY_SIZE(Keywords[Key]); i++)
    {
        const AsmKeyword *Entry = PotentialKeywords + i;
        if (Entry->Len == Len 
        && AsmStrEqual(Entry->Str, Iden, Len))
        {
            return Entry;
        }
    }
    return NULL;
}

static AsmToken AsmCreateRegisterToken(Assembler *Asm)
{
    uint RegisterNumber = 0;
    AsmTokenType RegisterType = TOK_REG;
    const char *Str = Asm->Begin + 1;
    int Len = 0;
    char Ch = *Asm->End;
    while (!AsmIsAtEnd(Asm) && (AsmIsAlpha(Ch) || IN_RANGE('0', Ch, '9')))
    {
        Ch = AsmConsumeChar(Asm);
        Len++;
    }

    if (Len >= 1 && IN_RANGE('0', Str[0], '6')) /* register 0..64 */
    {
        /* example syntax: $12, $31 */
        RegisterNumber = Str[0] - '0';
        if (Len == 2)
        {
            RegisterNumber *= 10;
            RegisterNumber += Str[1] - '0';
        }

        /* only the GTE has regs in 32..63 (control regs) */
        if (IN_RANGE(32, RegisterNumber, 63))
        {
            RegisterType = TOK_GTE_REG;
        }
        else if (RegisterNumber > 63)
        {
            goto UnknownRegister;
        }
    }
    else /* verbose register number */
    {
        switch (Str[0])
        {
        case 'z': /* zero */
        {
            if (Len == 4 && AsmStrEqual("ero", Str + 1, 3)) /* zero */
                RegisterNumber = 0;
            else goto UnknownRegister;
        } break;
        case 'a': /* at, a0..a1 */
        {
            if (Len == 2 && Str[1] == 't') /* at */
                RegisterNumber = 1;
            else if (Len == 2 && IN_RANGE('0', Str[1], '3')) /* a0..a3 */
                RegisterNumber = 4 + Str[1] - '0';
            else goto UnknownRegister;
        } break;
        case 'v': /* v0, v1 */
        {
            if (Len == 2 && IN_RANGE('0', Str[1], '1')) /* v0, v1 */
                RegisterNumber = 2 + Str[1] - '0';
            else goto UnknownRegister;
        } break;
        case 't': /* t0..t7, t8, t9 */
        {
            if (Len == 2 && IN_RANGE('0', Str[1], '7')) /* t0..t7 */
                RegisterNumber = 8 + Str[1] - '0';
            else if (Len == 2 && (Str[1] == '8' || Str[1] == '9')) /* t8, t9 */
                RegisterNumber = 24 + Str[1] - '8';
            else goto UnknownRegister;
        } break;
        case 's': /* s0..s7, s8 */
        {
            if (Len == 2 && IN_RANGE('0', Str[1], '7')) /* s0..s7 */
                RegisterNumber = 16 + Str[1] - '0';
            else if (Len == 2 && Str[1] == 'p') /* sp */
                RegisterNumber = 29;
            else if (Len == 2 && Str[1] == '8') /* s8 */
                RegisterNumber = 30;
            else goto UnknownRegister;
        } break;
        case 'k': /* k0, k1 */
        {
            if (Len == 2 && (Str[1] == '0' || Str[1] == '1')) /* k0, k1 */
                RegisterNumber = 26 + Str[1] - '0';
            else goto UnknownRegister;
        } break;
        case 'g':
        {
            if (Len == 2 && Str[1] == 'p') /* gp */
                RegisterNumber = 28;
            else goto UnknownRegister;
        } break;
        case 'r': /* ra */
        {
            if (Len == 2 && Str[1] == 'a')
                RegisterNumber = 31;
            else goto UnknownRegister;
        } break;

        default: 
UnknownRegister:
        {
            return AsmErrorToken(Asm, "Unknown register.");
        } break;
        }
    }

    AsmToken Token = AsmCreateToken(Asm, RegisterType);
    Token.As.Reg = RegisterNumber;
    return Token;
}

static AsmToken AsmConsumeIdentifier(Assembler *Asm)
{
    char Ch = *Asm->End;
    while ('_' == Ch 
        || IN_RANGE('a', Ch, 'z') 
        || IN_RANGE('A', Ch, 'Z') 
        || IN_RANGE('0', Ch, '9'))
    {
        Ch = AsmConsumeChar(Asm);
    }

    const AsmKeyword *KeywordInfo = AsmGetKeywordInfo(
        Asm->Begin, 
        Asm->End - Asm->Begin
    );
    if (NULL != KeywordInfo) /* is a keyword */
    {
        AsmToken Token = AsmCreateToken(Asm, KeywordInfo->Type);
        Token.As.Opcode = KeywordInfo->Opcode;
        return Token;
    }
    else /* is an identifier */
    {
        return AsmCreateToken(Asm, TOK_IDENTIFIER);
    }
}

static AsmToken AsmTokenize(Assembler *Asm)
{
    AsmSkipSpace(Asm);
    char Ch = *Asm->End;
    AsmConsumeChar(Asm);
    if (IN_RANGE('0', Ch, '9'))
    {
        return AsmConsumeNumber(Asm);
    }
    if ('_' == Ch || IN_RANGE('a', Ch, 'z') || IN_RANGE('A', Ch, 'Z'))
    {
        return AsmConsumeIdentifier(Asm);
    }

    switch (Ch)
    {
    case '$': return AsmCreateRegisterToken(Asm);
    case '+': return AsmCreateToken(Asm, TOK_PLUS);
    case '-': return AsmCreateToken(Asm, TOK_MINUS);
    case '/': return AsmCreateToken(Asm, TOK_SLASH);
    case '*': return AsmCreateToken(Asm, TOK_STAR);
    case '&': return AsmCreateToken(Asm, TOK_AMPERSAND);
    case '|': return AsmCreateToken(Asm, TOK_BAR);
    case '^': return AsmCreateToken(Asm, TOK_CARET); 
    case '<':
    {
        if (AsmConsumeIfNextCharIs(Asm, '<'))
            return AsmCreateToken(Asm, TOK_LESS_LESS);
    } break;
    case '>':
    {
        if (AsmConsumeIfNextCharIs(Asm, '>'))
            return AsmCreateToken(Asm, TOK_GREATER_GREATER);
        if (AsmConsumeIfNextCharIs(Asm, '-'))
            return AsmCreateToken(Asm, TOK_GREATER_MINUS);
    } break;

    case '(': return AsmCreateToken(Asm, TOK_LPAREN);
    case ')': return AsmCreateToken(Asm, TOK_RPAREN);

    case '=': return AsmCreateToken(Asm, TOK_EQUAL);
    case ':': return AsmCreateToken(Asm, TOK_COLON);
    case ',': return AsmCreateToken(Asm, TOK_COMMA);
    case '\0': return AsmCreateToken(Asm, TOK_EOF);

    default: break;
    }

    return AsmErrorToken(Asm, "Unknown token");
}


static void AsmErrorAtToken(Assembler *Asm, const AsmToken *Token, const char *cFmt, ...);
static void AsmErrorAtArgs(
    Assembler *Asm, 
    StringView Line, 
    StringView Token, 
    i32 LineNumber, int Offset,
    const char *cFmt, va_list Args
);
static StringView AsmGetLineContainingToken(const AsmToken *Token);

static AsmTokenType AsmConsumeToken(Assembler *Asm)
{
    Asm->CurrentToken = Asm->NextToken;
    Asm->NextToken = AsmTokenize(Asm);
    if (TOK_ERR == Asm->NextToken.Type)
    {
        AsmErrorAtToken(Asm, &Asm->NextToken, "%s", Asm->NextToken.As.Str);
    }
    return Asm->CurrentToken.Type;
}

static Bool8 AsmConsumeIfNextTokenIs(Assembler *Asm, AsmTokenType ExpectedType)
{
    if (Asm->NextToken.Type == ExpectedType)
    {
        AsmConsumeToken(Asm);
        return true;
    }
    return false;
}

static Bool8 AsmConsumeTokenOrError(Assembler *Asm, AsmTokenType ExpectedTokenType, const char *cFmt, ...)
{
    if (!AsmConsumeIfNextTokenIs(Asm, ExpectedTokenType))
    {
        va_list Args;
        va_start(Args, cFmt);
        AsmErrorAtArgs(
            Asm, 
            AsmGetLineContainingToken(&Asm->NextToken), 
            Asm->NextToken.Str, 
            Asm->NextToken.Line, Asm->NextToken.Offset,
            cFmt, Args
        );
        va_end(Args);
        return false;
    }
    return true;
}




static int AsmLineLength(const char *Str)
{
    int Length = 0;
    while (*Str != '\0' && *Str != '\r' && *Str != '\n')
    {
        Length++;
        Str++;
    }
    return Length;
}

static iSize AsmHighlight(char *Buffer, iSize BufferLength, StringView Offender, StringView Line)
{
    iSize BytesWritten = 0;
    const char *p = Line.Ptr;
    while (BytesWritten < BufferLength && p < Offender.Ptr)
    {
        char Space = ' ';
        if (*p == '\t')
            Space = '\t';

        *Buffer++ = Space;
        BytesWritten++;
        p++;
    }
    while (BytesWritten < BufferLength && p < Offender.Ptr + Offender.Len)
    {
        /* there will be no tabs here, since it is the token, hopefully */
        *Buffer++ = '^';
        BytesWritten++;
        p++;
    }
    return BytesWritten;
}

static StringView AsmGetLineContainingToken(const AsmToken *Token)
{
    return (StringView) {
        .Ptr = Token->Str.Ptr - Token->Offset + 1,
        .Len = AsmLineLength(Token->Str.Ptr) + Token->Offset - 1,
    };
}

static void AsmErrorAtArgs(
    Assembler *Asm, 
    StringView Line, 
    StringView Token, 
    int LineNumber, int Offset,
    const char *cFmt, va_list Args)
{
#define ASM_TMP_ERR_BUFFER_SIZE 2048
    char Buffer[ASM_TMP_ERR_BUFFER_SIZE];
    if (NULL == Asm->Logger || Asm->Panic)
        return;

    Asm->Panic = true;

#define PRINT_AND_UPDATE_PTR(...) ((Len += snprintf(Ptr, sizeof Buffer - Len, __VA_ARGS__)), Ptr = Buffer + Len)
    char *Ptr = Buffer;
    int Len = 0;

    PRINT_AND_UPDATE_PTR(
        "[Line %"PRIu32", %d]:\n"
        "  | %.*s\n"
        "  | ",
        LineNumber, Offset,
        Line.Len, Line.Ptr
    );

    Len += AsmHighlight(Ptr, sizeof Buffer - Len, Token, Line);
    Ptr = Buffer + Len;

    PRINT_AND_UPDATE_PTR(
        "\n  | Error: "
    );

    Len += vsnprintf(Ptr, sizeof Buffer - Len, cFmt, Args);
    Ptr = Buffer + Len;

    PRINT_AND_UPDATE_PTR(
        "\n"
    );

    Asm->Logger(Asm->LoggerContext, Buffer, Len);
#undef PRINT_AND_UPDATE_PTR
#undef ASM_TMP_ERR_BUFFER_SIZE
}


static void AsmErrorAtExpr(Assembler *Asm, const AsmExpression *Expression, const char *cFmt, ...)
{
    va_list Args;
    va_start(Args, cFmt);
    StringView Line = {
        .Ptr = Expression->Str.Ptr - Expression->Offset + 1,
        .Len = Expression->Str.Len + Expression->Offset - 1,
    };
    AsmErrorAtArgs(Asm, 
        Line, 
        Expression->Str, 
        Expression->Line, Expression->Offset, 
        cFmt, Args
    );
    va_end(Args);
}

static void AsmErrorAtToken(Assembler *Asm, const AsmToken *Token, const char *cFmt, ...)
{
    va_list Args;
    va_start(Args, cFmt);
    StringView Line = AsmGetLineContainingToken(Token);
    AsmErrorAtArgs(
        Asm, 
        Line, 
        Token->Str, 
        Token->Line, Token->Offset, 
        cFmt, Args
    );
    va_end(Args);
}





typedef enum AsmInfixPrecedence 
{
    PREC_NONE = 0,
    PREC_ANY,
    PREC_OR,
    PREC_XOR, 
    PREC_AND,
    PREC_SHIFT,
    PREC_ADDSUB,
    PREC_MULDIV,
    PREC_UNARY,
} AsmInfixPrecedence;

typedef AsmExpression (*PrefixParseFn)(Assembler *);
typedef AsmExpression (*InfixParseFn)(Assembler *, AsmExpression Intermediate);
typedef struct AsmParseRule 
{
    AsmInfixPrecedence Prec;
    PrefixParseFn Prefix;
    InfixParseFn Infix;
} AsmParseRule;

static AsmLabel *AsmFindIdentifier(Assembler *Asm, StringView Str);
static const AsmParseRule *AsmGetParseRule(AsmTokenType Token);
static AsmExpression AsmParsePrecedence(Assembler *Asm, AsmInfixPrecedence Prec);

static AsmExpression AsmTokenToExpr(const AsmToken *Token, Bool8 Incomplete, u64 Int)
{
    return (AsmExpression) {
        .Str = Token->Str,
        .Line = Token->Line,
        .Offset = Token->Offset,
        .Incomplete = Incomplete,
        .Value = Int,
    };
}

static AsmExpression AsmCombineExpr(AsmExpression A, AsmExpression B, u64 Value)
{
    const char *First = A.Str.Ptr;
    int Offset = A.Offset;
    int Line = A.Line;
    iSize CombinedExprLen = B.Str.Len + (B.Str.Ptr - First);
    if (B.Str.Ptr < First)
    {
        First = B.Str.Ptr;
        Offset = B.Offset;
        Line = B.Line;
        CombinedExprLen = A.Str.Len + (A.Str.Ptr - First);
    }
    return (AsmExpression) {
        .Str = {
            .Ptr = First,
            .Len = CombinedExprLen,
        },
        .Line = Line,
        .Offset = Offset,
        .Incomplete = A.Incomplete || B.Incomplete,
        .Value = Value,
    };
}

static AsmExpression AsmParseNumber(Assembler *Asm)
{
    ASSERT(Asm->CurrentToken.Type == TOK_INT);
    return AsmTokenToExpr(
        &Asm->CurrentToken, 
        false, 
        Asm->CurrentToken.As.Int
    );
}


static AsmExpression AsmParseIden(Assembler *Asm)
{
    ASSERT(Asm->CurrentToken.Type == TOK_IDENTIFIER);

    AsmLabel *Label = AsmFindIdentifier(Asm, Asm->CurrentToken.Str);
    if (NULL != Label)
        return AsmTokenToExpr(&Asm->CurrentToken, false, Label->Value);

    if (Asm->ErrorOnUndefinedLabel)
        AsmErrorAtToken(Asm, &Asm->CurrentToken, "Undefined identifier.");
    return AsmTokenToExpr(&Asm->CurrentToken, true, 0);
}

static AsmExpression AsmBinaryExpr(Assembler *Asm, AsmExpression Lhs)
{
    AsmToken OperatorToken = Asm->CurrentToken;
    AsmTokenType Operator = Asm->CurrentToken.Type;
    AsmInfixPrecedence OperatorPrec = AsmGetParseRule(Operator)->Prec;
    AsmExpression Rhs = AsmParsePrecedence(Asm, OperatorPrec + 1); /* +1 for left associative */

    AsmExpression Result = AsmCombineExpr(Lhs, Rhs, 0);
    if (Result.Incomplete)
        return Result;

    switch (Operator)
    {
    case TOK_PLUS:      Result.Value = Lhs.Value + Rhs.Value; break;
    case TOK_MINUS:     Result.Value = Lhs.Value - Rhs.Value; break;
    case TOK_STAR:      Result.Value = (i64)Lhs.Value * (i64)Rhs.Value; break;
    case TOK_SLASH:
    {
        if (Rhs.Value == 0)
            AsmErrorAtToken(Asm, &OperatorToken, "Integer division by 0.");
        else
            Result.Value = (i64)Lhs.Value / (i64)Rhs.Value;
    } break;
    case TOK_PERCENT:
    {
        if (Rhs.Value == 0)
            AsmErrorAtToken(Asm, &OperatorToken, "Integer remainder by 0.");
        else
            Result.Value = (i64)Lhs.Value % (i64)Rhs.Value;
    } break;
    case TOK_AMPERSAND: Result.Value = Lhs.Value & Rhs.Value; break;
    case TOK_BAR:       Result.Value = Lhs.Value | Rhs.Value; break;
    case TOK_CARET:     Result.Value = Lhs.Value ^ Rhs.Value; break;
    case TOK_GREATER_GREATER:
    {
        if (Rhs.Value > 63) 
            Result.Value = 0;
        else
            Result.Value = Lhs.Value >> Rhs.Value;
    } break;
    case TOK_GREATER_MINUS:
    {
        if (Rhs.Value > 63)
            Result.Value = 0;
        else 
        {
            /* arithmetic shift right */
            Result.Value = Lhs.Value >> 63? 
                ~(~(i64)Lhs.Value >> Rhs.Value) 
                : Lhs.Value >> Rhs.Value;
        }
    } break;
    case TOK_LESS_LESS:
    {
        if (Rhs.Value > 63)
            Result.Value = 0;
        else 
            Result.Value = Lhs.Value << Rhs.Value;
    } break;

    default: UNREACHABLE("Unhandled operator");
    }
    return Result;
}

static AsmExpression AsmPrefixExpr(Assembler *Asm)
{
    AsmExpression Sign = AsmTokenToExpr(&Asm->CurrentToken, false, 0);
    if (Asm->CurrentToken.Type == TOK_PLUS)
    {
        AsmExpression Result = AsmParsePrecedence(Asm, PREC_UNARY);
        return AsmCombineExpr(Sign, Result, Result.Value);
    }
    else if (Asm->CurrentToken.Type == TOK_MINUS)
    {
        AsmExpression Result = AsmParsePrecedence(Asm, PREC_UNARY);
        return AsmCombineExpr(Sign, Result, -Result.Value);
    }
    else
    {
        UNREACHABLE("");
        return (AsmExpression) { .Incomplete = true };
    }
}

static AsmExpression AsmParenExpr(Assembler *Asm)
{
    ASSERT(TOK_LPAREN == Asm->CurrentToken.Type);
    AsmExpression LeftParen = AsmTokenToExpr(&Asm->CurrentToken, false, 0);
    AsmExpression Result = AsmParsePrecedence(Asm, PREC_ANY);
    Result = AsmCombineExpr(LeftParen, Result, Result.Value);

    if (AsmConsumeTokenOrError(Asm, TOK_RPAREN, "Expected ')' after expression."))
    {
        AsmExpression RightParen = AsmTokenToExpr(&Asm->CurrentToken, false, 0);
        Result = AsmCombineExpr(Result, RightParen, Result.Value);
    }
    return Result;
}




static const AsmParseRule *AsmGetParseRule(AsmTokenType Token)
{
    static const AsmParseRule AsmPrecedenceTable[TOK_COUNT] = {
        [TOK_INT]               = { PREC_NONE,      AsmParseNumber, NULL },
        [TOK_LPAREN]            = { PREC_NONE,      AsmParenExpr,   NULL },
        [TOK_IDENTIFIER]        = { PREC_NONE,      AsmParseIden,   NULL },

        [TOK_PLUS]              = { PREC_ADDSUB,    AsmPrefixExpr,  AsmBinaryExpr },
        [TOK_MINUS]             = { PREC_ADDSUB,    AsmPrefixExpr,  AsmBinaryExpr },
        [TOK_STAR]              = { PREC_MULDIV,    NULL,           AsmBinaryExpr },
        [TOK_SLASH]             = { PREC_MULDIV,    NULL,           AsmBinaryExpr },
        [TOK_AMPERSAND]         = { PREC_AND,       NULL,           AsmBinaryExpr },
        [TOK_BAR]               = { PREC_OR,        NULL,           AsmBinaryExpr },
        [TOK_CARET]             = { PREC_XOR,       NULL,           AsmBinaryExpr },
        [TOK_GREATER_GREATER]   = { PREC_SHIFT,     NULL,           AsmBinaryExpr },
        [TOK_GREATER_MINUS]     = { PREC_SHIFT,     NULL,           AsmBinaryExpr },
        [TOK_LESS_LESS]         = { PREC_SHIFT,     NULL,           AsmBinaryExpr },
    };
    return &AsmPrecedenceTable[Token];
}

static AsmExpression AsmParsePrecedence(Assembler *Asm, AsmInfixPrecedence Prec)
{
    AsmTokenType CurrentType = AsmConsumeToken(Asm);
    PrefixParseFn PrefixFn = AsmGetParseRule(CurrentType)->Prefix;
    if (NULL == PrefixFn)
    {
        AsmErrorAtToken(Asm, &Asm->CurrentToken, "Expected expression.");
        return (AsmExpression) { 0 };
    }

    AsmExpression Lhs = PrefixFn(Asm);

    for (AsmTokenType BinaryOperator = Asm->NextToken.Type; 
        Prec <= AsmGetParseRule(BinaryOperator)->Prec; 
        BinaryOperator = Asm->NextToken.Type)
    {
        /* consume the operator */
        AsmConsumeToken(Asm);

        InfixParseFn InfixFn = AsmGetParseRule(BinaryOperator)->Infix;
        ASSERT(NULL != InfixFn);
        Lhs = InfixFn(Asm, Lhs);
    }
    return Lhs;
}

static AsmExpression AsmConsumeExpr(Assembler *Asm)
{
    return AsmParsePrecedence(Asm, PREC_ANY);
}

static void AsmPushIncompleteExpr(Assembler *Asm, AsmExpression Expr, AsmPatchType PatchType);
static u64 AsmConsumeOrSaveExpr(Assembler *Asm, AsmPatchType PatchType)
{
    AsmExpression Expr = AsmConsumeExpr(Asm);
    if (Expr.Incomplete)
    {
        AsmPushIncompleteExpr(Asm, Expr, PatchType);
        return 0;
    }
    return Expr.Value;
}




static Bool8 AsmResizeBuffer(Assembler *Asm, iSize NewSizeBytes)
{
    void *NewBuffer = Asm->Allocator(Asm->AllocatorContext, Asm->Buffer, NewSizeBytes);
    if (NULL == NewBuffer)
    {
        Asm->FatalError = true;
        Asm->Allocator(Asm->AllocatorContext, Asm->Buffer, 0); /* free the old buffer */
        Asm->Logger(Asm->LoggerContext, "Out of memory", 13);
        return false;
    }
    Asm->Buffer = NewBuffer;
    return true;
}

static void AsmEmit32(Assembler *Asm, u32 Word)
{
    /* resize */
    if (Asm->BufferSize + 4 >= Asm->BufferCap)
    {
        if (!AsmResizeBuffer(Asm, Asm->BufferSize*4 + 8))
            return;
    }

    /* TODO: endianess */
    for (uint i = 0; i < sizeof Word; i++)
    {
        Asm->Buffer[Asm->BufferSize++] = Word;
        Word >>= 8;
    }
    Asm->CurrentVirtualLocation += sizeof Word;
}

/* NOTE: push before emit */
static void AsmPushIncompleteExpr(Assembler *Asm, AsmExpression Expr, AsmPatchType PatchType)
{
    ASSERT(Asm->IncompleteExprCount < STATIC_ARRAY_SIZE(Asm->IncompleteExprs));
    Asm->IncompleteExprs[Asm->IncompleteExprCount++] = (AsmIncompleteExpr) {
        .Expr = Expr,
        .Location = Asm->BufferSize,
        .PatchType = PatchType,
    };
}

static void AsmPushIncompleteLabel(Assembler *Asm, AsmToken Token, AsmExpression Expr)
{
    ASSERT(Asm->IncompleteLabelCount < STATIC_ARRAY_SIZE(Asm->IncompleteLabels));
    Asm->IncompleteLabels[Asm->IncompleteLabelCount++] = (AsmIncompleteLabel) {
        .Token = Token,
        .Expr = Expr,
    };
}

static void AsmPushLabel(Assembler *Asm, AsmToken Token, u64 Value)
{
    ASSERT(Asm->LabelCount < STATIC_ARRAY_SIZE(Asm->Labels));
    Asm->Labels[Asm->LabelCount++] = (AsmLabel) {
        .Token = Token,
        .Value = Value,
    };
}

static AsmLabel *AsmFindIdentifier(Assembler *Asm, StringView Str)
{
    for (uint i = 0; i < Asm->LabelCount; i++)
    {
        AsmLabel *Entry = &Asm->Labels[i];
        if (AsmStrEqual(Str.Ptr, Entry->Token.Str.Ptr, Str.Len))
        {
            return Entry;
        }
    }
    return NULL;
}


static void AsmIdentifierStmt(Assembler *Asm)
{
    AsmToken Identifier = Asm->CurrentToken;
    /* assignment */
    if (AsmConsumeIfNextTokenIs(Asm, TOK_EQUAL))
    {
        AsmExpression Expression = AsmConsumeExpr(Asm);
        if (Expression.Incomplete)
        {
            AsmPushIncompleteLabel(Asm, 
                Identifier, 
                Expression
            );
        }
        else
        {
            AsmPushLabel(Asm, 
                Identifier, 
                Expression.Value 
            );
        }
    }
    /* label */
    else if (AsmConsumeIfNextTokenIs(Asm, TOK_COLON))
    {
        AsmPushLabel(Asm, 
            Identifier, 
            Asm->CurrentVirtualLocation
        );
    }
}

static uint AsmConsumeGPRegister(Assembler *Asm, const char *Name)
{
    AsmConsumeTokenOrError(Asm, TOK_REG, "Expected %s register.", Name);
    return Asm->CurrentToken.As.Reg;
}

#define ASM_CONSUME_COMMA(pAsm, ...) \
    AsmConsumeTokenOrError(Asm, TOK_COMMA, "Expected ',' after "__VA_ARGS__);

static void AsmJType(Assembler *Asm)
{
    /* j addr */
    u32 Opcode = Asm->CurrentToken.As.Opcode;

    AsmExpression AddrExpr = AsmConsumeExpr(Asm);
    if (AddrExpr.Incomplete)
    {
        AsmPushIncompleteExpr(Asm, AddrExpr, PATCH_JUMP);
    }
    else
    {
        if ((AddrExpr.Value ^ Asm->CurrentVirtualLocation) & 0xF0000000)
        {
            AsmErrorAtExpr(Asm, &AddrExpr, 
                "Bits 28 to 31 of jump address does not match that of PC."
            );
        }
        u32 JumpAddr = AddrExpr.Value & 0x0FFFFFFF;
        Opcode |= JumpAddr >> 2;
    }

    AsmEmit32(Asm, Opcode);
}

static void AsmRType3Operands(Assembler *Asm)
{
    /* instruction rd, rs, rt
     * instruction rd, reg      (rt = rd) */
    u32 Opcode = Asm->CurrentToken.As.Opcode;

    /* RD */
    uint Rd = AsmConsumeGPRegister(Asm, "destination");
    ASM_CONSUME_COMMA(Asm, "destination register.");

    /* RS */
    uint Rs = AsmConsumeGPRegister(Asm, "source");

    /* RT */
    uint Rt = Rd;
    if (AsmConsumeIfNextTokenIs(Asm, TOK_COMMA))
    {
        AsmConsumeTokenOrError(Asm, TOK_REG, "Expected register.");
        Rt = Asm->CurrentToken.As.Reg;
    }

    Opcode |= (Rd & 0x1F) << RD;
    Opcode |= (Rs & 0x1F) << RS;
    Opcode |= (Rt & 0x1F) << RT;
    AsmEmit32(Asm, Opcode);
}

static void AsmRType1Operand(Assembler *Asm, const char *RegisterType, int Offset)
{
    u32 Opcode = Asm->CurrentToken.As.Opcode;
    uint Reg = AsmConsumeGPRegister(Asm, RegisterType);

    Opcode |= (Reg & 0x1F) << Offset;
    AsmEmit32(Asm, Opcode);
}

static void AsmRType2Operands(
    Assembler *Asm, 
    const char *First, const char *Second, 
    int Offset1, int Offset2
)
{
    u32 Opcode = Asm->CurrentToken.As.Opcode;

    uint R1 = AsmConsumeGPRegister(Asm, First);
    ASM_CONSUME_COMMA(Asm, "register.");
    uint R2 = AsmConsumeGPRegister(Asm, Second);

    Opcode |= (R1 & 0x1F) << Offset1;
    Opcode |= (R2 & 0x1F) << Offset2;
    AsmEmit32(Asm, Opcode);
}

static void AsmRTypeShift(Assembler *Asm)
{
    u32 Opcode = Asm->CurrentToken.As.Opcode;

    uint Rd = AsmConsumeGPRegister(Asm, "destination");
    ASM_CONSUME_COMMA(Asm, "destination register.");
    uint Rt = Rd;
    if (AsmConsumeIfNextTokenIs(Asm, TOK_REG))
    {
        Rt = Asm->CurrentToken.As.Reg;
        ASM_CONSUME_COMMA(Asm, "source register.");
    }

    u64 Shamt = AsmConsumeOrSaveExpr(Asm, PATCH_SHAMT);
    TODO("check if shamt is in range");

    Opcode |= (Rd & 0x1F) << RD;
    Opcode |= (Rt & 0x1F) << RT;
    Opcode |= (Shamt & 0x1F) << 6;
    AsmEmit32(Asm, Opcode);
}

static void AsmIType2Operands(Assembler *Asm, const char *OperandName, int Offset)
{
    u32 Opcode = Asm->CurrentToken.As.Opcode;

    uint Reg = AsmConsumeGPRegister(Asm, OperandName);
    ASM_CONSUME_COMMA(Asm, "%s register.", OperandName);
    u64 Immediate = AsmConsumeOrSaveExpr(Asm, PATCH_U16);
    TODO("check if immediate is in range");

    Opcode |= (Reg & 0x1F) << Offset;
    Opcode |= Immediate & 0xFFFF;
    AsmEmit32(Asm, Opcode);
}

static void AsmIType3Operands(Assembler *Asm)
{
    u32 Opcode = Asm->CurrentToken.As.Opcode;

    uint Rt = AsmConsumeGPRegister(Asm, "destination");
    ASM_CONSUME_COMMA(Asm, "destination register.");

    uint Rs = Rt;
    if (AsmConsumeIfNextTokenIs(Asm, TOK_REG))
    {
        Rs = Asm->CurrentToken.As.Reg;
        ASM_CONSUME_COMMA(Asm, "register.");
    }

    u64 Immediate = AsmConsumeOrSaveExpr(Asm, PATCH_U16);
    TODO("check if immediate is in range");

    Opcode |= (Rt & 0x1F) << RT;
    Opcode |= (Rs & 0x1F) << Rs;
    Opcode |= Immediate & 0xFFFF;
    AsmEmit32(Asm, Opcode);
}

static void AsmITypeMemory(Assembler *Asm)
{
    u32 Opcode = Asm->CurrentToken.As.Opcode;

    uint Rt = AsmConsumeGPRegister(Asm, "a");
    ASM_CONSUME_COMMA(Asm, "register.");

    u64 Offset = AsmConsumeOrSaveExpr(Asm, PATCH_U16);
    TODO("check if memory offset is in range");
    AsmConsumeTokenOrError(Asm, TOK_LPAREN, "Expected '('.");
    uint Base = AsmConsumeGPRegister(Asm, "base");
    AsmConsumeTokenOrError(Asm, TOK_LPAREN, "Expected ')'.");

    Opcode |= (Base & 0x1F) << RT;
    Opcode |= (Rt & 0x1F) << RS;
    Opcode |= Offset & 0xFFFF;
    AsmEmit32(Asm, Opcode);
}

static void AsmITypeBranch(Assembler *Asm, int RegisterArgumentCount)
{
    u32 Opcode = Asm->CurrentToken.As.Opcode;

    uint FirstReg = AsmConsumeGPRegister(Asm, "a");
    ASM_CONSUME_COMMA(Asm, "register.");
    if (RegisterArgumentCount == 2)
    {
        uint SecondReg = AsmConsumeGPRegister(Asm, "a");
        ASM_CONSUME_COMMA(Asm, "register.");
        Opcode |= (SecondReg & 0x1F) << RT;
    }

    u32 BranchTarget = AsmConsumeOrSaveExpr(Asm, PATCH_BRANCH);
    i32 BranchOffset = (i32)(BranchTarget - (Asm->CurrentVirtualLocation + 4)) / 4;
    TODO("check if branch target is in range");

    Opcode |= (FirstReg & 0x1F) << RS;
    Opcode |= 0xFFFF & BranchOffset;
    AsmEmit32(Asm, Opcode);
}

#undef ASM_CONSUME_COMMA


static void AsmConsumeStmt(Assembler *Asm)
{
    switch (AsmConsumeToken(Asm))
    {
    case TOK_IDENTIFIER:        AsmIdentifierStmt(Asm); break;


    case TOK_INS_NO_ARG:        AsmEmit32(Asm, Asm->CurrentToken.As.Opcode); break;

    case TOK_INS_J_TYPE:        AsmJType(Asm); break;

    case TOK_INS_R_TYPE_3:      AsmRType3Operands(Asm); break;
    case TOK_INS_R_TYPE_RD:     AsmRType1Operand(Asm, "destination", RD); break;
    case TOK_INS_R_TYPE_RS:     AsmRType1Operand(Asm, "source", RS); break;
    case TOK_INS_R_TYPE_RDRS:   AsmRType2Operands(Asm, "destination", "source", RD, RS); break;
    case TOK_INS_R_TYPE_RTRD:   AsmRType2Operands(Asm, "general", "coprocessor", RT, RD); break;
    case TOK_INS_R_TYPE_RSRT:   AsmRType2Operands(Asm, "a", "a second", RS, RT); break;
    case TOK_INS_R_TYPE_SHAMT:  AsmRTypeShift(Asm); break;

    case TOK_INS_I_TYPE_ALU:    AsmIType3Operands(Asm); break;
    case TOK_INS_I_TYPE_RT:     AsmIType2Operands(Asm, "destination", RT); break;
    case TOK_INS_I_TYPE_MEM:    AsmITypeMemory(Asm); break;
    case TOK_INS_I_TYPE_BR1:    AsmITypeBranch(Asm, 1); break;
    case TOK_INS_I_TYPE_BR2:    AsmITypeBranch(Asm, 2); break;
    }
}

static Bool8 AsmTokenIsInstruction(AsmTokenType Token)
{
    return IN_RANGE(TOK_INS_I_TYPE_ALU, Token, TOK_INS_NO_ARG);
}

static void AsmRecoverFromPanic(Assembler *Asm)
{
    Asm->Panic = false;
    while (!AsmIsAtEnd(Asm) && !AsmTokenIsInstruction(Asm->NextToken.Type))
    {
        AsmConsumeToken(Asm);
    }
}


ByteBuffer R3051_Assemble(
    const char *Source, 
    void *LoggerContext, 
    AsmErrorLoggingFn Logger,
    void *AllocatorContext, 
    AsmAllocatorFn Allocator
)
{
    Assembler Asm = {
        .Source = Source,
        .Begin = Source,
        .End = Source,
        .LineStart = Source,
        .Line = 1,

        .LoggerContext = LoggerContext,
        .Logger = Logger,

        .AllocatorContext = AllocatorContext,
        .Allocator = Allocator,
        .CurrentVirtualLocation = 0,
    };
    if (!AsmResizeBuffer(&Asm, 1024*4))
        return (ByteBuffer) { 0 };

    AsmConsumeToken(&Asm);
    while (TOK_EOF != Asm.NextToken.Type)
    {
        AsmConsumeStmt(&Asm);
        if (Asm.Panic)
            AsmRecoverFromPanic(&Asm);
        if (Asm.FatalError)
            return (ByteBuffer) { 0 };
    }

    ByteBuffer Buffer = {
        .Ptr = Asm.Buffer,
        .Size = Asm.BufferSize,
    };
    return Buffer;
}



#ifdef STANDALONE
#undef STANDALONE
#include <stdio.h>
#include "Disassembler.c"

static void Log(void *Context, const char *ErrorMessage, uint Length)
{
    (void)Context;
    fprintf(stderr, "Test: %.*s", (int)Length, ErrorMessage);
}

static void *Allocator(void *Context, void *OldPtr, u32 SizeBytes)
{
    (void)Context;
    return realloc(OldPtr, SizeBytes);
}

int main(int argc, char **argv)
{
    const char *TestProgram = 
        "location: \n"
        "  beq $0, $0, location\n"
    ;

    ByteBuffer Buffer = R3051_Assemble(
        TestProgram, 
        NULL, Log,
        NULL, Allocator
    );
    if (NULL == Buffer.Ptr)
    {
        return 1;
    }

    u32 *Instructions = (u32 *)Buffer.Ptr;
    for (iSize i = 0; i < Buffer.Size / 4; i++)
    {
        char Mnemonic[128];
        R3051_Disasm(
            Instructions[i], 
            i, 
            DISASM_IMM16_AS_HEX, 
            Mnemonic, 
            sizeof Mnemonic
        );
        printf("%8x: %08x   %s\n", (u32)i, Instructions[i], Mnemonic);
    }
    return 0;
}
#endif /* STANDALONE */


#endif /* R3051_ASSEMBLER_C */

