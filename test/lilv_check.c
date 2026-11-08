/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* Checks the built bundle with the real LV2 parser (lilv) that ships with Ardour 9.
 * Windows only. Uses LV2_PATH, so nothing has to be installed.
 * Usage: lilv_check <folder containing tangerine_orbit.lv2> */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define PLUGIN_ID "urn:davidian:tangerine-orbit"
#define ARDOUR_LV2 "C:\\Program Files\\Ardour9\\lib\\ardour9\\LV2"

typedef void W; typedef void N; typedef void Pl; typedef void P; typedef void Po;
typedef struct { const void *d; void *h; void *pimpl; } LI;
typedef struct {
    const char *uri;
    void *(*instantiate)(const void *, double, const char *, const void *const *);
    void (*connect_port)(void *, unsigned, void *);
    void (*activate)(void *);
    void (*run)(void *, unsigned);
    void (*deactivate)(void *);
    void (*cleanup)(void *);
    const void *(*ext)(const char *);
} D;

#define SYM(ret, name, ...) ret (*name)(__VA_ARGS__) = (ret (*)(__VA_ARGS__))GetProcAddress(lib, #name); \
    if (!name) { printf("missing %s\n", #name); return 2; }

int main(int argc, char **argv)
{
    HMODULE lib;
    char path[2048];
    if (argc < 2) { printf("usage: lilv_check <folder containing tangerine_orbit.lv2>\n"); return 2; }
    setvbuf(stdout, NULL, _IONBF, 0); /* unbuffered, so the report survives a crash in the host library */
    {
        /* lilv crashes on a relative LV2_PATH entry, so make it absolute. Ardour's folder is
         * loaded bundle by bundle below, so an installed copy can't stand in for this build. */
        char full[1024];
        if (!GetFullPathNameA(argv[1], sizeof full, full, NULL)) { printf("bad path\n"); return 2; }
        snprintf(path, sizeof path, "%s", full);
    }
    _putenv_s("LV2_PATH", path);
    SetDllDirectoryA("C:\\Program Files\\Ardour9\\bin");
    lib = LoadLibraryA("lilv-0.dll");
    if (!lib) { printf("cannot load lilv: %lu\n", GetLastError()); return 2; }
    {
    SYM(W *, lilv_world_new, void)
    SYM(void, lilv_world_load_all, W *)
    SYM(const Pl *, lilv_world_get_all_plugins, W *)
    SYM(unsigned, lilv_plugins_size, const Pl *)
    SYM(N *, lilv_new_uri, W *, const char *)
    SYM(const P *, lilv_plugins_get_by_uri, const Pl *, const N *)
    SYM(int, lilv_plugin_verify, const P *)
    SYM(N *, lilv_plugin_get_name, const P *)
    SYM(const char *, lilv_node_as_string, const N *)
    SYM(float, lilv_node_as_float, const N *)
    SYM(unsigned, lilv_plugin_get_num_ports, const P *)
    SYM(const Po *, lilv_plugin_get_port_by_index, const P *, unsigned)
    SYM(const N *, lilv_port_get_symbol, const P *, const Po *)
    SYM(N *, lilv_port_get_name, const P *, const Po *)
    SYM(void, lilv_port_get_range, const P *, const Po *, N **, N **, N **)
    SYM(const N *, lilv_plugin_get_library_uri, const P *)
    SYM(const void *, lilv_plugin_get_class, const P *)
    SYM(const N *, lilv_plugin_class_get_label, const void *)
    SYM(LI *, lilv_plugin_instantiate, const P *, double, const void *const *)
    SYM(void *, lilv_plugin_get_uis, const P *)
    SYM(unsigned, lilv_uis_size, const void *)
    SYM(const void *, lilv_uis_get_by_uri, const void *, const N *)
    SYM(const N *, lilv_ui_get_binary_uri, const void *)
    SYM(int, lilv_ui_is_a, const void *, const N *)
    SYM(N *, lilv_new_file_uri, W *, const char *, const char *)
    SYM(void, lilv_world_load_bundle, W *, const N *)

    W *w = lilv_world_new();
    const Pl *all;
    const P *p;
    unsigned i, n;
    int verified;
    {
        /* Ardour's bundles supply the LV2 spec files the parser needs. */
        WIN32_FIND_DATAA fd;
        HANDLE fh = FindFirstFileA(ARDOUR_LV2 "\\*.lv2", &fd);
        if (fh != INVALID_HANDLE_VALUE) {
            do {
                char dir[1024];
                if (!_stricmp(fd.cFileName, "tangerine_orbit.lv2")) {
                    printf("note: skipping the copy installed in Ardour's folder\n");
                    continue;
                }
                snprintf(dir, sizeof dir, "%s\\%s\\", ARDOUR_LV2, fd.cFileName);
                lilv_world_load_bundle(w, lilv_new_file_uri(w, NULL, dir));
            } while (FindNextFileA(fh, &fd));
            FindClose(fh);
        }
    }
    lilv_world_load_all(w);
    all = lilv_world_get_all_plugins(w);
    printf("plugins found: %u\n", lilv_plugins_size(all));
    p = lilv_plugins_get_by_uri(all, lilv_new_uri(w, PLUGIN_ID));
    if (!p) { printf("FAIL: plugin not found\n"); return 1; }
    verified = lilv_plugin_verify(p);
    printf("verify: %s\n", verified ? "ok" : "FAILED");
    printf("name: %s\n", lilv_node_as_string(lilv_plugin_get_name(p)));
    printf("class: %s\n", lilv_node_as_string(lilv_plugin_class_get_label(lilv_plugin_get_class(p))));
    printf("binary: %s\n", lilv_node_as_string(lilv_plugin_get_library_uri(p)));
    n = lilv_plugin_get_num_ports(p);
    for (i = 0; i < n; i++) {
        const Po *po = lilv_plugin_get_port_by_index(p, i);
        N *d = 0, *lo = 0, *hi = 0;
        lilv_port_get_range(p, po, &d, &lo, &hi);
        printf("  %u %-8s %-16s def %6.2f min %6.2f max %6.2f\n", i,
               lilv_node_as_string(lilv_port_get_symbol(p, po)),
               lilv_node_as_string(lilv_port_get_name(p, po)),
               d ? lilv_node_as_float(d) : -1, lo ? lilv_node_as_float(lo) : -1,
               hi ? lilv_node_as_float(hi) : -1);
    }
    {
        /* The GUI, as a Windows host such as Ardour would find it. */
        void *uis = lilv_plugin_get_uis(p);
        const void *u = uis ? lilv_uis_get_by_uri(uis, lilv_new_uri(w, PLUGIN_ID "#ui")) : NULL;
        if (!u) { printf("FAIL: GUI not found\n"); return 1; }
        printf("GUIs: %u, %s, binary %s\n", lilv_uis_size(uis),
               lilv_ui_is_a(u, lilv_new_uri(w, "http://lv2plug.in/ns/extensions/ui#WindowsUI")) ? "WindowsUI" : "NOT WindowsUI",
               lilv_node_as_string(lilv_ui_get_binary_uri(u)));
    }
    {
        LI *inst = lilv_plugin_instantiate(p, 48000.0, NULL);
        static float in[48000], out[48000];
        /* Port defaults read back from the TTL through lilv, so the check uses what hosts see. */
        static float ctl[64];
        float peak = 0;
        const D *de;
        int k;
        if (!inst) { printf("FAIL: instantiate\n"); return 1; }
        de = (const D *)inst->d;
        for (k = 0; k < 48000; k++) in[k] = 0.1f * (float)sin(2 * 3.14159265 * 500 * k / 48000.0);
        for (k = 2; k < (int)n && k < 64; k++) {
            N *dv = 0, *lo = 0, *hi = 0;
            lilv_port_get_range(p, lilv_plugin_get_port_by_index(p, (unsigned)k), &dv, &lo, &hi);
            ctl[k] = dv ? lilv_node_as_float(dv) : 0.0f;
            de->connect_port(inst->h, (unsigned)k, &ctl[k]);
        }
        ctl[8] = -1.0f; /* sweep output: the plugin must overwrite this */
        de->connect_port(inst->h, 0, in); de->connect_port(inst->h, 1, out);
        de->activate(inst->h);
        de->run(inst->h, 48000);
        for (k = 24000; k < 48000; k++) if (fabs(out[k]) > peak) peak = (float)fabs(out[k]);
        printf("instantiated through lilv: peak %.3f, sweep output %.3f\n", peak, ctl[8]);
        if (!(peak > 0.001f)) { printf("FAIL: no output\n"); return 1; }
        if (!(ctl[8] >= 0.0f && ctl[8] <= 1.0f)) { printf("FAIL: sweep output not written\n"); return 1; }
    }
    return verified ? 0 : 1;
    }
}
