#ifndef OPENKICK_SHAPER_H
#define OPENKICK_SHAPER_H

#include <stdbool.h>

enum { CurveCount = 8, RateCount = 5 };

typedef enum {
    EasingOutQuad,
    EasingOutCubic,
    EasingInQuad,
    EasingLinear,
    EasingSmoothstep,
} Easing;

typedef struct {
    const char* name;
    double hold;
    double length;
    Easing easing;
} CurveShape;

typedef struct {
    const char* name;
    double quarterNotes;
} SyncRate;

typedef struct {
    int curve;
    int rate;
    double depth;
    bool bypassed;
} ShaperSettings;

typedef struct {
    double sampleRate;
    double tempo;
    double position;
    double phase;
    double gain;
    double attackCoefficient;
    double releaseCoefficient;
} Shaper;

extern const CurveShape curveShapes[CurveCount];
extern const SyncRate syncRates[RateCount];

double curveGain(int curve, double phase);
double applyDepth(double curveGain, double depth);

void shaperPrepare(Shaper* shaper, double sampleRate);
void shaperReset(Shaper* shaper);
void shaperSetTempo(Shaper* shaper, double beatsPerMinute);
void shaperLocate(Shaper* shaper, double quarterNotes);
void shaperRender(Shaper* shaper, const ShaperSettings* settings, double* gains, int frameCount);

#endif
