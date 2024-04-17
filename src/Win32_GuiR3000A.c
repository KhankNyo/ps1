
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




#define IS_MENU_COMMAND(cmd) IN_RANGE(MAINMENU_OPEN_FILE, cmd, MAINMENU_MEMORY_WINDOW)
typedef enum Win32_MainMenuCommand 
{
    MAINMENU_OPEN_FILE = 1,
    MAINMENU_DISASM_WINDOW,
    MAINMENU_CPUSTATE_WINDOW,
    MAINMENU_MEMORY_WINDOW,
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

typedef struct Win32_ClientRegion
{
    HDC TmpBackDC, TmpFrontDC;
    HBITMAP TmpBitmap;
    int x, y, w, h;
    RECT Rect;
} Win32_ClientRegion;

typedef struct Win32_Window 
{
    int w, h;
    HWND Handle;
    HDC TmpDC;
} Win32_Window;

typedef struct Win32_BufferData 
{
    u8 *Ptr;
    iSize SizeBytes;
} Win32_BufferData;
typedef struct Win32_BufferData Win32_Stack;

typedef struct Win32_MainWindowState 
{
    Bool8 KeyIsDown[0x100];
    Bool8 KeyWasDown[0x100];
    unsigned char LastKey;
    char DroppedFileName[1024];

    Win32_Window MainWindow, 
                 DisasmWindow, 
                 CPUWindow;
    HMENU MainMenu;
    R3000A CPU;
    u32 CPUCyclesPerSec;

    u32 DisasmFlags;
    u32 DisasmInstructionPerPage;
    u64 DisasmAddr, 
        DisasmResetAddr, 
        DisasmMinAddr;
    Win32_BufferData OSMem;
} Win32_MainWindowState;

typedef struct Win32_DisassemblyWindow
{
    Win32_CreateWindowArgs Attribute;
} Win32_DisassemblyWindow;


static DWORD Win32_MainThreadID;
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
    HWND ManagerWindow, 
    HWND Parent, 
    const char *Title,
    int x, int y, int w, int h,
    const char *ClassName, 
    WNDPROC WndProc,
    DWORD dwExStyle, 
    DWORD dwStyle,
    HMENU Menu
)
{
    WNDCLASSEXA WindowClass = {
        .cbSize = sizeof WindowClass,
        .style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC,
        .lpfnWndProc = WndProc,
        .hIcon = LoadIconA(NULL, IDI_APPLICATION),
        .hbrBackground = GetStockObject(WHITE_BRUSH),
        .hCursor = LoadCursorA(NULL, IDC_ARROW),
        .lpszClassName = ClassName,
        .hInstance = GetModuleHandleA(NULL),
    };
    RegisterClassExA(&WindowClass);

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
    HWND Handle = (HWND)SendMessageA(ManagerWindow, MAINTHREAD_CREATE_WINDOW, (WPARAM)&Args, 0);
    if (NULL == Handle)
    {
        Win32_Fatal("Unable to create a window");
    }
    Win32_Window Window = {
        .Handle = Handle,
        .w = w,
        .h = h,
    };
    return Window;
}

static Win32_ClientRegion Win32_BeginPaint(Win32_Window *Window)
{
    RECT Rect;
    GetClientRect(Window->Handle, &Rect);
    Win32_ClientRegion ClientRegion = { 
        .x = Rect.left, 
        .y = Rect.bottom,
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
        Result = TRUE;
    } break;
    case WM_PAINT:
    {
    } break;
    case WM_COMMAND:
    {
        Win32_MainMenuCommand MenuCommand = LOWORD(wParam);
        if (IS_MENU_COMMAND(MenuCommand))
            PostThreadMessageA(Win32_MainThreadID, WM_COMMAND, MenuCommand, (LPARAM)Window);
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

static void Win32_SetWindowScrollbar(Win32_Window *Window, u64 Pos, u64 Low, u64 High)
{
    /* to the scrollbar from disappearing */
    if (High == Low)
    {
        High = Low + 256;
    }
    SCROLLINFO ScrollInfo = {
        .cbSize = sizeof ScrollInfo, 
        .fMask = SIF_RANGE | SIF_POS,
        .nPos = Pos/4,
        .nMax = High/4,
        .nMin = Low/4,
    };
    SetScrollInfo(Window->Handle, SB_VERT, &ScrollInfo, TRUE);
}


static Bool8 Win32_MainPollInputs(HWND ManagerWindow, Win32_MainWindowState *State)
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
            if (Window != State->MainWindow.Handle) /* close child window, just hide it, don't actually close */
            {
                ShowWindow(Window, SW_HIDE);
            }
            else /* close main window */
            {
                /* because the message we're recieving is from the message window, 
                 * we can't just send the same message back (if we do, that'll be an infinite loop), 
                 * so we send our custom messages instead */
                SendMessageA(ManagerWindow, MAINTHREAD_CLOSE_WINDOW, (WPARAM)Window, 0);
                return false;
            }
        } break;
        case WM_COMMAND:
        {
            Win32_MainMenuCommand Cmd = LOWORD(Msg.wParam);
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
                ShowWindow(State->DisasmWindow.Handle, SW_SHOW); 
            } break;
            case MAINMENU_MEMORY_WINDOW:
            {
            } break;
            case MAINMENU_CPUSTATE_WINDOW:
            {
                ShowWindow(State->CPUWindow.Handle, SW_SHOW);
            } break;
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
            State->KeyIsDown[Key] = Msg.message == WM_KEYDOWN;
            State->LastKey = Key;
        } break;
        case WM_MOUSEWHEEL:
        {
            double Res = 8.0;
            double MouseDelta = GET_WHEEL_DELTA_WPARAM(Msg.wParam);
            i32 ScrollDelta;
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
        }
    }
    return true;
}







static void Memcpy(void *Dst, const void *Src, iSize SizeBytes)
{
    u8 *DstPtr = Dst;
    const u8 *SrcPtr = Src;
    while (SizeBytes --> 0)
        *DstPtr++ = *SrcPtr++;
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
        if (IN_RANGE(0x80000000, LogicalAddr, 0xA0000000))
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

static void MipsWrite(void *UserData, u32 Addr, u32 Data, R3000A_DataSize DataSize)
{
    Win32_MainWindowState *State = UserData;

    u32 PhysAddr = TranslateAddr(Addr);
    if (AddrIsValid(PhysAddr, DataSize, State->OSMem.SizeBytes))
    {
        for (int i = 0; i < DataSize; i++)
        {
            State->OSMem.Ptr[Addr + i] = (Data >> i*8) & 0xFF;
        }
    }
    else if (Addr == TESTSYS_EXIT)
    {
        /* TODO: log exit msg */
    }
    else if (Addr == TESTSYS_WRITEHEX)
    {
        /* TODO: log hex msg */
    }
    else if (Addr == TESTSYS_WRITESTR)
    {
        /* TODO: log string msg */
    }
    else
    {
        /* TODO: logging */
    }
}

static u32 MipsRead(void *UserData, u32 Addr, R3000A_DataSize DataSize)
{
    Win32_MainWindowState *State = UserData;
    u32 PhysAddr = TranslateAddr(Addr);
    if (AddrIsValid(PhysAddr, DataSize, State->OSMem.SizeBytes))
    {
        u32 Data = 0;
        for (int i = 0; i < DataSize; i++)
        {
            Data |= (u32)State->OSMem.Ptr[PhysAddr + i] << i*8;
        }
        return Data;
    }
    else
    {
        /* TODO: logging */
    }
    return 0;
}

static Bool8 MipsVerify(void *UserData, u32 Addr)
{
    (void)UserData;
    (void)Addr;
    return true;
}


static DWORD Win32_Main(LPVOID UserData)
{
    HWND ManagerWindow = UserData;

    Win32_InitSystem();

    double FPS = 60.0;
    Win32_MainWindowState State = { 
        .DisasmResetAddr = R3000A_RESET_VEC,
        .DisasmAddr = R3000A_RESET_VEC,
        .DisasmMinAddr = R3000A_RESET_VEC,
        .CPU = R3000A_Init(&State, MipsRead, MipsWrite, MipsVerify, MipsVerify),
        .CPUCyclesPerSec = FPS,
    };
    State.MainMenu = CreateMenu();
    AppendMenuA(State.MainMenu, MF_STRING, MAINMENU_DISASM_WINDOW, "&Disassembly");
    AppendMenuA(State.MainMenu, MF_STRING, MAINMENU_MEMORY_WINDOW, "&Memory");
    AppendMenuA(State.MainMenu, MF_STRING, MAINMENU_CPUSTATE_WINDOW, "&CPU State");
    AppendMenuA(State.MainMenu, MF_STRING, MAINMENU_OPEN_FILE, "&Open File");
    State.MainWindow = Win32_CreateWindowOrDie(
        ManagerWindow, 
        NULL,
        "R3000A Debug Emu",
        100, 100, 1080, 720, 
        "MainWndCls", 
        Win32_MainWndProc,
        WS_EX_OVERLAPPEDWINDOW,
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, 
        State.MainMenu
    );
    DragAcceptFiles(State.MainWindow.Handle, TRUE);
    State.DisasmWindow = Win32_CreateWindowOrDie(
        ManagerWindow, 
        State.MainWindow.Handle,
        "Disassembly",
        100, 150, 540, 670, 
        "DisasmWndCls",
        Win32_DisasmWndProc,
        WS_EX_OVERLAPPEDWINDOW,
        WS_THICKFRAME | WS_SYSMENU | WS_VISIBLE | WS_VSCROLL,  
        NULL
    );
    DragAcceptFiles(State.DisasmWindow.Handle, TRUE);
    Win32_SetWindowScrollbar(&State.DisasmWindow, R3000A_RESET_VEC, R3000A_RESET_VEC, R3000A_RESET_VEC);
    State.CPUWindow = Win32_CreateWindowOrDie(
        ManagerWindow, 
        State.MainWindow.Handle, 
        "CPUState",  
        100 + 540, 150, 540, 670/2, 
        "CPUStateCls", 
        Win32_MainWndProc, 
        WS_EX_OVERLAPPEDWINDOW, 
        WS_OVERLAPPED | WS_SYSMENU | WS_VISIBLE, 
        NULL
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

    double ElapsedTime = 0;
    double StartTime = Win32_GetTimeMillisec();
    while (Win32_MainPollInputs(ManagerWindow, &State))
    {
        /* reduce cpu usage */
        Sleep(5);
        double MipsCPUTimeMS = 0, 
              MipsCPUTimeStart = Win32_GetTimeMillisec();
        int CyclesPerFrame = State.CPUCyclesPerSec / FPS;
#if 0
        for (int i = 0; 
            i < CyclesPerFrame
            && MipsCPUTimeMS < 1000.0; 
            i++)
        {
            R3000A_StepClock(&State.CPU);
            MipsCPUTimeMS = Win32_GetTimeMillisec() - MipsCPUTimeStart;
        }
#endif


        double EndTime = Win32_GetTimeMillisec();
        double DeltaTime = EndTime - StartTime;
        ElapsedTime += DeltaTime;
        StartTime = EndTime;

        if (ElapsedTime >= 1000.0 / FPS)
        {
            int FontWidth = FontAttr.lfWidth;
            int FontHeight = FontAttr.lfHeight;
            Win32_ClientRegion Region = Win32_BeginPaint(&State.MainWindow);
            {
                HDC DC = Region.TmpBackDC;
                FillRect(DC, &Region.Rect, WHITE_BRUSH);
            }
            Win32_EndPaint(&Region);


            Region = Win32_BeginPaint(&State.CPUWindow);
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
                int RegisterBoxPerLine = 4;
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
                uint RegisterCount = STATIC_ARRAY_SIZE(State.CPU.R);
                for (uint i = 0; i < RegisterCount; i++)
                {
                    FillRect(DC, &CurrentRegisterBox, RegisterBoxColorBrush);
                    Win32_DrawStrFmt(DC, &CurrentRegisterBox, DT_CENTER, 64, 
                        "R%02d:\n%08x", i, State.CPU.R[i]
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
                PCRegisterBox.left += RegisterBoxHorDist * RegisterBoxPerLine;
                PCRegisterBox.right += PCRegisterBox.left;
                FillRect(DC, &PCRegisterBox, RegisterBoxColorBrush);
                static int FrameCounter = 0;
                Win32_DrawStrFmt(DC, &PCRegisterBox, DT_CENTER, 64, 
                    "f=%d\n dt: %3.2f", FrameCounter++, ElapsedTime
                );

                DeleteObject(SelectObject(DC, LastBrush));
            }
            Win32_EndPaint(&Region);


            Region = Win32_BeginPaint(&State.DisasmWindow);
            {
                HDC DC = Region.TmpBackDC;
                /* clear background */
                FillRect(DC, &Region.Rect, WHITE_BRUSH);

                /* draw disassembly */
                SelectObject(DC, FontHandle);
                int InstructionCount = Region.h / FontHeight + 1; /* +1 for smooth transition */
                State.DisasmInstructionPerPage = InstructionCount;
                int AddrWidth = FontWidth*(8+2);
                int HexWidth  = FontWidth*(8+4);
                DisassemblyData Disasm = DisassembleMemory(
                    State.OSMem.Ptr, State.OSMem.SizeBytes, 
                    InstructionCount, 
                    State.DisasmAddr, 
                    State.DisasmFlags
                );
                DisassemblyRegions DisasmRegion = GetDisassemblyRegion(
                    &Region.Rect, 
                    AddrWidth, 
                    HexWidth
                );
                DrawTextA(DC, Disasm.Addr, -1, &DisasmRegion.Addr, DT_LEFT);
                DrawTextA(DC, Disasm.HexCode, -1, &DisasmRegion.Hex, DT_LEFT);
                DrawTextA(DC, Disasm.Mnemonic, -1, &DisasmRegion.Mnemonic, DT_LEFT);
                if (State.OSMem.SizeBytes != 0)
                {
                    Win32_SetWindowScrollbar(
                        &State.DisasmWindow, 
                        State.DisasmAddr, 
                        State.DisasmMinAddr, 
                        State.DisasmMinAddr + State.OSMem.SizeBytes - InstructionCount*4
                    );
                }
            }
            Win32_EndPaint(&Region);
            ElapsedTime = 0;
        }
    }

    ExitProcess(0);
}


int WinMain(HINSTANCE Instance, HINSTANCE PrevInstance, PSTR CmdLine, int CmdShow)
{
    (void)PrevInstance, (void)CmdLine, (void)CmdShow;

    const char *ManagerWindowName = "WndMan";
    WNDCLASSEXA WndCls = {
        .cbSize = sizeof WndCls,
        .lpfnWndProc = Win32_ManagerWndProc,
        .hIcon = LoadIconA(NULL, IDI_APPLICATION),
        .hCursor = LoadCursorA(NULL, IDC_ARROW),
        .lpszClassName = ManagerWindowName,
        .hInstance = GetModuleHandleA(NULL),
    };
    RegisterClassExA(&WndCls);
    HWND ManagerWindow = CreateWindowExA(0, ManagerWindowName, ManagerWindowName, 0, 0, 0, 0, 0, NULL, NULL, Instance, 0);
    if (NULL == ManagerWindow)
    {
        Win32_Fatal("Unable to create a window.");
    }


    /* create the main thread */
    CreateThread(NULL, 0, Win32_Main, ManagerWindow, 0, &Win32_MainThreadID);

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

