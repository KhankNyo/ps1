
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
#define F_RGB(r, g, b) (Win32_Colorf32){.R = r, .G = g, .B = b}

#define IS_MENU_COMMAND(cmd) IN_RANGE(MAINMENU_OPEN_FILE, cmd, MAINMENU_MEMORY_WINDOW)
typedef enum Win32_MainMenuCommand 
{
    MAINMENU_OPEN_FILE = 1,
    MAINMENU_DISASM_WINDOW,
    MAINMENU_CPUSTATE_WINDOW,
    MAINMENU_MEMORY_WINDOW,
} Win32_MainMenuCommand;

typedef struct Win32_Colorf32
{
    float R, G, B;
} Win32_Colorf32;


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
} Win32_ClientRegion;

typedef struct Win32_Window 
{
    int w, h;
    HWND Handle;
    HDC TmpDC;
} Win32_Window;

typedef struct Win32_MainWindowState 
{
    Bool8 KeyIsDown[0x100];
    Bool8 KeyWasDown[0x100];
    unsigned char LastKey;
    char DroppedFileName[1024];

    Win32_Window MainWindow, 
                 DisasmWindow;
    HMENU MainMenu;
} Win32_MainWindowState;

typedef struct Win32_DisassemblyWindow
{
    Win32_CreateWindowArgs Attribute;
} Win32_DisassemblyWindow;

typedef struct Win32_DroppedFile 
{
    char FileName[1024];
} Win32_DroppedFile;

typedef struct DisassemblyData 
{
    char Addr[2048];
    char HexCode[2048];
    char Mnemonic[4096];
} DisassemblyData;

static DWORD Win32_MainThreadID;
static float Win32_PerfCounterRes;


#define Win32_MsgBox(title, msg, flags) MessageBoxA(NULL, msg, title, flags)

static void Win32_Fatal(const char *Msg)
{
    Win32_MsgBox("Fatal Error", Msg, MB_ICONERROR);
    ExitProcess(1);
}

static void Win32_InitPerfCounter(void)
{
    LARGE_INTEGER Li;
    QueryPerformanceFrequency(&Li);
    Win32_PerfCounterRes = 1000.0 / Li.QuadPart;
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
            Win32_MsgBox("Warning", "Multiple files recieved from drop, will only use 1.", MB_ICONWARNING);
        }
    } break;

    default: 
    {
        Result = DefWindowProc(Window, Msg, wParam, lParam);
    } break;
    }
    return Result;
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
            printf("cmd: %d\n", Cmd);
            switch (Cmd)
            {
            case MAINMENU_OPEN_FILE:
            {
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
        case WM_KEYUP:
        case WM_KEYDOWN:
        {
            unsigned char Key = Msg.wParam & 0xFF;
            State->KeyIsDown[Key] = Msg.message == WM_KEYDOWN;
            State->LastKey = Key;
        } break;
        case WM_DROPFILES:
        {
            HDROP Dropped = (HDROP)Msg.wParam;
            DragQueryFileA(Dropped, 0, State->DroppedFileName, sizeof State->DroppedFileName);
            /* TODO: open the file */
        } break;
        }
    }
    return true;
}



static DisassemblyData DisassembleMemory(const void *Mem, iSize SizeBytes, iSize InstructionCount, u32 VirtualPC, u32 DisasmFlags)
{
#define APPEND(ChrPtr, ChrLeft, Chr) do {\
    if (ChrLeft > 1) {\
        ChrPtr[0] = Chr;\
        ChrPtr[1] = '\0';\
        ChrLeft -= 2;\
    } else {\
        ChrPtr[0] = '\0';\
    }\
    } while (0)
    const u8 *BytePtr = Mem;
    DisassemblyData Disasm;
    char *CurrIns = Disasm.Mnemonic;
    char *CurrAddr = Disasm.Addr;
    char *CurrHex = Disasm.HexCode;
    int InsLenLeft = sizeof Disasm.Mnemonic;
    int AddrLenLeft = sizeof Disasm.Addr;
    int HexLenLeft = sizeof Disasm.HexCode;
    for (iSize i = 0; i < InstructionCount; i++)
    {
        u32 Instruction = 0;
        InsLenLeft -= R3000A_Disasm(Instruction, VirtualPC, DisasmFlags, CurrIns, InsLenLeft);
        APPEND(CurrIns, InsLenLeft, '\n');
    }

#undef APPEND
    return Disasm;
}


static DWORD Win32_Main(LPVOID UserData)
{
    HWND ManagerWindow = UserData;

    Win32_MainWindowState State = { 0 };
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
    State.DisasmWindow = Win32_CreateWindowOrDie(
        ManagerWindow, 
        State.MainWindow.Handle,
        "Disassembly",
        50, 50, 512, 360, 
        "DisasmWndCls",
        Win32_MainWndProc, 
        WS_EX_OVERLAPPEDWINDOW, 
        WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CHILD,  
        NULL
    );

    LOGFONTA FontAttr = {
        .lfWidth = 10, 
        .lfHeight = 20,
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



        if (ElapsedTime > 1000.0 / 100.0)
        {
            Win32_ClientRegion Region = Win32_BeginPaint(&State.MainWindow);
            {
                SelectObject(Region.TmpBackDC, FontHandle);
                RECT Rect;
                GetClientRect(State.MainWindow.Handle, &Rect);
                FillRect(Region.TmpBackDC, &Rect, WHITE_BRUSH);
                DrawTextA(Region.TmpBackDC, "Hello, world", -1, &Rect, DT_CENTER);
            }
            Win32_EndPaint(&Region);


            Region = Win32_BeginPaint(&State.DisasmWindow);
            {
                SelectObject(Region.TmpBackDC, FontHandle);
                RECT Rect;
                GetClientRect(State.DisasmWindow.Handle, &Rect);
                FillRect(Region.TmpBackDC, &Rect, WHITE_BRUSH);
            }
            Win32_EndPaint(&Region);
            ElapsedTime = 0;
        }
        else
        {
            float EndTime = Win32_GetTimeMillisec();
            ElapsedTime += EndTime - StartTime;
            StartTime = EndTime;
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

    Win32_InitPerfCounter();

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

