// Win32 backend of the private platform layer (see Platform.hpp). Windows only.
#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "Platform.hpp" // before <windows.h> so its macros cannot leak into Skia/Dawn headers

#include <windows.h>
#include <windowsx.h> // GET_X_LPARAM / GET_Y_LPARAM

#include <cmath>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

namespace Terracotta2::platform {

struct NativeWindow {
    HWND hwnd = nullptr;
    WindowEvents* ev = nullptr;
    float scale = 1.0f;          // GetDpiForWindow / 96, kept current by WM_DPICHANGED
    bool ready = false;          // events are only forwarded once createWindow() has returned
    bool tracking = false;       // TrackMouseEvent(TME_LEAVE) armed
    int buttons = 0;             // bitmask of held MouseButtons (for SetCapture / ReleaseCapture)
    wchar_t highSurrogate = 0;   // pending first half of a WM_CHAR surrogate pair
    Cursor cursor = Cursor::Arrow;
};

namespace {

constexpr const wchar_t* kClassName = L"Terracotta2Window";
constexpr float kWheelPixels = 40.0f; // logical px per wheel notch (WHEEL_DELTA)

HINSTANCE g_hinst = nullptr;
DWORD g_mainThread = 0;
std::vector<NativeWindow*> g_windows; // registry; also used to filter the z-order walk

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

std::string narrow(const wchar_t* s, int len) {
    if (len <= 0) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s, len, nullptr, 0, nullptr, nullptr);
    std::string out((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s, len, out.data(), n, nullptr, nullptr);
    return out;
}

int currentMods() {
    int m = 0;
    if (GetKeyState(VK_SHIFT) & 0x8000) m |= ModShift;
    if (GetKeyState(VK_CONTROL) & 0x8000) m |= ModCtrl;
    if (GetKeyState(VK_MENU) & 0x8000) m |= ModAlt;
    if ((GetKeyState(VK_LWIN) & 0x8000) || (GetKeyState(VK_RWIN) & 0x8000)) m |= ModSuper;
    return m;
}

Key mapKey(WPARAM vk) {
    if (vk >= 'A' && vk <= 'Z') return static_cast<Key>((int)Key::A + (int)(vk - 'A'));
    if (vk >= '0' && vk <= '9') return static_cast<Key>((int)Key::Num0 + (int)(vk - '0'));
    if (vk >= VK_F1 && vk <= VK_F12) return static_cast<Key>((int)Key::F1 + (int)(vk - VK_F1));
    switch (vk) {
        case VK_LEFT: return Key::Left;
        case VK_RIGHT: return Key::Right;
        case VK_UP: return Key::Up;
        case VK_DOWN: return Key::Down;
        case VK_HOME: return Key::Home;
        case VK_END: return Key::End;
        case VK_PRIOR: return Key::PageUp;
        case VK_NEXT: return Key::PageDown;
        case VK_BACK: return Key::Backspace;
        case VK_DELETE: return Key::Delete;
        case VK_RETURN: return Key::Enter;
        case VK_ESCAPE: return Key::Escape;
        case VK_TAB: return Key::Tab;
        case VK_SPACE: return Key::Space;
        default: return Key::Unknown;
    }
}

HCURSOR cursorHandle(Cursor c) {
    LPCWSTR id = IDC_ARROW;
    switch (c) {
        case Cursor::Arrow: id = IDC_ARROW; break;
        case Cursor::IBeam: id = IDC_IBEAM; break;
        case Cursor::Hand: id = IDC_HAND; break;
        case Cursor::HResize: id = IDC_SIZEWE; break;
        case Cursor::VResize: id = IDC_SIZENS; break;
        case Cursor::Move: id = IDC_SIZEALL; break;
    }
    return LoadCursorW(nullptr, id); // shared system cursor: no cleanup needed
}

inline bool live(const NativeWindow* w) { return w && w->ev && w->ready; }

void mouseButton(NativeWindow* w, MouseButton b, bool down, LPARAM lp) {
    const int bit = 1 << (int)b;
    if (down) {
        if (w->buttons == 0) SetCapture(w->hwnd);
        w->buttons |= bit;
    } else {
        w->buttons &= ~bit;
    }
    if (live(w))
        w->ev->onMouseButton(GET_X_LPARAM(lp) / w->scale, GET_Y_LPARAM(lp) / w->scale, b, down, currentMods());
    // Capture keeps the drag flowing outside the window (cross-window docking).
    if (!down && w->buttons == 0 && GetCapture() == w->hwnd) ReleaseCapture();
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    auto* w = reinterpret_cast<NativeWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!w) return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg) {
        case WM_NCDESTROY: // also reached when the owner window takes us down with it
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            w->hwnd = nullptr;
            w->ev = nullptr;
            break;

        case WM_SIZE:
            if (wp != SIZE_MINIMIZED && live(w)) w->ev->onResize();
            return 0;

        case WM_DPICHANGED: {
            w->scale = (float)HIWORD(wp) / 96.0f;
            const RECT* r = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            if (live(w)) w->ev->onResize();
            return 0;
        }

        case WM_PAINT:
            ValidateRect(hwnd, nullptr);
            if (live(w)) w->ev->onRedraw();
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_CLOSE:
            if (live(w)) w->ev->onCloseRequest();
            return 0; // never DestroyWindow here

        case WM_MOUSEMOVE:
            if (!w->tracking) {
                TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, hwnd, 0};
                if (TrackMouseEvent(&t)) w->tracking = true;
            }
            if (live(w)) w->ev->onMouseMove(GET_X_LPARAM(lp) / w->scale, GET_Y_LPARAM(lp) / w->scale);
            return 0;

        case WM_MOUSELEAVE:
            w->tracking = false;
            if (live(w)) w->ev->onMouseLeave();
            return 0;

        case WM_CAPTURECHANGED: // capture stolen (alt-tab, modal dialog...): forget held buttons
            w->buttons = 0;
            break;

        case WM_LBUTTONDOWN: mouseButton(w, MouseButton::Left, true, lp); return 0;
        case WM_LBUTTONUP: mouseButton(w, MouseButton::Left, false, lp); return 0;
        case WM_RBUTTONDOWN: mouseButton(w, MouseButton::Right, true, lp); return 0;
        case WM_RBUTTONUP: mouseButton(w, MouseButton::Right, false, lp); return 0;
        case WM_MBUTTONDOWN: mouseButton(w, MouseButton::Middle, true, lp); return 0;
        case WM_MBUTTONUP: mouseButton(w, MouseButton::Middle, false, lp); return 0;

        case WM_MOUSEWHEEL: // positive delta = wheel pushed forward = scroll up
            if (live(w)) w->ev->onScroll(0.0f, (float)GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * kWheelPixels);
            return 0;
        case WM_MOUSEHWHEEL: // positive delta = wheel tilted right
            if (live(w)) w->ev->onScroll((float)GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * kWheelPixels, 0.0f);
            return 0;

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYUP: {
            const bool down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
            const bool sys = msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP;
            const Key k = mapKey(wp);
            if (k != Key::Unknown && live(w)) {
                KeyAction a = !down ? KeyAction::Release : ((lp & (1 << 30)) ? KeyAction::Repeat : KeyAction::Press);
                w->ev->onKey(k, a, currentMods());
            }
            // Alt+F4 must reach DefWindowProc (-> WM_CLOSE). Swallow every other Alt combo so a lone Alt/F10
            // press does not drop into the (nonexistent) system-menu modal loop.
            if (sys && wp == VK_F4) break;
            return 0;
        }

        case WM_SYSCHAR: // keep Alt+Space (system menu); silence the "no mnemonic" beep for everything else
            if (wp == VK_SPACE) break;
            return 0;

        case WM_CHAR: {
            const wchar_t c = (wchar_t)wp;
            uint32_t cp = 0;
            if (c >= 0xD800 && c <= 0xDBFF) {
                w->highSurrogate = c;
                return 0;
            }
            if (c >= 0xDC00 && c <= 0xDFFF) {
                if (w->highSurrogate) cp = 0x10000 + (((uint32_t)w->highSurrogate - 0xD800) << 10) + ((uint32_t)c - 0xDC00);
                w->highSurrogate = 0;
                if (!cp) return 0; // lone low surrogate
            } else {
                w->highSurrogate = 0;
                cp = c;
            }
            if (cp < 0x20 || cp == 0x7F) return 0;
            if (live(w)) w->ev->onChar(cp);
            return 0;
        }

        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT || GetCapture() == hwnd) {
                SetCursor(cursorHandle(w->cursor));
                return TRUE;
            }
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// Size/position the window so its CLIENT area is cw x ch logical px at screen logical (x, y);
// NAN x/y centers on the primary monitor work area. Re-runs once if the move changed the DPI.
void placeClient(NativeWindow* w, float x, float y, int cw, int ch) {
    for (int pass = 0; pass < 2; ++pass) {
        const UINT dpi = GetDpiForWindow(w->hwnd);
        w->scale = (float)dpi / 96.0f;
        RECT r{0, 0, (LONG)std::lround(cw * w->scale), (LONG)std::lround(ch * w->scale)};
        AdjustWindowRectExForDpi(&r, (DWORD)GetWindowLongPtrW(w->hwnd, GWL_STYLE), FALSE,
                                 (DWORD)GetWindowLongPtrW(w->hwnd, GWL_EXSTYLE), dpi);
        const int ow = r.right - r.left, oh = r.bottom - r.top;
        int px, py;
        if (std::isnan(x) || std::isnan(y)) {
            RECT wa{};
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
            px = wa.left + ((wa.right - wa.left) - ow) / 2;
            py = wa.top + ((wa.bottom - wa.top) - oh) / 2;
        } else {
            // r.left/top are the (negative) frame offsets from the client origin.
            px = (int)std::lround(x * w->scale) + r.left;
            py = (int)std::lround(y * w->scale) + r.top;
        }
        SetWindowPos(w->hwnd, nullptr, px, py, ow, oh, SWP_NOZORDER | SWP_NOACTIVATE);
        if (GetDpiForWindow(w->hwnd) == dpi) break;
    }
}

} // namespace

void init() {
    // Fails harmlessly if already set (e.g. by a manifest).
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    g_hinst = GetModuleHandleW(nullptr);
    g_mainThread = GetCurrentThreadId();
    MSG m;
    PeekMessageW(&m, nullptr, WM_USER, WM_USER, PM_NOREMOVE); // force-create the thread queue so wake() can post

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = 0; // no CS_HREDRAW/VREDRAW (we present ourselves), no CS_DBLCLKS
    wc.lpfnWndProc = wndProc;
    wc.hInstance = g_hinst;
    wc.hIcon = LoadIconW(g_hinst, MAKEINTRESOURCEW(1)); // the app's `1 ICON` resource; null = default icon
    wc.hCursor = nullptr; // set per WM_SETCURSOR
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);
}

void shutdown() {
    UnregisterClassW(kClassName, g_hinst);
}

NativeWindow* createWindow(const WindowDesc& desc, WindowEvents* events, NativeWindow* /*owner*/) {
    auto* w = new NativeWindow();
    w->ev = events;
    const std::wstring title = widen(desc.title);
    // Floating windows are unowned with a taskbar entry: ordinary z-order (they can go behind
    // the main window) and still reachable via the taskbar / Alt+Tab.
    const DWORD ex = 0;
    HWND parent = nullptr;
    w->hwnd = CreateWindowExW(ex, kClassName, title.c_str(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                              CW_USEDEFAULT, CW_USEDEFAULT, parent, nullptr, g_hinst, w);
    if (!w->hwnd) {
        delete w;
        return nullptr;
    }
    g_windows.push_back(w);
    // ponytail: the DPI is only known once the window exists on some monitor; placeClient() moves it and
    // re-runs once if that changed the DPI. Position is converted with the window's own DPI, so a window
    // placed on a secondary monitor with a different scale than the one it was created on may land slightly off.
    placeClient(w, desc.x, desc.y, desc.width, desc.height);
    ShowWindow(w->hwnd, SW_SHOW);
    w->ready = true;
    return w;
}

void destroyWindow(NativeWindow* w) {
    if (!w) return;
    for (size_t i = 0; i < g_windows.size(); ++i)
        if (g_windows[i] == w) {
            g_windows.erase(g_windows.begin() + (std::ptrdiff_t)i);
            break;
        }
    w->ev = nullptr;
    if (w->hwnd) DestroyWindow(w->hwnd); // WM_NCDESTROY clears hwnd/userdata (no-op if owner already took us down)
    delete w;
}

wgpu::Surface createSurface(const wgpu::Instance& instance, NativeWindow* w) {
    wgpu::SurfaceSourceWindowsHWND src;
    src.hinstance = g_hinst;
    src.hwnd = w->hwnd;
    wgpu::SurfaceDescriptor desc;
    desc.nextInChain = &src;
    return instance.CreateSurface(&desc);
}

SkISize framebufferSize(NativeWindow* w) {
    RECT r{};
    GetClientRect(w->hwnd, &r);
    return SkISize::Make(r.right - r.left, r.bottom - r.top);
}

SkSize logicalSize(NativeWindow* w) {
    SkISize s = framebufferSize(w);
    return SkSize::Make((float)s.width() / w->scale, (float)s.height() / w->scale);
}

float contentScale(NativeWindow* w) {
    return w->scale;
}

// ponytail: screen logical px <-> physical px uses THIS window's DPI. On mixed-DPI multi-monitor setups the
// mapping differs per window, so cross-window drags can jump at monitor boundaries. Fix would be a
// virtual-desktop logical space (e.g. everything relative to the primary monitor's DPI).
SkPoint contentScreenPos(NativeWindow* w) {
    POINT p{0, 0};
    ClientToScreen(w->hwnd, &p);
    return SkPoint::Make((float)p.x / w->scale, (float)p.y / w->scale);
}

void setContentScreenPos(NativeWindow* w, SkPoint p) {
    // Frame offset (client origin relative to the outer window origin) measured on the live window,
    // which equals AdjustWindowRectExForDpi's result but also covers invisible resize borders.
    POINT c{0, 0};
    ClientToScreen(w->hwnd, &c);
    RECT wr{};
    GetWindowRect(w->hwnd, &wr);
    const int x = (int)std::lround(p.x() * w->scale) - (c.x - wr.left);
    const int y = (int)std::lround(p.y() * w->scale) - (c.y - wr.top);
    SetWindowPos(w->hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void setTitle(NativeWindow* w, const std::string& title) {
    SetWindowTextW(w->hwnd, widen(title).c_str());
}

void raise(NativeWindow* w) {
    if (IsIconic(w->hwnd)) ShowWindow(w->hwnd, SW_RESTORE);
    BringWindowToTop(w->hwnd);
    SetForegroundWindow(w->hwnd); // may be refused by the OS focus-stealing rules; z-order change still applies
}

void setCursor(NativeWindow* w, Cursor c) {
    w->cursor = c;
    // Apply immediately only if the pointer is over (or captured by) this window; otherwise
    // WM_SETCURSOR picks it up on the next move.
    POINT p{};
    if (GetCapture() == w->hwnd || (GetCursorPos(&p) && WindowFromPoint(p) == w->hwnd)) SetCursor(cursorHandle(c));
}

std::vector<NativeWindow*> windowsFrontToBack() {
    std::vector<NativeWindow*> out;
    for (HWND h = GetTopWindow(nullptr); h; h = GetWindow(h, GW_HWNDNEXT))
        for (NativeWindow* w : g_windows)
            if (w->hwnd == h) {
                out.push_back(w);
                break;
            }
    return out;
}

void pollEvents() {
    MSG m;
    while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
        if (m.message == WM_QUIT) {
            Application::get().quit();
            continue;
        }
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
}

void waitEvents() {
    MSG m;
    if (!PeekMessageW(&m, nullptr, 0, 0, PM_NOREMOVE)) WaitMessage();
    pollEvents();
}

void wake() {
    PostThreadMessageW(g_mainThread, WM_NULL, 0, 0);
}

std::string clipboard() {
    std::string out;
    if (!OpenClipboard(nullptr)) return out;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (const wchar_t* p = static_cast<const wchar_t*>(GlobalLock(h))) {
            std::string s = narrow(p, (int)wcslen(p));
            for (char ch : s)
                if (ch != '\r') out.push_back(ch); // CRLF -> LF
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return out;
}

void setClipboard(const std::string& text) {
    if (g_windows.empty()) return;
    std::string crlf;
    crlf.reserve(text.size());
    for (char ch : text) {
        if (ch == '\n') crlf.push_back('\r');
        crlf.push_back(ch);
    }
    const std::wstring ws = widen(crlf);
    // EmptyClipboard + SetClipboardData require a real owner window (OpenClipboard(nullptr) makes it fail).
    if (!OpenClipboard(g_windows.front()->hwnd)) return;
    EmptyClipboard();
    const size_t bytes = (ws.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (void* p = GlobalLock(h)) {
            memcpy(p, ws.c_str(), bytes); // includes the terminating NUL
            GlobalUnlock(h);
            if (!SetClipboardData(CF_UNICODETEXT, h)) GlobalFree(h);
        } else {
            GlobalFree(h);
        }
    }
    CloseClipboard();
}

} // namespace Terracotta2::platform

#endif // _WIN32
