
#include <windows.h>
#include <shellapi.h>
#include <wingdi.h>
#include <winuser.h>

#include "Common.h"
#include "R3000A.h"

#include "Disassembler.c"
#include "R3000A.c"
#include "CP0.c"

#define true 1
#define false 0
typedef unsigned char u8;
typedef u8 Bool8;
#define MAINTHREAD_CREATE_WINDOW (WM_USER + 0)
#define MAINTHREAD_CLOSE_WINDOW (WM_USER + 1)
#define MAINTHREAD_SET_VERT_SCROLL_INFO (WM_USER + 3)
#define MSGTHREAD_MOVING (WM_USER + 32)
#define MSGTHREAD_SIZING (WM_USER + 33)

#define SET_SIZING_TYPE(v) (((v) & 0x7) << (14*2))
#define SET_SIZING_HEIGHT(v) (((v) & 0x3FFF) << 14)
#define SET_SIZING_WIDTH(v) (((v) & 0x3FFF) << 0)
#define GET_SIZING_TYPE(u32v) (((u32v) >> (14*2)) & 0x7)
#define GET_SIZING_HEIGHT(u32v) (((u32v) >> 14) & 0x3FFF)
#define GET_SIZING_WIDTH(u32v) ((u32v) & 0x3FFF)

typedef enum TestSyscall 
{
    TESTSYS_WRITESTR    = 0x70000000,
    TESTSYS_WRITEHEX    = 0x70010000,
    TESTSYS_EXIT        = 0x72000000,
} TestSyscall;

typedef struct DisassemblyRegions 
{
    RECT Addr, 
         Hex, 
         Mnemonic;
} DisassemblyRegions;

typedef struct DisassemblyData 
{
    char Addr[2048];
    char HexCode[2048];
    char Mnemonic[4096];
} DisassemblyData;




#define IS_MENU_COMMAND(cmd) IN_RANGE(MAINMENU_OPEN_FILE, cmd, LOGMENU_SWAP)
typedef enum Win32_MainMenuCommand 
{
    MAINMENU_OPEN_FILE = 2,
    MAINMENU_DISASM_WINDOW,
    MAINMENU_CPUSTATE_WINDOW,
    MAINMENU_LOG_WINDOW,
    LOGMENU_CLEAR,
    LOGMENU_SWAP,
} Win32_MainMenuCommand;


typedef struct Win32_CreateWindowArgs 
{
    UINT dwExStyle;
    const char *lpClassName;
    const char *lpWindowName;
    UINT dwStyle;
    int x, y, w, h;
    HWND ParentWindow; 
    HMENU Menu;
} Win32_CreateWindowArgs;

typedef struct Win32_EnumMoveArgs 
{
    int Dx, Dy;
    int WindowsLeft;
    HWND WindowHandles[4];
} Win32_EnumMoveArgs;

typedef struct Win32_ClientRegion
{
    HDC TmpBackDC, TmpFrontDC;
    HBITMAP TmpBitmap;
    int x, y, w, h;
    RECT Rect;
} Win32_ClientRegion;

typedef struct Win32_Window 
{
    int x, y, w, h;
    HWND Handle;
    uint CurrentShowState:1;
    UINT ShowState[2];
} Win32_Window;

typedef struct Win32_BufferData 
{
    u8 *Ptr;
    iSize SizeBytes;
} Win32_BufferData;

typedef struct StringBuffer 
{
    char *Ptr;
    iSize Size;
    iSize Capacity;
} StringBuffer;

typedef struct Win32_MainWindowState 
{
    HWND WindowManager;
    Win32_Window MainWindow;
    union {
        struct {
            Win32_Window DisasmWindow,
                         CPUWindow, 
                         LogWindow;
        };
        Win32_Window ChildWindow[3];
    };
    HMENU MainMenu;
    HMENU LogMenu;

    R3000A CPU;
    u32 CPUCyclesPerSec;

    u32 DisasmFlags;
    u32 DisasmInstructionPerPage;
    u64 DisasmAddr, 
        DisasmResetAddr, 
        DisasmMinAddr;
    Win32_BufferData OSMem;

    unsigned char LastKey;
    Bool8 KeyIsDown[0x100];
    Bool8 KeyWasDown[0x100];
    double KeyDownTimestampMillisec[0x100];
    char DroppedFileName[1024];
    StringBuffer *WorkingStrBuffer;
    StringBuffer StdOut;
    StringBuffer StdErr;
} Win32_MainWindowState;

typedef struct Win32_DisassemblyWindow
{
    Win32_CreateWindowArgs Attribute;
} Win32_DisassemblyWindow;


static DWORD Win32_MainThreadID;
static DWORD Win32_MsgThreadID;
static double Win32_PerfCounterRes;
static SYSTEM_INFO Win32_SysInfo;
static char Win32_TmpFileName[0x10000];


#define Win32_MsgBox(title, flags, ...) do {\
    char tmp_buffer[1024];\
    snprintf(tmp_buffer, sizeof tmp_buffer, __VA_ARGS__);\
    MessageBoxA(NULL, tmp_buffer, title, flags);\
} while (0)

#define Win32_DrawStrFmt(device_context, rectangle_region, flags, buffer_size, ...) do {\
    char tmp_buffer[buffer_size];\
    snprintf(tmp_buffer, sizeof tmp_buffer, __VA_ARGS__);\
    DrawTextA(device_context, tmp_buffer, -1, rectangle_region, flags);\
} while (0)

static Bool8 Win32_IsKeyPressed(const Win32_MainWindowState *State, UINT VirtualKeyCode)
{
    ASSERT(VirtualKeyCode <= sizeof State->KeyWasDown);
    return State->KeyWasDown[VirtualKeyCode] && !State->KeyIsDown[VirtualKeyCode];
}


static void Win32_Fatal(const char *Msg)
{
    Win32_MsgBox("Fatal Error", MB_ICONERROR, "%s", Msg);
    ExitProcess(1);
}

static void Win32_InitSystem(void)
{
    /* init perf counter */
    LARGE_INTEGER Li;
    QueryPerformanceFrequency(&Li);
    Win32_PerfCounterRes = 1000.0 / Li.QuadPart;

    /* init sysinfo */
    GetSystemInfo(&Win32_SysInfo);
}

static double Win32_GetTimeMillisec(void)
{
    LARGE_INTEGER Li;
    QueryPerformanceCounter(&Li);
    return Win32_PerfCounterRes * Li.QuadPart;
}



static Win32_Window Win32_CreateWindowOrDie(
    HWND WindowManager, 
    HWND Parent, 
    const char *Title,
    int x, int y, int w, int h,
    const char *ClassName, 
    DWORD dwExStyle, 
    DWORD dwStyle,
    HMENU Menu
)
{
    Win32_CreateWindowArgs Args = {
        .x = x, 
        .y = y, 
        .w = w, 
        .h = h, 
        .Menu = Menu, 
        .dwExStyle = dwExStyle, 
        .dwStyle = dwStyle, 
        .lpClassName = ClassName, 
        .lpWindowName = Title, 
        .ParentWindow = Parent, 
    };
    HWND Handle = (HWND)SendMessageA(WindowManager, MAINTHREAD_CREATE_WINDOW, (WPARAM)&Args, 0);
    if (NULL == Handle)
    {
        Win32_Fatal("Unable to create a window");
    }
    Win32_Window Window = {
        .Handle = Handle,
        .x = x, 
        .y = y,
        .w = w,
        .h = h,
        .CurrentShowState = 0,
        .ShowState = { SW_HIDE, SW_SHOW },
    };
    return Window;
}

static void Win32_ToggleShowState(Win32_Window *Window)
{
    ShowWindow(Window->Handle, Window->ShowState[Window->CurrentShowState++]); 
}


static Win32_ClientRegion Win32_BeginPaint(Win32_Window *Window)
{
    RECT Rect;
    GetClientRect(Window->Handle, &Rect);
    Win32_ClientRegion ClientRegion = { 
        .x = Rect.left, 
        .y = Rect.top,
        .w = Rect.right - Rect.left,
        .h = Rect.bottom - Rect.top,
        .Rect = Rect,
    };

    ClientRegion.TmpFrontDC = GetDC(Window->Handle);
    if (NULL == ClientRegion.TmpFrontDC)
        Win32_Fatal("Unable to retrieve device context of a window.");

    ClientRegion.TmpBackDC = CreateCompatibleDC(ClientRegion.TmpFrontDC);
    if (NULL == ClientRegion.TmpBackDC)
        Win32_Fatal("Unable to retrieve a backbuffer.");

    ClientRegion.TmpBitmap = CreateCompatibleBitmap(ClientRegion.TmpFrontDC, ClientRegion.w, ClientRegion.h);
    if (NULL == ClientRegion.TmpBitmap)
        Win32_Fatal("Unable to retrieve a backbuffer.");

    SelectObject(ClientRegion.TmpBackDC, ClientRegion.TmpBitmap);
    return ClientRegion;
}

static void Win32_EndPaint(Win32_ClientRegion *Region)
{
    /* BitBlt preserves alpha */
    BitBlt(
        Region->TmpFrontDC, 
        0, 0, Region->w, Region->h,
        Region->TmpBackDC, 
        0, 0,
        SRCCOPY
    );
    DeleteDC(Region->TmpBackDC);
    DeleteDC(Region->TmpFrontDC);
    DeleteObject(Region->TmpBitmap);
}

static void Win32_BlendRegion(HDC DeviceContext, const RECT *Rect, COLORREF Color)
{
    /* we create a 1x1 32bpp bitmap, 
     * then spread it over the destination */
    BITMAPINFO BitmapInfo = {
        .bmiHeader = {
            .biSize = sizeof BitmapInfo.bmiHeader, 
            .biWidth = 1,
            .biHeight = 1,
            .biPlanes = 1,
            .biBitCount = 32,
            .biCompression = BI_RGB,
        },
    };

    StretchDIBits(DeviceContext, 
        Rect->left, Rect->top, Rect->right - Rect->left, Rect->bottom - Rect->top,
        0, 0, 1, 1, 
        &Color, &BitmapInfo, 
        DIB_RGB_COLORS, 
        SRCAND
    );
}



static void *Win32_AllocateMemory(iSize SizeBytes)
{
    void *Ptr = VirtualAlloc(NULL, SizeBytes, MEM_COMMIT, PAGE_READWRITE);
    if (NULL == Ptr)
    {
        Win32_Fatal("Out of memory.");
    }
    return Ptr;
}

static void Win32_DeallocateMemory(void *Ptr)
{
    VirtualFree(Ptr, 0, 0);
}

static void Memcpy(void *Dst, const void *Src, iSize Size)
{
    u8 *DstPtr = Dst;
    const u8 *SrcPtr = Src;
    while (Size --> 0)
    {
        *DstPtr++ = *SrcPtr++;
    }
}



static void StrResizeCapacity(StringBuffer *StrBuf, iSize NewCapacity)
{
    char *Ptr = Win32_AllocateMemory(NewCapacity);
    ASSERT(NewCapacity > StrBuf->Size);
    Memcpy(Ptr, StrBuf->Ptr, StrBuf->Size);
    Win32_DeallocateMemory(StrBuf->Ptr);

    StrBuf->Ptr = Ptr;
    StrBuf->Capacity = NewCapacity;
}

static void StrWriteFmt(StringBuffer *StrBuf, const char *Fmt, ...)
{
    va_list Args;
    va_start(Args, Fmt);

    /* TODO: this is slow */
    int RequiredLen = vsnprintf(NULL, 0, Fmt, Args);
    if (StrBuf->Size + RequiredLen + 1 > StrBuf->Capacity)
    {
        iSize NewCapacity = (StrBuf->Size + RequiredLen + 1)*4;
        StrResizeCapacity(StrBuf, NewCapacity);
    }

    int WrittenLen = vsnprintf(
        StrBuf->Ptr + StrBuf->Size, 
        StrBuf->Capacity - StrBuf->Size, 
        Fmt, Args
    );
    StrBuf->Size += WrittenLen;

    va_end(Args);
}




static LRESULT CALLBACK Win32_ManagerWndProc(HWND Window, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    LRESULT Result = 0;
    switch (Msg)
    {
    case MAINTHREAD_CREATE_WINDOW:
    {
        Win32_CreateWindowArgs *Args = (Win32_CreateWindowArgs*)(wParam);
        Result = (LRESULT)CreateWindowExA(
            Args->dwExStyle, 
            Args->lpClassName, 
            Args->lpWindowName, 
            Args->dwStyle, 
            Args->x, 
            Args->y, 
            Args->w, 
            Args->h, 
            Args->ParentWindow, 
            Args->Menu, 
            NULL, 
            NULL
        );
    } break;
    case MAINTHREAD_CLOSE_WINDOW:
    {
        CloseWindow((HWND)wParam);
    } break;
    case MAINTHREAD_SET_VERT_SCROLL_INFO:
    {
        HWND Window = (HWND)wParam;
        SCROLLINFO *ScrollInfo = (SCROLLINFO *)lParam;
        SetScrollInfo(Window, SB_VERT, ScrollInfo, FALSE);
    } break;
    default:
    {
        Result = DefWindowProcA(Window, Msg, wParam, lParam);
    } break;
    }
    return Result;
}

static LRESULT CALLBACK Win32_MainWndProc(HWND Window, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    /* this function runs on the message thread (the first thread),
     * to forward anything to the processing thread, 
     * we pass it using PostThreadMessage through the wParam or lParam or both. 
     * By using PostThreadMessage, we don't need to worry about any race condition, mutexes and any of that stuff ourselves, 
     *
     * https://github.com/cmuratori/dtc/blob/main/dtc.cpp
     */
    LRESULT Result = 0;
    switch (Msg)
    {
    case WM_CLOSE:
    case WM_QUIT:
    {
        /* telling the main thread to quit (it'll send back a destroy message to the manager window) */
        PostThreadMessageA(Win32_MainThreadID, WM_CLOSE, (WPARAM)Window, 0);
    } break;

    case WM_ERASEBKGND:
    {
        Result = TRUE; /* don't want GDI to clear our background, we clear it ourselves */
    } break;
    case WM_COMMAND:
    {
        Win32_MainMenuCommand MenuCommand = LOWORD(wParam);
        if (IS_MENU_COMMAND(MenuCommand))
            PostThreadMessageA(Win32_MainThreadID, WM_COMMAND, MenuCommand, (LPARAM)Window);
    } break;

    case WM_MOVING:
    {
        RECT Rect = *(RECT *)lParam;
        int x = Rect.left;
        int y = Rect.top;
        lParam = y << 16 | (x & 0xFFFF);
        PostThreadMessage(Win32_MainThreadID, MSGTHREAD_MOVING, (WPARAM)Window, lParam);
    } break;
    case WM_SIZING:
    {
        RECT Rect = *(RECT *)lParam;
        int w = Rect.right - Rect.left;
        int h = Rect.bottom - Rect.top;
        u32 Param = 
            SET_SIZING_TYPE(wParam)
            | SET_SIZING_WIDTH(w) 
            | SET_SIZING_HEIGHT(h);
        PostThreadMessage(Win32_MainThreadID, MSGTHREAD_SIZING, (WPARAM)Window, Param);
    } break;
    case WM_KEYUP:
    case WM_KEYDOWN:
    case WM_SIZE:
    case WM_DROPFILES:
    {
        PostThreadMessage(Win32_MainThreadID, Msg, wParam, lParam);
    } break;

    default: 
    {
        Result = DefWindowProc(Window, Msg, wParam, lParam);
    } break;
    }
    return Result;
}

static LRESULT CALLBACK Win32_DisasmWndProc(HWND Window, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    LRESULT Result = 0;
    switch (Msg)
    {
    case WM_MOUSEWHEEL:
    case WM_VSCROLL:
    {
        PostThreadMessageA(Win32_MainThreadID, Msg, wParam, (LPARAM)Window);
    } break;
    default: 
    {
        Result = Win32_MainWndProc(Window, Msg, wParam, lParam);
    } break;
    }
    return Result;
}


static OPENFILENAMEA Win32_CreateFileSelectionPrompt(HWND Window, char *FileNameBuffer, iSize SizeBytes, DWORD PromptFlags)
{
    /* get the current file name to show in the dialogue */
    OPENFILENAMEA DialogueConfig = {
        .lStructSize = sizeof(DialogueConfig),
        .hwndOwner = Window,
        .hInstance = 0, /* ignored if OFN_OPENTEMPLATEHANDLE is not set */
        .lpstrFilter = "Text documents (*.txt)\0*.txt\0All files (*)\0*\0",
        /* no custom filter */
        /* no custom filter size */
        /* this is how you get hacked */
        .nFilterIndex = 2, /* the second pair of strings in lpstrFilter separated by the null terminator???? */
        .lpstrFile = FileNameBuffer,

        .nMaxFile = SizeBytes,
        /* no file title, optional */
        /* no size of the above, optional */
        /* initial directory is current directory */
        /* default title ('Save as' or 'Open') */
        .Flags = PromptFlags,
        .nFileOffset = 0,
        .nFileExtension = 0, /* ??? */
        /* no default extension */
        /* no extra flags */
    };
    return DialogueConfig;
}

static void Win32_ReadFileSyncIntoOSMem(
    Win32_MainWindowState *State, 
    const char *FileName, 
    DWORD dwCreationDisposition)
{
    /* open file, despite name */
    HANDLE FileHandle = CreateFile(
        FileName, 
        GENERIC_READ, 
        0, 
        NULL, 
        dwCreationDisposition, 
        FILE_ATTRIBUTE_NORMAL, 
        NULL
    );
    if (INVALID_HANDLE_VALUE == FileHandle)
        goto CreateFileFailed;

    LARGE_INTEGER ArchaicFileSize;
    if (!GetFileSizeEx(FileHandle, &ArchaicFileSize))
        goto GetFileSizeFailed;

    iSize FileSize = ArchaicFileSize.QuadPart;
    if (FileSize <= State->OSMem.SizeBytes)
    {
        /* reuse old buffer */
        DWORD ReadSize;
        if (!ReadFile(FileHandle, State->OSMem.Ptr, FileSize, &ReadSize, NULL) || ReadSize != FileSize)
            goto ReadFileFailed;
    }
    else
    {
        /* allocate a new one */
        /* round the file size to multiple of the memory page size */
        iSize PageSize = Win32_SysInfo.dwAllocationGranularity;
        iSize BufferSize = ((FileSize + PageSize) / PageSize) * PageSize;
        void *Buffer = Win32_AllocateMemory(BufferSize);

        DWORD ReadSize;
        if (!ReadFile(FileHandle, Buffer, FileSize, &ReadSize, NULL) || ReadSize != FileSize)
            goto ReadFileFailed;

        /* deallocate the old OS memory */
        Win32_DeallocateMemory(State->OSMem.Ptr);
        State->OSMem.Ptr = Buffer;
        State->OSMem.SizeBytes = BufferSize;
    }
    State->DisasmAddr = State->DisasmResetAddr;
    CloseHandle(FileHandle);
    return;


ReadFileFailed:
GetFileSizeFailed:
    CloseHandle(FileHandle);
CreateFileFailed:
    Win32_MsgBox("Error", MB_ICONERROR, 
        "Unable to open '%s'", FileName
    );
}

static void Win32_SetWindowScrollbar(HWND WindowManager, Win32_Window *Window, u64 Pos, u64 Low, u64 High)
{
    /* to prevent the scrollbar from disappearing */
    if (High == Low)
    {
        High = Low + 256;
    }
    SCROLLINFO ScrollInfo = {
        .cbSize = sizeof ScrollInfo, 
        .fMask = SIF_RANGE | SIF_POS,
        .nPos = Pos,
        .nMax = High,
        .nMin = Low,
    };
    SendMessageA(WindowManager, MAINTHREAD_SET_VERT_SCROLL_INFO, (WPARAM)Window->Handle, (LPARAM)&ScrollInfo);
}


static Bool8 Win32_MainPollInputs(HWND WindowManager, Win32_MainWindowState *State)
{
    MSG Msg;
    State->KeyWasDown[State->LastKey] = State->KeyIsDown[State->LastKey];
    while (PeekMessageA(&Msg, 0, 0, 0, PM_REMOVE))
    {
        switch (Msg.message)
        {
        case WM_CLOSE:
        case WM_QUIT:
        {
            HWND Window = (HWND)Msg.wParam;
            if (Window == State->DisasmWindow.Handle)
            {
                Win32_ToggleShowState(&State->DisasmWindow);
            }
            else if (Window == State->CPUWindow.Handle)
            {
                Win32_ToggleShowState(&State->CPUWindow);
            }
            else /* close main window */
            {
                /* because the message we're recieving is from the message window, 
                 * we can't just send the same message back (if we do, that'll be an infinite loop), 
                 * so we send our custom messages instead */
                SendMessageA(WindowManager, MAINTHREAD_CLOSE_WINDOW, (WPARAM)Window, 0);
                return false;
            }
        } break;
        case WM_COMMAND:
        {
            Win32_MainMenuCommand Cmd = LOWORD(Msg.wParam);
            if (HIWORD(Msg.wParam) == 0)
            {
                switch (Cmd)
                {
                case MAINMENU_OPEN_FILE:
                {
                    OPENFILENAMEA Prompt = Win32_CreateFileSelectionPrompt(
                        State->MainWindow.Handle, 
                        Win32_TmpFileName, 
                        sizeof Win32_TmpFileName,
                        0
                    );
                    if (GetOpenFileNameA(&Prompt))
                    {
                        Win32_ReadFileSyncIntoOSMem(State, Prompt.lpstrFile, OPEN_EXISTING);
                    }
                } break;
                case MAINMENU_DISASM_WINDOW: 
                {
                    Win32_ToggleShowState(&State->DisasmWindow);
                } break;
                case MAINMENU_CPUSTATE_WINDOW:
                {
                    Win32_ToggleShowState(&State->CPUWindow);
                } break;
                case MAINMENU_LOG_WINDOW:
                {
                    Win32_ToggleShowState(&State->LogWindow);
                } break;

                case LOGMENU_CLEAR:
                {
                    ASSERT(State->WorkingStrBuffer 
                        && (State->WorkingStrBuffer == &State->StdOut 
                        || State->WorkingStrBuffer == &State->StdErr)
                    );
                    State->WorkingStrBuffer->Size = 0;
                } break;
                case LOGMENU_SWAP:
                {
                    if (State->WorkingStrBuffer == &State->StdOut)
                        State->WorkingStrBuffer = &State->StdErr;
                    else if (State->WorkingStrBuffer == &State->StdErr)
                        State->WorkingStrBuffer = &State->StdOut;
                } break;
                }
            }
        } break;
        case WM_DROPFILES:
        {
            HDROP Dropped = (HDROP)Msg.wParam;

            /* get the first dropped file's name */
            UINT FileNameLength = DragQueryFileA(Dropped, 0, NULL, 0);
            if (FileNameLength < sizeof Win32_TmpFileName)
            {
                DragQueryFileA(Dropped, 0, Win32_TmpFileName, sizeof Win32_TmpFileName);
            }
            else
            {
                Win32_MsgBox("Error", MB_ICONERROR, 
                    "File name too big (must be less than %zu).", sizeof Win32_TmpFileName
                );
            }

            UINT FileCount = DragQueryFileA(Dropped, -1, NULL, 0); /* get file count */
            if (FileCount != 1)
            {
                Win32_MsgBox("Warning", MB_ICONWARNING, 
                    "%u files recieved, but only '%s' will be used.", 
                    FileCount, Win32_TmpFileName
                );
            }


            Win32_ReadFileSyncIntoOSMem(State, Win32_TmpFileName, OPEN_EXISTING);
            ShowWindow(State->DisasmWindow.Handle, SW_SHOW);
        } break;
        case WM_KEYUP:
        case WM_KEYDOWN:
        {
            unsigned char Key = Msg.wParam & 0xFF;
            Bool8 KeyDown = Msg.message == WM_KEYDOWN;
            State->KeyIsDown[Key] = KeyDown;
            State->LastKey = Key;
            if (!KeyDown)
            {
                State->KeyDownTimestampMillisec[Key] = 0;
            }
            else if (!State->KeyDownTimestampMillisec[Key])
            {
                State->KeyDownTimestampMillisec[Key] = Win32_GetTimeMillisec();
            }
        } break;
        case WM_MOUSEWHEEL:
        {
            double Res = 8;
            double MouseDelta = GET_WHEEL_DELTA_WPARAM(Msg.wParam);
            i32 ScrollDelta;

            /* make sure that when mouse delta is in -1..1, it will be map to -1, 0, or 1 */
            if (IN_RANGE(0, MouseDelta, Res))
                ScrollDelta = -1;
            else if (IN_RANGE(-Res, MouseDelta, 0))
                ScrollDelta = 1;
            else ScrollDelta = -MouseDelta / Res;

            State->DisasmAddr += ScrollDelta*4;
            SetScrollPos(State->DisasmWindow.Handle, SB_VERT, State->DisasmAddr/4, TRUE);
        } break;
        case WM_VSCROLL:
        {
            if (SB_ENDSCROLL == Msg.wParam)
                break;

            SCROLLINFO ScrollInfo = {
                .cbSize = sizeof ScrollInfo,
                .fMask = SIF_POS | SIF_RANGE | SIF_TRACKPOS,
            };
            HWND Window = (HWND)Msg.lParam;
            GetScrollInfo(Window, SB_VERT, &ScrollInfo);

            u32 Pos = ScrollInfo.nPos;
            switch (LOWORD(Msg.wParam))
            {
            case SB_BOTTOM:     Pos = ScrollInfo.nMax; break;
            case SB_TOP:        Pos = ScrollInfo.nMin; break;
            case SB_LINEUP:     Pos -= 1; break;
            case SB_LINEDOWN:   Pos += 1; break;
            case SB_PAGEUP:     Pos -= State->DisasmInstructionPerPage; break;
            case SB_PAGEDOWN:   Pos += State->DisasmInstructionPerPage; break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION:
            {
                Pos = ScrollInfo.nTrackPos;
            } break;
            }

            ScrollInfo.nPos = Pos;
            State->DisasmAddr = Pos*4;
            SetScrollInfo(Window, SB_VERT, &ScrollInfo, TRUE);
        } break;
        case MSGTHREAD_SIZING:
        {
            HWND Window = (HWND)Msg.wParam;
            if (Window != State->MainWindow.Handle)
                break;

            Win32_Window *MainWindow = &State->MainWindow;
            int w = GET_SIZING_WIDTH(Msg.lParam);
            int h = GET_SIZING_HEIGHT(Msg.lParam);
            int Dw = w - MainWindow->w;
            int Dh = h - MainWindow->h;
            MainWindow->w = w;
            MainWindow->h = h;

            /* resizing types that might affect x and y pos */
            uint Type = GET_SIZING_TYPE(Msg.lParam);
            switch (Type)
            {
            case WMSZ_TOPRIGHT:
            case WMSZ_TOP:
            {
                MainWindow->y -= Dh;
            } break;
            case WMSZ_BOTTOMLEFT:
            case WMSZ_LEFT:
            {
                MainWindow->x -= Dw;
            } break;
            case WMSZ_TOPLEFT:
            {
                MainWindow->x -= Dw;
                MainWindow->y -= Dh;
            } break;
            }
        } break;
        case MSGTHREAD_MOVING:
        {
            HWND Window = (HWND)Msg.wParam;
            if (Window != State->MainWindow.Handle)
                break;

            int x = (i16)LOWORD(Msg.lParam);
            int y = (i16)HIWORD(Msg.lParam);
            int Dx = x - State->MainWindow.x;
            int Dy = y - State->MainWindow.y;
            State->MainWindow.x = x;
            State->MainWindow.y = y;
            for (uint i = 0; i < STATIC_ARRAY_SIZE(State->ChildWindow); i++)
            {
                RECT Rect;
                GetWindowRect(State->ChildWindow[i].Handle, &Rect);
                Rect.left += Dx;
                Rect.right += Dx;
                Rect.top += Dy;
                Rect.bottom += Dy;
                State->ChildWindow[i].x += Dx;
                State->ChildWindow[i].y += Dy;

                /* resize the window on its owner thread */
                SetWindowPos(
                    State->ChildWindow[i].Handle, 
                    NULL, 
                    Rect.left,
                    Rect.top,
                    Rect.right - Rect.left,
                    Rect.bottom - Rect.top, 
                    SWP_NOOWNERZORDER
                );
            }
        } break;
        }
    }
    return true;
}






static u32 TranslateAddr(u32 LogicalAddr)
{
    switch (LogicalAddr)
    {
    case TESTSYS_WRITESTR:
    case TESTSYS_WRITEHEX:
    case TESTSYS_EXIT:
    {
        return LogicalAddr;
    } break;
    default: 
    {
        if (IN_RANGE(0x80000000, LogicalAddr, 0x9FFFFFFF))
            return LogicalAddr - 0x80000000;
        return LogicalAddr - R3000A_RESET_VEC;
    } break;
    }
}

static Bool8 AddrIsValid(u32 Addr, u32 DataSize, u32 MemSize)
{
    return Addr < MemSize 
        && Addr + DataSize < MemSize 
        && Addr + DataSize > Addr;
}

static DisassemblyData DisassembleMemory(
    const void *Mem, iSize SizeBytes, 
    iSize InstructionCount, 
    u32 VirtualAddr, 
    u32 DisasmFlags)
{
    const u8 *BytePtr = Mem;
    DisassemblyData Disasm;
    char *CurrIns = Disasm.Mnemonic;
    char *CurrAddr = Disasm.Addr;
    char *CurrHex = Disasm.HexCode;
    int InsLenLeft = sizeof Disasm.Mnemonic;
    int AddrLenLeft = sizeof Disasm.Addr;
    int HexLenLeft = sizeof Disasm.HexCode;
    for (iSize i = 0; 
        i < InstructionCount; 
        i++, VirtualAddr += 4)
    {
        /* disassemble the address */
        int Len = snprintf(CurrAddr, AddrLenLeft, "%08x:\n", VirtualAddr);
        CurrAddr += Len;
        AddrLenLeft -= Len;

        /* disassemble the instruction */
        /* disassemble the hexcode */
        u32 Addr = TranslateAddr(VirtualAddr);
        u32 Instruction = 0;
        if (AddrIsValid(Addr, sizeof Instruction, SizeBytes))
        {
            Memcpy(&Instruction, BytePtr + Addr, sizeof Instruction);
            char Mnemonic[128];
            R3000A_Disasm(Instruction, VirtualAddr, DisasmFlags, Mnemonic, sizeof Mnemonic);
            int Len = snprintf(CurrIns, InsLenLeft, "%s\n", Mnemonic);
            CurrIns += Len;
            InsLenLeft -= Len;

            Len = snprintf(CurrHex, HexLenLeft, "%08x\n", Instruction);
            CurrHex += Len;
            HexLenLeft -= Len;
        }
        else
        {
            int Len = snprintf(CurrIns, InsLenLeft, "???\n");
            CurrIns += Len;
            InsLenLeft -= Len;

            Len = snprintf(CurrHex, HexLenLeft, "????????\n");
            CurrHex += Len;
            HexLenLeft -= Len;
        }
    }
    return Disasm;
}

static DisassemblyRegions GetDisassemblyRegion(const RECT *ClientRect, int MaxAddrLen, int MaxHexLen)
{
    DisassemblyRegions Disasm = {
        .Addr = *ClientRect,
        .Hex = *ClientRect,
        .Mnemonic = *ClientRect,
    };
    Disasm.Addr.right = ClientRect->left + MaxAddrLen;
    Disasm.Hex.left = Disasm.Addr.right;
    Disasm.Hex.right = Disasm.Hex.left + MaxHexLen;
    Disasm.Mnemonic.left = Disasm.Hex.right;
    return Disasm;
}

static void MipsWrite(void *UserData, u32 LogicalAddr, u32 Data, R3000A_DataSize DataSize)
{
    Win32_MainWindowState *State = UserData;

    u32 Addr = TranslateAddr(LogicalAddr);
    if (AddrIsValid(Addr, DataSize, State->OSMem.SizeBytes))
    {
        for (int i = 0; i < (int)DataSize; i++)
        {
            State->OSMem.Ptr[Addr + i] = (Data >> i*8) & 0xFF;
        }
    }
    else if (Addr == TESTSYS_EXIT)
    {
        StrWriteFmt(&State->StdErr, "[LOG]: Exited\n");
    }
    else if (Addr == TESTSYS_WRITEHEX)
    {
        StrWriteFmt(&State->StdOut, "0x%0x\n", DataSize, Data);
    }
    else if (Addr == TESTSYS_WRITESTR)
    {
        u32 Location = TranslateAddr(Data);
        if (AddrIsValid(Location, 1, State->OSMem.SizeBytes))
        {
            const char *Str = (const char *)State->OSMem.Ptr + Location;
            int StrLen = 0;
            for (u32 i = Location; i < State->OSMem.SizeBytes && Str[StrLen]; i++)
            {
                StrLen++;
            }
            StrWriteFmt(&State->StdOut, "%.*s", StrLen, Str);
        }
        else
        {
            StrWriteFmt(&State->StdErr, "[LOG]: Invalid string address: %08x\n", Data);
        }
    }
    else
    {
        StrWriteFmt(&State->StdErr, "[LOG]: Invalid address: writing %08x of size %d to %08x\n", Data, DataSize, Addr);
    }
}

static u32 MipsRead(void *UserData, u32 LogicalAddr, R3000A_DataSize DataSize)
{
    Win32_MainWindowState *State = UserData;
    u32 Addr = TranslateAddr(LogicalAddr);
    if (AddrIsValid(Addr, DataSize, State->OSMem.SizeBytes))
    {
        u32 Data = 0;
        for (int i = 0; i < (int)DataSize; i++)
        {
            Data |= (u32)State->OSMem.Ptr[Addr + i] << i*8;
        }
        return Data;
    }
    else
    {
        StrWriteFmt(&State->StdErr, "[LOG]: Invalid address: reading %d bytes at %08x\n", DataSize, LogicalAddr);
    }
    return 0;
}

static Bool8 MipsVerify(void *UserData, u32 Addr)
{
    (void)UserData;
    (void)Addr;
    return true;
}



static Bool8 Win32_PaintDisasmWindow(
    Win32_MainWindowState *State, 
    int FontWidth, 
    int FontHeight, 
    HFONT FontHandle, 
    Bool8 RedrawPCIfNotViewable
)
{
    Win32_ClientRegion Region = Win32_BeginPaint(&State->DisasmWindow);
    {
        HDC DC = Region.TmpBackDC;
        /* clear background */
        FillRect(DC, &Region.Rect, WHITE_BRUSH);
        SelectObject(DC, FontHandle);


        /* disassembly variables */
        int InstructionCount = Region.h / FontHeight + 1; /* +1 for smooth transition */
        State->DisasmInstructionPerPage = InstructionCount;
        int AddrWidth = FontWidth*(8+2);
        int HexWidth  = FontWidth*(8+4);

        /* make sure PC is in drawable range before disassembly */
        u32 ViewableDisasmAddrLo = State->DisasmAddr;
        u32 ViewableDisasmAddrHi = ViewableDisasmAddrLo + InstructionCount*4;
        if (RedrawPCIfNotViewable && !IN_RANGE(ViewableDisasmAddrLo, State->CPU.PC, ViewableDisasmAddrHi))
        {
            State->DisasmAddr = State->CPU.PC;
            ViewableDisasmAddrLo = State->CPU.PC;
            ViewableDisasmAddrHi = State->CPU.PC + InstructionCount*4;
        }
        RedrawPCIfNotViewable = false;

        /* disassmble the instructions */
        DisassemblyData Disasm = DisassembleMemory(
            State->OSMem.Ptr, 
            State->OSMem.SizeBytes, 
            InstructionCount, 
            State->DisasmAddr, 
            State->DisasmFlags
        );
        DisassemblyRegions DisasmRegion = GetDisassemblyRegion(
            &Region.Rect, 
            AddrWidth, 
            HexWidth
        );

        /* draw disassembly */
        DrawTextA(DC, Disasm.Addr, -1, &DisasmRegion.Addr, DT_LEFT);
        DrawTextA(DC, Disasm.HexCode, -1, &DisasmRegion.Hex, DT_LEFT);
        DrawTextA(DC, Disasm.Mnemonic, -1, &DisasmRegion.Mnemonic, DT_LEFT);
        if (State->OSMem.SizeBytes != 0)
        {
            Win32_SetWindowScrollbar(
                State->WindowManager, 
                &State->DisasmWindow, 
                State->DisasmAddr/sizeof(u32), 
                State->DisasmMinAddr/sizeof(u32), 
                (State->DisasmMinAddr + State->OSMem.SizeBytes)/sizeof(u32) - InstructionCount
            );
        }

        /* draw the PC highlighter */
        int PCInstructionIndex = (State->CPU.PC - ViewableDisasmAddrLo)/sizeof(u32);
        if (IN_RANGE(0, PCInstructionIndex, InstructionCount - 2))
        {
            /* the highlighter surrounds an instruction */
            RECT PCRect = Region.Rect;
            PCRect.top += PCInstructionIndex * FontHeight;
            PCRect.bottom = PCRect.top + FontHeight;

            /* draw the highlighter */
            u32 HighlighterColorARGB = 0x00eb5d05;
            Win32_BlendRegion(DC, &PCRect, HighlighterColorARGB);
        }
    }
    Win32_EndPaint(&Region);
    return RedrawPCIfNotViewable;
}

static void Win32_PaintStateWindow(Win32_MainWindowState *State, int FontWidth, int FontHeight, HFONT FontHandle)
{
    Win32_ClientRegion Region = Win32_BeginPaint(&State->CPUWindow);
    {
        HDC DC = Region.TmpBackDC;
        SelectObject(DC, FontHandle);

        /* clear background */
        FillRect(DC, &Region.Rect, WHITE_BRUSH);

        /* register variables */
        int RegisterBoxWidth = FontWidth * 10;
        int RegisterBoxHeight = 2*FontHeight + 2;
        int RegisterBoxVerDist = RegisterBoxHeight + 5;
        int RegisterBoxHorDist = RegisterBoxWidth + 5;
        int RegisterBoxPerLine = 6;
        RECT RegisterBox = {
            .left = 10,
            .right = 10 + RegisterBoxWidth,
            .top = 10,
            .bottom = 10 + RegisterBoxHeight,
        };

        /* draw the registers R00..R31 */
        COLORREF Color = RGB(0x40, 0x40, 0x40);
        SetBkColor(DC, Color);
        SetTextColor(DC, RGB(0xFF, 0xFF, 0xFF));
        HBRUSH RegisterBoxColorBrush = CreateSolidBrush(Color);
        HBRUSH LastBrush = SelectObject(DC, RegisterBoxColorBrush);
        RECT CurrentRegisterBox = RegisterBox;
        uint RegisterCount = STATIC_ARRAY_SIZE(State->CPU.R);
        for (uint i = 0; i < RegisterCount; i++)
        {
            FillRect(DC, &CurrentRegisterBox, RegisterBoxColorBrush);
            Win32_DrawStrFmt(DC, &CurrentRegisterBox, DT_CENTER, 64, 
                "R%02d:\n%08x", i, State->CPU.R[i]
            );

            if ((i + 1) % RegisterBoxPerLine == 0)
            {
                CurrentRegisterBox.left = RegisterBox.left;
                CurrentRegisterBox.right = RegisterBox.right;
                CurrentRegisterBox.top += RegisterBoxVerDist;
                CurrentRegisterBox.bottom += RegisterBoxVerDist;
            }
            else
            {
                CurrentRegisterBox.left += RegisterBoxHorDist;
                CurrentRegisterBox.right += RegisterBoxHorDist;
            }
        }

        RECT PCRegisterBox = RegisterBox;
        FillRect(DC, &PCRegisterBox, RegisterBoxColorBrush);
        Win32_DrawStrFmt(DC, &PCRegisterBox, DT_CENTER, 64, 
            "PC:\n%08x", State->CPU.PC
        );

        DeleteObject(SelectObject(DC, LastBrush));
    }
    Win32_EndPaint(&Region);
}


static DWORD Win32_Main(LPVOID UserData)
{
    HWND WindowManager = UserData;

    Win32_InitSystem();

    double FPS = 60.0;
    Win32_MainWindowState State = { 
        .DisasmResetAddr = R3000A_RESET_VEC,
        .DisasmAddr = R3000A_RESET_VEC,
        .DisasmMinAddr = R3000A_RESET_VEC,
        .CPU = R3000A_Init(&State, MipsRead, MipsWrite, MipsVerify, MipsVerify),
        .CPUCyclesPerSec = FPS,
        .WindowManager = WindowManager,

        .WorkingStrBuffer = &State.StdOut,
    };

    /* create the main window */
    State.MainMenu = CreateMenu();
    AppendMenuA(State.MainMenu, MF_STRING, MAINMENU_DISASM_WINDOW, "&Disassembly");
    AppendMenuA(State.MainMenu, MF_STRING, MAINMENU_CPUSTATE_WINDOW, "&CPU");
    AppendMenuA(State.MainMenu, MF_STRING, MAINMENU_LOG_WINDOW, "&Log");
    AppendMenuA(State.MainMenu, MF_STRING, MAINMENU_OPEN_FILE, "&Open File");
    const char *MainWndClsName = "MainWndCls";
    WNDCLASSEXA MainWindowClass = {
        .cbSize = sizeof MainWindowClass,
        .style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC,
        .lpfnWndProc = Win32_MainWndProc,
        .hIcon = LoadIconA(NULL, IDI_APPLICATION),
        .hCursor = LoadCursorA(NULL, IDC_ARROW),
        .lpszClassName = MainWndClsName,
        .hInstance = GetModuleHandleA(NULL),
    };
    RegisterClassExA(&MainWindowClass);
    State.MainWindow = Win32_CreateWindowOrDie(
        WindowManager, 
        NULL,
        "R3000A Debug Emu",
        100, 100, 1080, 720, 
        MainWndClsName,
        WS_EX_OVERLAPPEDWINDOW,
        WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN, 
        State.MainMenu
    );
    DragAcceptFiles(State.MainWindow.Handle, TRUE);

    /* create the disassembly window */
    const char *ChildWndClsName = "ChildWndCls";
    WNDCLASSEXA ChildWindowClass = {
        .cbSize = sizeof ChildWindowClass,
        .style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC,
        .lpfnWndProc = Win32_DisasmWndProc,
        .hCursor = LoadCursorA(NULL, IDC_ARROW),
        .lpszClassName = ChildWndClsName,
        .hInstance = GetModuleHandleA(NULL),
    };
    RegisterClassExA(&ChildWindowClass);
    State.DisasmWindow = Win32_CreateWindowOrDie(
        WindowManager, 
        State.MainWindow.Handle,
        "Disassembly",
        State.MainWindow.x, 
        State.MainWindow.y + 60, 
        540, 660, 
        ChildWndClsName,
        WS_EX_OVERLAPPEDWINDOW,
        WS_THICKFRAME | WS_SYSMENU | WS_CAPTION | WS_VISIBLE | WS_VSCROLL,  
        NULL
    );
    DragAcceptFiles(State.DisasmWindow.Handle, TRUE);
    Win32_SetWindowScrollbar(
        State.WindowManager,
        &State.DisasmWindow, 
        R3000A_RESET_VEC/sizeof(u32), 
        R3000A_RESET_VEC/sizeof(u32), 
        R3000A_RESET_VEC/sizeof(u32)
    );

    /* create the state window */
    State.CPUWindow = Win32_CreateWindowOrDie(
        WindowManager, 
        State.MainWindow.Handle, 
        "CPU State",  
        State.DisasmWindow.x + State.DisasmWindow.w, 
        State.DisasmWindow.y, 
        540, 300, 
        ChildWndClsName, 
        WS_EX_OVERLAPPEDWINDOW, 
        WS_THICKFRAME | WS_SYSMENU | WS_CAPTION | WS_VISIBLE, 
        NULL
    );

    /* create the log window */
    State.LogMenu = CreateMenu();
    AppendMenuA(State.LogMenu, MF_STRING, LOGMENU_CLEAR, "&Clear");
    AppendMenuA(State.LogMenu, MF_STRING, LOGMENU_SWAP, "&Swap");
    State.LogWindow = Win32_CreateWindowOrDie(
        WindowManager,
        State.MainWindow.Handle, 
        "Log",
        State.CPUWindow.x, 
        State.CPUWindow.y + State.CPUWindow.h, 
        540, 360,
        ChildWndClsName, 
        WS_EX_OVERLAPPEDWINDOW, 
        WS_THICKFRAME | WS_SYSMENU | WS_CAPTION | WS_VISIBLE | WS_VSCROLL,
        State.LogMenu
    );

    LOGFONTA FontAttr = {
        .lfWidth = 8, 
        .lfHeight = 16,
        .lfWeight = FW_NORMAL,
        .lfCharSet = ANSI_CHARSET, 
        .lfOutPrecision = OUT_DEFAULT_PRECIS, 
        .lfClipPrecision = CLIP_DEFAULT_PRECIS,
        .lfQuality = DEFAULT_QUALITY,
        .lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE,
        .lfFaceName = "Cascadia Mono",
    };
    HFONT FontHandle = CreateFontIndirectA(&FontAttr);

    Bool8 UserStepClock = false;
    Bool8 AutoClock = false;
    Bool8 RedrawPCIfNotViewable = true;
    double ElapsedTimeMillisec = 1000; /* force initial frame to be drawn immediately */
    double StartTime = Win32_GetTimeMillisec();
    while (Win32_MainPollInputs(WindowManager, &State))
    {
        /* reduce cpu usage */
        /* slowing down because it's way too fast (finished the test in less than 100ms) */
        Sleep(1);

        /* user inputs */
        /* if the key is down, its timestamp will be nonzero, so it's safe to check the delta */
        double SpaceDownTimeDelta = Win32_GetTimeMillisec() - State.KeyDownTimestampMillisec[VK_SPACE];
        UserStepClock = 
            Win32_IsKeyPressed(&State, VK_SPACE)
            || (State.KeyIsDown[VK_SPACE] 
                && SpaceDownTimeDelta > 150.0);
        if (Win32_IsKeyPressed(&State, 'R'))
        {
            R3000A_Reset(&State.CPU);
            RedrawPCIfNotViewable = true;
        }
        if (Win32_IsKeyPressed(&State, 'A'))
        {
            AutoClock = !AutoClock;
            RedrawPCIfNotViewable = true;
        }

        /* clocking the mips */
        if (UserStepClock)
        {
            R3000A_StepClock(&State.CPU);
            RedrawPCIfNotViewable = true;
        }
        else if (AutoClock)
        {
            int CyclesPerFrame = State.CPUCyclesPerSec / FPS;
            for (int i = 0; 
                i < CyclesPerFrame;
                i++)
            {
                R3000A_StepClock(&State.CPU);
            }
            RedrawPCIfNotViewable = true;
        }

        /* rendering */
        if (ElapsedTimeMillisec > 1000.0 / FPS)
        {
            int FontWidth = FontAttr.lfWidth;
            int FontHeight = FontAttr.lfHeight;
            Win32_ClientRegion Region = Win32_BeginPaint(&State.MainWindow);
            {
                /* clear background */
                HDC DC = Region.TmpBackDC;
                FillRect(DC, &Region.Rect, WHITE_BRUSH);
            }
            Win32_EndPaint(&Region);

            Win32_PaintStateWindow(&State, FontWidth, FontHeight, FontHandle);
            RedrawPCIfNotViewable = Win32_PaintDisasmWindow(&State, FontWidth, FontHeight, FontHandle, RedrawPCIfNotViewable);

            Region = Win32_BeginPaint(&State.LogWindow);
            {
                HDC DC = Region.TmpBackDC;
                SelectObject(DC, FontHandle);

                COLORREF BackgroundColor = 0x00202020;
                COLORREF TextColor = 0x00FFFFFF;
                /* clear background */
                {
                    HBRUSH BackgroundBrush = CreateSolidBrush(BackgroundColor);
                    FillRect(DC, &Region.Rect, BackgroundBrush);
                    DeleteObject(BackgroundBrush);
                }

                /* draw current text buffer */
                SetTextColor(DC, TextColor);
                SetBkColor(DC, BackgroundColor);
                RECT StdOutRect = Region.Rect;
                DrawText(DC, State.WorkingStrBuffer->Ptr, State.WorkingStrBuffer->Size, &StdOutRect, DT_LEFT);
            }
            Win32_EndPaint(&Region);

            ElapsedTimeMillisec = 0;
        }

        /* frame time calculation */
        double EndTime = Win32_GetTimeMillisec();
        double DeltaTime = EndTime - StartTime;
        ElapsedTimeMillisec += DeltaTime;
        StartTime = EndTime;
    }

    /* NOTE: These variables contain dynamic memory, 
     * but they don't need to be deallocated, 
     * because we're exiting so Windows does it for us, and they do it faster than us */
    (void)State.OSMem.Ptr;
    (void)State.StdOut.Ptr;
    (void)State.StdErr.Ptr;
    ExitProcess(0);
}


int WinMain(HINSTANCE Instance, HINSTANCE PrevInstance, PSTR CmdLine, int CmdShow)
{
    (void)PrevInstance, (void)CmdLine, (void)CmdShow;

    const char *WindowManagerName = "WndMan";
    WNDCLASSEXA WndCls = {
        .cbSize = sizeof WndCls,
        .lpfnWndProc = Win32_ManagerWndProc,
        .hIcon = LoadIconA(NULL, IDI_APPLICATION),
        .hCursor = LoadCursorA(NULL, IDC_ARROW),
        .lpszClassName = WindowManagerName,
        .hInstance = GetModuleHandleA(NULL),
    };
    RegisterClassExA(&WndCls);
    HWND WindowManager = CreateWindowExA(0, WindowManagerName, WindowManagerName, 0, 0, 0, 0, 0, NULL, NULL, Instance, 0);
    if (NULL == WindowManager)
    {
        Win32_Fatal("Unable to create a window.");
    }

    Win32_MsgThreadID = GetCurrentThreadId();

    /* create the main thread */
    CreateThread(NULL, 0, Win32_Main, WindowManager, 0, &Win32_MainThreadID);

    while (1)
    {
        MSG Msg;
        GetMessageA(&Msg, 0, 0, 0);
        TranslateMessage(&Msg);
        if (Msg.message == WM_QUIT 
        || Msg.message == WM_SIZE
        || Msg.message == WM_KEYUP
        || Msg.message == WM_KEYDOWN
        || Msg.message == WM_DROPFILES)
        {
            /* dispatch to main thread */
            PostThreadMessageA(Win32_MainThreadID, Msg.message, Msg.wParam, Msg.lParam);
        }
        else /* dispatch to this thread */
        {
            DispatchMessageA(&Msg);
        }
    }
}

