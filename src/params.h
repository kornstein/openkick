#ifndef OPENKICK_PARAMS_H
#define OPENKICK_PARAMS_H

#include <stdbool.h>
#include <stddef.h>

#include "dsp/shaper.h"

typedef enum {
    ParamMix,
    ParamCurve,
    ParamRate,
    ParamBypass,
    ParamCount,
} ParamId;

typedef struct {
    const char* title;
    const char* units;
    int stepCount;
    double defaultValue;
    bool isList;
    bool isBypass;
} ParamInfo;

extern const ParamInfo paramInfos[ParamCount];

int paramStep(ParamId id, double normalized);
double paramFromStep(ParamId id, int step);
double paramToPlain(ParamId id, double normalized);
double paramFromPlain(ParamId id, double plain);
void paramFormat(ParamId id, double normalized, char* text, size_t size);
bool paramParse(ParamId id, const char* text, double* normalized);
double paramClamp(double normalized);

void settingsFromParams(const double params[ParamCount], ShaperSettings* settings);

#endif
