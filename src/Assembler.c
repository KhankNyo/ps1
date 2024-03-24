#ifndef R3051_ASSEMBLER_C
#define R3051_ASSEMBLER_C

#include "Common.h"

/* returns true on success, false on failure */
typedef void (*AsmErrorLoggingFn)(void *Context, const char *ErrorMessage, iSize MessageSizeBytes);

Bool8 R3051_Assemble(
    const char *Source, 
    void *ErrorLoggingContext, 
    AsmErrorLoggingFn Logger
);



/*==================================================================================
 *
 *                              IMPLEMENTATION
 *
 *==================================================================================*/

#include <inttypes.h> /* PRIi64 */
#include <stdarg.h> 
#define ASM_TMP_ERR_BUFFER_SIZE 2048

typedef enum AsmTokenType 
{
    TOK_ERR = 0,
    TOK_EOF,

    TOK_INT, 
    TOK_PLUS, 
    TOK_MINUS, 
    TOK_STAR, 
    TOK_SLASH,
    TOK_AMPERSAND,
    TOK_BAR,
    TOK_CARET,
    TOK_EQUAL,
} AsmTokenType;

typedef struct AsmToken 
{
    StringView Str;
    int Offset;
    u32 Line;
    AsmTokenType Type;
    union {
        u64 Int;
        const char *Str;
    } As;
} AsmToken;

typedef struct AsmExpression 
{
    StringView Str;
    int Offset;
    u32 Line;
    Bool8 Incomplete;
    u64 Value;
} AsmExpression;

typedef struct Assembler
{
    const char *Source;
    const char *Begin;
    const char *End;
    const char *LineStart;

    void *LoggerContext;
    AsmErrorLoggingFn Logger;

    AsmToken CurrentToken, NextToken;
    u32 Line;
} Assembler;



static Bool8 AsmIsAtEnd(const Assembler *Asm)
{
    return '\0' == *Asm->End;
}

static char AsmConsumeChar(Assembler *Asm)
{
    if (AsmIsAtEnd(Asm))
        return 0;
    return *Asm->End++;
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
    char Ch = 0;
    while (!AsmIsAtEnd(Asm))
    {
        Asm->Begin = Asm->End;
        Ch = AsmConsumeChar(Asm);
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
            {
                Ch = *Asm->End;
            }
            Asm->Line++;
            Asm->LineStart = Asm->End;
        } break;
        default: break;
        }
    }
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
        do {
            Ch = AsmConsumeChar(Asm);
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
        } while (!AsmIsAtEnd(Asm));
    }
    /* binary */
    else if (*Asm->Begin == '0' && AsmConsumeIfNextCharIs(Asm, 'b'))
    {
        /* example syntax: 0b1100_1010 */
        do {
            Ch = AsmConsumeChar(Asm);
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
        } while (!AsmIsAtEnd(Asm));
    }
    /* decimal */
    else 
    {
        /* example syntax: 101_999 */
        Ch = AsmConsumeChar(Asm);
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
        } while (!AsmIsAtEnd(Asm));
    }

    if (IN_RANGE('a', Ch, 'z'))
        return AsmErrorToken(Asm, "Invalid character after number");

    AsmToken Token = AsmCreateToken(Asm, TOK_INT);
    Token.As.Int = Integer;
    return Token;
}

static AsmToken AsmTokenize(Assembler *Asm)
{
    char Ch = AsmSkipSpace(Asm);
    if (IN_RANGE('0', Ch, '9'))
    {
        return AsmConsumeNumber(Asm);
    }

    switch (Ch)
    {
    case '+': return AsmCreateToken(Asm, TOK_PLUS);
    case '-': return AsmCreateToken(Asm, TOK_MINUS);
    case '/': return AsmCreateToken(Asm, TOK_SLASH);
    case '*': return AsmCreateToken(Asm, TOK_STAR);
    case '&': return AsmCreateToken(Asm, TOK_AMPERSAND);
    case '|': return AsmCreateToken(Asm, TOK_BAR);
    case '^': return AsmCreateToken(Asm, TOK_CARET); 

    case '=': return AsmCreateToken(Asm, TOK_EQUAL);
    case '\0': return AsmCreateToken(Asm, TOK_EOF);
    default: return AsmErrorToken(Asm, "Unknown token");
    }
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
    while (BytesWritten > BufferLength && p < Offender.Ptr)
    {
        char Space = ' ';
        if (*p == '\t')
            Space = '\t';

        *Buffer++ = Space;
        BytesWritten++;
        p++;
    }
    while (BytesWritten < BufferLength && p <= Offender.Ptr + Offender.Len)
    {
        /* there will be no tabs here, since it is the token, hopefully */
        *Buffer++ = '^';
        BytesWritten++;
        p++;
    }
    return BytesWritten;
}

static void AsmErrorAtToken(Assembler *Asm, const AsmToken *Token, const char *cFmt, ...)
{
    va_list Args;
    char Buffer[ASM_TMP_ERR_BUFFER_SIZE];
    if (NULL == Asm->Logger)
        return;

    va_start(Args, cFmt);
    {
#define PRINT_AND_UPDATE_PTR(...) ((Len += snprintf(Ptr, sizeof Buffer - Len, __VA_ARGS__)), Ptr = Buffer + Len)
        char *Ptr = Buffer;
        int Len = 0;
        StringView Line = {
            .Ptr = Token->Str.Ptr - Token->Offset + 1,
            .Len = AsmLineLength(Token->Str.Ptr) + Token->Offset - 1,
        };

        PRINT_AND_UPDATE_PTR(
            "\n[Line %"PRIu32", %d]:\n"
            "  | %.*s\n"
            "  | ",
            Token->Line, Token->Offset,
            Line.Len, Line.Ptr
        );

        Len += AsmHighlight(Ptr, sizeof Buffer - Len, Token->Str, Line);
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
    }
    va_end(Args);
}

static AsmTokenType AsmConsumeToken(Assembler *Asm)
{
    AsmTokenType CurrentType = Asm->NextToken.Type;
    Asm->CurrentToken = Asm->NextToken;
    Asm->NextToken = AsmTokenize(Asm);
    if (TOK_ERR == Asm->NextToken.Type)
    {
        AsmErrorAtToken(Asm, &Asm->NextToken, "%s", Asm->NextToken.As.Str);
    }
    return CurrentType;
}

static AsmExpression AsmConsumeExpr(Assembler *Asm)
{
    AsmExpression Expr = {
        .Str.Ptr = Asm->Begin,
        .Str.Len = 0,
        .Incomplete = false,

        .Line = Asm->Line,
        .Offset = Asm->Begin - Asm->LineStart + 1,
    };
    return Expr;
}



Bool8 R3051_Assemble(
    const char *Source, 
    void *ErrorLoggingContext, 
    AsmErrorLoggingFn Logger
)
{
    Assembler Asm = {
        .Source = Source,
        .Begin = Source,
        .End = Source,
        .LineStart = Source,
        .Line = 1,

        .LoggerContext = ErrorLoggingContext,
        .Logger = Logger,
    };
    AsmExpression Expr = AsmConsumeExpr(&Asm);
    printf("%.*s = %"PRIi64"\n", 
        Expr.Str.Len, Expr.Str.Ptr, Expr.Value
    );
    return true;
}


#ifdef STANDALONE
int main(int argc, char **argv)
{
    return 0;
}
#endif /* STANDALONE */


#endif /* R3051_ASSEMBLER_C */

