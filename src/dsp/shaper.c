#include "shaper.h"

#include <math.h>

static const double defaultTempo = 120.0;
static const double attackSeconds = 0.0004;
static const double releaseSeconds = 0.0015;

const CurveShape curveShapes[CurveCount] = {
    {"Classic", 0.00, 0.60, EasingOutQuad},   {"Short", 0.00, 0.30, EasingOutQuad},
    {"Long", 0.00, 0.90, EasingOutQuad},      {"Linear", 0.00, 0.70, EasingLinear},
    {"Smooth", 0.00, 0.75, EasingSmoothstep}, {"Punchy", 0.06, 0.40, EasingOutCubic},
    {"Hold", 0.25, 0.40, EasingSmoothstep},   {"Slow", 0.00, 0.85, EasingInQuad},
};

const SyncRate syncRates[RateCount] = {
    {"1/1", 4.0}, {"1/2", 2.0}, {"1/4", 1.0}, {"1/8", 0.5}, {"1/16", 0.25},
};

static double ease(Easing easing, double t) {
    switch (easing) {
    case EasingOutQuad:
        return 1.0 - (1.0 - t) * (1.0 - t);
    case EasingOutCubic:
        return 1.0 - (1.0 - t) * (1.0 - t) * (1.0 - t);
    case EasingInQuad:
        return t * t;
    case EasingSmoothstep:
        return t * t * (3.0 - 2.0 * t);
    case EasingLinear:
        break;
    }
    return t;
}

double curveGain(int curve, double phase) {
    const CurveShape* shape = &curveShapes[curve];
    if (phase < shape->hold) {
        return 0.0;
    }
    double t = (phase - shape->hold) / shape->length;
    return t >= 1.0 ? 1.0 : ease(shape->easing, t);
}

double applyDepth(double gain, double depth) {
    return 1.0 - depth * (1.0 - gain);
}

static double smoothingCoefficient(double seconds, double sampleRate) {
    return 1.0 - exp(-1.0 / (seconds * sampleRate));
}

void shaperPrepare(Shaper* shaper, double sampleRate) {
    shaper->sampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    shaper->attackCoefficient = smoothingCoefficient(attackSeconds, shaper->sampleRate);
    shaper->releaseCoefficient = smoothingCoefficient(releaseSeconds, shaper->sampleRate);
    if (shaper->tempo <= 0.0) {
        shaper->tempo = defaultTempo;
    }
}

void shaperReset(Shaper* shaper) {
    shaper->position = 0.0;
    shaper->phase = 0.0;
    shaper->gain = 1.0;
}

void shaperSetTempo(Shaper* shaper, double beatsPerMinute) {
    if (beatsPerMinute > 0.0) {
        shaper->tempo = beatsPerMinute;
    }
}

void shaperLocate(Shaper* shaper, double quarterNotes) {
    shaper->position = quarterNotes;
}

void shaperRender(Shaper* shaper, const ShaperSettings* settings, double* gains, int frameCount) {
    const double cycle = syncRates[settings->rate].quarterNotes;
    const double step = shaper->tempo / 60.0 / shaper->sampleRate;
    double position = shaper->position;
    double gain = shaper->gain;
    double phase = shaper->phase;

    for (int frame = 0; frame < frameCount; ++frame) {
        double cycles = position / cycle;
        phase = cycles - floor(cycles);
        double target = settings->bypassed ? 1.0 : applyDepth(curveGain(settings->curve, phase), settings->depth);
        double coefficient = target < gain ? shaper->attackCoefficient : shaper->releaseCoefficient;
        gain += (target - gain) * coefficient;
        gains[frame] = gain;
        position += step;
    }

    shaper->position = position;
    shaper->gain = gain;
    shaper->phase = phase;
}
