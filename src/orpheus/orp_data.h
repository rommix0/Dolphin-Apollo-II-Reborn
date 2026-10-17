#ifndef ORPHEUS_ORP_DATA_H
#define ORPHEUS_ORP_DATA_H
/* The Orpheus synth's state after init at 10 kHz (a3s.dll 2320bb10, 2320c620, 2320c510), captured from the Unicorn
   oracle by xtras/orpheus/orpheus_gendata.py into orp_data.c (spec 2026-10-16-orpheus-synth-analysis.md). Doubles are
   kept as their bit patterns. */
#include <stdint.h>

typedef union {
    uint64_t bits;
    double value;
} orp_f64;

#define ORP_COS_N      1873   /* centi-Bark 0..1872 */
#define ORP_BW_N       100    /* bandwidth in 8 Hz steps */
#define ORP_AMP_N      64     /* amplitude codes, 1 dB per step */
#define ORP_VMIX_N     32     /* voicing mix index */
#define ORP_SOURCES    2      /* p15: 0 shaped pulse, 1 impulse */
#define ORP_GLOTTAL_N  72     /* steps per source table */
#define ORP_PHASE_N    18     /* step / 4 -> pitch-synchronous phase */
#define ORP_RATE_MIN   40     /* p14 clamp, and the glottal means' first rate */
#define ORP_RATE_MAX   550
#define ORP_MEAN_N     53     /* one mean per 10 of pulse rate */
#define ORP_BWTAB_N    17     /* phase 0..16 */

/* orp_filters: 7 doubles per filter, in this order */
enum { ORP_FA, ORP_FB, ORP_FC, ORP_FE, ORP_FD, ORP_FY1, ORP_FY2, ORP_F_FIELDS };
enum {
    ORP_F_NOISE, ORP_F_FINAL, ORP_F_CASC, ORP_F_CH0,
    ORP_F_FIX0 = ORP_F_CH0 + 4,       /* two cascades of three at 10 kHz */
    ORP_F_SMOOTH0 = ORP_F_FIX0 + 6,   /* parameters 0-14 */
    ORP_F_COUNT = ORP_F_SMOOTH0 + 15
};

/* orp_cfg */
enum {
    ORP_C_GAIN0,                      /* channel gains 0-7 */
    ORP_C_BWMOD = ORP_C_GAIN0 + 8,    /* noise share -> bandwidth phase offset */
    ORP_C_WIDEN_DB, ORP_C_WIDEN_F1, ORP_C_WIDEN_DIV,
    ORP_C_NOISE_SCALE, ORP_C_BURST_AMP, ORP_C_COUNT
};

/* orp_cfg_int */
enum {
    ORP_I_VOFF0,                      /* per-channel voicing offsets 0-7 */
    ORP_I_WIDEN0 = ORP_I_VOFF0 + 8,   /* bandwidth widening, channels 0-3 (Hz) */
    ORP_I_HOLD_DROP = ORP_I_WIDEN0 + 4, ORP_I_HOLD_SILENT,
    ORP_I_LCG_X, ORP_I_LCG_MUL, ORP_I_LCG_MOD, ORP_I_BURST_LEN, ORP_I_BURST_POS, ORP_I_COUNT
};

extern const orp_f64 orp_cos2[ORP_COS_N];
extern const orp_f64 orp_r[ORP_BW_N];
extern const orp_f64 orp_negr2[ORP_BW_N];
extern const orp_f64 orp_amp[ORP_AMP_N];
extern const orp_f64 orp_vmix[ORP_VMIX_N];
extern const orp_f64 orp_glottal_mean[ORP_SOURCES * ORP_MEAN_N];
extern const orp_f64 orp_filters[ORP_F_COUNT * ORP_F_FIELDS];
extern const orp_f64 orp_cfg[ORP_C_COUNT];
extern const int16_t orp_cfg_int[ORP_I_COUNT];
extern const int16_t orp_init_params[4 * 16];   /* current, previous, jump threshold, jump flag */
extern const int16_t orp_glottal[ORP_SOURCES * ORP_GLOTTAL_N];
extern const int16_t orp_phase_map[ORP_SOURCES * ORP_PHASE_N];
extern const int16_t orp_bwtab[4 * ORP_BWTAB_N];

#endif
