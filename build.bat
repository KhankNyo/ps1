@echo off

REM ------------------------------------------------------------------------------------------------
REM                                    Raylib compliation configs
REM ------------------------------------------------------------------------------------------------
set RAYLIB_SRC="%CD%\extern\raylib_multi50\src"

REM MSVC specific stuff 
set RAYLIB_MSVC_COMP=/std:c11 /Od /Zi /utf-8 /validate-charset /EHsc
REM set RAYLIB_MSVC_COMP=/std:c11 /O1 /GL /favor:blend /utf-8 /validate-charset /EHsc
set RAYLIB_MSVC_LINK=/link kernel32.lib user32.lib shell32.lib winmm.lib gdi32.lib opengl32.lib
set RAYLIB_MSVC_INC=/I"%RAYLIB_SRC%" /I"%RAYLIB_SRC%\external\glfw\include"
set RAYLIB_MSVC_DEFINES=/D_DEFAULT_SOURCE /DPLATFORM_DESKTOP /DGRAPHICS_API_OPENGL_33

REM generic compiler stuff 
REM RAYLIB_CC_COMP=-std=c11 -O0 -ggdb
set RAYLIB_CC_COMP=-std=c11 -O1
set RAYLIB_CC_LINK=-lkernel32 -luser32 -lshell32 -lwinmm -lgdi32 -lopengl32
set RAYLIB_CC_INC=-I"%RAYLIB_SRC%" -I"%RAYLIB_SRC%\external\glfw\include"
set RAYLIB_CC_DEFINES=-D_DEFAULT_SOURCE -DPLATFORM_DESKTOP -DGRAPHICS_API_OPENGL_33

set RAYLIB_C_FILES="%RAYLIB_SRC%\rcore.c" "%RAYLIB_SRC%\rshapes.c" "%RAYLIB_SRC%\rtextures.c" ^
                    "%RAYLIB_SRC%\rtext.c" "%RAYLIB_SRC%\rmodels.c" "%RAYLIB_SRC%\utils.c" ^
                    "%RAYLIB_SRC%\raudio.c" "%RAYLIB_SRC%\rglfw.c"


REM ------------------------------------------------------------------------------------------------
REM                                     Project compilation config 
REM ------------------------------------------------------------------------------------------------
set SRC_DIR="%CD%\src"
set EXTERN_DIR="%CD%\extern"
set BIN_DIR="%CD%\bin"
set UNITY_BUILD_FILE="%SRC_DIR%\Build.c"
set APPNAME=PS1Emu.exe

set MSVC_COMP=/DDEBUG /Zi /Od
set MSVC_INC=/I"%SRC_DIR%\Include" /I"%RAYLIB_SRC%" /I"%EXTERN_DIR%\glew"
set CC=gcc
set CC_COMP=-O1 -DDEBUG -Wall -Wextra -Wpedantic -Wno-missing-braces
set CC_INC=-I"%SRC_DIR%\Include" -I"%RAYLIB_SRC%" -I"%EXTERN_DIR%\glew"


REM ------------------------------------------------------------------------------------------------
REM                                          Build logic 
REM ------------------------------------------------------------------------------------------------
if "clean"=="%1" (
    REM remove project build artifacts.
    if exist "%BIN_DIR%\" rmdir /q /s "%BIN_DIR%\"

    REM remove external library's build artifacts.
    if "all"=="%2" if exist "%EXTERN_DIR%\bin\" rmdir /q /s "%EXTERN_DIR%\bin"

    echo:
    echo      Removed build binaries 
    echo:
) else (
    if not exist "%BIN_DIR%\" mkdir "%BIN_DIR%"

    REM compile with cl (MSVC)
    if "cl"=="%1" (
        if "%VisualStudioVersion%"=="" call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64

        REM compile external libraries (only glew for now)
        if not exist "%EXTERN_DIR%\bin\glew.obj" (
            if not exist "%EXTERN_DIR%\bin" mkdir "%EXTERN_DIR%\bin"
            pushd "%EXTERN_DIR%\bin"
                cl /c %MSVC_COMP% /DGLEW_STATIC %MSVC_INC% ^
                    "%EXTERN_DIR%\glew\glew.c"
            popd
        )
        pushd %BIN_DIR%

            cl %MSVC_COMP% %MSVC_INC% /FeGuiR3000A.exe "%SRC_DIR%\Win32_GuiR3000A.c" ^
                /link gdi32.lib opengl32.lib user32.lib kernel32.lib shell32.lib comdlg32.lib

            cl %MSVC_COMP% %MSVC_INC% %UNITY_BUILD_FILE% /Fe%APPNAME%
            cl %MSVC_COMP% %MSVC_INC% /DSTANDALONE "%SRC_DIR%\R3000A.c" /FeR3000A.exe
            cl %MSVC_COMP% %MSVC_INC% /DSTANDALONE "%SRC_DIR%\Disassembler.c" /FeDisassembler.exe
            cl %MSVC_COMP% %MSVC_INC% /DSTANDALONE "%SRC_DIR%\Assembler.c" /FeAssembler.exe
        popd 

    ) else ( REM compile with other compilers

        REM compile external library (only glew for now)
        if not exist "%EXTERN_DIR%\bin\glew.o" (
            if not exist "%EXTERN_DIR%\bin" mkdir "%EXTERN_DIR%\bin"
            pushd "%EXTERN_DIR%\bin"
                %CC% %CC_INC% -O2 -DGLEW_STATIC -Wno-attributes^
                    -c "%EXTERN_DIR%\glew\glew.c" "%EXTERN_DIR%\bin\glew.o"
            popd
        )
        %CC% %CC_COMP% %CC_INC% "%SRC_DIR%\Win32_GuiR3000A.c" -o "%BIN_DIR%\GuiR3000A.exe" ^
            -lgdi32 -lopengl32 -lshell32 -lcomdlg32

        %CC% %CC_COMP% %CC_INC% %UNITY_BUILD_FILE% -o "%BIN_DIR%\%APPNAME%"
        %CC% %CC_COMP% %CC_INC% -DSTANDALONE "%SRC_DIR%\R3000A.c" -o "%BIN_DIR%\R3000A.exe"
        %CC% %CC_COMP% %CC_INC% -DSTANDALONE "%SRC_DIR%\Disassembler.c" -o "%BIN_DIR%\Disassembler.exe"
        %CC% %CC_COMP% %CC_INC% -DSTANDALONE "%SRC_DIR%\Assembler.c" -o "%BIN_DIR%\Assembler.exe"
    )

    echo:
    if ERRORLEVEL 1 (
        echo        Build failed with exit code 1
    ) else (
        echo        Build finished
    )
    echo:
)

