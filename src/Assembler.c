#ifndef R3051_ASSEMBLER_C
#define R3051_ASSEMBLER_C

#include "Common.h"

/* returns true on success, false on failure */
typedef void (*AssemblerErrorLoggingFn)(void *Context, const char *ErrorMessage);
Bool8 R3051_Assemble(
    const char *Source, 
    void *ErrorLoggingContext, 
    AssemblerErrorLoggingFn Logger
);



/*==================================================================================
 *
 *                              IMPLEMENTATION
 *
 *==================================================================================*/

typedef struct Assembler
{
    const char *Source;
    const char *Begin;
    const char *End;

    void *LoggerContext;
    AssemblerErrorLoggingFn Logger;
} Assembler;

Bool8 R3051_Assemble(
    const char *Source, 
    void *ErrorLoggingContext, 
    AssemblerErrorLoggingFn Logger
)
{
    Assembler Asm = {
        .Source = Source,
        .Begin = Source,
        .End = Source,

        .LoggerContext = ErrorLoggingContext,
        .Logger = Logger,
    };
    return true;
}


#endif /* R3051_ASSEMBLER_C */

