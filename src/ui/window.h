/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* The window layer under the panel, shared by the LV2, VST3 and AU GUIs: a child window
 * for Win32, X11 or Cocoa that shows the panel's pixels and feeds it mouse input.
 *   window_open(w, parent)   make the window (inside parent if not NULL); 0 on failure
 *   window_close(w)          destroy it
 *   window_widget(w)         the HWND, X11 Window or NSView
 *   window_idle(w)           pump events (X11) and redraw; call often from the UI thread
 *   pw_redraw(w)             redraw now if the panel changed
 * The window's timer (or idle on X11) calls the panel's poll callback, if set, before it
 * redraws. On macOS, build as Objective-C without ARC, and define PANEL_VIEW_CLASS to a name that
 * is unique to the binary: Objective-C class names are global in the host process. */
#ifndef UI_WINDOW_H
#define UI_WINDOW_H

#if defined(__APPLE__)
#    import <Cocoa/Cocoa.h> /* before panel.h so its names can't clash with the system headers */
#endif

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "panel.h"

#define DOUBLE_CLICK_MS 350

typedef struct PanelWindow PanelWindow;
static void pw_redraw(PanelWindow* ui);

/* ---- Window layer: Win32 ---- */
#if defined(_WIN32)
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#    include <windowsx.h>

struct PanelWindow {
    Panel panel;
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
    PanelWindow* ui = (PanelWindow*)GetWindowLongPtrA(hwnd, GWLP_USERDATA);
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
        pw_redraw(ui);
        return 0;
    case WM_MOUSEMOVE:
        panel_mouse_move(&ui->panel, (float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp), (wp & MK_SHIFT) != 0);
        pw_redraw(ui);
        return 0;
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
        if (msg == WM_LBUTTONUP)
            ReleaseCapture();
        panel_mouse_up(&ui->panel);
        pw_redraw(ui);
        return 0;
    case WM_MOUSEWHEEL: {
        POINT pt;
        pt.x = GET_X_LPARAM(lp);
        pt.y = GET_Y_LPARAM(lp);
        ScreenToClient(hwnd, &pt);
        panel_wheel(&ui->panel, (float)pt.x, (float)pt.y, GET_WHEEL_DELTA_WPARAM(wp) / (float)WHEEL_DELTA,
                    (GET_KEYSTATE_WPARAM(wp) & MK_SHIFT) != 0);
        pw_redraw(ui);
        return 0;
    }
    case WM_MOUSEACTIVATE:
        SetFocus(hwnd); /* so the wheel reaches this window */
        return MA_ACTIVATE;
    case WM_TIMER:
        panel_poll(&ui->panel);
        pw_redraw(ui);
        return 0;
    default:
        break;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static int window_open(PanelWindow* ui, void* parent)
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
    SetTimer(ui->hwnd, 1, 33, NULL); /* picks up host updates such as automation */
    return 1;
}

static void window_close(PanelWindow* ui)
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

static void* window_widget(PanelWindow* ui) { return (void*)ui->hwnd; }

static void pw_redraw(PanelWindow* ui)
{
    if (ui->panel.dirty && ui->hwnd)
        InvalidateRect(ui->hwnd, NULL, FALSE);
}

/* The window's own timer drives redraws, so idle has nothing to do. */
static int window_idle(PanelWindow* ui) { pw_redraw(ui); return 0; }

/* ---- Window layer: X11 ---- */
#elif defined(__unix__) && !defined(__APPLE__)
#    include <X11/Xlib.h>
#    include <X11/Xutil.h>

struct PanelWindow {
    Panel panel;
    Display* dpy;
    Window win;
    GC gc;
    XImage* img;
    Time last_click;
    int last_x, last_y;
};

static int window_open(PanelWindow* ui, void* parent)
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

static void window_close(PanelWindow* ui)
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

static void* window_widget(PanelWindow* ui) { return (void*)(uintptr_t)ui->win; }

static void blit(PanelWindow* ui)
{
    XPutImage(ui->dpy, ui->win, ui->gc, ui->img, 0, 0, 0, 0, PANEL_W, PANEL_H);
    XFlush(ui->dpy);
}

static void pw_redraw(PanelWindow* ui)
{
    if (ui->panel.dirty && ui->dpy) {
        panel_draw(&ui->panel);
        blit(ui);
    }
}

static int window_idle(PanelWindow* ui)
{
    if (!ui->dpy)
        return 1;
    panel_poll(&ui->panel);
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
    pw_redraw(ui);
    return 0;
}

/* ---- Window layer: macOS Cocoa ---- */
#elif defined(__APPLE__)

#    if __has_feature(objc_arc)
#        error "Build the GUI without ARC (-fno-objc-arc); it manages retain counts itself."
#    endif
#    ifndef PANEL_VIEW_CLASS
#        error "Define PANEL_VIEW_CLASS to an Objective-C class name unique to this binary."
#    endif

struct PanelWindow {
    Panel panel;
    NSView* view;
    NSTimer* timer;
};

/* Objective-C class names are global in the host process, so each binary names its own
 * (brand, model and format) to stay unique among other plugins' GUIs. */
@interface PANEL_VIEW_CLASS : NSView {
@public
    PanelWindow* ui;
    float wheel_acc;
}
@end

@implementation PANEL_VIEW_CLASS

- (BOOL)isFlipped { return YES; } /* top-left origin, like the panel */
- (BOOL)acceptsFirstMouse:(NSEvent*)e { (void)e; return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

- (void)drawRect:(NSRect)dirty
{
    CGContextRef ctx;
    CGColorSpaceRef cs;
    CGDataProviderRef dp;
    CGImageRef img;
    (void)dirty;
    if (!ui)
        return;
    if (ui->panel.dirty)
        panel_draw(&ui->panel);
    ctx = [[NSGraphicsContext currentContext] CGContext];
    cs = CGColorSpaceCreateDeviceRGB();
    dp = CGDataProviderCreateWithData(NULL, ui->panel.px, (size_t)PANEL_W * PANEL_H * 4, NULL);
    /* Panel pixels are 0x00RRGGBB words: little-endian, alpha byte skipped. */
    img = CGImageCreate(PANEL_W, PANEL_H, 8, 32, (size_t)PANEL_W * 4, cs,
                        kCGBitmapByteOrder32Little | kCGImageAlphaNoneSkipFirst, dp, NULL, false,
                        kCGRenderingIntentDefault);
    if (img) {
        /* The view is flipped; flip back so the image isn't drawn upside down. */
        CGContextSaveGState(ctx);
        CGContextTranslateCTM(ctx, 0, PANEL_H);
        CGContextScaleCTM(ctx, 1, -1);
        CGContextDrawImage(ctx, CGRectMake(0, 0, PANEL_W, PANEL_H), img);
        CGContextRestoreGState(ctx);
        CGImageRelease(img);
    }
    CGDataProviderRelease(dp);
    CGColorSpaceRelease(cs);
}

- (NSPoint)panelPoint:(NSEvent*)e
{
    return [self convertPoint:[e locationInWindow] fromView:nil];
}

- (void)redrawIfDirty
{
    if (ui && ui->panel.dirty)
        [self setNeedsDisplay:YES];
}

- (void)tick:(NSTimer*)t
{
    (void)t;
    if (ui)
        panel_poll(&ui->panel);
    [self redrawIfDirty]; /* picks up host updates such as automation */
}

- (void)mouseDown:(NSEvent*)e
{
    NSPoint p = [self panelPoint:e];
    if (!ui)
        return;
    panel_mouse_down(&ui->panel, (float)p.x, (float)p.y, [e clickCount] == 2);
    [self redrawIfDirty];
}

- (void)mouseDragged:(NSEvent*)e
{
    NSPoint p = [self panelPoint:e];
    if (!ui)
        return;
    panel_mouse_move(&ui->panel, (float)p.x, (float)p.y, ([e modifierFlags] & NSEventModifierFlagShift) != 0);
    [self redrawIfDirty];
}

- (void)mouseUp:(NSEvent*)e
{
    (void)e;
    if (!ui)
        return;
    panel_mouse_up(&ui->panel);
    [self redrawIfDirty];
}

- (void)scrollWheel:(NSEvent*)e
{
    NSPoint p = [self panelPoint:e];
    float d = (float)[e scrollingDeltaY];
    if (!ui)
        return;
    /* Trackpads send many small precise deltas: gather them into whole wheel steps. */
    wheel_acc += [e hasPreciseScrollingDeltas] ? d / 10.0f : d;
    while (wheel_acc >= 1.0f || wheel_acc <= -1.0f) {
        const float step = wheel_acc > 0 ? 1.0f : -1.0f;
        panel_wheel(&ui->panel, (float)p.x, (float)p.y, step, ([e modifierFlags] & NSEventModifierFlagShift) != 0);
        wheel_acc -= step;
    }
    [self redrawIfDirty];
}

@end

static int window_open(PanelWindow* ui, void* parent)
{
    PANEL_VIEW_CLASS* v =
        [[PANEL_VIEW_CLASS alloc] initWithFrame:NSMakeRect(0, 0, PANEL_W, PANEL_H)];
    if (!v)
        return 0;
    v->ui = ui;
    v->wheel_acc = 0.0f;
    ui->view = v;
    if (parent)
        [(NSView*)parent addSubview:v];
    ui->timer = [NSTimer timerWithTimeInterval:1.0 / 30.0 target:v selector:@selector(tick:)
                                      userInfo:nil repeats:YES];
    [[NSRunLoop currentRunLoop] addTimer:ui->timer forMode:NSRunLoopCommonModes];
    return 1;
}

static void window_close(PanelWindow* ui)
{
    PANEL_VIEW_CLASS* v = (PANEL_VIEW_CLASS*)ui->view;
    if (!v)
        return;
    [ui->timer invalidate]; /* releases the timer's hold on the view */
    ui->timer = nil;
    v->ui = NULL;
    [v removeFromSuperview];
    [v release];
    ui->view = nil;
}

static void* window_widget(PanelWindow* ui) { return (void*)ui->view; }

static void pw_redraw(PanelWindow* ui)
{
    if (ui->view)
        [(PANEL_VIEW_CLASS*)ui->view redrawIfDirty];
}

/* The view's own timer drives redraws, so idle has nothing more to do. */
static int window_idle(PanelWindow* ui) { pw_redraw(ui); return 0; }

#else
#    error "No window layer for this platform (Windows, X11 and macOS are supported)."
#endif

#endif
