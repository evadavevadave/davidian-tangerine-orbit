/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* Davidian Tangerine Orbit as a macOS Audio Unit (version 2, type aufx), written in C
 * against Apple's AudioToolbox API, with no AU SDK classes. The parameters are the LV2
 * control ports, with the same indices as their AU parameter IDs (ports.h). The GUI is the
 * same panel as the LV2 and VST3 versions, in a Cocoa view the host gets through
 * kAudioUnitProperty_CocoaUI.
 *
 * The circuit is mono. It runs 1 in / 1 out, 1 in / 2 out or 2 in / 2 out; stereo input is
 * averaged and stereo output carries the same signal on both sides. The sweep position for
 * the panel's lamp is a read-only custom property, not a parameter.
 * Build as Objective-C without ARC (see the Makefile's au target). */
#import <Cocoa/Cocoa.h> /* first: AUCocoaUIView.h and panel.h both need it before them */
#import <AudioToolbox/AudioToolbox.h>
#import <AudioUnit/AUCocoaUIView.h>
#import <CoreAudio/CoreAudio.h>

#define PANEL_VIEW_CLASS DavidianTangerineOrbitAUPanelView
#include "../ui/window.h" /* imports Cocoa, then the panel */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../engine.h"
#include "../ports.h"

#define BUNDLE_ID   "com.davidian.tangerine-orbit.au" /* must match bundle/au/Info.plist */
#define VIEW_FACTORY_CLASS "DavidianTangerineOrbitAUViewFactory"
#define AU_TYPE     'aufx'
#define AU_SUBTYPE  'Torb'
#define AU_MANU     'Dvdn'

#define FIRST_PARAM PORT_SPEED
#define LAST_PARAM  PORT_ENGAGE
#define N_PARAMS    (LAST_PARAM - FIRST_PARAM + 1)
#define MAX_LISTENERS 64
#define MAX_NOTIFIES  16

/* Custom property (IDs from 64000 are free for plugins): the sweep position, 0 to 1, as a
 * Float32. Global scope, read-only. The panel's lamp polls it. */
#define kOrbitProperty_SweepPosition 64000

#define EXPORT __attribute__((visibility("default")))

/* ---- Parameters: AU parameter ID = LV2 port index, values as the engine uses them ---- */

typedef struct {
    const char* name;
    float min, max, def;
    AudioUnitParameterUnit unit;
    const char* opt[2]; /* names for indexed parameters */
} Param;

static const Param PARAMS[N_PARAMS] = {
    { "Speed", 0.2f, 10.0f, DEF_SPEED, kAudioUnitParameterUnit_Hertz, { 0, 0 } },
    { "Bias Trim", 0, 1, DEF_BIAS, kAudioUnitParameterUnit_Generic, { 0, 0 } },
    { "Volts at 0 dBFS", 0.1f, 10.0f, DEF_VOLTS, kAudioUnitParameterUnit_CustomUnit, { 0, 0 } },
    { "Output", -24.0f, 12.0f, DEF_OUTPUT, kAudioUnitParameterUnit_Decibels, { 0, 0 } },
    /* The host's bypass (kAudioUnitProperty_BypassEffect) turns this off. */
    { "On", 0, 1, 1, kAudioUnitParameterUnit_Boolean, { 0, 0 } },
    /* The panel's footswitch. */
    { "Effect On", 0, 1, 1, kAudioUnitParameterUnit_Boolean, { 0, 0 } },
};

static const Param* param(AudioUnitParameterID id)
{
    return id >= FIRST_PARAM && id <= LAST_PARAM ? &PARAMS[id - FIRST_PARAM] : NULL;
}

static float clamp_param(const Param* p, float v)
{
    if (!isfinite(v))
        return p->def;
    if (p->unit == kAudioUnitParameterUnit_Indexed || p->unit == kAudioUnitParameterUnit_Boolean)
        v = v >= 0.5f ? 1.0f : 0.0f;
    return v < p->min ? p->min : (v > p->max ? p->max : v);
}

/* ---- The instance ---- */

typedef struct {
    AudioUnitPropertyID id;
    AudioUnitPropertyListenerProc proc;
    void* data;
} Listener;

typedef struct {
    AURenderCallback proc;
    void* data;
} Notify;

typedef struct {
    AudioComponentPlugInInterface iface; /* first: the host's pointer is the instance's */
    AudioComponentInstance self;

    Plugin* engine;
    double rate;
    int initialized;
    UInt32 max_frames, in_ch, out_ch;
    float value[PORT_COUNT];
    float* in_buf[2];   /* the host's input lands here, max_frames each */
    float* out_buf[2];  /* used when the host gives no output buffers */
    float* mono_in;
    float* mono_out;

    AURenderCallbackStruct input_cb;
    AudioUnitConnection connection;
    int has_connection;
    OSStatus last_error;
    UInt32 should_allocate, offline;

    Listener listeners[MAX_LISTENERS];
    int n_listeners;
    Notify notifies[MAX_NOTIFIES];
    int n_notifies;

    SInt32 preset_number;
    CFStringRef preset_name;
} OrbitUnit;

static void notify_property(OrbitUnit* au, AudioUnitPropertyID id, AudioUnitScope scope, AudioUnitElement el)
{
    int i;
    for (i = 0; i < au->n_listeners; ++i)
        if (au->listeners[i].id == id)
            au->listeners[i].proc(au->listeners[i].data, au->self, id, scope, el);
}

static void notify_parameter(OrbitUnit* au, AudioUnitParameterID id)
{
    AudioUnitParameter p;
    p.mAudioUnit = au->self;
    p.mParameterID = id;
    p.mScope = kAudioUnitScope_Global;
    p.mElement = 0;
    AUParameterListenerNotify(NULL, NULL, &p);
}

static int latency_samples(const OrbitUnit* au)
{
    return au->engine ? engine_latency(au->engine) : 0;
}

static void free_buffers(OrbitUnit* au)
{
    int c;
    for (c = 0; c < 2; ++c) {
        free(au->in_buf[c]);
        free(au->out_buf[c]);
        au->in_buf[c] = au->out_buf[c] = NULL;
    }
    free(au->mono_in);
    free(au->mono_out);
    au->mono_in = au->mono_out = NULL;
}

static int alloc_buffers(OrbitUnit* au)
{
    const size_t n = au->max_frames ? au->max_frames : 1;
    int c;
    free_buffers(au);
    for (c = 0; c < 2; ++c) {
        au->in_buf[c] = (float*)calloc(n, sizeof(float));
        au->out_buf[c] = (float*)calloc(n, sizeof(float));
    }
    au->mono_in = (float*)calloc(n, sizeof(float));
    au->mono_out = (float*)calloc(n, sizeof(float));
    return au->in_buf[0] && au->in_buf[1] && au->out_buf[0] && au->out_buf[1] && au->mono_in && au->mono_out;
}

static void wire_engine(OrbitUnit* au)
{
    int i;
    for (i = 0; i < PORT_COUNT; ++i)
        engine_connect(au->engine, (uint32_t)i, NULL);
    for (i = FIRST_PARAM; i <= LAST_PARAM; ++i)
        engine_connect(au->engine, (uint32_t)i, &au->value[i]);
    engine_connect(au->engine, PORT_SWEEP, &au->value[PORT_SWEEP]);
    engine_connect(au->engine, PORT_IN, au->mono_in);
    engine_connect(au->engine, PORT_OUT, au->mono_out);
}

/* ---- Saved state: the same "TOR1" blob as the VST3 version ---- */

static CFDataRef state_data(const OrbitUnit* au)
{
    unsigned char b[8 + 8 * N_PARAMS];
    int i;
    memcpy(b, "TOR1", 4);
    b[4] = N_PARAMS; b[5] = b[6] = b[7] = 0;
    for (i = 0; i < N_PARAMS; ++i) {
        const UInt32 id = (UInt32)(FIRST_PARAM + i);
        UInt32 bits;
        int k;
        memcpy(&bits, &au->value[id], 4);
        for (k = 0; k < 4; ++k) {
            b[8 + 8 * i + k] = (unsigned char)(id >> (8 * k));
            b[12 + 8 * i + k] = (unsigned char)(bits >> (8 * k));
        }
    }
    return CFDataCreate(NULL, b, (CFIndex)sizeof b);
}

static UInt32 le32(const UInt8* b) { return (UInt32)b[0] | (UInt32)b[1] << 8 | (UInt32)b[2] << 16 | (UInt32)b[3] << 24; }

static int load_state_data(OrbitUnit* au, CFDataRef data)
{
    const UInt8* b;
    CFIndex len;
    UInt32 count, i;
    if (!data || CFGetTypeID(data) != CFDataGetTypeID())
        return 0;
    b = CFDataGetBytePtr(data);
    len = CFDataGetLength(data);
    if (len < 8 || memcmp(b, "TOR1", 4) != 0)
        return 0;
    count = le32(b + 4);
    for (i = 0; i < count && 8 + 8 * (CFIndex)i + 8 <= len; ++i) {
        const UInt32 id = le32(b + 8 + 8 * i), bits = le32(b + 12 + 8 * i);
        float f;
        memcpy(&f, &bits, 4);
        if (param(id))
            au->value[id] = clamp_param(param(id), f);
    }
    return 1;
}

static void dict_set_int(CFMutableDictionaryRef d, const char* key, SInt32 v)
{
    CFStringRef k = CFStringCreateWithCString(NULL, key, kCFStringEncodingUTF8);
    CFNumberRef n = CFNumberCreate(NULL, kCFNumberSInt32Type, &v);
    CFDictionarySetValue(d, k, n);
    CFRelease(n);
    CFRelease(k);
}

static int dict_get_int(CFDictionaryRef d, const char* key, SInt32* v)
{
    CFStringRef k = CFStringCreateWithCString(NULL, key, kCFStringEncodingUTF8);
    const void* n = CFDictionaryGetValue(d, k);
    CFRelease(k);
    return n && CFGetTypeID(n) == CFNumberGetTypeID() && CFNumberGetValue((CFNumberRef)n, kCFNumberSInt32Type, v);
}

static CFDictionaryRef class_info(const OrbitUnit* au)
{
    CFMutableDictionaryRef d = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks,
                                                         &kCFTypeDictionaryValueCallBacks);
    CFDataRef data = state_data(au);
    dict_set_int(d, kAUPresetVersionKey, 0);
    dict_set_int(d, kAUPresetTypeKey, (SInt32)AU_TYPE);
    dict_set_int(d, kAUPresetSubtypeKey, (SInt32)AU_SUBTYPE);
    dict_set_int(d, kAUPresetManufacturerKey, (SInt32)AU_MANU);
    CFDictionarySetValue(d, CFSTR(kAUPresetDataKey), data);
    CFDictionarySetValue(d, CFSTR(kAUPresetNameKey), au->preset_name ? au->preset_name : CFSTR("Untitled"));
    CFRelease(data);
    return d;
}

static OSStatus set_class_info(OrbitUnit* au, CFDictionaryRef d)
{
    SInt32 version, type, subtype, manu;
    const void* name;
    int i;
    if (!d || CFGetTypeID(d) != CFDictionaryGetTypeID())
        return kAudioUnitErr_InvalidPropertyValue;
    if (!dict_get_int(d, kAUPresetVersionKey, &version) || version != 0
        || !dict_get_int(d, kAUPresetTypeKey, &type) || (OSType)type != AU_TYPE
        || !dict_get_int(d, kAUPresetSubtypeKey, &subtype) || (OSType)subtype != AU_SUBTYPE
        || !dict_get_int(d, kAUPresetManufacturerKey, &manu) || (OSType)manu != AU_MANU)
        return kAudioUnitErr_InvalidPropertyValue;
    if (!load_state_data(au, (CFDataRef)CFDictionaryGetValue(d, CFSTR(kAUPresetDataKey))))
        return kAudioUnitErr_InvalidPropertyValue;
    name = CFDictionaryGetValue(d, CFSTR(kAUPresetNameKey));
    if (name && CFGetTypeID(name) == CFStringGetTypeID()) {
        if (au->preset_name)
            CFRelease(au->preset_name);
        au->preset_name = CFStringCreateCopy(NULL, (CFStringRef)name);
        au->preset_number = -1;
        notify_property(au, kAudioUnitProperty_PresentPreset, kAudioUnitScope_Global, 0);
    }
    for (i = FIRST_PARAM; i <= LAST_PARAM; ++i)
        notify_parameter(au, (AudioUnitParameterID)i);
    return noErr;
}

/* ---- Stream formats ---- */

static void fill_format(const OrbitUnit* au, UInt32 channels, AudioStreamBasicDescription* f)
{
    memset(f, 0, sizeof *f);
    f->mSampleRate = au->rate;
    f->mFormatID = kAudioFormatLinearPCM;
    f->mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
    f->mBytesPerPacket = 4;
    f->mFramesPerPacket = 1;
    f->mBytesPerFrame = 4;
    f->mChannelsPerFrame = channels;
    f->mBitsPerChannel = 32;
}

static int format_ok(const AudioStreamBasicDescription* f)
{
    return f->mFormatID == kAudioFormatLinearPCM && (f->mFormatFlags & kAudioFormatFlagIsFloat)
        && (f->mFormatFlags & kAudioFormatFlagIsNonInterleaved) && f->mBitsPerChannel == 32
        && f->mChannelsPerFrame >= 1 && f->mChannelsPerFrame <= 2 && f->mSampleRate > 0.0;
}

static int channels_ok(UInt32 in, UInt32 out)
{
    return (in == 1 && out == 1) || (in == 1 && out == 2) || (in == 2 && out == 2);
}

/* ---- Property handling ---- */

static int is_global_only(AudioUnitPropertyID id)
{
    switch (id) {
    case kAudioUnitProperty_ClassInfo:
    case kAudioUnitProperty_ParameterInfo:
    case kAudioUnitProperty_Latency:
    case kAudioUnitProperty_SupportedNumChannels:
    case kAudioUnitProperty_MaximumFramesPerSlice:
    case kAudioUnitProperty_TailTime:
    case kAudioUnitProperty_BypassEffect:
    case kAudioUnitProperty_LastRenderError:
    case kAudioUnitProperty_CocoaUI:
    case kAudioUnitProperty_PresentPreset:
    case kAudioUnitProperty_OfflineRender:
    case kAudioUnitProperty_HostCallbacks:
    case kAudioUnitProperty_ContextName:
    case kOrbitProperty_SweepPosition:
        return 1;
    default:
        return 0;
    }
}

/* Size and writability of a property, or an error if it isn't supported there. */
static OSStatus prop_info(OrbitUnit* au, AudioUnitPropertyID id, AudioUnitScope scope, AudioUnitElement el,
                          UInt32* size, Boolean* writable)
{
    UInt32 s = 0;
    Boolean w = false;
    if (is_global_only(id) && scope != kAudioUnitScope_Global)
        return kAudioUnitErr_InvalidScope;
    switch (id) {
    case kAudioUnitProperty_ClassInfo: s = sizeof(CFPropertyListRef); w = true; break;
    case kAudioUnitProperty_MakeConnection:
        if (scope != kAudioUnitScope_Input)
            return kAudioUnitErr_InvalidScope;
        if (el != 0)
            return kAudioUnitErr_InvalidElement;
        s = sizeof(AudioUnitConnection); w = true; break;
    case kAudioUnitProperty_SetRenderCallback:
        if (scope != kAudioUnitScope_Input)
            return kAudioUnitErr_InvalidScope;
        if (el != 0)
            return kAudioUnitErr_InvalidElement;
        s = sizeof(AURenderCallbackStruct); w = true; break;
    case kAudioUnitProperty_SampleRate:
        if (scope != kAudioUnitScope_Input && scope != kAudioUnitScope_Output)
            return kAudioUnitErr_InvalidScope;
        if (el != 0)
            return kAudioUnitErr_InvalidElement;
        s = sizeof(Float64); w = true; break;
    case kAudioUnitProperty_StreamFormat:
        if (scope != kAudioUnitScope_Input && scope != kAudioUnitScope_Output)
            return kAudioUnitErr_InvalidScope;
        if (el != 0)
            return kAudioUnitErr_InvalidElement;
        s = sizeof(AudioStreamBasicDescription); w = true; break;
    case kAudioUnitProperty_ParameterList:
        s = scope == kAudioUnitScope_Global ? (UInt32)(N_PARAMS * sizeof(AudioUnitParameterID)) : 0; break;
    case kAudioUnitProperty_ParameterInfo:
        if (!param(el))
            return kAudioUnitErr_InvalidParameter;
        s = sizeof(AudioUnitParameterInfo); break;
    case kAudioUnitProperty_ParameterValueStrings:
        if (scope != kAudioUnitScope_Global)
            return kAudioUnitErr_InvalidScope;
        if (!param(el) || !param(el)->opt[0])
            return kAudioUnitErr_InvalidProperty;
        s = sizeof(CFArrayRef); break;
    case kAudioUnitProperty_ElementCount: s = sizeof(UInt32); break;
    case kAudioUnitProperty_Latency:
    case kAudioUnitProperty_TailTime: s = sizeof(Float64); break;
    case kAudioUnitProperty_SupportedNumChannels: s = 3 * sizeof(AUChannelInfo); break;
    case kAudioUnitProperty_MaximumFramesPerSlice: s = sizeof(UInt32); w = true; break;
    case kAudioUnitProperty_BypassEffect: s = sizeof(UInt32); w = true; break;
    case kAudioUnitProperty_LastRenderError: s = sizeof(OSStatus); break;
    case kAudioUnitProperty_ShouldAllocateBuffer:
        if (scope != kAudioUnitScope_Input && scope != kAudioUnitScope_Output)
            return kAudioUnitErr_InvalidScope;
        s = sizeof(UInt32); w = true; break;
    case kAudioUnitProperty_OfflineRender: s = sizeof(UInt32); w = true; break;
    case kAudioUnitProperty_CocoaUI: s = sizeof(AudioUnitCocoaViewInfo); break;
    case kAudioUnitProperty_PresentPreset: s = sizeof(AUPreset); w = true; break;
    case kAudioUnitProperty_HostCallbacks: s = sizeof(HostCallbackInfo); w = true; break;
    case kAudioUnitProperty_ContextName: s = sizeof(CFStringRef); w = true; break;
    case kOrbitProperty_SweepPosition: s = sizeof(Float32); break;
    default:
        return kAudioUnitErr_InvalidProperty;
    }
    (void)au;
    if (size)
        *size = s;
    if (writable)
        *writable = w;
    return noErr;
}

static OSStatus orbit_get_property_info(void* self, AudioUnitPropertyID id, AudioUnitScope scope, AudioUnitElement el,
                                  UInt32* size, Boolean* writable)
{
    return prop_info((OrbitUnit*)self, id, scope, el, size, writable);
}

static OSStatus orbit_get_property(void* self, AudioUnitPropertyID id, AudioUnitScope scope, AudioUnitElement el,
                              void* out, UInt32* io_size)
{
    OrbitUnit* au = (OrbitUnit*)self;
    UInt32 size = 0;
    OSStatus err = prop_info(au, id, scope, el, &size, NULL);
    if (err)
        return err;
    if (!io_size)
        return kAudio_ParamError;
    if (!out) { /* some hosts ask for the size this way */
        *io_size = size;
        return noErr;
    }
    if (*io_size < size)
        return kAudioUnitErr_InvalidPropertyValue;
    switch (id) {
    case kAudioUnitProperty_ClassInfo:
        *(CFPropertyListRef*)out = class_info(au); /* the caller releases it */
        break;
    case kAudioUnitProperty_MakeConnection:
        *(AudioUnitConnection*)out = au->connection;
        break;
    case kAudioUnitProperty_SetRenderCallback:
        *(AURenderCallbackStruct*)out = au->input_cb;
        break;
    case kAudioUnitProperty_SampleRate:
        *(Float64*)out = au->rate;
        break;
    case kAudioUnitProperty_StreamFormat:
        fill_format(au, scope == kAudioUnitScope_Input ? au->in_ch : au->out_ch, (AudioStreamBasicDescription*)out);
        break;
    case kAudioUnitProperty_ParameterList: {
        AudioUnitParameterID* ids = (AudioUnitParameterID*)out;
        int i;
        if (scope == kAudioUnitScope_Global)
            for (i = 0; i < N_PARAMS; ++i)
                ids[i] = (AudioUnitParameterID)(FIRST_PARAM + i);
        break;
    }
    case kAudioUnitProperty_ParameterInfo: {
        const Param* p = param(el);
        AudioUnitParameterInfo* info = (AudioUnitParameterInfo*)out;
        memset(info, 0, sizeof *info);
        strlcpy(info->name, p->name, sizeof info->name);
        info->cfNameString = CFStringCreateWithCString(NULL, p->name, kCFStringEncodingUTF8);
        info->unit = p->unit;
        if (p->unit == kAudioUnitParameterUnit_CustomUnit)
            info->unitName = CFSTR("V");
        info->minValue = p->min;
        info->maxValue = p->max;
        info->defaultValue = p->def;
        info->flags = kAudioUnitParameterFlag_IsReadable | kAudioUnitParameterFlag_IsWritable
                    | kAudioUnitParameterFlag_HasCFNameString | kAudioUnitParameterFlag_CFNameRelease;
        if (el == PORT_SPEED || el == PORT_VOLTS)
            info->flags |= kAudioUnitParameterFlag_DisplayLogarithmic;
        break;
    }
    case kAudioUnitProperty_ParameterValueStrings: {
        const Param* p = param(el);
        CFStringRef names[2];
        names[0] = CFStringCreateWithCString(NULL, p->opt[0], kCFStringEncodingUTF8);
        names[1] = CFStringCreateWithCString(NULL, p->opt[1], kCFStringEncodingUTF8);
        *(CFArrayRef*)out = CFArrayCreate(NULL, (const void**)names, 2, &kCFTypeArrayCallBacks);
        CFRelease(names[0]);
        CFRelease(names[1]);
        break;
    }
    case kAudioUnitProperty_ElementCount:
        *(UInt32*)out = scope == kAudioUnitScope_Global || scope == kAudioUnitScope_Input
                                || scope == kAudioUnitScope_Output ? 1 : 0;
        break;
    case kAudioUnitProperty_Latency:
        *(Float64*)out = latency_samples(au) / au->rate;
        break;
    case kAudioUnitProperty_TailTime:
        *(Float64*)out = 0.05; /* the all-pass stages and coupling caps settle in tens of ms */
        break;
    case kAudioUnitProperty_SupportedNumChannels: {
        AUChannelInfo* c = (AUChannelInfo*)out;
        c[0].inChannels = 1; c[0].outChannels = 1;
        c[1].inChannels = 1; c[1].outChannels = 2;
        c[2].inChannels = 2; c[2].outChannels = 2;
        break;
    }
    case kAudioUnitProperty_MaximumFramesPerSlice:
        *(UInt32*)out = au->max_frames;
        break;
    case kAudioUnitProperty_BypassEffect:
        *(UInt32*)out = au->value[PORT_ENABLED] > 0.5f ? 0 : 1;
        break;
    case kAudioUnitProperty_LastRenderError:
        *(OSStatus*)out = au->last_error;
        au->last_error = noErr;
        break;
    case kAudioUnitProperty_ShouldAllocateBuffer:
        *(UInt32*)out = au->should_allocate;
        break;
    case kAudioUnitProperty_OfflineRender:
        *(UInt32*)out = au->offline;
        break;
    case kAudioUnitProperty_CocoaUI: {
        AudioUnitCocoaViewInfo* info = (AudioUnitCocoaViewInfo*)out;
        CFBundleRef b = CFBundleGetBundleWithIdentifier(CFSTR(BUNDLE_ID));
        if (!b)
            return kAudioUnitErr_InvalidProperty;
        info->mCocoaAUViewBundleLocation = CFBundleCopyBundleURL(b); /* the caller releases both */
        info->mCocoaAUViewClass[0] = CFStringCreateWithCString(NULL, VIEW_FACTORY_CLASS, kCFStringEncodingUTF8);
        break;
    }
    case kAudioUnitProperty_PresentPreset: {
        AUPreset* p = (AUPreset*)out;
        p->presetNumber = au->preset_number;
        p->presetName = au->preset_name ? (CFStringRef)CFRetain(au->preset_name) : CFSTR("Untitled");
        break;
    }
    case kOrbitProperty_SweepPosition:
        *(Float32*)out = au->value[PORT_SWEEP];
        break;
    default:
        return kAudioUnitErr_InvalidProperty; /* write-only, such as the host callbacks */
    }
    *io_size = size;
    return noErr;
}

static OSStatus orbit_set_property(void* self, AudioUnitPropertyID id, AudioUnitScope scope, AudioUnitElement el,
                              const void* in, UInt32 size)
{
    OrbitUnit* au = (OrbitUnit*)self;
    UInt32 need = 0;
    Boolean writable = false;
    OSStatus err = prop_info(au, id, scope, el, &need, &writable);
    if (err)
        return err;
    if (!writable)
        return kAudioUnitErr_PropertyNotWritable;
    if (!in || size < need)
        return kAudioUnitErr_InvalidPropertyValue;
    switch (id) {
    case kAudioUnitProperty_ClassInfo:
        return set_class_info(au, *(const CFDictionaryRef*)in);
    case kAudioUnitProperty_MakeConnection:
        au->connection = *(const AudioUnitConnection*)in;
        au->has_connection = au->connection.sourceAudioUnit != NULL;
        if (au->has_connection)
            memset(&au->input_cb, 0, sizeof au->input_cb);
        break;
    case kAudioUnitProperty_SetRenderCallback:
        au->input_cb = *(const AURenderCallbackStruct*)in;
        if (au->input_cb.inputProc)
            au->has_connection = 0;
        break;
    case kAudioUnitProperty_SampleRate:
    case kAudioUnitProperty_StreamFormat: {
        AudioStreamBasicDescription f;
        if (id == kAudioUnitProperty_SampleRate) {
            const Float64 r = *(const Float64*)in;
            fill_format(au, scope == kAudioUnitScope_Input ? au->in_ch : au->out_ch, &f);
            f.mSampleRate = r;
        } else {
            f = *(const AudioStreamBasicDescription*)in;
        }
        if (!format_ok(&f))
            return kAudioUnitErr_FormatNotSupported;
        if (au->initialized) {
            AudioStreamBasicDescription cur;
            fill_format(au, scope == kAudioUnitScope_Input ? au->in_ch : au->out_ch, &cur);
            if (f.mSampleRate != cur.mSampleRate || f.mChannelsPerFrame != cur.mChannelsPerFrame)
                return kAudioUnitErr_Initialized;
            break;
        }
        if (scope == kAudioUnitScope_Input)
            au->in_ch = f.mChannelsPerFrame;
        else
            au->out_ch = f.mChannelsPerFrame;
        if (f.mSampleRate != au->rate) {
            /* One rate for both sides: tell listeners of the other side and the latency. */
            au->rate = f.mSampleRate;
            notify_property(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0);
            notify_property(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0);
            notify_property(au, kAudioUnitProperty_SampleRate, kAudioUnitScope_Input, 0);
            notify_property(au, kAudioUnitProperty_SampleRate, kAudioUnitScope_Output, 0);
            notify_property(au, kAudioUnitProperty_Latency, kAudioUnitScope_Global, 0);
            return noErr;
        }
        break;
    }
    case kAudioUnitProperty_MaximumFramesPerSlice: {
        const UInt32 n = *(const UInt32*)in;
        if (n == 0 || n > 65536)
            return kAudioUnitErr_InvalidPropertyValue;
        if (n != au->max_frames) {
            au->max_frames = n;
            if (au->initialized && !alloc_buffers(au))
                return kAudio_MemFullError;
            if (au->initialized)
                wire_engine(au);
        }
        break;
    }
    case kAudioUnitProperty_BypassEffect: {
        const float enabled = *(const UInt32*)in ? 0.0f : 1.0f;
        if (au->value[PORT_ENABLED] != enabled) {
            au->value[PORT_ENABLED] = enabled;
            notify_parameter(au, PORT_ENABLED);
        }
        break;
    }
    case kAudioUnitProperty_ShouldAllocateBuffer:
        au->should_allocate = *(const UInt32*)in;
        break;
    case kAudioUnitProperty_OfflineRender:
        au->offline = *(const UInt32*)in;
        break;
    case kAudioUnitProperty_PresentPreset: {
        const AUPreset* p = (const AUPreset*)in;
        if (au->preset_name)
            CFRelease(au->preset_name);
        au->preset_name = p->presetName ? CFStringCreateCopy(NULL, p->presetName) : NULL;
        au->preset_number = p->presetNumber;
        break;
    }
    case kAudioUnitProperty_HostCallbacks:
    case kAudioUnitProperty_ContextName:
        break; /* not needed: the sweep runs free, with no tempo or transport */
    default:
        return kAudioUnitErr_InvalidProperty;
    }
    notify_property(au, id, scope, el);
    return noErr;
}

static OSStatus orbit_add_property_listener(void* self, AudioUnitPropertyID id, AudioUnitPropertyListenerProc proc, void* data)
{
    OrbitUnit* au = (OrbitUnit*)self;
    if (!proc)
        return kAudio_ParamError;
    if (au->n_listeners >= MAX_LISTENERS)
        return kAudio_MemFullError;
    au->listeners[au->n_listeners].id = id;
    au->listeners[au->n_listeners].proc = proc;
    au->listeners[au->n_listeners].data = data;
    ++au->n_listeners;
    return noErr;
}

static void remove_listeners(OrbitUnit* au, AudioUnitPropertyID id, AudioUnitPropertyListenerProc proc, int match_data, void* data)
{
    int i = 0;
    while (i < au->n_listeners) {
        const Listener* l = &au->listeners[i];
        if (l->id == id && l->proc == proc && (!match_data || l->data == data))
            au->listeners[i] = au->listeners[--au->n_listeners];
        else
            ++i;
    }
}

static OSStatus orbit_remove_property_listener(void* self, AudioUnitPropertyID id, AudioUnitPropertyListenerProc proc)
{
    remove_listeners((OrbitUnit*)self, id, proc, 0, NULL);
    return noErr;
}

static OSStatus orbit_remove_property_listener_with_user_data(void* self, AudioUnitPropertyID id,
                                                     AudioUnitPropertyListenerProc proc, void* data)
{
    remove_listeners((OrbitUnit*)self, id, proc, 1, data);
    return noErr;
}

static OSStatus orbit_add_render_notify(void* self, AURenderCallback proc, void* data)
{
    OrbitUnit* au = (OrbitUnit*)self;
    if (!proc)
        return kAudio_ParamError;
    if (au->n_notifies >= MAX_NOTIFIES)
        return kAudio_MemFullError;
    au->notifies[au->n_notifies].proc = proc;
    au->notifies[au->n_notifies].data = data;
    ++au->n_notifies;
    return noErr;
}

static OSStatus orbit_remove_render_notify(void* self, AURenderCallback proc, void* data)
{
    OrbitUnit* au = (OrbitUnit*)self;
    int i = 0;
    while (i < au->n_notifies) {
        if (au->notifies[i].proc == proc && au->notifies[i].data == data)
            au->notifies[i] = au->notifies[--au->n_notifies];
        else
            ++i;
    }
    return noErr;
}

/* ---- Parameters ---- */

static OSStatus orbit_get_parameter(void* self, AudioUnitParameterID id, AudioUnitScope scope, AudioUnitElement el,
                               AudioUnitParameterValue* value)
{
    OrbitUnit* au = (OrbitUnit*)self;
    (void)el;
    if (scope != kAudioUnitScope_Global)
        return kAudioUnitErr_InvalidScope;
    if (!param(id))
        return kAudioUnitErr_InvalidParameter;
    if (!value)
        return kAudio_ParamError;
    *value = au->value[id];
    return noErr;
}

static OSStatus orbit_set_parameter(void* self, AudioUnitParameterID id, AudioUnitScope scope, AudioUnitElement el,
                               AudioUnitParameterValue value, UInt32 offset)
{
    OrbitUnit* au = (OrbitUnit*)self;
    (void)el;
    (void)offset; /* the engine smooths every control, so per-block is enough */
    if (scope != kAudioUnitScope_Global)
        return kAudioUnitErr_InvalidScope;
    if (!param(id))
        return kAudioUnitErr_InvalidParameter;
    au->value[id] = clamp_param(param(id), value);
    return noErr;
}

static OSStatus orbit_schedule_parameters(void* self, const AudioUnitParameterEvent* events, UInt32 n)
{
    UInt32 i;
    for (i = 0; i < n; ++i) {
        const AudioUnitParameterEvent* e = &events[i];
        const AudioUnitParameterValue v = e->eventType == kParameterEvent_Ramped
                                              ? e->eventValues.ramp.endValue
                                              : e->eventValues.immediate.value;
        const OSStatus err = orbit_set_parameter(self, e->parameter, e->scope, e->element, v, 0);
        if (err)
            return err;
    }
    return noErr;
}

/* ---- Life cycle and rendering ---- */

static OSStatus orbit_initialize(void* self)
{
    OrbitUnit* au = (OrbitUnit*)self;
    if (au->initialized)
        return noErr;
    if (!channels_ok(au->in_ch, au->out_ch))
        return kAudioUnitErr_FormatNotSupported;
    if (!au->engine || au->engine->rate != au->rate) {
        if (au->engine)
            engine_free(au->engine);
        au->engine = engine_new(au->rate);
        if (!au->engine)
            return kAudioUnitErr_FailedInitialization;
    }
    if (!alloc_buffers(au))
        return kAudioUnitErr_FailedInitialization;
    wire_engine(au);
    engine_activate(au->engine);
    au->initialized = 1;
    notify_property(au, kAudioUnitProperty_Latency, kAudioUnitScope_Global, 0);
    return noErr;
}

static OSStatus orbit_uninitialize(void* self)
{
    ((OrbitUnit*)self)->initialized = 0;
    return noErr;
}

static OSStatus orbit_reset(void* self, AudioUnitScope scope, AudioUnitElement el)
{
    OrbitUnit* au = (OrbitUnit*)self;
    (void)scope;
    (void)el;
    if (au->initialized && au->engine)
        engine_activate(au->engine);
    return noErr;
}

static void call_notifies(OrbitUnit* au, AudioUnitRenderActionFlags flag, AudioUnitRenderActionFlags* flags,
                          const AudioTimeStamp* ts, UInt32 bus, UInt32 frames, AudioBufferList* io)
{
    int i;
    for (i = 0; i < au->n_notifies; ++i) {
        AudioUnitRenderActionFlags f = (flags ? *flags : 0) | flag;
        au->notifies[i].proc(au->notifies[i].data, &f, ts, bus, frames, io);
    }
}

static OSStatus pull_input(OrbitUnit* au, const AudioTimeStamp* ts, UInt32 frames, AudioBufferList* abl)
{
    AudioUnitRenderActionFlags flags = 0;
    UInt32 c;
    abl->mNumberBuffers = au->in_ch;
    for (c = 0; c < au->in_ch; ++c) {
        abl->mBuffers[c].mNumberChannels = 1;
        abl->mBuffers[c].mDataByteSize = frames * sizeof(float);
        abl->mBuffers[c].mData = au->in_buf[c];
    }
    if (au->input_cb.inputProc)
        return au->input_cb.inputProc(au->input_cb.inputProcRefCon, &flags, ts, 0, frames, abl);
    if (au->has_connection)
        return AudioUnitRender(au->connection.sourceAudioUnit, &flags, ts, au->connection.sourceOutputNumber,
                               frames, abl);
    return kAudioUnitErr_NoConnection;
}

static OSStatus orbit_render(void* self, AudioUnitRenderActionFlags* flags, const AudioTimeStamp* ts,
                         UInt32 bus, UInt32 frames, AudioBufferList* io)
{
    OrbitUnit* au = (OrbitUnit*)self;
    struct {
        AudioBufferList list;
        AudioBuffer extra; /* room for a second channel */
    } in;
    OSStatus err;
    UInt32 c, i;
    if (!au->initialized)
        return kAudioUnitErr_Uninitialized;
    if (bus != 0)
        return kAudioUnitErr_InvalidElement;
    if (frames > au->max_frames)
        return kAudioUnitErr_TooManyFramesToProcess;
    if (!io || io->mNumberBuffers != au->out_ch)
        return kAudio_ParamError;
    call_notifies(au, kAudioUnitRenderAction_PreRender, flags, ts, bus, frames, io);
    err = pull_input(au, ts, frames, &in.list);
    if (err) {
        au->last_error = err;
        return err;
    }
    for (i = 0; i < frames; ++i) {
        float x = 0.0f;
        for (c = 0; c < in.list.mNumberBuffers && c < 2; ++c)
            x += in.list.mBuffers[c].mData ? ((const float*)in.list.mBuffers[c].mData)[i] : 0.0f;
        au->mono_in[i] = in.list.mNumberBuffers > 1 ? x * 0.5f : x;
    }
    engine_run(au->engine, frames);
    for (c = 0; c < io->mNumberBuffers; ++c) {
        if (!io->mBuffers[c].mData)
            io->mBuffers[c].mData = au->out_buf[c];
        io->mBuffers[c].mDataByteSize = frames * sizeof(float);
        memcpy(io->mBuffers[c].mData, au->mono_out, frames * sizeof(float));
    }
    if (flags)
        *flags &= ~kAudioUnitRenderAction_OutputIsSilence;
    call_notifies(au, kAudioUnitRenderAction_PostRender, flags, ts, bus, frames, io);
    return noErr;
}

/* ---- The component plug-in interface ---- */

static OSStatus orbit_open(void* self, AudioComponentInstance instance)
{
    ((OrbitUnit*)self)->self = instance;
    return noErr;
}

static OSStatus orbit_close(void* self)
{
    OrbitUnit* au = (OrbitUnit*)self;
    if (au->engine)
        engine_free(au->engine);
    free_buffers(au);
    if (au->preset_name)
        CFRelease(au->preset_name);
    free(au);
    return noErr;
}

static AudioComponentMethod orbit_lookup(SInt16 selector)
{
    switch (selector) {
    case kAudioUnitInitializeSelect: return (AudioComponentMethod)orbit_initialize;
    case kAudioUnitUninitializeSelect: return (AudioComponentMethod)orbit_uninitialize;
    case kAudioUnitGetPropertyInfoSelect: return (AudioComponentMethod)orbit_get_property_info;
    case kAudioUnitGetPropertySelect: return (AudioComponentMethod)orbit_get_property;
    case kAudioUnitSetPropertySelect: return (AudioComponentMethod)orbit_set_property;
    case kAudioUnitAddPropertyListenerSelect: return (AudioComponentMethod)orbit_add_property_listener;
    case kAudioUnitRemovePropertyListenerSelect: return (AudioComponentMethod)orbit_remove_property_listener;
    case kAudioUnitRemovePropertyListenerWithUserDataSelect:
        return (AudioComponentMethod)orbit_remove_property_listener_with_user_data;
    case kAudioUnitAddRenderNotifySelect: return (AudioComponentMethod)orbit_add_render_notify;
    case kAudioUnitRemoveRenderNotifySelect: return (AudioComponentMethod)orbit_remove_render_notify;
    case kAudioUnitGetParameterSelect: return (AudioComponentMethod)orbit_get_parameter;
    case kAudioUnitSetParameterSelect: return (AudioComponentMethod)orbit_set_parameter;
    case kAudioUnitScheduleParametersSelect: return (AudioComponentMethod)orbit_schedule_parameters;
    case kAudioUnitRenderSelect: return (AudioComponentMethod)orbit_render;
    case kAudioUnitResetSelect: return (AudioComponentMethod)orbit_reset;
    default: return NULL;
    }
}

/* Named in bundle/au/Info.plist. */
EXPORT void* TangerineOrbitAUFactory(const AudioComponentDescription* desc)
{
    OrbitUnit* au = (OrbitUnit*)calloc(1, sizeof(OrbitUnit));
    int i;
    (void)desc;
    if (!au)
        return NULL;
    au->iface.Open = orbit_open;
    au->iface.Close = orbit_close;
    au->iface.Lookup = orbit_lookup;
    au->iface.reserved = NULL;
    au->rate = 44100.0;
    au->max_frames = 1156; /* the usual AU default */
    au->in_ch = au->out_ch = 1;
    au->should_allocate = 1;
    au->preset_number = -1;
    for (i = FIRST_PARAM; i <= LAST_PARAM; ++i)
        au->value[i] = PARAMS[i - FIRST_PARAM].def;
    return au;
}

/* ---- The Cocoa view: the panel in a container the host owns ---- */

typedef struct {
    PanelWindow win;
    AudioUnit au;
    AUEventListenerRef listener;
} ViewState;

static AudioUnitParameter au_param(ViewState* st, int port)
{
    AudioUnitParameter p;
    p.mAudioUnit = st->au;
    p.mParameterID = (AudioUnitParameterID)port;
    p.mScope = kAudioUnitScope_Global;
    p.mElement = 0;
    return p;
}

static void view_write(void* host, int port, float value)
{
    ViewState* st = (ViewState*)host;
    AudioUnitParameter p = au_param(st, port);
    if (param((AudioUnitParameterID)port))
        AUParameterSet(st->listener, st, &p, value, 0); /* sets it and tells the host */
}

static void view_gesture(void* host, int port, int begin)
{
    ViewState* st = (ViewState*)host;
    AudioUnitEvent e;
    if (!param((AudioUnitParameterID)port))
        return;
    memset(&e, 0, sizeof e);
    e.mEventType = begin ? kAudioUnitEvent_BeginParameterChangeGesture : kAudioUnitEvent_EndParameterChangeGesture;
    e.mArgument.mParameter = au_param(st, port);
    AUEventListenerNotify(st->listener, st, &e);
}

/* The view's timer: the sweep lamp follows the unit's custom sweep property. */
static void view_poll(void* host)
{
    ViewState* st = (ViewState*)host;
    Float32 v = 0.0f;
    UInt32 size = sizeof v;
    if (AudioUnitGetProperty(st->au, kOrbitProperty_SweepPosition, kAudioUnitScope_Global, 0, &v, &size) == noErr)
        panel_set(&st->win.panel, PORT_SWEEP, v);
}

/* Parameter changes from the host (automation, presets), on the main run loop. */
static void view_event(void* data, void* object, const AudioUnitEvent* e, UInt64 time, AudioUnitParameterValue v)
{
    ViewState* st = (ViewState*)data;
    (void)object;
    (void)time;
    if (e->mEventType != kAudioUnitEvent_ParameterValueChange)
        return;
    panel_set(&st->win.panel, (int)e->mArgument.mParameter.mParameterID, v);
    pw_redraw(&st->win);
}

@interface DavidianTangerineOrbitAUView : NSView {
    ViewState* st;
}
- (id)initWithAudioUnit:(AudioUnit)au;
@end

@implementation DavidianTangerineOrbitAUView

- (id)initWithAudioUnit:(AudioUnit)au
{
    int i;
    self = [super initWithFrame:NSMakeRect(0, 0, PANEL_W, PANEL_H)];
    if (!self)
        return nil;
    st = (ViewState*)calloc(1, sizeof(ViewState));
    if (!st || !panel_init(&st->win.panel, view_write, st)) {
        free(st);
        st = NULL;
        [self release];
        return nil;
    }
    st->au = au;
    st->win.panel.gesture = view_gesture;
    st->win.panel.poll = view_poll;
    if (!window_open(&st->win, self)) {
        panel_free(&st->win.panel);
        free(st);
        st = NULL;
        [self release];
        return nil;
    }
    if (AUEventListenerCreate(view_event, st, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode, 0.02f, 0.02f,
                              &st->listener) != noErr)
        st->listener = NULL;
    for (i = FIRST_PARAM; i <= LAST_PARAM; ++i) {
        AudioUnitParameterValue v;
        AudioUnitEvent e;
        if (AudioUnitGetParameter(au, (AudioUnitParameterID)i, kAudioUnitScope_Global, 0, &v) == noErr)
            panel_set(&st->win.panel, i, v);
        if (st->listener) {
            memset(&e, 0, sizeof e);
            e.mEventType = kAudioUnitEvent_ParameterValueChange;
            e.mArgument.mParameter = au_param(st, i);
            AUEventListenerAddEventType(st->listener, st, &e);
        }
    }
    pw_redraw(&st->win);
    return self;
}

- (BOOL)isFlipped { return YES; }

- (void)dealloc
{
    if (st) {
        if (st->listener)
            AUListenerDispose(st->listener);
        window_close(&st->win);
        panel_free(&st->win.panel);
        free(st);
        st = NULL;
    }
    [super dealloc];
}

@end

/* The class the host finds through kAudioUnitProperty_CocoaUI. */
@interface DavidianTangerineOrbitAUViewFactory : NSObject <AUCocoaUIBase>
@end

@implementation DavidianTangerineOrbitAUViewFactory

- (unsigned)interfaceVersion { return 0; }

- (NSString*)description { return @"Davidian Tangerine Orbit"; }

- (NSView*)uiViewForAudioUnit:(AudioUnit)au withSize:(NSSize)size
{
    (void)size; /* the panel has one size */
    return [[[DavidianTangerineOrbitAUView alloc] initWithAudioUnit:au] autorelease];
}

@end
