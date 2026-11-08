/* LV2 GUI for the Davidian Tangerine Orbit: the LV2 UI entry points plus a thin window layer
 * for Win32 (ui:WindowsUI) and X11 (ui:X11UI). Everything else is in panel.h, which knows
 * nothing about windows. The UI ABI is declared here so no LV2 SDK is needed. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../lv2_abi.h"
#include "panel.h"

/* ---- LV2 UI ABI (layout and names match lv2/ui/ui.h) ---- */

typedef void* LV2UI_Widget;
typedef void* LV2UI_Handle;
typedef void* LV2UI_Controller;
typedef void* LV2UI_Feature_Handle;
typedef void (*LV2UI_Write_Function)(LV2UI_Controller controller, uint32_t port_index,
                                     uint32_t buffer_size, uint32_t port_protocol,
                                     const void* buffer);
typedef struct LV2UI_Descriptor {
    const char* URI;
    LV2UI_Handle (*instantiate)(const struct LV2UI_Descriptor* descriptor, const char* plugin_uri,
                                const char* bundle_path, LV2UI_Write_Function write_function,
                                LV2UI_Controller controller, LV2UI_Widget* widget,
                                const LV2_Feature* const* features);
    void (*cleanup)(LV2UI_Handle ui);
    void (*port_event)(LV2UI_Handle ui, uint32_t port_index, uint32_t buffer_size,
                       uint32_t format, const void* buffer);
    const void* (*extension_data)(const char* uri);
} LV2UI_Descriptor;
typedef struct { int (*idle)(LV2UI_Handle ui); } LV2UI_Idle_Interface;
typedef struct {
    LV2UI_Feature_Handle handle;
    int (*ui_resize)(LV2UI_Feature_Handle handle, int width, int height);
} LV2UI_Resize;

#define LV2_UI__parent        "http://lv2plug.in/ns/extensions/ui#parent"
#define LV2_UI__resize        "http://lv2plug.in/ns/extensions/ui#resize"
#define LV2_UI__idleInterface "http://lv2plug.in/ns/extensions/ui#idleInterface"

#define DOUBLE_CLICK_MS 350

typedef struct UI UI;
static void ui_redraw(UI* ui);

/* ---- Window layer: Win32 ---- */
#if defined(_WIN32)
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#    include <windowsx.h>

struct UI {
    Panel panel;
    LV2UI_Write_Function write;
    LV2UI_Controller controller;
    HWND hwnd;
    BITMAPINFO bmi;
};

static const char* const CLASS_NAME = "TangerineOrbitPanel";
static int class_users;

static HINSTANCE this_module(void)
{
    HMODULE m = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)(void*)&this_module, &m);
    return (HINSTANCE)m;
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    UI* ui = (UI*)GetWindowLongPtrA(hwnd, GWLP_USERDATA);
    if (!ui)
        return DefWindowProcA(hwnd, msg, wp, lp);
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        if (ui->panel.dirty)
            panel_draw(&ui->panel);
        SetDIBitsToDevice(dc, 0, 0, PANEL_W, PANEL_H, 0, 0, 0, PANEL_H, ui->panel.px, &ui->bmi, DIB_RGB_COLORS);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_PRINTCLIENT:
        if (ui->panel.dirty)
            panel_draw(&ui->panel);
        SetDIBitsToDevice((HDC)wp, 0, 0, PANEL_W, PANEL_H, 0, 0, 0, PANEL_H, ui->panel.px, &ui->bmi, DIB_RGB_COLORS);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        SetCapture(hwnd);
        panel_mouse_down(&ui->panel, (float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp), msg == WM_LBUTTONDBLCLK);
        ui_redraw(ui);
        return 0;
    case WM_MOUSEMOVE:
        panel_mouse_move(&ui->panel, (float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp), (wp & MK_SHIFT) != 0);
        ui_redraw(ui);
        return 0;
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
        if (msg == WM_LBUTTONUP)
            ReleaseCapture();
        panel_mouse_up(&ui->panel);
        ui_redraw(ui);
        return 0;
    case WM_MOUSEWHEEL: {
        POINT pt;
        pt.x = GET_X_LPARAM(lp);
        pt.y = GET_Y_LPARAM(lp);
        ScreenToClient(hwnd, &pt);
        panel_wheel(&ui->panel, (float)pt.x, (float)pt.y, GET_WHEEL_DELTA_WPARAM(wp) / (float)WHEEL_DELTA,
                    (GET_KEYSTATE_WPARAM(wp) & MK_SHIFT) != 0);
        ui_redraw(ui);
        return 0;
    }
    case WM_MOUSEACTIVATE:
        SetFocus(hwnd); /* so the wheel reaches this window */
        return MA_ACTIVATE;
    case WM_TIMER:
        ui_redraw(ui);
        return 0;
    default:
        break;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static int window_open(UI* ui, void* parent)
{
    WNDCLASSEXA wc;
    HINSTANCE inst = this_module();
    if (class_users++ == 0) {
        memset(&wc, 0, sizeof wc);
        wc.cbSize = sizeof wc;
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = wndproc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.lpszClassName = CLASS_NAME;
        if (!RegisterClassExA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            --class_users;
            return 0;
        }
    }
    memset(&ui->bmi, 0, sizeof ui->bmi);
    ui->bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    ui->bmi.bmiHeader.biWidth = PANEL_W;
    ui->bmi.bmiHeader.biHeight = -PANEL_H; /* top-down */
    ui->bmi.bmiHeader.biPlanes = 1;
    ui->bmi.bmiHeader.biBitCount = 32;
    ui->bmi.bmiHeader.biCompression = BI_RGB;
    ui->hwnd = CreateWindowExA(0, CLASS_NAME, "Tangerine Orbit",
                               (parent ? WS_CHILD : WS_OVERLAPPEDWINDOW) | WS_VISIBLE | WS_CLIPSIBLINGS,
                               0, 0, PANEL_W, PANEL_H, (HWND)parent, NULL, inst, NULL);
    if (!ui->hwnd) {
        if (--class_users == 0)
            UnregisterClassA(CLASS_NAME, inst);
        return 0;
    }
    SetWindowLongPtrA(ui->hwnd, GWLP_USERDATA, (LONG_PTR)ui);
    SetTimer(ui->hwnd, 1, 33, NULL); /* picks up host updates such as the sweep lamp */
    return 1;
}

static void window_close(UI* ui)
{
    if (ui->hwnd) {
        KillTimer(ui->hwnd, 1);
        SetWindowLongPtrA(ui->hwnd, GWLP_USERDATA, 0);
        DestroyWindow(ui->hwnd);
        ui->hwnd = NULL;
        if (--class_users == 0)
            UnregisterClassA(CLASS_NAME, this_module());
    }
}

static void* window_widget(UI* ui) { return (void*)ui->hwnd; }

static void ui_redraw(UI* ui)
{
    if (ui->panel.dirty && ui->hwnd)
        InvalidateRect(ui->hwnd, NULL, FALSE);
}

/* The window's own timer drives redraws, so idle has nothing to do. */
static int window_idle(UI* ui) { ui_redraw(ui); return 0; }

/* ---- Window layer: X11 ---- */
#elif defined(__unix__) && !defined(__APPLE__)
#    include <X11/Xlib.h>
#    include <X11/Xutil.h>

struct UI {
    Panel panel;
    LV2UI_Write_Function write;
    LV2UI_Controller controller;
    Display* dpy;
    Window win;
    GC gc;
    XImage* img;
    Time last_click;
    int last_x, last_y;
};

static int window_open(UI* ui, void* parent)
{
    XSetWindowAttributes a;
    Visual* vis;
    int screen, depth;
    Window root;
    ui->dpy = XOpenDisplay(NULL);
    if (!ui->dpy)
        return 0;
    screen = DefaultScreen(ui->dpy);
    vis = DefaultVisual(ui->dpy, screen);
    depth = DefaultDepth(ui->dpy, screen);
    if (depth < 24 || vis->red_mask != 0xff0000 || vis->blue_mask != 0xff) {
        XCloseDisplay(ui->dpy); /* this simple layer needs a standard 24/32-bit RGB visual */
        ui->dpy = NULL;
        return 0;
    }
    root = parent ? (Window)(uintptr_t)parent : RootWindow(ui->dpy, screen);
    memset(&a, 0, sizeof a);
    a.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | StructureNotifyMask;
    a.background_pixel = 0;
    ui->win = XCreateWindow(ui->dpy, root, 0, 0, PANEL_W, PANEL_H, 0, depth, InputOutput, vis,
                            CWEventMask | CWBackPixel, &a);
    ui->gc = XCreateGC(ui->dpy, ui->win, 0, NULL);
    ui->img = XCreateImage(ui->dpy, vis, (unsigned)depth, ZPixmap, 0, (char*)ui->panel.px,
                           PANEL_W, PANEL_H, 32, 0);
    if (!ui->img) {
        XFreeGC(ui->dpy, ui->gc);
        XDestroyWindow(ui->dpy, ui->win);
        XCloseDisplay(ui->dpy);
        ui->dpy = NULL;
        return 0;
    }
    XMapRaised(ui->dpy, ui->win);
    XFlush(ui->dpy);
    return 1;
}

static void window_close(UI* ui)
{
    if (!ui->dpy)
        return;
    ui->img->data = NULL; /* the panel owns the pixels */
    XDestroyImage(ui->img);
    XFreeGC(ui->dpy, ui->gc);
    XDestroyWindow(ui->dpy, ui->win);
    XCloseDisplay(ui->dpy);
    ui->dpy = NULL;
}

static void* window_widget(UI* ui) { return (void*)(uintptr_t)ui->win; }

static void blit(UI* ui)
{
    XPutImage(ui->dpy, ui->win, ui->gc, ui->img, 0, 0, 0, 0, PANEL_W, PANEL_H);
    XFlush(ui->dpy);
}

static void ui_redraw(UI* ui)
{
    if (ui->panel.dirty && ui->dpy) {
        panel_draw(&ui->panel);
        blit(ui);
    }
}

static int window_idle(UI* ui)
{
    if (!ui->dpy)
        return 1;
    while (XPending(ui->dpy)) {
        XEvent e;
        XNextEvent(ui->dpy, &e);
        switch (e.type) {
        case Expose:
            if (e.xexpose.count == 0) {
                if (ui->panel.dirty)
                    panel_draw(&ui->panel);
                blit(ui);
            }
            break;
        case ButtonPress:
            if (e.xbutton.button == 1) {
                const int dbl = e.xbutton.time - ui->last_click < DOUBLE_CLICK_MS
                                && abs(e.xbutton.x - ui->last_x) < 4 && abs(e.xbutton.y - ui->last_y) < 4;
                panel_mouse_down(&ui->panel, (float)e.xbutton.x, (float)e.xbutton.y, dbl);
                ui->last_click = dbl ? 0 : e.xbutton.time;
                ui->last_x = e.xbutton.x;
                ui->last_y = e.xbutton.y;
            } else if (e.xbutton.button == 4 || e.xbutton.button == 5) {
                panel_wheel(&ui->panel, (float)e.xbutton.x, (float)e.xbutton.y,
                            e.xbutton.button == 4 ? 1.0f : -1.0f, (e.xbutton.state & ShiftMask) != 0);
            }
            break;
        case MotionNotify:
            panel_mouse_move(&ui->panel, (float)e.xmotion.x, (float)e.xmotion.y,
                             (e.xmotion.state & ShiftMask) != 0);
            break;
        case ButtonRelease:
            if (e.xbutton.button == 1)
                panel_mouse_up(&ui->panel);
            break;
        default:
            break;
        }
    }
    ui_redraw(ui);
    return 0;
}

#else
#    error "No window layer for this platform yet (Windows and X11 are supported)."
#endif

/* ---- LV2 UI entry points ---- */

static void host_write(void* host, int port, float value)
{
    UI* ui = (UI*)host;
    if (ui->write)
        ui->write(ui->controller, (uint32_t)port, sizeof(float), 0, &value);
}

static LV2UI_Handle instantiate(const LV2UI_Descriptor* descriptor, const char* plugin_uri,
                                const char* bundle_path, LV2UI_Write_Function write_function,
                                LV2UI_Controller controller, LV2UI_Widget* widget,
                                const LV2_Feature* const* features)
{
    UI* ui;
    void* parent = NULL;
    const LV2UI_Resize* resize = NULL;
    int i;
    (void)descriptor;
    (void)bundle_path;
    if (!plugin_uri || strcmp(plugin_uri, ORBIT_URI) != 0)
        return NULL;
    for (i = 0; features && features[i]; ++i) {
        if (!strcmp(features[i]->URI, LV2_UI__parent))
            parent = features[i]->data;
        else if (!strcmp(features[i]->URI, LV2_UI__resize))
            resize = (const LV2UI_Resize*)features[i]->data;
    }
    ui = (UI*)calloc(1, sizeof(UI));
    if (!ui)
        return NULL;
    ui->write = write_function;
    ui->controller = controller;
    if (!panel_init(&ui->panel, host_write, ui)) {
        free(ui);
        return NULL;
    }
    if (!window_open(ui, parent)) {
        panel_free(&ui->panel);
        free(ui);
        return NULL;
    }
    if (resize)
        resize->ui_resize(resize->handle, PANEL_W, PANEL_H);
    *widget = window_widget(ui);
    return ui;
}

static void cleanup(LV2UI_Handle h)
{
    UI* ui = (UI*)h;
    window_close(ui);
    panel_free(&ui->panel);
    free(ui);
}

static void port_event(LV2UI_Handle h, uint32_t port, uint32_t size, uint32_t format, const void* buffer)
{
    UI* ui = (UI*)h;
    if (format != 0 || size != sizeof(float))
        return;
    panel_set(&ui->panel, (int)port, *(const float*)buffer);
    ui_redraw(ui);
}

static int idle(LV2UI_Handle h) { return window_idle((UI*)h); }

static const LV2UI_Idle_Interface idle_iface = { idle };

static const void* extension_data(const char* uri)
{
    return !strcmp(uri, LV2_UI__idleInterface) ? &idle_iface : NULL;
}

static const LV2UI_Descriptor ui_descriptor = {
    ORBIT_UI_URI, instantiate, cleanup, port_event, extension_data
};

LV2_EXPORT const LV2UI_Descriptor* lv2ui_descriptor(uint32_t index)
{
    return index == 0 ? &ui_descriptor : NULL;
}
