/* Values and helpers shared by every stage. Voltages inside the audio path are
 * relative to VREF, the circuit's signal ground. */
#ifndef CIRCUIT_H
#define CIRCUIT_H

#include <math.h>

/* The parts list gives a 3.6 V to 4.7 V zener. 4.3 V is a stocked value in that range. */
#define VREF 4.3

/* TL072 output swing on a 9 V supply. Estimate, not from the schematic. */
#define OPAMP_LOW  1.5
#define OPAMP_HIGH 7.5

static inline double softplus(double x, double width)
{
    return 0.5 * (x + sqrt(x * x + width * width));
}

/* Linear up to 80% of the rail, then rounds off into it. */
static inline double opamp_limit(double v)
{
    const double hi = OPAMP_HIGH - VREF, lo = VREF - OPAMP_LOW;
    const double hi_knee = 0.8 * hi, lo_knee = 0.8 * lo;
    if (v > hi_knee)
        return hi_knee + (hi - hi_knee) * tanh((v - hi_knee) / (hi - hi_knee));
    if (v < -lo_knee)
        return -lo_knee - (lo - lo_knee) * tanh((-v - lo_knee) / (lo - lo_knee));
    return v;
}

#endif
