#include <windows.h>
#include <stdio.h>
#include <stdint.h>

#define LOG_FILE "C:\\d3d9hook.log"
#define FORCED_W 2560
#define FORCED_H 1440
#define NATIVE_W 1280
#define NATIVE_H 720

static void log_msg(const char *msg) {
    HANDLE f = CreateFileA(LOG_FILE, FILE_APPEND_DATA, FILE_SHARE_READ,
                           NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(f, msg, lstrlenA(msg), &written, NULL);
        WriteFile(f, "\r\n", 2, &written, NULL);
        CloseHandle(f);
    }
}

static HMODULE g_real        = NULL;
static void*   g_realCreateDevice = NULL;
static WNDPROC g_origWndProc = NULL;
static HWND    g_gameHwnd    = NULL;

/* ════════════════════════════════════════
   Trampoline / hotpatch helpers
   ════════════════════════════════════════ */

static void* create_trampoline(void *target, int nbytes) {
    BYTE *p = (BYTE*)target, *t;
    t = (BYTE*)VirtualAlloc(NULL, nbytes + 5, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!t) return NULL;
    memcpy(t, p, nbytes);
    t[nbytes] = 0xE9;
    *(uint32_t*)(t+nbytes+1) = (uintptr_t)(p+nbytes) - (uintptr_t)(t+nbytes) - 5;
    FlushInstructionCache(GetCurrentProcess(), t, nbytes+5);
    return t;
}

static void hotpatch(void *target, void *hook, void **tramp) {
    if (!target) return;
    *tramp = create_trampoline(target, 5);
    if (!*tramp) return;
    DWORD old;
    VirtualProtect(target, 5, PAGE_EXECUTE_READWRITE, &old);
    ((BYTE*)target)[0] = 0xE9;
    *(uint32_t*)((BYTE*)target+1) = (uintptr_t)hook - (uintptr_t)target - 5;
    VirtualProtect(target, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), target, 5);
}

/* ──── GetCursorPos hook ──── */
static void* g_gcp_trampoline = NULL;
static BOOL WINAPI myGetCursorPos(LPPOINT pt) {
    typedef BOOL (WINAPI *Fn)(LPPOINT);
    BOOL r = ((Fn)g_gcp_trampoline)(pt);
    if (r && pt && g_gameHwnd) {
        RECT win;
        GetWindowRect(g_gameHwnd, &win);
        pt->x = (pt->x + win.left) / 2;
        pt->y = (pt->y + win.top)  / 2;
    }
    return r;
}

/* ──── GetMessagePos hook ──── */
static void* g_gmp_trampoline = NULL;
static DWORD WINAPI myGetMessagePos(void) {
    typedef DWORD (WINAPI *Fn)(void);
    DWORD r = ((Fn)g_gmp_trampoline)();
    if (g_gameHwnd) {
        RECT win;
        GetWindowRect(g_gameHwnd, &win);
        int x = (int)(short)LOWORD(r);
        int y = (int)(short)HIWORD(r);
        x = (x + win.left) / 2;
        y = (y + win.top)  / 2;
        r = (DWORD)MAKELONG((WORD)x, (WORD)y);
    }
    return r;
}



static void hotpatch_user32(void) {
    HMODULE u32 = GetModuleHandleA("user32.dll");
    if (!u32) return;
    hotpatch(GetProcAddress(u32, "GetCursorPos"),   myGetCursorPos,   &g_gcp_trampoline);
    hotpatch(GetProcAddress(u32, "GetMessagePos"),  myGetMessagePos,  &g_gmp_trampoline);
    log_msg("[proxy] user32 patched: GetCursorPos, GetMessagePos");
}

/* ════════════════════════════════════════
   Window subclass for WM_MSG coords
   ════════════════════════════════════════ */

LRESULT CALLBACK myWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_MOUSEMOVE: case WM_LBUTTONDOWN: case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK: case WM_RBUTTONDOWN: case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK: case WM_MBUTTONDOWN: case WM_MBUTTONUP:
    case WM_MBUTTONDBLCLK: case WM_XBUTTONDOWN: case WM_XBUTTONUP:
    case WM_XBUTTONDBLCLK:
        {
            int x = (int)(short)LOWORD(lParam);
            int y = (int)(short)HIWORD(lParam);
            x /= 2; y /= 2;
            lParam = MAKELPARAM((WORD)x, (WORD)y);
        }
        break;
    case WM_SIZE:
    case WM_SHOWWINDOW:
        /* Re-apply size on restore */
        SetWindowPos(hWnd, NULL, 0, 0, FORCED_W, FORCED_H,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        break;
    }
    return CallWindowProcA(g_origWndProc, hWnd, msg, wParam, lParam);
}

/* ════════════════════════════════════════
   CreateDevice hook
   ════════════════════════════════════════ */

typedef long (__stdcall *CreateDeviceFn)(void*, unsigned int, unsigned int,
    void*, unsigned long, void*, void**);

static long __stdcall myCreateDevice(
    void *self, unsigned int Adapter, unsigned int DeviceType,
    void *hFocusWindow, unsigned long BehaviorFlags,
    void *pParams, void **ppDevice)
{
    log_msg("[proxy] CreateDevice");

    if (hFocusWindow) {
        if (!g_gameHwnd) {
            g_gameHwnd = (HWND)hFocusWindow;
            g_origWndProc = (WNDPROC)SetWindowLongPtrA(
                g_gameHwnd, GWLP_WNDPROC, (LONG_PTR)myWndProc);
            log_msg("[proxy] Window subclassed");
        }
        SetWindowPos(g_gameHwnd, NULL, 0, 0, FORCED_W, FORCED_H,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    ((unsigned int*)pParams)[0] = NATIVE_W;
    ((unsigned int*)pParams)[1] = NATIVE_H;

    long hr = ((CreateDeviceFn)g_realCreateDevice)(self, Adapter, DeviceType,
                hFocusWindow, BehaviorFlags, pParams, ppDevice);
    if (hr >= 0) log_msg("[proxy] Device OK");
    return hr;
}

/* ════════════════════════════════════════
   Direct3DCreate9 / Direct3DCreate9Ex
   ════════════════════════════════════════ */

__declspec(dllexport) void* __stdcall Direct3DCreate9(unsigned int ver) {
    log_msg("[proxy] Direct3DCreate9");
    static void* (__stdcall *real)(unsigned int) = NULL;
    if (!real) real = (void*)GetProcAddress(g_real, "Direct3DCreate9");
    void *d3d9 = real(ver);
    if (d3d9) {
        void **vtbl = *(void***)d3d9;
        DWORD old;
        VirtualProtect(&vtbl[16], sizeof(void*), PAGE_EXECUTE_READWRITE, &old);
        g_realCreateDevice = vtbl[16]; vtbl[16] = myCreateDevice;
        VirtualProtect(&vtbl[16], sizeof(void*), old, &old);
    }
    return d3d9;
}

__declspec(dllexport) long __stdcall Direct3DCreate9Ex(unsigned int ver, void **pp) {
    log_msg("[proxy] Direct3DCreate9Ex");
    static long (__stdcall *real)(unsigned int, void**) = NULL;
    if (!real) real = (void*)GetProcAddress(g_real, "Direct3DCreate9Ex");
    long hr = real(ver, pp);
    if (hr >= 0 && pp && *pp) {
        void **vtbl = *(void***)*pp;
        DWORD old;
        VirtualProtect(&vtbl[16], sizeof(void*), PAGE_EXECUTE_READWRITE, &old);
        g_realCreateDevice = vtbl[16]; vtbl[16] = myCreateDevice;
        VirtualProtect(&vtbl[16], sizeof(void*), old, &old);
    }
    return hr;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        DeleteFileA(LOG_FILE);
        log_msg("[proxy] DllMain loading syswow64...");
        g_real = LoadLibraryA("C:\\windows\\syswow64\\d3d9.dll");
        if (!g_real) { log_msg("[proxy] FAILED"); return FALSE; }
        hotpatch_user32();
        log_msg("[proxy] OK");
    }
    return TRUE;
}