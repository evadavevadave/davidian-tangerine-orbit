/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* macOS counterpart of ui_check.c: loads the built Cocoa GUI the way a host does, embeds
 * it in a window, drives it with mouse events and checks the values it sends back. Also
 * checks the drawn panel is the right way up and saves a screenshot.
 * Build: clang -x objective-c -fno-objc-arc test/ui_check_mac.m -framework Cocoa -o ui_check_mac
 * Usage: ui_check_mac <bundle folder> [screenshot.png] */
#import <Cocoa/Cocoa.h>
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../src/lv2_abi.h"
#include "../src/ports.h"




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

/* A mouse event at panel point (x, y), top-left origin. */
static NSEvent* mouse(NSEventType type, NSView* v, float x, float y, int clicks)
{
    NSPoint p = [v convertPoint:NSMakePoint(x, y) toView:nil];
    return [NSEvent mouseEventWithType:type location:p modifierFlags:0 timestamp:0
                          windowNumber:[[v window] windowNumber] context:nil eventNumber:0
                            clickCount:clicks pressure:1.0f];
}

static void click(NSView* v, float x, float y)
{
    [v mouseDown:mouse(NSEventTypeLeftMouseDown, v, x, y, 1)];
    [v mouseUp:mouse(NSEventTypeLeftMouseUp, v, x, y, 1)];
}

static void dblclick(NSView* v, float x, float y)
{
    [v mouseDown:mouse(NSEventTypeLeftMouseDown, v, x, y, 2)];
    [v mouseUp:mouse(NSEventTypeLeftMouseUp, v, x, y, 2)];
}

static void drag(NSView* v, float x, float y, float dy)
{
    int i;
    [v mouseDown:mouse(NSEventTypeLeftMouseDown, v, x, y, 1)];
    for (i = 1; i <= 6; ++i)
        [v mouseDragged:mouse(NSEventTypeLeftMouseDragged, v, x, y + dy * i / 6, 1)];
    [v mouseUp:mouse(NSEventTypeLeftMouseUp, v, x, y + dy, 1)];
}

/* One line-based wheel notch up at panel point (x, y). NSEvent has no scroll-event
 * constructor, so build it with Core Graphics, which places events in global
 * coordinates (top-left origin of the main screen). An event with no window reports
 * its screen location as locationInWindow; the test window sits at the screen origin,
 * so that matches the view's window coordinates. */
static void wheel_up(NSView* v, float x, float y)
{
    NSPoint wp = [v convertPoint:NSMakePoint(x, y) toView:nil];
    NSPoint sp = [[v window] convertPointToScreen:wp];
    const CGFloat top = NSMaxY([[[NSScreen screens] objectAtIndex:0] frame]);
    CGEventRef cg = CGEventCreateScrollWheelEvent(NULL, kCGScrollEventUnitLine, 1, 1);
    NSEvent* e;
    CGEventSetLocation(cg, CGPointMake(sp.x, top - sp.y));
    e = [NSEvent eventWithCGEvent:cg];
    [v scrollWheel:e];
    CFRelease(cg);
}

#define WIDGET_T NSView*
#include "ui_script.h"

/* Brightness of the panel pixel at (x, y), top-left origin, whatever the backing scale. */
static float brightness(NSBitmapImageRep* rep, float x, float y)
{
    const float s = (float)[rep pixelsWide] / PANEL_W;
    NSColor* c = [[rep colorAtX:(NSInteger)(x * s) y:(NSInteger)(y * s)]
        colorUsingColorSpace:[NSColorSpace deviceRGBColorSpace]];
    return (float)(([c redComponent] + [c greenComponent] + [c blueComponent]) / 3.0);
}

int main(int argc, char** argv)
{
    @autoreleasepool {
        char path[1024], label[160];
        void* lib;
        const UiDesc* (*get)(uint32_t);
        const UiDesc* d;
        NSWindow* win;
        NSView* parent;
        NSView* v;
        LV2_Feature parent_feature;
        const LV2_Feature* features[2];
        void* ui;
        void* widget = NULL;
        int i;

        if (argc < 2) {
            printf("usage: ui_check_mac <bundle folder> [screenshot.png]\n");
            return 2;
        }
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];

        snprintf(path, sizeof path, "%s/tangerine_orbit_ui.dylib", argv[1]);
        lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        check("GUI library loads", lib != NULL);
        if (!lib) {
            printf("  %s\n", dlerror());
            return 1;
        }
        *(void**)&get = dlsym(lib, "lv2ui_descriptor");
        check("exports lv2ui_descriptor", get != NULL);
        if (!get)
            return 1;
        d = get(0);
        check("descriptor has the GUI's URI", d && strcmp(d->URI, ORBIT_UI_URI) == 0);
        check("only one GUI in the library", get(1) == NULL);

        win = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, PANEL_W, PANEL_H)
                                          styleMask:NSWindowStyleMaskBorderless
                                            backing:NSBackingStoreBuffered
                                              defer:NO];
        [win setFrameOrigin:NSMakePoint(0, 0)];
        parent = [win contentView];
        parent_feature.URI = "http://lv2plug.in/ns/extensions/ui#parent";
        parent_feature.data = parent;
        features[0] = &parent_feature;
        features[1] = NULL;

        check("refuses another plugin's URI",
              d->instantiate(d, "urn:other", argv[1], on_write, NULL, &widget, features) == NULL);
        ui = d->instantiate(d, ORBIT_URI, argv[1], on_write, NULL, &widget, features);
        check("instantiates inside the host's view", ui != NULL && widget != NULL);
        if (!ui)
            return 1;
        v = (NSView*)widget;
        check("the widget is a subview of the host's view", [v superview] == parent);
        snprintf(label, sizeof label, "view is %.0fx%.0f", [v frame].size.width, [v frame].size.height);
        check(label, [v frame].size.width == PANEL_W && [v frame].size.height == PANEL_H);
        check("offers the idle interface",
              d->extension_data("http://lv2plug.in/ns/extensions/ui#idleInterface") != NULL);

        for (i = PORT_SPEED; i < PORT_COUNT; ++i)
            d->port_event(ui, (uint32_t)i, sizeof(float), 0, &UI_DEFAULTS[i]);
        check("port updates from the host write nothing back", writes == 0);

        ui_script(v);

        /* Draw the view and check it is the right way up: the readout window at the top
         * right is near-black, the panel just below it is graphite. */
        {
            NSBitmapImageRep* rep = [v bitmapImageRepForCachingDisplayInRect:[v bounds]];
            float top, below;
            [v cacheDisplayInRect:[v bounds] toBitmapImageRep:rep];
            top = brightness(rep, DISP_X1 - 20, 48);
            below = brightness(rep, DISP_X1 - 20, 66);
            snprintf(label, sizeof label, "panel draws the right way up (readout %.3f, panel %.3f)", top, below);
            check(label, top < 0.08f && below > top + 0.04f);
            if (argc > 2) {
                NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
                check("screenshot written", [png writeToFile:[NSString stringWithUTF8String:argv[2]] atomically:YES]);
                printf("         %s\n", argv[2]);
            }
        }

        d->cleanup(ui);
        check("cleanup removes the view from the host", [[parent subviews] count] == 0);
        [win release];
        printf(failures ? "\n%d FAILED\n" : "\nAll passed\n", failures);
    }
    return failures ? 1 : 0;
}
