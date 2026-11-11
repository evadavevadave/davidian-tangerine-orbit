/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* Loads the built VST3 plugin the way a Windows VST3 host does: through GetPluginFactory,
 * with the host's side of the interfaces (component handler, parameter changes, streams)
 * implemented here in C. Checks the factory, buses, parameters, audio, latency, bypass and
 * saved state, then opens the editor in a window and drives it with the mouse.
 * Usage: vst3_check <path to tangerine_orbit.vst3 bundle> [screenshot.bmp]
 * Steinberg's own validator is run by the CI as well; this test needs no SDK. */
#define _CRT_SECURE_NO_WARNINGS
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "../src/ports.h"
#include "../src/vst3/vst3_c_api.h"

#define API SMTG_STDMETHODCALLTYPE
typedef Steinberg_tresult tresult;
typedef Steinberg_uint32 uint32;

static int failures;
static int last_port = -1, writes;
static float last_value;
static int gestures_open, edits_outside_gesture, begins, ends;

static void check(const char* what, int ok)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        ++failures;
}

/* Normalized value -> the panel's own value, so ui_script.h can check what was sent. */
static float plain(int id, double n)
{
    switch (id) {
    case PORT_SPEED: return (float)(0.2 * pow(50.0, n));
    case PORT_VOLTS: return (float)(0.1 * pow(100.0, n));
    case PORT_OUTPUT: return (float)(-24.0 + 36.0 * n);
    case PORT_ENABLED: return n >= 0.5 ? 0.0f : 1.0f;
    case PORT_ENGAGE: return n >= 0.5 ? 1.0f : 0.0f;
    default: return (float)n;
    }
}

/* ---- Host side: IComponentHandler ---- */

static tresult API h_query(void* s, const Steinberg_TUID iid, void** obj)
{
    if (!memcmp(iid, Steinberg_FUnknown_iid, 16) || !memcmp(iid, Steinberg_Vst_IComponentHandler_iid, 16)) {
        *obj = s;
        return Steinberg_kResultOk;
    }
    *obj = NULL;
    return Steinberg_kNoInterface;
}
static uint32 API h_ref(void* s) { (void)s; return 1; }
static tresult API h_begin(void* s, Steinberg_Vst_ParamID id) { (void)s; (void)id; ++gestures_open; ++begins; return 0; }
static tresult API h_perform(void* s, Steinberg_Vst_ParamID id, Steinberg_Vst_ParamValue n)
{
    (void)s;
    if (gestures_open <= 0)
        ++edits_outside_gesture;
    last_port = (int)id;
    last_value = plain((int)id, n);
    ++writes;
    return 0;
}
static tresult API h_end(void* s, Steinberg_Vst_ParamID id) { (void)s; (void)id; --gestures_open; ++ends; return 0; }
static tresult API h_restart(void* s, Steinberg_int32 f) { (void)s; (void)f; return 0; }
static const Steinberg_Vst_IComponentHandlerVtbl HANDLER_VTBL = { h_query, h_ref, h_ref, h_begin, h_perform, h_end, h_restart };
static struct { const Steinberg_Vst_IComponentHandlerVtbl* v; } handler = { &HANDLER_VTBL };

/* ---- Host side: one parameter change (IParameterChanges with one IParamValueQueue) ---- */

typedef struct {
    const Steinberg_Vst_IParamValueQueueVtbl* v;
    Steinberg_Vst_ParamID id;
    Steinberg_Vst_ParamValue value;
} Queue;
typedef struct {
    const Steinberg_Vst_IParameterChangesVtbl* v;
    Queue q;
    int count;
} Changes;

static tresult API no_query(void* s, const Steinberg_TUID iid, void** obj) { (void)s; (void)iid; *obj = NULL; return Steinberg_kNoInterface; }
static Steinberg_Vst_ParamID API q_id(void* s) { return ((Queue*)s)->id; }
static Steinberg_int32 API q_count(void* s) { (void)s; return 1; }
static tresult API q_point(void* s, Steinberg_int32 i, Steinberg_int32* off, Steinberg_Vst_ParamValue* v)
{
    if (i != 0)
        return Steinberg_kInvalidArgument;
    *off = 0;
    *v = ((Queue*)s)->value;
    return 0;
}
static tresult API q_add(void* s, Steinberg_int32 o, Steinberg_Vst_ParamValue v, Steinberg_int32* i) { (void)s; (void)o; (void)v; (void)i; return Steinberg_kResultFalse; }
static const Steinberg_Vst_IParamValueQueueVtbl QUEUE_VTBL = { no_query, h_ref, h_ref, q_id, q_count, q_point, q_add };
static Steinberg_int32 API c_count(void* s) { return ((Changes*)s)->count; }
static struct Steinberg_Vst_IParamValueQueue* API c_get(void* s, Steinberg_int32 i)
{
    return i == 0 && ((Changes*)s)->count ? (struct Steinberg_Vst_IParamValueQueue*)&((Changes*)s)->q : NULL;
}
static struct Steinberg_Vst_IParamValueQueue* API c_add(void* s, const Steinberg_Vst_ParamID* id, Steinberg_int32* i) { (void)s; (void)id; (void)i; return NULL; }
static const Steinberg_Vst_IParameterChangesVtbl CHANGES_VTBL = { no_query, h_ref, h_ref, c_count, c_get, c_add };

/* ---- Host side: a memory IBStream ---- */

typedef struct {
    const Steinberg_IBStreamVtbl* v;
    unsigned char data[4096];
    Steinberg_int64 size, pos;
} Stream;
static tresult API s_read(void* s, void* buf, Steinberg_int32 n, Steinberg_int32* got)
{
    Stream* m = (Stream*)s;
    Steinberg_int32 k = (Steinberg_int32)(m->size - m->pos < n ? m->size - m->pos : n);
    memcpy(buf, m->data + m->pos, (size_t)k);
    m->pos += k;
    if (got)
        *got = k;
    return 0;
}
static tresult API s_write(void* s, void* buf, Steinberg_int32 n, Steinberg_int32* put)
{
    Stream* m = (Stream*)s;
    if (m->pos + n > (Steinberg_int64)sizeof m->data)
        return Steinberg_kResultFalse;
    memcpy(m->data + m->pos, buf, (size_t)n);
    m->pos += n;
    if (m->pos > m->size)
        m->size = m->pos;
    if (put)
        *put = n;
    return 0;
}
static tresult API s_seek(void* s, Steinberg_int64 pos, Steinberg_int32 mode, Steinberg_int64* res)
{
    Stream* m = (Stream*)s;
    m->pos = mode == 0 ? pos : (mode == 1 ? m->pos + pos : m->size + pos);
    if (res)
        *res = m->pos;
    return 0;
}
static tresult API s_tell(void* s, Steinberg_int64* pos) { *pos = ((Stream*)s)->pos; return 0; }
static const Steinberg_IBStreamVtbl STREAM_VTBL = { no_query, h_ref, h_ref, s_read, s_write, s_seek, s_tell };

/* ---- Helpers ---- */

#define VT(obj) ((obj)->lpVtbl)

typedef struct {
    struct Steinberg_Vst_IComponent* comp;
    struct Steinberg_Vst_IAudioProcessor* proc;
    struct Steinberg_Vst_IEditController* ctl;
} Inst;

static int make(struct Steinberg_IPluginFactory* f, const Steinberg_TUID cid, Inst* in)
{
    memset(in, 0, sizeof *in);
    if (VT(f)->createInstance(f, cid, Steinberg_Vst_IComponent_iid, (void**)&in->comp) != 0 || !in->comp)
        return 0;
    VT(in->comp)->initialize(in->comp, NULL);
    VT(in->comp)->queryInterface(in->comp, Steinberg_Vst_IAudioProcessor_iid, (void**)&in->proc);
    VT(in->comp)->queryInterface(in->comp, Steinberg_Vst_IEditController_iid, (void**)&in->ctl);
    return in->proc && in->ctl;
}

static void destroy(Inst* in)
{
    if (in->ctl)
        VT(in->ctl)->release(in->ctl);
    if (in->proc)
        VT(in->proc)->release(in->proc);
    if (in->comp) {
        VT(in->comp)->terminate(in->comp);
        VT(in->comp)->release(in->comp);
    }
}

static int start(Inst* in, double rate, Steinberg_Vst_SpeakerArrangement arr)
{
    struct Steinberg_Vst_ProcessSetup setup = { 0, Steinberg_Vst_SymbolicSampleSizes_kSample32, 4096, 0 };
    Steinberg_Vst_SpeakerArrangement a = arr, b = arr;
    setup.sampleRate = rate;
    return VT(in->proc)->setBusArrangements(in->proc, &a, 1, &b, 1) == Steinberg_kResultTrue
        && VT(in->proc)->setupProcessing(in->proc, &setup) == 0
        && VT(in->comp)->setActive(in->comp, 1) == 0
        && VT(in->proc)->setProcessing(in->proc, 1) == 0;
}

static void stop(Inst* in)
{
    VT(in->proc)->setProcessing(in->proc, 0);
    VT(in->comp)->setActive(in->comp, 0);
}

/* Runs n samples of in through the plugin (both channels get the same input when stereo),
 * with an optional parameter change at the start of the first block. */
static int run(Inst* in, const float* x, float* l, float* r, int n, int stereo, int id, double value)
{
    struct Steinberg_Vst_AudioBusBuffers ib, ob;
    struct Steinberg_Vst_ProcessData d;
    Changes ch;
    float* ich[2];
    float* och[2];
    int done = 0;
    memset(&ch, 0, sizeof ch);
    ch.v = &CHANGES_VTBL;
    ch.q.v = &QUEUE_VTBL;
    ch.q.id = (Steinberg_Vst_ParamID)id;
    ch.q.value = value;
    ch.count = id >= 0;
    do { /* runs once with no samples for a parameter flush */
        const int k = n - done < 1000 ? n - done : 1000; /* odd sizes on purpose */
        memset(&ib, 0, sizeof ib);
        memset(&ob, 0, sizeof ob);
        ich[0] = ich[1] = (float*)x + done;
        och[0] = l + done;
        och[1] = r ? r + done : NULL;
        ib.numChannels = ob.numChannels = stereo ? 2 : 1;
        ib.Steinberg_Vst_AudioBusBuffers_channelBuffers32 = ich;
        ob.Steinberg_Vst_AudioBusBuffers_channelBuffers32 = och;
        memset(&d, 0, sizeof d);
        d.processMode = 0;
        d.symbolicSampleSize = Steinberg_Vst_SymbolicSampleSizes_kSample32;
        d.numSamples = k;
        d.numInputs = d.numOutputs = 1;
        d.inputs = &ib;
        d.outputs = &ob;
        d.inputParameterChanges = (struct Steinberg_Vst_IParameterChanges*)&ch;
        if (VT(in->proc)->process(in->proc, &d) != 0)
            return 0;
        ch.count = 0;
        done += k;
    } while (done < n);
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

static int all_finite(const float* x, int n)
{
    int i;
    for (i = 0; i < n; ++i)
        if (!isfinite(x[i]))
            return 0;
    return 1;
}

static void to_utf8(const Steinberg_char16* s, char* out, int n)
{
    int i;
    for (i = 0; i < n - 1 && s[i]; ++i)
        out[i] = (char)s[i];
    out[i] = 0;
}

/* ---- GUI helpers for ui_script.h (same as ui_check.c) ---- */

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

static void dblclick(HWND w, int x, int y)
{
    SendMessageA(w, WM_LBUTTONDBLCLK, MK_LBUTTON, xy(x, y));
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

static void wheel_up(HWND w, int x, int y)
{
    POINT pt;
    pt.x = x;
    pt.y = y;
    ClientToScreen(w, &pt);
    SendMessageA(w, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), MAKELPARAM(pt.x, pt.y));
}

#define WIDGET_T HWND
#include "ui_script.h"

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


#define N 48000

/* Largest difference between the output and the input over the second half. */
static double passthrough_error(const float* out, const float* in)
{
    double err = 0;
    int i;
    for (i = N / 2; i < N; ++i)
        err = fmax(err, fabs((double)out[i] - in[i]));
    return err;
}

int main(int argc, char** argv)
{
    static float x[N], l[N], r[N];
    char path[1024], label[200], text[129];
    HMODULE lib;
    struct Steinberg_IPluginFactory* (API * get)(void);
    struct Steinberg_IPluginFactory* f;
    struct Steinberg_IPluginFactory2* f2 = NULL;
    struct Steinberg_PFactoryInfo fi;
    struct Steinberg_PClassInfo2 ci;
    Steinberg_TUID cid;
    struct Steinberg_Vst_BusInfo bus;
    struct Steinberg_Vst_ParameterInfo pi;
    Steinberg_Vst_String128 s16;
    Steinberg_Vst_ParamValue nv;
    Steinberg_Vst_SpeakerArrangement a51 = 0x3F;
    struct Steinberg_IPlugView* view;
    struct Steinberg_ViewRect rect;
    Stream st, st2;
    Inst in, in2;
    HWND parent, child;
    RECT rc;
    int i, ok, bypass_index = -1, n_params;
    double quiet, loud, err;

    if (argc < 2) {
        printf("usage: vst3_check <tangerine_orbit.vst3 bundle> [screenshot.bmp]\n");
        return 2;
    }
    snprintf(path, sizeof path, "%s/Contents/x86_64-win/tangerine_orbit.vst3", argv[1]);
    lib = LoadLibraryA(path);
    check("plugin module loads from the bundle", lib != NULL);
    if (!lib)
        return 1;
    *(void**)&get = (void*)GetProcAddress(lib, "GetPluginFactory");
    check("exports GetPluginFactory, InitDll and ExitDll",
          get && GetProcAddress(lib, "InitDll") && GetProcAddress(lib, "ExitDll"));
    if (!get)
        return 1;
    f = get();
    check("factory exists", f != NULL);
    if (!f)
        return 1;
    VT(f)->getFactoryInfo(f, &fi);
    check("factory vendor is Davidian", !strcmp(fi.vendor, "Davidian"));
    check("one class", VT(f)->countClasses(f) == 1);
    VT(f)->queryInterface(f, Steinberg_IPluginFactory2_iid, (void**)&f2);
    check("factory offers IPluginFactory2", f2 != NULL);
    if (!f2 || VT(f2)->getClassInfo2(f2, 0, &ci) != 0)
        return 1;
    printf("         class: \"%s\", %s, %s, %s\n", ci.name, ci.category, ci.subCategories, ci.sdkVersion);
    check("class is an audio effect named Davidian Tangerine Orbit",
          !strcmp(ci.category, "Audio Module Class") && !strcmp(ci.name, "Davidian Tangerine Orbit")
              && !strncmp(ci.subCategories, "Fx", 2));
    memcpy(cid, ci.cid, 16);
    {
        void* none = NULL;
        Steinberg_TUID bad;
        memset(bad, 0x5a, 16);
        check("refuses an unknown class ID",
              VT(f)->createInstance(f, bad, Steinberg_Vst_IComponent_iid, &none) != 0 && none == NULL);
    }

    ok = make(f, cid, &in);
    check("creates the component, with the processor and controller on it", ok);
    if (!ok)
        return 1;
    VT(in.ctl)->setComponentHandler(in.ctl, (struct Steinberg_Vst_IComponentHandler*)&handler);

    /* Buses */
    check("one audio input and one audio output, no event buses",
          VT(in.comp)->getBusCount(in.comp, 0, 0) == 1 && VT(in.comp)->getBusCount(in.comp, 0, 1) == 1
              && VT(in.comp)->getBusCount(in.comp, 1, 0) == 0);
    VT(in.comp)->getBusInfo(in.comp, 0, 0, 0, &bus);
    check("input bus is mono by default", bus.channelCount == 1);
    check("refuses 5.1", VT(in.proc)->setBusArrangements(in.proc, &a51, 1, &a51, 1) == Steinberg_kResultFalse);
    check("accepts 32-bit float, refuses 64-bit",
          VT(in.proc)->canProcessSampleSize(in.proc, 0) == 0 && VT(in.proc)->canProcessSampleSize(in.proc, 1) != 0);

    /* Parameters */
    n_params = VT(in.ctl)->getParameterCount(in.ctl);
    snprintf(label, sizeof label, "%d parameters", n_params);
    check(label, n_params == PORT_ENGAGE - PORT_SPEED + 1);
    ok = 1;
    for (i = 0; i < n_params; ++i) {
        if (VT(in.ctl)->getParameterInfo(in.ctl, i, &pi) != 0 || pi.id != (Steinberg_Vst_ParamID)(PORT_SPEED + i))
            ok = 0;
        if (pi.flags & Steinberg_Vst_ParameterInfo_ParameterFlags_kIsBypass)
            bypass_index = i;
        if (fabs(VT(in.ctl)->getParamNormalized(in.ctl, pi.id) - pi.defaultNormalizedValue) > 1e-9)
            ok = 0;
    }
    check("parameter IDs are the LV2 port indices, and start at their defaults", ok);
    check("the host's bypass parameter is the Enabled port", bypass_index == PORT_ENABLED - PORT_SPEED);
    VT(in.ctl)->getParameterInfo(in.ctl, 0, &pi);
    to_utf8(pi.title, text, sizeof text);
    check("the first parameter is Speed", !strcmp(text, "Speed"));
    VT(in.ctl)->getParameterInfo(in.ctl, PORT_ENGAGE - PORT_SPEED, &pi);
    to_utf8(pi.title, text, sizeof text);
    check("the footswitch is its own parameter, Effect On",
          !strcmp(text, "Effect On") && pi.stepCount == 1
              && !(pi.flags & Steinberg_Vst_ParameterInfo_ParameterFlags_kIsBypass));
    VT(in.ctl)->getParamStringByValue(in.ctl, PORT_SPEED, VT(in.ctl)->getParamNormalized(in.ctl, PORT_SPEED), s16);
    to_utf8(s16, text, sizeof text);
    snprintf(label, sizeof label, "Speed at its default reads \"%s\"", text);
    check(label, !strcmp(text, "0.50"));
    VT(in.ctl)->getParamStringByValue(in.ctl, PORT_VOLTS, 0.5, s16);
    to_utf8(s16, text, sizeof text);
    snprintf(label, sizeof label, "Volts at half travel reads \"%s\"", text);
    check(label, !strcmp(text, "1.00"));
    {
        Steinberg_Vst_TChar two[4];
        two[0] = '2'; two[1] = '.'; two[2] = '0'; two[3] = 0;
        check("typing 2.0 into Speed gives 2 Hz on the log scale",
              VT(in.ctl)->getParamValueByString(in.ctl, PORT_SPEED, two, &nv) == 0
                  && fabs(nv - log(10.0) / log(50.0)) < 1e-9);
    }
    check("plain and normalized round trip (Output -6 dB)",
          fabs(VT(in.ctl)->normalizedParamToPlain(in.ctl, PORT_OUTPUT,
                   VT(in.ctl)->plainParamToNormalized(in.ctl, PORT_OUTPUT, -6.0)) + 6.0) < 1e-9);

    /* Audio */
    ok = start(&in, 48000.0, Steinberg_Vst_SpeakerArr_kMono);
    check("sets up and activates at 48 kHz, mono", ok);
    if (!ok)
        return 1;
    snprintf(label, sizeof label, "reports %u samples latency", VT(in.proc)->getLatencySamples(in.proc));
    check(label, VT(in.proc)->getLatencySamples(in.proc) == 0);
    for (i = 0; i < N; ++i)
        x[i] = (float)(0.2 * sin(2.0 * 3.14159265358979 * 220.0 * i / 48000.0));
    /* At 10 Hz each half-second window averages about five sweeps, so two windows can be
     * compared; at slower speeds the notches sit in different places in each. */
    ok = run(&in, x, l, NULL, N, 0, PORT_SPEED, 1.0);
    quiet = rms(l + N / 2, N / 2);
    snprintf(label, sizeof label, "processes a 200 mV sine with Speed automated to 10 Hz: rms %.3f", quiet);
    check(label, ok && all_finite(l, N) && quiet > 1e-3);
    ok = run(&in, x, l, NULL, N, 0, PORT_OUTPUT, (6.0 + 24.0) / 36.0);
    loud = rms(l + N / 2, N / 2);
    snprintf(label, sizeof label, "automating Output from -6 to +6 dB raises the level %.1f dB",
             20.0 * log10(loud / quiet));
    check(label, ok && all_finite(l, N) && loud > 3.5 * quiet && loud < 4.5 * quiet);
    ok = run(&in, x, l, NULL, N, 0, PORT_ENGAGE, 0.0);
    err = passthrough_error(l, x);
    snprintf(label, sizeof label, "Effect On off passes the input through untouched (error %.1e)", err);
    check(label, ok && err == 0.0);
    run(&in, x, l, NULL, N, 0, PORT_ENGAGE, 1.0);
    ok = run(&in, x, l, NULL, N, 0, PORT_ENABLED, 1.0);
    err = passthrough_error(l, x);
    snprintf(label, sizeof label, "host bypass passes the input through untouched (error %.1e)", err);
    check(label, ok && err == 0.0);
    stop(&in);

    ok = start(&in, 96000.0, Steinberg_Vst_SpeakerArr_kStereo);
    check("restarts at 96 kHz, stereo", ok);
    ok = ok && run(&in, x, l, r, N, 1, PORT_ENABLED, 0.0);
    ok = ok && memcmp(l, r, sizeof l) == 0 && rms(l + N / 2, N / 2) > 1e-3;
    check("stereo out carries the same signal on both sides", ok);
    stop(&in);

    /* Saved state */
    run(&in, x, l, NULL, 0, 0, PORT_BIAS, 0.9); /* a parameter flush, as hosts send */
    memset(&st, 0, sizeof st);
    st.v = &STREAM_VTBL;
    check("saves its state", VT(in.comp)->getState(in.comp, (struct Steinberg_IBStream*)&st) == 0 && st.size > 0);
    ok = make(f, cid, &in2);
    st.pos = 0;
    ok = ok && VT(in2.comp)->setState(in2.comp, (struct Steinberg_IBStream*)&st) == 0;
    st.pos = 0;
    ok = ok && VT(in2.ctl)->setComponentState(in2.ctl, (struct Steinberg_IBStream*)&st) == 0;
    memset(&st2, 0, sizeof st2);
    st2.v = &STREAM_VTBL;
    ok = ok && VT(in2.comp)->getState(in2.comp, (struct Steinberg_IBStream*)&st2) == 0;
    check("a new instance restores it exactly", ok && st2.size == st.size && !memcmp(st.data, st2.data, (size_t)st.size));
    check("the controller follows the restored state (Bias 0.9)",
          fabs(VT(in2.ctl)->getParamNormalized(in2.ctl, PORT_BIAS) - 0.9) < 1e-6);
    memcpy(st2.data, "JUNK", 4);
    st2.pos = 0;
    check("refuses a state that isn't its own", VT(in2.comp)->setState(in2.comp, (struct Steinberg_IBStream*)&st2) != 0);
    destroy(&in2);

    /* Editor */
    view = VT(in.ctl)->createView(in.ctl, "editor");
    check("creates the editor view", view != NULL);
    if (!view)
        return 1;
    check("the view takes an HWND and nothing else",
          VT(view)->isPlatformTypeSupported(view, "HWND") == 0
              && VT(view)->isPlatformTypeSupported(view, "NSView") != 0);
    VT(view)->getSize(view, &rect);
    snprintf(label, sizeof label, "view is %dx%d", rect.right - rect.left, rect.bottom - rect.top);
    check(label, rect.right - rect.left == PANEL_W && rect.bottom - rect.top == PANEL_H);
    parent = CreateWindowExA(0, "STATIC", "host", WS_OVERLAPPEDWINDOW, 0, 0, PANEL_W + 40, PANEL_H + 60,
                             NULL, NULL, NULL, NULL);
    check("attaches to the host's window", VT(view)->attached(view, parent, "HWND") == 0);
    child = GetWindow(parent, GW_CHILD);
    GetClientRect(child, &rc);
    check("puts a panel-sized child window in it", child && rc.right == PANEL_W && rc.bottom == PANEL_H);

    /* The editor shows the controller's values, as host automation sets them. */
    VT(in.ctl)->setParamNormalized(in.ctl, PORT_BIAS, 0.46);
    VT(in.ctl)->setParamNormalized(in.ctl, PORT_OUTPUT, (24.0 - 6.0) / 36.0);
    VT(in.ctl)->setParamNormalized(in.ctl, PORT_ENGAGE, 1.0);
    check("host updates write nothing back", writes == 0);
    pump();

    ui_script(child);

    snprintf(label, sizeof label, "every edit is inside a gesture (%d begin, %d end)", begins, ends);
    check(label, edits_outside_gesture == 0 && begins == ends && begins > 0);
    check("the controller keeps the values the editor sent (Output -5.1 dB)",
          fabs(VT(in.ctl)->getParamNormalized(in.ctl, PORT_OUTPUT) - (24.0 - 5.1) / 36.0) < 1e-4);
    Sleep(100); /* let the window's timer pick up the sweep lamp */
    pump();
    if (argc > 2) {
        check("screenshot written", save_window(child, argv[2]));
        printf("         %s\n", argv[2]);
    }
    VT(view)->removed(view);
    check("removing the view destroys its window", !IsWindow(child));
    VT(view)->release(view);
    DestroyWindow(parent);

    destroy(&in);
    FreeLibrary(lib);
    printf(failures ? "\n%d FAILED\n" : "\nAll passed\n", failures);
    return failures ? 1 : 0;
}
