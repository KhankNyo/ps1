
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
                 DisasmWindow;
    HMENU MainMenu;

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
static float Win32_PerfCounterRes;
static SYSTEM_INFO Win32_SysInfo;
static char Win32_TmpFileName[0x10000];


#define Win32_MsgBox(title, flags, ...) do {\
    char TmpBuffer[1024];\
    snprintf(TmpBuffer, sizeof TmpBuffer, __VA_ARGS__);\
    MessageBoxA(NULL, TmpBuffer, title, flags);\
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

static float Win32_GetTimeMillisec(void)
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
    {
        Win32_Fatal("Unable to retrieve device context of a window.");
    }

    ClientRegion.TmpBackDC = CreateCompatibleDC(ClientRegion.TmpFrontDC);
    if (NULL == ClientRegion.TmpBackDC)
        goto NoBackBuffer;

    ClientRegion.TmpBitmap = CreateCompatibleBitmap(ClientRegion.TmpFrontDC, ClientRegion.w, ClientRegion.h);
    if (NULL == ClientRegion.TmpBitmap)
        goto NoBackBuffer;

    SelectObject(ClientRegion.TmpBackDC, ClientRegion.TmpBitmap);
    return ClientRegion;

NoBackBuffer:
    Win32_Fatal("Unable to retrieve a backbuffer.");
    return (Win32_ClientRegion){ 0 };
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
    case WM_COMMAND:
    {
        Win32_MainMenuCommand MenuCommand = LOWORD(wParam);
        if (IS_MENU_COMMAND(MenuCommand))
            PostThreadMessageA(Win32_MainThreadID, WM_COMMAND, MenuCommand, (LPARAM)Window);
    } break;

    case WM_KEYUP:
    case WM_KEYDOWN:
    case WM_SIZE:
    {
        PostThreadMessage(Win32_MainThreadID, Msg, wParam, lParam);
    } break;
    case WM_DROPFILES:
    {
        PostThreadMessageA(Win32_MainThreadID, WM_DROPFILES, wParam, 0);

        HDROP Dropped = (HDROP)wParam;
        UINT FileCount = DragQueryFileA(Dropped, -1, NULL, 0); /* get file count */
        if (FileCount != 1)
        {
            Win32_MsgBox("Warning", MB_ICONWARNING, "%u files recieved, but only 1 will be used.", FileCount);
        }
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
        PostThreadMessageA(Win32_MainThreadID, Msg, wParam, lParam);
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

static void Win32_DisasmSetScroll(Win32_Window *Window, u64 Pos, u64 Low, u64 High)
{
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
            } break;
            }
        } break;
        case WM_DROPFILES:
        {
            /* get the first dropped file's name */
            HDROP Dropped = (HDROP)Msg.wParam;
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

            Win32_ReadFileSyncIntoOSMem(State, Win32_TmpFileName, OPEN_EXISTING);
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
            float MouseDelta = GET_WHEEL_DELTA_WPARAM(Msg.wParam);
            i32 ScrollDelta = -MouseDelta*(1.0 / WHEEL_DELTA);
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
            GetScrollInfo(State->DisasmWindow.Handle, SB_VERT, &ScrollInfo);

            u32 Pos = ScrollInfo.nPos;
            switch (LOWORD(Msg.wParam))
            {
            case SB_BOTTOM:     Pos = ScrollInfo.nMax; break;
            case SB_TOP:        Pos = ScrollInfo.nMin; break;
            case SB_LINEDOWN:   Pos = State->DisasmAddr/4 + 1; break;
            case SB_LINEUP:     Pos = State->DisasmAddr/4 - 1; break;
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
            SetScrollInfo(State->DisasmWindow.Handle, SB_VERT, &ScrollInfo, TRUE);
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
        if (Addr < SizeBytes)
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


static DWORD Win32_Main(LPVOID UserData)
{
    HWND ManagerWindow = UserData;

    Win32_MainWindowState State = { 
        .DisasmResetAddr = R3000A_RESET_VEC,
        .DisasmAddr = R3000A_RESET_VEC,
        .DisasmMinAddr = R3000A_RESET_VEC,
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
        WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN, 
        State.MainMenu
    );
    DragAcceptFiles(State.MainWindow.Handle, TRUE);
    State.DisasmWindow = Win32_CreateWindowOrDie(
        ManagerWindow, 
        State.MainWindow.Handle,
        "Disassembly",
        100, 100 + 50, 540, 720 - 50, 
        "DisasmWndCls",
        Win32_DisasmWndProc,
        0,
        WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_VSCROLL,  
        NULL
    );
    DragAcceptFiles(State.DisasmWindow.Handle, TRUE);
    Win32_DisasmSetScroll(&State.DisasmWindow, R3000A_RESET_VEC, R3000A_RESET_VEC, R3000A_RESET_VEC);

    LOGFONTA FontAttr = {
        .lfWidth = 8, 
        .lfHeight = 16,
        .lfWeight = FW_BOLD,
        .lfCharSet = ANSI_CHARSET, 
        .lfOutPrecision = OUT_DEFAULT_PRECIS, 
        .lfClipPrecision = CLIP_DEFAULT_PRECIS,
        .lfQuality = DEFAULT_QUALITY,
        .lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE,
        .lfFaceName = "Cascadia Mono",
    };
    HFONT FontHandle = CreateFontIndirectA(&FontAttr);

    float ElapsedTime = 0;
    float StartTime = Win32_GetTimeMillisec();
    while (Win32_MainPollInputs(ManagerWindow, &State))
    {
        Sleep(5);


        if (ElapsedTime == 0)
        {
            Win32_ClientRegion Region = Win32_BeginPaint(&State.MainWindow);
            {
                FillRect(Region.TmpBackDC, &Region.Rect, WHITE_BRUSH);
                SelectObject(Region.TmpBackDC, FontHandle);
                DrawTextA(Region.TmpBackDC, "Hello, world", -1, &Region.Rect, DT_CENTER);
            }
            Win32_EndPaint(&Region);


            Region = Win32_BeginPaint(&State.DisasmWindow);
            {
                HDC DC = Region.TmpBackDC;
                /* clear background */
                FillRect(DC, &Region.Rect, WHITE_BRUSH);

                /* draw disassembly */
                SelectObject(DC, FontHandle);
                int InstructionCount = Region.h / FontAttr.lfHeight;
                State.DisasmInstructionPerPage = InstructionCount;
                int AddrWidth = FontAttr.lfWidth*(8+2);
                int HexWidth = FontAttr.lfWidth*(8+4);
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
                    Win32_DisasmSetScroll(
                        &State.DisasmWindow, 
                        State.DisasmAddr, 
                        State.DisasmMinAddr, 
                        State.DisasmMinAddr + State.OSMem.SizeBytes - InstructionCount*4
                    );
                }
            }
            Win32_EndPaint(&Region);
        }
        else
        {
            float EndTime = Win32_GetTimeMillisec();
            ElapsedTime += EndTime - StartTime;
            StartTime = EndTime;

            if (ElapsedTime == 1000.0 / 100.0)
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

    Win32_InitSystem();

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

