/* Checks for the Tangerine Orbit model. Usage: test_dsp <bundle folder>
 * Part 1 drives the circuit code directly and compares it to the schematic's transfer
 * function, worked out by hand below and independent of the solver. Part 2 loads the built
 * plugin library the way a host does. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../src/lfo.h"
#include "../src/lv2_abi.h"
#include "../src/phaser.h"
#include "../src/ports.h"

#ifdef _WIN32
#    include <windows.h>
#    define LIB_NAME "tangerine_orbit.dll"
#    define LIB_OPEN(p) ((void*)LoadLibraryA(p))
#    define LIB_SYM(l, s) ((void*)GetProcAddress((HMODULE)(l), s))
#else
#    include <dlfcn.h>
#    ifdef __APPLE__
#        define LIB_NAME "tangerine_orbit.dylib"
#    else
#        define LIB_NAME "tangerine_orbit.so"
#    endif
#    define LIB_OPEN(p) dlopen(p, RTLD_NOW)
#    define LIB_SYM(l, s) dlsym(l, s)
#endif

#define PI 3.14159265358979323846

static int failures;

static void check(const char* what, int ok)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        ++failures;
}

typedef struct { double re, im; } C;
static C cx(double r, double i) { C c; c.re = r; c.im = i; return c; }
static C cadd(C a, C b) { return cx(a.re + b.re, a.im + b.im); }
static C csub(C a, C b) { return cx(a.re - b.re, a.im - b.im); }
static C cmul(C a, C b) { return cx(a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re); }
static C cdiv(C a, C b)
{
    double d = b.re * b.re + b.im * b.im;
    return cx((a.re * b.re + a.im * b.im) / d, (a.im * b.re - a.re * b.im) / d);
}
static double cabs2(C a) { return sqrt(a.re * a.re + a.im * a.im); }

/* High-pass s*T / (1 + s*T). */
static C highpass(double w, double t)
{
    C st = cx(0.0, w * t);
    return cdiv(st, cadd(cx(1.0, 0.0), st));
}

/* The schematic's response, without the mix amp's constant gain. */
static C expected(double f, double fs, double vgst)
{
    const double w = 2.0 * fs * tan(PI * f / fs); /* the trapezoidal rule's frequency warp */
    const double fb = AP_RI / AP_RFB;
    const double gd = 2.0 * JFET_BETA * softplus(vgst, JFET_SOFT);
    const double rd = 1.0 / (1.0 / AP_R + gd);
    C h0 = highpass(w, rd * AP_C);
    C ap = csub(cmul(cx(2.0, 0.0), h0), cx(1.0, 0.0));
    C ap3 = cmul(ap, cmul(ap, ap));
    C k = csub(cmul(cx(2.0 + fb, 0.0), h0), cx(1.0, 0.0));
    C out1 = cdiv(k, cadd(cx(1.0, 0.0), cmul(cx(fb, 0.0), ap3)));
    C wet = cmul(ap3, out1);
    C in = cmul(cx(IN_R_BIAS / (IN_R_SERIES + IN_R_BIAS), 0.0),
                highpass(w, (IN_R_SERIES + IN_R_BIAS) * IN_C));
    return cmul(cmul(in, cadd(cx(1.0, 0.0), wet)), highpass(w, MIX_RO * MIX_CO));
}

/* Measures the circuit at one frequency with a small sine and a held gate voltage. */
static C measure(Phaser* p, double f, double fs, double vgst, double amp)
{
    const long settle = (long)(0.3 * fs);
    const long cycles = (long)ceil(0.1 * f) + 4;
    const long n = (long)floor(cycles * fs / f);
    const double used_cycles = floor(n * f / fs);
    const long m = (long)floor(used_cycles * fs / f);
    double re = 0.0, im = 0.0;
    long i;
    phaser_reset(p);
    for (i = 0; i < settle + m; ++i) {
        double ph = 2.0 * PI * f * (double)i / fs;
        double y = phaser_tick(p, amp * sin(ph), vgst);
        if (i >= settle) {
            re += y * sin(ph);
            im += y * cos(ph);
        }
    }
    return cx(2.0 * re / (double)m / amp, 2.0 * im / (double)m / amp);
}

static void test_transfer_function(double fs)
{
    static const double vgst[] = { 0.02, 0.1, 0.25, 0.45, 1.0 };
    Phaser* p = (Phaser*)malloc(sizeof(Phaser));
    C gain = cx(0.0, 0.0);
    double worst = 0.0, worst_f = 0.0, worst_v = 0.0, loud = 0.0;
    int count = 0, v, k;
    char label[160];
    phaser_init(p, fs);

    /* The formula is small-signal, so the probe has to be small next to the channel overdrive. */
    for (k = 0; k < 2; ++k) {
        for (v = 0; v < 5; ++v) {
            double f;
            for (f = 40.0; f < 16000.0 && f < 0.4 * fs; f *= 1.5) {
                C got = measure(p, f, fs, vgst[v], 1e-4);
                C want = expected(f, fs, vgst[v]);
                if (k == 0) {
                    gain = cadd(gain, cdiv(got, want));
                    ++count;
                } else {
                    /* The small constant keeps deep notches from dominating the figure. */
                    C scaled = cmul(want, gain);
                    double e = cabs2(csub(got, scaled)) / (cabs2(scaled) + 0.02);
                    if (e > worst) {
                        worst = e;
                        worst_f = f;
                        worst_v = vgst[v];
                    }
                    /* The same point with a 10x larger probe, to show the error is the JFET's. */
                    if (f == 40.0 * pow(1.5, 5.0)) {
                        C big = measure(p, f, fs, vgst[v], 1e-3);
                        double eb = cabs2(csub(big, scaled)) / (cabs2(scaled) + 0.02);
                        if (eb > loud)
                            loud = eb;
                    }
                }
            }
        }
        if (k == 0)
            gain = cx(gain.re / count, gain.im / count);
    }

    snprintf(label, sizeof label, "%.0f Hz: matches the schematic's response (worst error %.3f%% at %.0f Hz, %.2f V)",
             fs, worst * 100.0, worst_f, worst_v);
    check(label, worst < 0.005);
    printf("         at 1 mV instead of 0.1 mV the error near 300 Hz is %.3f%%\n", loud * 100.0);
    snprintf(label, sizeof label, "%.0f Hz: mix amp inverts with gain near 1 (%.3f%+.4fj)",
             fs, gain.re, gain.im);
    check(label, gain.re < -0.8 && gain.re > -1.05 && fabs(gain.im) < 0.01);
    free(p);
}

static void test_lfo_rates(void)
{
    static const double rates[] = { 0.2, 0.5, 1.0, 2.0, 5.0, 10.0 };
    static const double sample_rates[] = { 44100.0, 192000.0 };
    int r, s;
    for (s = 0; s < 2; ++s) {
        const double fs = sample_rates[s];
        for (r = 0; r < 6; ++r) {
            Lfo l;
            char label[120];
            double prev, first = -1.0, last = -1.0, hz, seconds = 12.0 / rates[r] + 2.0;
            long i, n = (long)(seconds * fs);
            int edges = 0;
            lfo_init(&l, fs, rates[r], 0.46);
            prev = l.vo;
            for (i = 0; i < n; ++i) {
                lfo_tick(&l);
                if (prev < VREF && l.vo >= VREF) {
                    if (first < 0.0)
                        first = (double)i;
                    last = (double)i;
                    ++edges;
                }
                prev = l.vo;
            }
            hz = edges > 1 ? (edges - 1) * fs / (last - first) : 0.0;
            snprintf(label, sizeof label, "%.0f Hz sample rate: Speed %.1f Hz measures %.3f Hz",
                     fs, rates[r], hz);
            check(label, fabs(hz - rates[r]) < 0.03 * rates[r]);
        }
    }
}

static void report_sweep(void)
{
    const double fs = 48000.0;
    Lfo l;
    Phaser* p = (Phaser*)malloc(sizeof(Phaser));
    double lo = 1e9, hi = -1e9, ends[2];
    long i;
    int e;
    phaser_init(p, fs);
    lfo_init(&l, fs, 0.5, 0.46);
    for (i = 0; i < (long)(20.0 * fs); ++i) {
        lfo_tick(&l);
        if (i > (long)(8.0 * fs)) {
            double v = lfo_vgst(&l);
            if (v < lo) lo = v;
            if (v > hi) hi = v;
        }
    }
    printf("  gate overdrive at default bias, 0.5 Hz: %.3f to %.3f V\n", lo, hi);
    check("sweep keeps the JFETs conducting without leaving the useful range", lo > -0.05 && hi < 1.0);
    ends[0] = lo;
    ends[1] = hi;
    for (e = 0; e < 2; ++e) {
        double f, prev2 = 1e9, prev1 = 1e9, fprev = 0.0;
        double peak = 0.0, deepest = 1e9;
        printf("  notches at overdrive %.3f V:", ends[e]);
        for (f = 40.0; f < 12000.0; f *= 1.03) {
            double m = cabs2(measure(p, f, fs, ends[e], 1e-3));
            if (m > peak) peak = m;
            if (m < deepest) deepest = m;
            if (prev1 < prev2 && prev1 < m && fprev > 40.0)
                printf(" %.0f Hz", fprev);
            prev2 = prev1;
            prev1 = m;
            fprev = f;
        }
        printf("  (peak %+.1f dB, deepest %+.1f dB)\n", 20.0 * log10(peak), 20.0 * log10(deepest));
    }
    free(p);
}

/* ---- Part 2: the built library ---- */

typedef struct {
    const LV2_Descriptor* d;
    LV2_Handle h;
    float speed, bias, volts, output, enabled, engage, sweep;
} Inst;

static void inst_open(Inst* x, const LV2_Descriptor* d, double fs)
{
    x->d = d;
    x->h = d->instantiate(d, fs, "", NULL);
    x->speed = 0.5f; x->bias = 0.46f; x->volts = 1.0f; x->output = -6.0f; x->enabled = 1.0f;
    x->engage = 1.0f; x->sweep = -1.0f;
    d->connect_port(x->h, PORT_SPEED, &x->speed);
    d->connect_port(x->h, PORT_BIAS, &x->bias);
    d->connect_port(x->h, PORT_VOLTS, &x->volts);
    d->connect_port(x->h, PORT_OUTPUT, &x->output);
    d->connect_port(x->h, PORT_ENABLED, &x->enabled);
    d->connect_port(x->h, PORT_ENGAGE, &x->engage);
    d->connect_port(x->h, PORT_SWEEP, &x->sweep);
    d->activate(x->h);
}

static void inst_render(Inst* x, const float* in, float* out, long n, int block)
{
    long pos = 0;
    while (pos < n) {
        int m = n - pos < block ? (int)(n - pos) : block;
        x->d->connect_port(x->h, 0, (void*)(in + pos));
        x->d->connect_port(x->h, 1, out + pos);
        x->d->run(x->h, (uint32_t)m);
        pos += m;
    }
}

static void inst_close(Inst* x)
{
    x->d->deactivate(x->h);
    x->d->cleanup(x->h);
}

static void fill_sine(float* b, long n, double f, double fs, double amp)
{
    long i;
    for (i = 0; i < n; ++i)
        b[i] = (float)(amp * sin(2.0 * PI * f * (double)i / fs));
}

static int all_finite(const float* b, long n, double limit, double* peak)
{
    long i;
    double p = 0.0;
    int ok = 1;
    for (i = 0; i < n; ++i) {
        if (!isfinite(b[i]))
            ok = 0;
        else if (fabs(b[i]) > p)
            p = fabs(b[i]);
    }
    *peak = p;
    return ok && p <= limit;
}

static void test_plugin(const char* bundle)
{
    char path[1024], label[160];
    const double fs = 48000.0;
    const long n = (long)(10.0 * fs);
    float* in = (float*)malloc(sizeof(float) * n);
    float* out = (float*)malloc(sizeof(float) * n);
    float* out2 = (float*)malloc(sizeof(float) * n);
    const LV2_Descriptor* (*get)(uint32_t);
    const LV2_Descriptor* d;
    void* lib;
    Inst x;
    long i;
    double peak;
    static const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 };
    int r;

    snprintf(path, sizeof path, "%s/%s", bundle, LIB_NAME);
    lib = LIB_OPEN(path);
    check("library loads", lib != NULL);
    if (!lib)
        return;
    *(void**)&get = LIB_SYM(lib, "lv2_descriptor");
    check("exports lv2_descriptor", get != NULL);
    if (!get)
        return;
    d = get(0);
    check("descriptor has the plugin's URI", d && strcmp(d->URI, "urn:davidian:tangerine-orbit") == 0);
    check("only one plugin in the library", get(1) == NULL);

    fill_sine(in, n, 1000.0, fs, 0.1);

    /* The sweep has to move the level of a steady tone. */
    inst_open(&x, d, fs);
    inst_render(&x, in, out, n, 256);
    {
        double lo = 1e9, hi = 0.0;
        long w = (long)(0.02 * fs), p;
        for (p = (long)fs; p + w < n; p += w) {
            double e = 0.0;
            for (i = 0; i < w; ++i)
                e += out[p + i] * out[p + i];
            e = sqrt(e / w);
            if (e < lo) lo = e;
            if (e > hi) hi = e;
        }
        snprintf(label, sizeof label, "sweep moves a 1 kHz tone by %.1f dB", 20.0 * log10(hi / lo));
        check(label, hi / lo > 2.0);
    }
    inst_close(&x);

    /* Block size must not change the audio. */
    inst_open(&x, d, fs);
    inst_render(&x, in, out, n, 1);
    inst_close(&x);
    inst_open(&x, d, fs);
    inst_render(&x, in, out2, n, 512);
    inst_close(&x);
    check("output is the same at block sizes 1 and 512", memcmp(out, out2, sizeof(float) * n) == 0);

    /* Hosts may pass the same buffer for input and output. */
    memcpy(out2, in, sizeof(float) * n);
    inst_open(&x, d, fs);
    inst_render(&x, out2, out2, n, 256);
    inst_close(&x);
    check("works with the same buffer for in and out", memcmp(out, out2, sizeof(float) * n) == 0);

    /* Bypass passes audio untouched. */
    inst_open(&x, d, fs);
    x.enabled = 0.0f;
    d->activate(x.h);
    inst_render(&x, in, out, n, 256);
    inst_close(&x);
    check("bypass output is identical to input", memcmp(in, out, sizeof(float) * n) == 0);

    /* The footswitch port bypasses the same way. */
    inst_open(&x, d, fs);
    x.engage = 0.0f;
    d->activate(x.h);
    inst_render(&x, in, out, n, 256);
    inst_close(&x);
    check("footswitch off passes the input untouched", memcmp(in, out, sizeof(float) * n) == 0);

    /* The sweep output follows the oscillator through its whole range. */
    inst_open(&x, d, fs);
    x.speed = 2.0f;
    {
        float lo = 2.0f, hi = -2.0f;
        long pos;
        for (pos = 0; pos + 512 <= (long)(2.0 * fs); pos += 512) {
            inst_render(&x, in + pos, out + pos, 512, 512);
            if (x.sweep < lo) lo = x.sweep;
            if (x.sweep > hi) hi = x.sweep;
        }
        snprintf(label, sizeof label, "sweep output runs %.2f to %.2f", lo, hi);
        check(label, lo >= 0.0f && lo < 0.1f && hi <= 1.0f && hi > 0.9f);
    }
    inst_close(&x);

    /* Switching in has to fade, not jump. */
    inst_open(&x, d, fs);
    x.enabled = 0.0f;
    d->activate(x.h);
    {
        float* silence = (float*)calloc(n, sizeof(float));
        inst_render(&x, silence, out, (long)fs, 256);
        x.enabled = 1.0f;
        inst_render(&x, silence, out, (long)fs, 256);
        all_finite(out, (long)fs, 1e9, &peak);
        snprintf(label, sizeof label, "switching in on silence is quiet (peak %.6f)", peak);
        check(label, peak < 0.001);
        inst_render(&x, silence, out, n, 256);
        all_finite(out, n, 1e9, &peak);
        snprintf(label, sizeof label, "no output on silence (peak %.6f)", peak);
        check(label, peak < 0.001);
        free(silence);
    }
    inst_close(&x);

    /* Too-hot input stays bounded. Rails limit the circuit, so the peak is known. */
    inst_open(&x, d, fs);
    x.output = 0.0f;
    x.volts = 10.0f;
    d->activate(x.h);
    fill_sine(in, n, 200.0, fs, 4.0);
    inst_render(&x, in, out, n, 64);
    inst_close(&x);
    snprintf(label, sizeof label, "very hot input stays finite and bounded");
    check(label, all_finite(out, n, 4.0, &peak));
    printf("         peak %.3f\n", peak);

    /* Bad input must not leave the plugin stuck. */
    fill_sine(in, n, 1000.0, fs, 0.1);
    in[1000] = (float)NAN;
    in[2000] = (float)INFINITY;
    inst_open(&x, d, fs);
    inst_render(&x, in, out, n, 256);
    inst_close(&x);
    check("recovers from NaN and infinity in the input", all_finite(out, n, 4.0, &peak) && peak > 0.01);

    /* Every sample rate, including all controls at their extremes. */
    for (r = 0; r < 5; ++r) {
        const double rate = rates[r];
        const long m = (long)(2.0 * rate);
        float* a = (float*)malloc(sizeof(float) * m);
        float* b = (float*)malloc(sizeof(float) * m);
        int ok = 1, s, t;
        fill_sine(a, m, 440.0, rate, 0.5);
        for (s = 0; s < 2; ++s) {
            for (t = 0; t < 2; ++t) {
                Inst y;
                inst_open(&y, d, rate);
                y.speed = s ? 10.0f : 0.2f;
                y.bias = t ? 1.0f : 0.0f;
                inst_render(&y, a, b, m, 128);
                inst_close(&y);
                ok = ok && all_finite(b, m, 4.0, &peak);
            }
        }
        free(a);
        free(b);
        snprintf(label, sizeof label, "%.0f Hz: stable at control extremes", rate);
        check(label, ok);
    }

    /* CPU, played as a guitar-level tone. */
    fill_sine(in, n, 220.0, fs, 0.25);
    inst_open(&x, d, fs);
    {
        clock_t t0 = clock();
        double seconds;
        inst_render(&x, in, out, n, 256);
        seconds = (double)(clock() - t0) / CLOCKS_PER_SEC;
        printf("  CPU: %.2f%% of one core at 48 kHz\n", 100.0 * seconds / 10.0);
        check("runs far faster than real time", seconds < 1.0);
    }
    inst_close(&x);

    free(in);
    free(out);
    free(out2);
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        printf("usage: test_dsp <bundle folder>\n");
        return 2;
    }
    printf("Circuit against the schematic's transfer function\n");
    test_transfer_function(44100.0);
    test_transfer_function(48000.0);
    test_transfer_function(96000.0);
    printf("Sweep rate\n");
    test_lfo_rates();
    printf("Sweep range\n");
    report_sweep();
    printf("Built plugin\n");
    test_plugin(argv[1]);
    printf(failures ? "\n%d FAILED\n" : "\nAll passed\n", failures);
    return failures ? 1 : 0;
}
