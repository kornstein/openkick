#include "params.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

const ParamInfo paramInfos[ParamCount] = {
    [ParamMix] = {"Mix", "%", 0, 1.0, false, false},
    [ParamCurve] = {"Curve", "", CurveCount - 1, 0.0, true, false},
    [ParamRate] = {"Rate", "", RateCount - 1, 2.0 / (RateCount - 1), true, false},
    [ParamBypass] = {"Bypass", "", 1, 0.0, false, true},
};

double paramClamp(double normalized) {
    return normalized < 0.0 ? 0.0 : (normalized > 1.0 ? 1.0 : normalized);
}

int paramStep(ParamId id, double normalized) {
    int stepCount = paramInfos[id].stepCount;
    int step = (int)(paramClamp(normalized) * stepCount + 0.5);
    return step > stepCount ? stepCount : step;
}

double paramFromStep(ParamId id, int step) {
    int stepCount = paramInfos[id].stepCount;
    return stepCount > 0 ? paramClamp((double)step / stepCount) : 0.0;
}

double paramToPlain(ParamId id, double normalized) {
    return id == ParamMix ? paramClamp(normalized) * 100.0 : paramStep(id, normalized);
}

double paramFromPlain(ParamId id, double plain) {
    return id == ParamMix ? paramClamp(plain / 100.0) : paramFromStep(id, (int)(plain + 0.5));
}

static const char* stepName(ParamId id, int step) {
    switch (id) {
    case ParamCurve:
        return curveShapes[step].name;
    case ParamRate:
        return syncRates[step].name;
    case ParamBypass:
        return step ? "On" : "Off";
    default:
        return "";
    }
}

void paramFormat(ParamId id, double normalized, char* text, size_t size) {
    if (id == ParamMix) {
        snprintf(text, size, "%.0f", paramClamp(normalized) * 100.0);
    } else {
        snprintf(text, size, "%s", stepName(id, paramStep(id, normalized)));
    }
}

static bool equalsIgnoringCase(const char* a, const char* b) {
    for (; *a && *b; ++a, ++b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return false;
        }
    }
    return *a == *b;
}

bool paramParse(ParamId id, const char* text, double* normalized) {
    if (id == ParamMix) {
        char* end = NULL;
        double percent = strtod(text, &end);
        if (end == text) {
            return false;
        }
        *normalized = paramClamp(percent / 100.0);
        return true;
    }
    for (int step = 0; step <= paramInfos[id].stepCount; ++step) {
        if (equalsIgnoringCase(text, stepName(id, step))) {
            *normalized = paramFromStep(id, step);
            return true;
        }
    }
    return false;
}

void settingsFromParams(const double params[ParamCount], ShaperSettings* settings) {
    settings->depth = paramClamp(params[ParamMix]);
    settings->curve = paramStep(ParamCurve, params[ParamCurve]);
    settings->rate = paramStep(ParamRate, params[ParamRate]);
    settings->bypassed = paramStep(ParamBypass, params[ParamBypass]) != 0;
}
