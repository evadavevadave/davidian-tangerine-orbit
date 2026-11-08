/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* Port indices, shared by the plugin and its GUI. They must match bundle/tangerine_orbit.ttl. */
#ifndef PORTS_H
#define PORTS_H

#define ORBIT_URI    "urn:davidian:tangerine-orbit"
#define ORBIT_UI_URI "urn:davidian:tangerine-orbit#ui"

enum {
    PORT_IN, PORT_OUT,
    PORT_SPEED, PORT_BIAS, PORT_VOLTS, PORT_OUTPUT, PORT_ENABLED,
    PORT_ENGAGE, /* the pedal's footswitch, separate from the host's bypass */
    PORT_SWEEP,  /* output: where the sweep is, 0 at the bottom to 1 at the top, for the GUI */
    PORT_COUNT
};

#endif
