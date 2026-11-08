/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* Loads the built GUI library the way a Windows LV2 host does, embeds it in a window,
 * drives it with mouse messages and checks the values it sends back. Windows only.
 * Usage: ui_check <bundle folder> [screenshot.bmp] */
#define _CRT_SECURE_NO_WARNINGS
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "../src/lv2_abi.h"
#include "../src/ports.h"
#include "../src/ui/panel.h" /* widget positions */

typedef void (*WriteFn)(void*, uint32_t, uint32_t, uint32_t, const void*);
typedef struct {
    const char* URI;
    void* (*instantiate)(const void*, const char*, const char*, WriteFn, void*, void**, const LV2_Feature* const*);
    void (*cleanup)(void*);
    void (*port_event)(void*, uint32_t, uint32_t, uint32_t, const void*);
    const void* (*extension_data)(const char*);
} UiDesc;

static int failures;
static int last_port = -1, writes;
static float last_value;

static void check(const char* what, int ok)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        ++failures;
}

static void on_write(void* controller, uint32_t port, uint32_t size, uint32_t format, const void* buf)
{
    (void)controller;
    if (size == sizeof(float) && format == 0) {
        last_port = (int)port;
        last_value = *(const float*)buf;
        ++writes;
    }
}

static void pump(void)
{
    MSG m;
    while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }
}

static LPARAM xy(int x, int y) { return MAKELPARAM(x, y); }

static void click(HWND w, int x, int y)
{
    SendMessageA(w, WM_LBUTTONDOWN, MK_LBUTTON, xy(x, y));
    SendMessageA(w, WM_LBUTTONUP, 0, xy(x, y));
}

static void drag(HWND w, int x, int y, int dy)
{
    int i;
    SendMessageA(w, WM_LBUTTONDOWN, MK_LBUTTON, xy(x, y));
    for (i = 1; i <= 6; ++i)
        SendMessageA(w, WM_MOUSEMOVE, MK_LBUTTON, xy(x, y + dy * i / 6));
    SendMessageA(w, WM_LBUTTONUP, 0, xy(x, y + dy));
}

static int save_window(HWND w, const char* path)
{
    RECT r;
    HDC screen = GetDC(NULL), mem = CreateCompatibleDC(screen);
    BITMAPINFO bi;
    void* bits = NULL;
    HBITMAP bmp;
    FILE* f;
    BITMAPFILEHEADER fh;
    int width, height;
    GetClientRect(w, &r);
    width = r.right;
    height = r.bottom;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    SelectObject(mem, bmp);
    SendMessageA(w, WM_PRINTCLIENT, (WPARAM)mem, PRF_CLIENT);
    GdiFlush();
    f = fopen(path, "wb");
    if (f) {
        memset(&fh, 0, sizeof fh);
        fh.bfType = 0x4d42;
        fh.bfOffBits = sizeof fh + sizeof(BITMAPINFOHEADER);
        fh.bfSize = fh.bfOffBits + 4u * (unsigned)(width * height);
        fwrite(&fh, sizeof fh, 1, f);
        fwrite(&bi.bmiHeader, sizeof(BITMAPINFOHEADER), 1, f);
        fwrite(bits, 4, (size_t)(width * height), f);
        fclose(f);
    }
    DeleteDC(mem);
    DeleteObject(bmp);
    ReleaseDC(NULL, screen);
    return f != NULL;
}

int main(int argc, char** argv)
{
    char path[1024];
    HMODULE lib;
    const UiDesc* (*get)(uint32_t);
    const UiDesc* d;
    HWND parent, child = NULL;
    LV2_Feature parent_feature;
    const LV2_Feature* features[2];
    void* ui;
    void* widget = NULL;
    RECT rc;
    int i;
    char label[160];
    const Widget* speed = &WIDGETS[0];
    const Widget* bias = &WIDGETS[1];
    const Widget* output = &WIDGETS[3];
    const Widget* foot = &WIDGETS[4];

    if (argc < 2) {
        printf("usage: ui_check <bundle folder> [screenshot.bmp]\n");
        return 2;
    }
    snprintf(path, sizeof path, "%s/tangerine_orbit_ui.dll", argv[1]);
    lib = LoadLibraryA(path);
    check("GUI library loads", lib != NULL);
    if (!lib)
        return 1;
    *(void**)&get = (void*)GetProcAddress(lib, "lv2ui_descriptor");
    check("exports lv2ui_descriptor", get != NULL);
    if (!get)
        return 1;
    d = get(0);
    check("descriptor has the GUI's URI", d && strcmp(d->URI, ORBIT_UI_URI) == 0);
    check("only one GUI in the library", get(1) == NULL);

    parent = CreateWindowExA(0, "STATIC", "host", WS_OVERLAPPEDWINDOW, 0, 0, 700, 460, NULL, NULL, NULL, NULL);
    parent_feature.URI = "http://lv2plug.in/ns/extensions/ui#parent";
    parent_feature.data = parent;
    features[0] = &parent_feature;
    features[1] = NULL;

    check("refuses another plugin's URI",
          d->instantiate(d, "urn:other", argv[1], on_write, NULL, &widget, features) == NULL);
    ui = d->instantiate(d, ORBIT_URI, argv[1], on_write, NULL, &widget, features);
    check("instantiates inside the host's window", ui != NULL && widget != NULL);
    if (!ui)
        return 1;
    child = (HWND)widget;
    check("the widget is a child of the host's window", GetParent(child) == parent);
    GetClientRect(child, &rc);
    snprintf(label, sizeof label, "window is %ldx%ld", rc.right, rc.bottom);
    check(label, rc.right == PANEL_W && rc.bottom == PANEL_H);
    check("offers the idle interface",
          d->extension_data("http://lv2plug.in/ns/extensions/ui#idleInterface") != NULL);

    /* The host sends every port's value first, including the sweep output. */
    {
        static const float vals[PORT_COUNT] = { 0, 0, 0.5f, 0.46f, 1.0f, -6.0f, 1, 1, 0.6f };
        for (i = PORT_SPEED; i < PORT_COUNT; ++i)
            d->port_event(ui, (uint32_t)i, sizeof(float), 0, &vals[i]);
    }
    check("port updates from the host write nothing back", writes == 0);
    pump();

    /* 180 px is the knob's full travel, so 36 px down is a fifth of it. */
    drag(child, (int)bias->x, (int)bias->y, 36);
    snprintf(label, sizeof label, "dragging Bias down 36 px sends port %d = %.3f", last_port, last_value);
    check(label, last_port == PORT_BIAS && fabsf(last_value - 0.26f) < 0.01f);

    drag(child, (int)speed->x, (int)speed->y, -400);
    snprintf(label, sizeof label, "dragging Speed far up stops at %.3f Hz", last_value);
    check(label, last_port == PORT_SPEED && last_value == 10.0f);

    SendMessageA(child, WM_LBUTTONDBLCLK, MK_LBUTTON, xy((int)speed->x, (int)speed->y));
    SendMessageA(child, WM_LBUTTONUP, 0, xy((int)speed->x, (int)speed->y));
    snprintf(label, sizeof label, "double-click resets Speed to %.2f Hz", last_value);
    check(label, last_port == PORT_SPEED && fabsf(last_value - 0.5f) < 1e-4f);

    click(child, (int)foot->x, (int)foot->y);
    check("footswitch turns the effect off", last_port == PORT_ENGAGE && last_value == 0.0f);
    click(child, (int)foot->x, (int)foot->y);
    check("footswitch turns it back on", last_port == PORT_ENGAGE && last_value == 1.0f);

    {
        POINT pt = { (LONG)output->x, (LONG)output->y };
        ClientToScreen(child, &pt);
        writes = 0;
        SendMessageA(child, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), MAKELPARAM(pt.x, pt.y));
        snprintf(label, sizeof label, "mouse wheel on Output sends %.2f dB", last_value);
        check(label, writes == 1 && last_port == PORT_OUTPUT && fabsf(last_value - (-6.0f + 0.9f)) < 1e-3f);
    }
    {
        const float lamp = 0.9f;
        writes = 0;
        d->port_event(ui, PORT_SWEEP, sizeof(float), 0, &lamp);
        check("sweep lamp update writes nothing back", writes == 0);
    }
    click(child, 5, 5);
    check("clicking empty panel sends nothing new", last_port == PORT_OUTPUT);

    pump();
    if (argc > 2) {
        check("screenshot written", save_window(child, argv[2]));
        printf("         %s\n", argv[2]);
    }

    d->cleanup(ui);
    check("cleanup destroys the window", !IsWindow(child));
    DestroyWindow(parent);
    FreeLibrary(lib);
    printf(failures ? "\n%d FAILED\n" : "\nAll passed\n", failures);
    return failures ? 1 : 0;
}
