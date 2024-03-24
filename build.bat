@echo off


set "UNITY_BUILD_FILE=src\Build.c"
set "APPNAME=PS1Emu.exe"

set "MSVC_INC=/I..\src\Include"
set "CC_CMD=gcc -O0"
set "CC_INC=-Isrc\Include"

if "clean"=="%1" (
    if exist bin\ rmdir /q /s bin
    echo:
    echo Removed build binaries 
    echo:
) else (
    if not exist bin\ mkdir bin\

    if "cl"=="%1" (
        if "%VisualStudioVersion%"=="" call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
        pushd bin
            cl /Zi ..\%UNITY_BUILD_FILE% /Fe%APPNAME% %MSVC_INC%
            cl /Zi /DSTANDALONE ..\src\Disassembler.c /FeDisassembler.exe %MSVC_INC%
            cl /Zi /DSTANDALONE ..\src\R3051.c /FeR3051.exe %MSVC_INC%
            cl /Zi /DSTANDALONE ..\src\Assembler.c /FeAssembler.exe %MSVC_INC%
        popd 
    ) else (
        %CC_CMD% %CC_INC% %UNITY_BUILD_FILE% -o bin\%APPNAME%
        %CC_CMD% %CC_INC% -DSTANDALONE src\Disassembler.c -o bin\Disassembler.exe
        %CC_CMD% %CC_INC% -DSTANDALONE src\R3051.c -o bin\R3051.exe
        %CC_CMD% %CC_INC% -DSTANDALONE src\Assembler.c -o bin\Assembler.exe
    )

    echo:
    if ERRORLEVEL 1 (
        echo Build failed with exit code 1
    ) else (
        echo Build finished
    )
    echo:
)

