#include <math.h>
#include "orpheus/orp_map.h"
#include "synth/calib.h"

/* Level offsets (dB codes) per channel, and the mark-space ratio the p14 formula takes: chosen by listening. */
#define OFF_ALF -10
#define OFF_A1  -10
#define OFF_A2  0
#define OFF_A3  -2
#define OFF_P8  -14   /* AHF -> the 3.2-3.8 kHz cascade */
#define OFF_P9  -36   /* AHF -> the 4.0-4.6 kHz cascade, about 22 dB hotter per code */
#define MSR     50

static double clampd(double v, double lo, double hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static int16_t hz(double v)
{
    return (int16_t)floor(clampd(v, 50.0, 4500.0) + 0.5);
}

static int16_t amp(int code, int off)
{
    if (code <= 1)
        return 0;
    int v = code + off;
    return (int16_t)(v < 0 ? 0 : v > 63 ? 63 : v);
}

void orp_map_frame(const uint8_t f[SYNTH_FRAME_BYTES], float centre_hz, int16_t p[ORP_PARAMS])
{
    p[0] = hz((double)CAL_FN_B * f[1]);
    p[1] = hz((double)CAL_F1_A + (double)CAL_F1_B * f[3]);
    p[2] = hz((double)CAL_F2_A + (double)CAL_F2_B * f[5]);
    p[3] = hz((double)CAL_F3_A * exp((double)CAL_F3_B * f[7]));
    p[4] = amp(f[0], OFF_ALF);
    p[5] = amp(f[2], OFF_A1);
    p[6] = amp(f[4], OFF_A2);
    p[7] = amp(f[6], OFF_A3);
    p[8] = amp(f[8], OFF_P8);
    p[9] = amp(f[8], OFF_P9);
    p[10] = p[11] = 0;
    p[12] = (int16_t)clampd(floor((f[9] - 1) * 100 / 62.0 + 0.5), 0.0, 100.0);
    double f0 = params_f0_hz(f[10]), c = centre_hz;
    if (c > 0.0)
        f0 = clampd(c * pow(f0 / c, (double)CAL_F0_RANGE_GAIN), (double)CAL_F0_MIN_HZ, (double)CAL_F0_MAX_HZ);
    int f0i = (int)floor(f0 + 0.5);
    int cx = 2 * f0i - ((2 * f0i - (f0i >> 1)) * MSR) / 100;   /* the engine's pulse rate (a3s.dll 23206847) */
    p[13] = (int16_t)f0i;
    p[14] = (int16_t)(cx * 100 / 60);
    p[15] = 0;
}
