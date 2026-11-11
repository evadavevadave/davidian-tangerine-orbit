/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* Opens the installed Audio Unit the way a macOS host does: finds it by its component
 * description, renders audio through it, checks latency, bypass, parameters and saved
 * state, then loads its Cocoa view through kAudioUnitProperty_CocoaUI, drives it with
 * mouse events and saves a screenshot. Apple's auval is run by the CI as well.
 * Install the component first (make install), then:
 * Build: clang -x objective-c -fno-objc-arc test/au_check_mac.m -framework Cocoa \
 *        -framework AudioToolbox -framework AudioUnit -framework CoreAudio -o au_check_mac
 * Usage: au_check_mac [screenshot.png] */
#import <AudioToolbox/AudioToolbox.h>
#import <AudioUnit/AUCocoaUIView.h>
#import <Cocoa/Cocoa.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../src/ports.h"

#define RATE 48000.0
#define N 48000
#define BLOCK 512

static int failures;
static int last_port = -1, writes;
static float last_value;
static int begins, ends;
static AudioUnit unit;
static float snapshot[PORT_COUNT];

static void check(const char* what, int ok)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        ++failures;
}

static float get_param(int id)
{
    AudioUnitParameterValue v = NAN;
    AudioUnitGetParameter(unit, (AudioUnitParameterID)id, kAudioUnitScope_Global, 0, &v);
    return v;
}

/* The view sets parameters on the unit directly. After each mouse action, compare them
 * with the last snapshot to see what it sent, like the write callback in ui_check_mac.m. */
static void take_snapshot(void)
{
    int i;
    for (i = PORT_SPEED; i <= PORT_ENGAGE; ++i)
        snapshot[i] = get_param(i);
}

static void poll_params(void)
{
    int i;
    for (i = PORT_SPEED; i <= PORT_ENGAGE; ++i) {
        const float v = get_param(i);
        if (v != snapshot[i]) {
            last_port = i;
            last_value = v;
            ++writes;
            snapshot[i] = v;
        }
    }
}

/* ---- Audio ---- */

static float input[N];
static int in_pos;

static OSStatus feed(void* ref, AudioUnitRenderActionFlags* flags, const AudioTimeStamp* ts, UInt32 bus,
                     UInt32 frames, AudioBufferList* io)
{
    UInt32 c;
    (void)ref; (void)flags; (void)ts; (void)bus;
    for (c = 0; c < io->mNumberBuffers; ++c)
        memcpy(io->mBuffers[c].mData, input + in_pos, frames * sizeof(float));
    return noErr;
}

static int set_format(UInt32 in_ch, UInt32 out_ch)
{
    AudioStreamBasicDescription f;
    memset(&f, 0, sizeof f);
    f.mSampleRate = RATE;
    f.mFormatID = kAudioFormatLinearPCM;
    f.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
    f.mBytesPerPacket = f.mBytesPerFrame = 4;
    f.mFramesPerPacket = 1;
    f.mBitsPerChannel = 32;
    f.mChannelsPerFrame = in_ch;
    if (AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &f, sizeof f))
        return 0;
    f.mChannelsPerFrame = out_ch;
    return AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f) == noErr;
}

/* Renders the whole input through the unit into l (and r when stereo out). */
static int render(float* l, float* r, UInt32 out_ch)
{
    AudioTimeStamp ts;
    struct { AudioBufferList list; AudioBuffer extra; } out;
    memset(&ts, 0, sizeof ts);
    ts.mFlags = kAudioTimeStampSampleTimeValid;
    for (in_pos = 0; in_pos < N; in_pos += BLOCK) {
        /* N is not a multiple of BLOCK: the last block is shorter */
        const UInt32 n = N - in_pos < BLOCK ? (UInt32)(N - in_pos) : BLOCK;
        AudioUnitRenderActionFlags flags = 0;
        UInt32 c;
        out.list.mNumberBuffers = out_ch;
        for (c = 0; c < out_ch; ++c) {
            out.list.mBuffers[c].mNumberChannels = 1;
            out.list.mBuffers[c].mDataByteSize = n * sizeof(float);
            out.list.mBuffers[c].mData = NULL; /* let the unit supply the buffers */
        }
        ts.mSampleTime = in_pos;
        if (AudioUnitRender(unit, &flags, &ts, 0, n, &out.list) != noErr)
            return 0;
        memcpy(l + in_pos, out.list.mBuffers[0].mData, n * sizeof(float));
        if (r && out_ch > 1)
            memcpy(r + in_pos, out.list.mBuffers[1].mData, n * sizeof(float));
    }
    return 1;
}

static double rms(const float* x, int n)
{
    double s = 0;
    int i;
    for (i = 0; i < n; ++i)
        s += (double)x[i] * x[i];
    return sqrt(s / n);
}

/* ---- GUI helpers for ui_script.h (as ui_check_mac.m) ---- */

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
    poll_params();
}

static void dblclick(NSView* v, float x, float y)
{
    [v mouseDown:mouse(NSEventTypeLeftMouseDown, v, x, y, 2)];
    [v mouseUp:mouse(NSEventTypeLeftMouseUp, v, x, y, 2)];
    poll_params();
}

static void drag(NSView* v, float x, float y, float dy)
{
    int i;
    [v mouseDown:mouse(NSEventTypeLeftMouseDown, v, x, y, 1)];
    for (i = 1; i <= 6; ++i)
        [v mouseDragged:mouse(NSEventTypeLeftMouseDragged, v, x, y + dy * i / 6, 1)];
    [v mouseUp:mouse(NSEventTypeLeftMouseUp, v, x, y + dy, 1)];
    poll_params();
}

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
    poll_params();
}

#define WIDGET_T NSView*
#include "ui_script.h"

static void on_event(void* data, void* object, const AudioUnitEvent* e, UInt64 time, AudioUnitParameterValue v)
{
    (void)data; (void)object; (void)time; (void)v;
    if (e->mEventType == kAudioUnitEvent_BeginParameterChangeGesture)
        ++begins;
    else if (e->mEventType == kAudioUnitEvent_EndParameterChangeGesture)
        ++ends;
}

static void spin(double secs) { CFRunLoopRunInMode(kCFRunLoopDefaultMode, secs, false); }

int main(int argc, char** argv)
{
    @autoreleasepool {
        static float l[N], r[N];
        AudioComponentDescription desc = { kAudioUnitType_Effect, 'Torb', 'Dvdn', 0, 0 };
        AudioComponent comp;
        CFStringRef name = NULL;
        char label[200], text[128];
        AURenderCallbackStruct cb = { feed, NULL };
        Float64 latency = -1;
        UInt32 size, bypass, n_params;
        CFPropertyListRef state = NULL;
        AudioUnitCocoaViewInfo vi;
        double quiet, loud, err;
        Float32 sweep = -1.0f;
        int i, ok;

        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];

        comp = AudioComponentFindNext(NULL, &desc);
        check("the system finds the Audio Unit (aufx Torb Dvdn)", comp != NULL);
        if (!comp)
            return 1;
        AudioComponentCopyName(comp, &name);
        text[0] = 0;
        if (name)
            CFStringGetCString(name, text, sizeof text, kCFStringEncodingUTF8);
        snprintf(label, sizeof label, "its name is \"%s\"", text);
        check(label, !strcmp(text, "Davidian: Tangerine Orbit"));
        check("opens an instance", AudioComponentInstanceNew(comp, &unit) == noErr && unit);
        if (!unit)
            return 1;

        size = 0;
        AudioUnitGetPropertyInfo(unit, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, &size, NULL);
        n_params = size / sizeof(AudioUnitParameterID);
        snprintf(label, sizeof label, "%u parameters", n_params);
        check(label, n_params == PORT_ENGAGE - PORT_SPEED + 1);
        check("parameters start at their defaults (Speed 0.5 Hz, Bias 0.46, Output -6 dB, On, Effect On)",
              fabsf(get_param(PORT_SPEED) - 0.5f) < 1e-6f && fabsf(get_param(PORT_BIAS) - 0.46f) < 1e-6f
                  && get_param(PORT_OUTPUT) == -6.0f && get_param(PORT_ENABLED) == 1.0f
                  && get_param(PORT_ENGAGE) == 1.0f);
        check("the sweep position is not a parameter",
              AudioUnitSetParameter(unit, PORT_SWEEP, kAudioUnitScope_Global, 0, 0.5f, 0) != noErr);
        check("refuses 2 in / 1 out", set_format(2, 1) && AudioUnitInitialize(unit) != noErr);

        ok = set_format(1, 1)
            && AudioUnitSetProperty(unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb) == noErr
            && AudioUnitInitialize(unit) == noErr;
        check("initializes at 48 kHz, mono", ok);
        if (!ok)
            return 1;
        size = sizeof latency;
        AudioUnitGetProperty(unit, kAudioUnitProperty_Latency, kAudioUnitScope_Global, 0, &latency, &size);
        snprintf(label, sizeof label, "reports %.0f samples latency", latency * RATE);
        check(label, fabs(latency * RATE) < 1e-6);

        for (i = 0; i < N; ++i)
            input[i] = (float)(0.2 * sin(2.0 * M_PI * 220.0 * i / RATE));
        /* At 10 Hz each half-second window averages about five sweeps, so two windows can be
         * compared; at slower speeds the notches sit in different places in each. */
        AudioUnitSetParameter(unit, PORT_SPEED, kAudioUnitScope_Global, 0, 10.0f, 0);
        ok = render(l, NULL, 1);
        quiet = rms(l + N / 2, N / 2);
        snprintf(label, sizeof label, "processes a 200 mV sine at Speed 10 Hz: rms %.3f", quiet);
        check(label, ok && isfinite(quiet) && quiet > 1e-3);
        size = sizeof sweep;
        ok = AudioUnitGetProperty(unit, 64000, kAudioUnitScope_Global, 0, &sweep, &size) == noErr;
        snprintf(label, sizeof label, "reports the sweep position for the panel's lamp (%.2f)", sweep);
        check(label, ok && sweep >= 0.0f && sweep <= 1.0f);
        AudioUnitSetParameter(unit, PORT_OUTPUT, kAudioUnitScope_Global, 0, 6.0f, 0);
        ok = render(l, NULL, 1);
        loud = rms(l + N / 2, N / 2);
        snprintf(label, sizeof label, "raising Output from -6 to +6 dB raises the level %.1f dB", 20.0 * log10(loud / quiet));
        check(label, ok && isfinite(loud) && loud > 3.5 * quiet && loud < 4.5 * quiet);
        AudioUnitSetParameter(unit, PORT_OUTPUT, kAudioUnitScope_Global, 0, -6.0f, 0);
        AudioUnitSetParameter(unit, PORT_SPEED, kAudioUnitScope_Global, 0, 0.5f, 0);

        AudioUnitSetParameter(unit, PORT_ENGAGE, kAudioUnitScope_Global, 0, 0.0f, 0);
        ok = render(l, NULL, 1);
        err = 0;
        for (i = N / 2; i < N; ++i)
            err = fmax(err, fabs(l[i] - input[i]));
        snprintf(label, sizeof label, "Effect On off passes the input through untouched (error %.1e)", err);
        check(label, ok && err == 0.0);
        AudioUnitSetParameter(unit, PORT_ENGAGE, kAudioUnitScope_Global, 0, 1.0f, 0);

        bypass = 1;
        AudioUnitSetProperty(unit, kAudioUnitProperty_BypassEffect, kAudioUnitScope_Global, 0, &bypass, sizeof bypass);
        check("the host's bypass turns On off", get_param(PORT_ENABLED) == 0.0f);
        ok = render(l, NULL, 1);
        err = 0;
        for (i = N / 2; i < N; ++i)
            err = fmax(err, fabs(l[i] - input[i]));
        snprintf(label, sizeof label, "host bypass passes the input through untouched (error %.1e)", err);
        check(label, ok && err == 0.0);
        bypass = 0;
        AudioUnitSetProperty(unit, kAudioUnitProperty_BypassEffect, kAudioUnitScope_Global, 0, &bypass, sizeof bypass);

        AudioUnitUninitialize(unit);
        {
            const int fmt = set_format(1, 2);
            const OSStatus init = fmt ? AudioUnitInitialize(unit) : -1;
            const int rendered = init == noErr && render(l, r, 2);
            ok = rendered && !memcmp(l, r, sizeof l) && rms(l + N / 2, N / 2) > 1e-3;
            snprintf(label, sizeof label, "1 in / 2 out carries the same signal on both sides (format %d, init %d, render %d)",
                     fmt, (int)init, rendered);
            check(label, ok);
        }

        /* Saved state */
        {
            OSStatus get, set = -1;
            AudioUnitSetParameter(unit, PORT_BIAS, kAudioUnitScope_Global, 0, 0.9f, 0);
            size = sizeof state;
            get = AudioUnitGetProperty(unit, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &state, &size);
            snprintf(label, sizeof label, "saves its state (ClassInfo, status %d)", (int)get);
            check(label, get == noErr && state);
            AudioUnitSetParameter(unit, PORT_BIAS, kAudioUnitScope_Global, 0, 0.1f, 0);
            if (state)
                set = AudioUnitSetProperty(unit, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &state, sizeof state);
            snprintf(label, sizeof label, "restores it (status %d, Bias %.2f)", (int)set, get_param(PORT_BIAS));
            check(label, set == noErr && fabsf(get_param(PORT_BIAS) - 0.9f) < 1e-6f);
        }
        if (state)
            CFRelease(state);
        AudioUnitSetParameter(unit, PORT_BIAS, kAudioUnitScope_Global, 0, 0.46f, 0);

        /* The Cocoa view */
        {
            OSStatus st;
            size = sizeof vi;
            memset(&vi, 0, sizeof vi);
            st = AudioUnitGetProperty(unit, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0, &vi, &size);
            ok = st == noErr && vi.mCocoaAUViewBundleLocation && vi.mCocoaAUViewClass[0];
            snprintf(label, sizeof label, "offers a Cocoa view (status %d)", (int)st);
            check(label, ok);
        }
        if (ok) {
            NSBundle* bundle = [NSBundle bundleWithURL:(NSURL*)vi.mCocoaAUViewBundleLocation];
            Class cls = [bundle classNamed:(NSString*)vi.mCocoaAUViewClass[0]];
            id<AUCocoaUIBase> factory = cls ? [[cls alloc] init] : nil;
            NSView* view = factory ? [factory uiViewForAudioUnit:unit withSize:NSMakeSize(PANEL_W, PANEL_H)] : nil;
            NSWindow* win;
            NSView* panel;
            AUEventListenerRef listener = NULL;
            check("the view factory class loads and conforms to AUCocoaUIBase",
                  cls && [cls conformsToProtocol:@protocol(AUCocoaUIBase)]);
            snprintf(label, sizeof label, "the factory makes a %.0fx%.0f view",
                     view ? [view frame].size.width : 0.0, view ? [view frame].size.height : 0.0);
            check(label, view && [view frame].size.width == PANEL_W && [view frame].size.height == PANEL_H);
            if (!view)
                return 1;
            [view retain];
            win = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, PANEL_W, PANEL_H)
                                              styleMask:NSWindowStyleMaskBorderless
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];
            [win setFrameOrigin:NSMakePoint(0, 0)];
            [[win contentView] addSubview:view];
            panel = [[view subviews] count] ? [[view subviews] objectAtIndex:0] : nil;
            check("the panel is inside it", panel != nil);
            if (!panel)
                return 1;

            AUEventListenerCreate(on_event, NULL, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode, 0, 0, &listener);
            for (i = PORT_SPEED; i <= PORT_ENGAGE; ++i) {
                AudioUnitEvent e;
                memset(&e, 0, sizeof e);
                e.mArgument.mParameter.mAudioUnit = unit;
                e.mArgument.mParameter.mParameterID = (AudioUnitParameterID)i;
                e.mArgument.mParameter.mScope = kAudioUnitScope_Global;
                e.mEventType = kAudioUnitEvent_BeginParameterChangeGesture;
                AUEventListenerAddEventType(listener, NULL, &e);
                e.mEventType = kAudioUnitEvent_EndParameterChangeGesture;
                AUEventListenerAddEventType(listener, NULL, &e);
            }
            take_snapshot();
            spin(0.1);

            ui_script(panel);

            spin(0.3);
            snprintf(label, sizeof label, "edits are reported as gestures (%d begin, %d end)", begins, ends);
            check(label, begins > 0 && begins == ends);

            /* Automation from the host reaches the panel. */
            {
                AudioUnitParameter p = { unit, PORT_BIAS, kAudioUnitScope_Global, 0 };
                AUParameterSet(NULL, NULL, &p, 1.0f, 0);
                spin(0.2);
            }
            {
                NSBitmapImageRep* rep = [panel bitmapImageRepForCachingDisplayInRect:[panel bounds]];
                [panel cacheDisplayInRect:[panel bounds] toBitmapImageRep:rep];
                if (argc > 1) {
                    NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
                    check("screenshot written", [png writeToFile:[NSString stringWithUTF8String:argv[1]] atomically:YES]);
                    printf("         %s\n", argv[1]);
                }
            }
            if (listener)
                AUListenerDispose(listener);
            [view removeFromSuperview];
            [view release];
            [win release];
            [(id)factory release];
            CFRelease(vi.mCocoaAUViewBundleLocation);
            CFRelease(vi.mCocoaAUViewClass[0]);
            spin(0.1);
            check("closing the view leaves the unit working", render(l, r, 2));
        }

        AudioUnitUninitialize(unit);
        AudioComponentInstanceDispose(unit);
        if (name)
            CFRelease(name);
        printf(failures ? "\n%d FAILED\n" : "\nAll passed\n", failures);
    }
    return failures ? 1 : 0;
}
