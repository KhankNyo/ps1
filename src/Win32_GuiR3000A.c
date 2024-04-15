
#include "GL/glew.h"
#include "gl/wglew.h"
#include <gl/GL.h>

#include <windows.h>
#include <shellapi.h>

#include "R3000A.h"
#include "R3000A.c"
#include "CP0.c"

#define true 1
#define false 0
typedef unsigned char u8;
typedef u8 Bool8;
#define MAINTHREAD_CREATE_WINDOW (WM_USER + 0)
#define MAINTHREAD_CLOSE_WINDOW (WM_USER + 1)

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

typedef struct Win32_InputBuffer 
{
    Bool8 KeyIsDown[0x100];
    Bool8 KeyWasDown[0x100];
    unsigned char LastKey;
    char DroppedFileName[1024];
} Win32_InputBuffer;

typedef struct Win32_DisassemblyWindow
{
    Win32_CreateWindowArgs Attribute;
} Win32_DisassemblyWindow;

typedef struct Win32_DroppedFile 
{
    char FileName[1024];
} Win32_DroppedFile;

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

    case WM_PAINT:
    case WM_ERASEBKGND:
    {
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
        UINT FileCount = DragQueryFileA(Dropped, -1, NULL, 0);
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


static Bool8 Win32_MainPollInputs(HWND ManagerWindow, Win32_InputBuffer *Input)
{
    MSG Msg;
    Input->KeyWasDown[Input->LastKey] = Input->KeyIsDown[Input->LastKey];
    while (PeekMessageA(&Msg, 0, 0, 0, PM_REMOVE))
    {
        switch (Msg.message)
        {
        case WM_CLOSE:
        case WM_QUIT:
        {
            /* because the message we're recieving is from the message window, 
             * we can't just send the same message back (if we do, that'll be an infinite loop), 
             * so we send our custom messages instead */
            SendMessageA(ManagerWindow, MAINTHREAD_CLOSE_WINDOW, Msg.wParam, 0);
            return false;
        } break;
        case WM_KEYUP:
        case WM_KEYDOWN:
        {
            unsigned char Key = Msg.wParam & 0xFF;
            Input->KeyIsDown[Key] = Msg.message == WM_KEYDOWN;
            Input->LastKey = Key;
        } break;
        case WM_DROPFILES:
        {
            HDROP Dropped = (HDROP)Msg.wParam;
            DragQueryFileA(Dropped, 0, Input->DroppedFileName, sizeof Input->DroppedFileName);
        } break;
        }
    }
    return true;
}

static HGLRC Win32_CreateRenderContext(HDC DeviceContext)
{
    PIXELFORMATDESCRIPTOR Pfd = {
        .nSize = sizeof Pfd,
        .nVersion = 1,
        .dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER, 
        .iPixelType = PFD_TYPE_RGBA, 
        .cColorBits = 32,
        .cAlphaBits = 8, 
        .iLayerType = PFD_MAIN_PLANE,
        .cDepthBits = 24,
        .cStencilBits = 8,
    };
    int FormatIndex = ChoosePixelFormat(DeviceContext, &Pfd);
    if (!FormatIndex)
    {
        Win32_Fatal("ChoosePixelFormat failed.");
    }
    if (!SetPixelFormat(DeviceContext, FormatIndex, &Pfd))
    {
        Win32_Fatal("SetPixelFormat failed.");
    }

    /* now init renderer */
    HGLRC RenderContext = wglCreateContext(DeviceContext);
    if (NULL == RenderContext)
    {
        Win32_Fatal("wglCreateContext failed.");
    }
    wglMakeCurrent(DeviceContext, RenderContext);

    glewExperimental = GL_TRUE;
    GLenum Err = glewInit();
    if (Err != GLEW_OK)
    {
        const char *ErrMsg = (const char *)glewGetErrorString(Err);
        Win32_Fatal(ErrMsg);
    }

    /* now attempt to create a newer context */
    static const GLint Attribs[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 3, 
        WGL_CONTEXT_MINOR_VERSION_ARB, 3, 
        WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB,
        0
    };
    HGLRC BetterRenderContext = wglCreateContextAttribsARB(DeviceContext, 0, Attribs);
    if (NULL == BetterRenderContext || !wglMakeCurrent(DeviceContext, BetterRenderContext))
    {
        Win32_MsgBox("OpenGL context", "Version 3.3 unavailable, falling back to compatibility mode.", MB_OK); 
        if (NULL != BetterRenderContext)
            wglDeleteContext(BetterRenderContext);
    }
    else
    {
        wglDeleteContext(RenderContext);
        return BetterRenderContext;
    }
    return RenderContext;
}

static DWORD Win32_Main(LPVOID UserData)
{
    HWND ManagerWindow = UserData;

    const char *MainWndClsName = "MainWndCls";
    WNDCLASSEXA WindowClass = {
        .cbSize = sizeof WindowClass,
        .style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC,
        .lpfnWndProc = Win32_MainWndProc,
        .hIcon = LoadIconA(NULL, IDI_APPLICATION),
        .hCursor = LoadCursorA(NULL, IDC_ARROW),
        .hbrBackground = GetStockObject(WHITE_BRUSH),
        .lpszClassName = MainWndClsName,
        .hInstance = GetModuleHandleA(NULL),
    };
    RegisterClassExA(&WindowClass);

    Win32_CreateWindowArgs Args = {
        .x = CW_USEDEFAULT, 
        .y = CW_USEDEFAULT, 
        .w = 1080,
        .h = 720,
        .lpWindowName = "Main window",
        .lpClassName = MainWndClsName, 
        .dwExStyle = WS_EX_OVERLAPPEDWINDOW,
        .dwStyle = WS_OVERLAPPEDWINDOW | WS_VISIBLE,
    };
    /* by sending a custom create message to the manager window, 
     * we're telling it to handle the messages of this window for us in Win32_MainWndProc, 
     * and send anything that we care back to us (the main thread) */
    HWND Window = (HWND)SendMessageA(ManagerWindow, MAINTHREAD_CREATE_WINDOW, (WPARAM)&Args, 0);
    if (NULL == Window)
    {
        Win32_Fatal("Unable to create window.");
    }
    HDC DeviceContext = GetDC(Window);
    HGLRC RenderContext = Win32_CreateRenderContext(DeviceContext);

    LOGFONTA FontAttr = {
        .lfWidth = 16, 
        .lfHeight = 16,
        .lfFaceName = "Cascadia Mono",
    };
    HFONT FontHandle = CreateFontIndirectA(&FontAttr);

    float ElapsedTime = 0;
    float StartTime = Win32_GetTimeMillisec();
    Win32_InputBuffer Input = { 0 };
    while (Win32_MainPollInputs(ManagerWindow, &Input))
    {
        if (ElapsedTime > 1000.0 / 60.0)
        {
            RECT Rect;
            GetClientRect(Window, &Rect);
            glViewport(0, 0, Rect.right - Rect.left, Rect.bottom - Rect.top);

            glClearColor(.7, .7, .7, 1.0);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            glMatrixMode(GL_PROJECTION);
            glPushMatrix();
            glLoadIdentity();
            glMatrixMode(GL_MODELVIEW);
            glPushMatrix();
            glLoadIdentity();

            glBegin( GL_QUADS );
                glColor3f(1, 0, 0);
                glTexCoord2f(0,0);
                glVertex2f(-1.0f, -1.0f);

                glColor3f(0, 1, 0);
                glTexCoord2f(1,0);
                glVertex2f(1.0f, -1.0f);

                glColor3f(0, 0, 1);
                glTexCoord2f(1,1);
                glVertex2f(1.0f, 1.0f);

                glColor3f(0, 1, 0);
                glTexCoord2f(0,1);
                glVertex2f(-1.0f, 1.0f);
            glEnd();

            glPopMatrix();
            glMatrixMode(GL_PROJECTION);
            glPopMatrix();
            glMatrixMode(GL_MODELVIEW);

            SwapBuffers(DeviceContext);
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

