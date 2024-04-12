@echo off


set "UNITY_BUILD_FILE=src\Build.c"
set "APPNAME=PS1Emu.exe"

set "MSVC_INC=/I%CD%\src\Include /I%CD%\extern\Include"
set "MSVC_FLAGS=/DDEBUG"
set "CC_CMD=gcc -O2 -DDEBUG -Wall -Wextra -Wpedantic -Wno-missing-braces"
set "CC_INC=-Isrc\Include -Iextern\Include"

set RAYLIB_SRC="%CD%\extern\raylib\src"
set RAYLIB_INC=-I"%RAYLIB_SRC%" -I"%RAYLIB_SRC%\external\glfw\include"
set "RAYLIB_DEFINES=-D_DEFAULT_SOURCE -DPLATFORM_DESKTOP -DGRAPHICS_API_OPENGL_33"

set RAYLIB_MSVC_COMP=/std:c11 /Od /Zi /utf-8 /validate-charset /EHsc
set RAYLIB_MSVC_LINK=/link /LTCG kernel32.lib user32.lib shell32.lib winmm.lib gdi32.lib opengl32.lib
set RAYLIB_MSVC_INC=/I"%RAYLIB_SRC%" /I"%RAYLIB_SRC%\external\glfw\include"
set "RAYLIB_MSVC_DEFINES=/D_DEFAULT_SOURCE /DPLATFORM_DESKTOP /DGRAPHICS_API_OPENGL_33"

set RAYLIB_C_FILES="%RAYLIB_SRC%\rcore.c" "%RAYLIB_SRC%\rshapes.c" "%RAYLIB_SRC%\rtextures.c" ^
                    "%RAYLIB_SRC%\rtext.c" "%RAYLIB_SRC%\rmodels.c" "%RAYLIB_SRC%\utils.c" ^
                    "%RAYLIB_SRC%\raudio.c" "%RAYLIB_SRC%\rglfw.c"

if "clean"=="%1" (
    if exist bin\ rmdir /q /s bin
    echo:
    echo Removed build binaries 
    echo:
) else (
    if not exist bin\ mkdir bin\

    if "cl"=="%1" (
        if "%VisualStudioVersion%"=="" call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64

        if not exist extern\bin\ (
            mkdir extern\bin
            pushd extern\bin
                cl /c %RAYLIB_MSVC_COMP% %RAYLIB_MSVC_INC% %RAYLIB_C_FILES%
            popd
        )
        pushd bin
            cl /c %MSVC_FLAGS% %MSVC_INC% /DSTANDALONE ..\src\R3000A.c
            cl /FeR3000A.exe ..\extern\bin\*.obj R3000A.obj %RAYLIB_MSVC_LINK%

            cl /Zi %MSVC_FLAGS% %MSVC_INC% ..\%UNITY_BUILD_FILE% /Fe%APPNAME%
            cl /Zi %MSVC_FLAGS% %MSVC_INC% /DSTANDALONE ..\src\Disassembler.c /FeDisassembler.exe
            cl /Zi %MSVC_FLAGS% %MSVC_INC% /DSTANDALONE ..\src\Assembler.c /FeAssembler.exe
        popd 
    ) else (
        %CC_CMD% %CC_INC% %UNITY_BUILD_FILE% -o bin\%APPNAME%
        %CC_CMD% %CC_INC% -DSTANDALONE src\Disassembler.c -o bin\Disassembler.exe
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

