#include "editor.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "canvas.h"
#include "font.h"
#include "fonts.h"
#include "version.h"

#define PI 3.14159265358979323846f

enum {
    ColorBackground = 0x2231C4,
    ColorDeep = 0x1B289E,
    ColorInk = 0xF2F2F2,
    ColorAccent = 0xF2F200,
};

static const float knobStartAngle = 1.25f * PI;
static const float knobSweep = -1.5f * PI;
static const float knobRadius = 62.0f;
static const Point knobCenter = {642.0f, 198.0f};
static const Rect plotRect = {24.0f, 112.0f, 476.0f, 200.0f};
static const Rect bypassRect = {604.0f, 23.0f, 132.0f, 30.0f};
static const Rect meterRect = {548.0f, 307.0f, 188.0f, 3.0f};
static const float columnDivider = 524.0f;
static const float headerBottom = 72.0f;
static const float mainBottom = 336.0f;
static const float rateBottom = 396.0f;
static const float footerTop = 516.0f;
static const float contentLeft = 24.0f;
static const float contentRight = 736.0f;
static const float sideLeft = 548.0f;

typedef enum {
    TargetNone,
    TargetBypass,
    TargetKnob,
    TargetPlot,
    TargetRate,
    TargetCurve,
} TargetKind;

typedef struct {
    TargetKind kind;
    int index;
} Target;

typedef struct {
    double mix;
    int curve;
    int rate;
    bool bypassed;
    EditorMeter meter;
} Snapshot;

struct Editor {
    EditorHost host;
    Canvas canvas;
    Font* title;
    Font* display;
    Font* mono;
    Font* monoMedium;
    Target hovered;
    float playheadPhase;
    float meterGain;
    bool draggingKnob;
    bool dragFine;
    float dragOriginY;
    double dragOriginValue;
};

Editor* editorCreate(const EditorHost* host) {
    Editor* editor = calloc(1, sizeof(Editor));
    if (!editor) {
        return NULL;
    }
    editor->host = *host;
    editor->meterGain = 1.0f;
    editor->title = fontCreate(fontTitleData);
    editor->display = fontCreate(fontDisplayData);
    editor->mono = fontCreate(fontMonoData);
    editor->monoMedium = fontCreate(fontMonoMediumData);
    if (!editor->title || !editor->display || !editor->mono || !editor->monoMedium) {
        editorDestroy(editor);
        return NULL;
    }
    return editor;
}

void editorDestroy(Editor* editor) {
    if (!editor) {
        return;
    }
    fontDestroy(editor->title);
    fontDestroy(editor->display);
    fontDestroy(editor->mono);
    fontDestroy(editor->monoMedium);
    canvasRelease(&editor->canvas);
    free(editor);
}

static Color ink(float alpha) {
    return colorWithAlpha(colorFromRgb(ColorInk), alpha);
}

static Color accent(const Snapshot* snapshot) {
    return snapshot->bypassed ? ink(0.45f) : colorFromRgb(ColorAccent);
}

static TextStyle textStyle(Font* font, float size, float tracking, Color color) {
    return (TextStyle){font, size, tracking, color};
}

static void uppercase(char* text) {
    for (; *text; ++text) {
        if (*text >= 'a' && *text <= 'z') {
            *text = (char)(*text - 'a' + 'A');
        }
    }
}

static Rect rateRect(int index) {
    const float left = 132.0f, width = (500.0f - left) / RateCount;
    return (Rect){left + index * width, 350.0f, width, 32.0f};
}

static Rect curveRect(int index) {
    const float gap = 8.0f, width = (contentRight - contentLeft - gap * (CurveCount - 1)) / CurveCount;
    return (Rect){contentLeft + index * (width + gap), 434.0f, width, 66.0f};
}

static bool sameTarget(Target a, Target b) {
    return a.kind == b.kind && a.index == b.index;
}

static Target hitTest(float x, float y) {
    if (hypotf(x - knobCenter.x, y - knobCenter.y) <= knobRadius + 18.0f) {
        return (Target){TargetKnob, 0};
    }
    if (rectContains(bypassRect, x, y)) {
        return (Target){TargetBypass, 0};
    }
    if (rectContains(plotRect, x, y)) {
        return (Target){TargetPlot, 0};
    }
    for (int index = 0; index < RateCount; ++index) {
        if (rectContains(rateRect(index), x, y)) {
            return (Target){TargetRate, index};
        }
    }
    for (int index = 0; index < CurveCount; ++index) {
        if (rectContains(curveRect(index), x, y)) {
            return (Target){TargetCurve, index};
        }
    }
    return (Target){TargetNone, 0};
}

static void hairline(Editor* editor, float x0, float y0, float x1, float y1, Color color) {
    float pixel = 1.0f / editor->canvas.scale;
    float x = floorf(x0 * editor->canvas.scale) * pixel;
    float y = floorf(y0 * editor->canvas.scale) * pixel;
    Rect rule = x0 == x1 ? (Rect){x, y0, pixel, y1 - y0} : (Rect){x0, y, x1 - x0, pixel};
    canvasFillRect(&editor->canvas, rule, color);
}

static void drawSectionLabel(Editor* editor, const Snapshot* snapshot, float x, float baseline, const char* index,
                             const char* title) {
    TextStyle number = textStyle(editor->monoMedium, 10.0f, 1.0f, accent(snapshot));
    TextStyle name = textStyle(editor->monoMedium, 10.0f, 1.6f, ink(1.0f));
    char bracketed[8];
    snprintf(bracketed, sizeof(bracketed), "[%s]", index);
    textDraw(&editor->canvas, &number, x, baseline, TextAlignLeft, bracketed);
    textDraw(&editor->canvas, &name, x + textMeasure(&number, bracketed) + 10.0f, baseline, TextAlignLeft, title);
}

static void drawTag(Editor* editor, float x, float y, const char* text, Color background, Color foreground) {
    TextStyle style = textStyle(editor->monoMedium, 9.0f, 0.8f, foreground);
    Rect tag = {x, y, textMeasure(&style, text) + 10.0f, 16.0f};
    canvasFillRect(&editor->canvas, tag, background);
    textDraw(&editor->canvas, &style, tag.x + 5.0f, tag.y + 11.5f, TextAlignLeft, text);
}

static void drawHeader(Editor* editor, const Snapshot* snapshot) {
    TextStyle title = textStyle(editor->title, 50.0f, -0.5f, ink(1.0f));
    TextStyle line = textStyle(editor->mono, 9.0f, 1.2f, ink(1.0f));
    TextStyle muted = textStyle(editor->mono, 9.0f, 1.2f, ink(0.55f));
    textDraw(&editor->canvas, &title, contentLeft, 52.0f, TextAlignLeft, "openkick");

    float infoX = contentLeft + textMeasure(&title, "openkick") + 20.0f;
    hairline(editor, infoX - 10.0f, 30.0f, infoX - 10.0f, 56.0f, ink(0.3f));
    textDraw(&editor->canvas, &line, infoX, 39.0f, TextAlignLeft, "SIDECHAIN ENVELOPE SHAPER");
    textDraw(&editor->canvas, &muted, infoX, 53.0f, TextAlignLeft, "VST3 \xC2\xB7 V" OPENKICK_VERSION);

    bool hovered = editor->hovered.kind == TargetBypass;
    canvasStrokeRoundRect(&editor->canvas, bypassRect, 2.0f, 1.0f, ink(hovered ? 1.0f : 0.45f));
    Rect indicator = {bypassRect.x + 14.0f, bypassRect.y + bypassRect.height * 0.5f - 4.0f, 8.0f, 8.0f};
    if (snapshot->bypassed) {
        canvasStrokeRoundRect(&editor->canvas, indicator, 1.0f, 1.0f, ink(0.55f));
    } else {
        canvasFillRect(&editor->canvas, indicator, colorFromRgb(ColorAccent));
    }
    TextStyle label = textStyle(editor->monoMedium, 10.0f, 1.6f, ink(snapshot->bypassed ? 0.6f : 1.0f));
    textDraw(&editor->canvas, &label, bypassRect.x + 32.0f, bypassRect.y + 19.5f, TextAlignLeft,
             snapshot->bypassed ? "BYPASSED" : "ACTIVE");
}

static void envelopePoints(Rect area, int curve, double depth, Point* points, int count) {
    for (int index = 0; index < count; ++index) {
        double phase = (double)index / (count - 1);
        double gain = applyDepth(curveGain(curve, phase < 1.0 ? phase : 0.99999), depth);
        points[index] = (Point){area.x + (float)phase * area.width, area.y + (float)(1.0 - gain) * area.height};
    }
}

static void drawGrid(Editor* editor, Rect area) {
    const int columns = 4, rows = 4;
    float dot = 1.0f / editor->canvas.scale;
    for (int column = 1; column < columns; ++column) {
        float x = area.x + area.width * column / columns;
        for (float y = area.y + 4.0f; y < area.y + area.height; y += 6.0f) {
            canvasFillRect(&editor->canvas, (Rect){x, y, dot, dot}, ink(0.35f));
        }
    }
    for (int row = 1; row < rows; ++row) {
        float y = area.y + area.height * row / rows;
        for (float x = area.x + 4.0f; x < area.x + area.width; x += 6.0f) {
            canvasFillRect(&editor->canvas, (Rect){x, y, dot, dot}, ink(0.35f));
        }
    }
    for (int column = 1; column < columns; ++column) {
        for (int row = 1; row < rows; ++row) {
            Point center = {area.x + area.width * column / columns, area.y + area.height * row / rows};
            hairline(editor, center.x - 4.0f, center.y, center.x + 5.0f, center.y, ink(0.7f));
            hairline(editor, center.x, center.y - 4.0f, center.x, center.y + 5.0f, ink(0.7f));
        }
    }
}

static void drawEnvelope(Editor* editor, const Snapshot* snapshot) {
    enum { Resolution = 240 };
    char text[48];
    TextStyle small = textStyle(editor->mono, 9.0f, 1.0f, ink(0.55f));
    TextStyle summary = textStyle(editor->monoMedium, 10.0f, 1.4f, ink(1.0f));

    drawSectionLabel(editor, snapshot, contentLeft, 98.0f, "01", "ENVELOPE");
    snprintf(text, sizeof(text), "%s \xC2\xB7 %s", curveShapes[snapshot->curve].name, syncRates[snapshot->rate].name);
    uppercase(text);
    textDraw(&editor->canvas, &summary, plotRect.x + plotRect.width, 98.0f, TextAlignRight, text);

    canvasFillRect(&editor->canvas, plotRect, colorFromRgb(ColorDeep));
    drawGrid(editor, plotRect);
    canvasStrokeRoundRect(&editor->canvas, plotRect, 0.0f, 1.0f / editor->canvas.scale,
                          ink(editor->hovered.kind == TargetPlot ? 0.8f : 0.3f));

    Rect area = rectInset(plotRect, 0.0f, 14.0f);
    Point points[Resolution];
    float strength = snapshot->bypassed ? 0.4f : 1.0f;
    if (snapshot->mix < 0.995) {
        envelopePoints(area, snapshot->curve, 1.0, points, Resolution);
        canvasStrokePolyline(&editor->canvas, points, Resolution, 1.0f, ink(0.3f * strength));
    }
    envelopePoints(area, snapshot->curve, snapshot->mix, points, Resolution);
    canvasDitherBelow(&editor->canvas, points, Resolution, plotRect.y + plotRect.height, 90.0f, 0.55f, 0.07f,
                      ink(0.8f * strength));
    canvasStrokePolyline(&editor->canvas, points, Resolution, 2.0f, ink(strength));

    double phase = snapshot->meter.phase;
    double gain = applyDepth(curveGain(snapshot->curve, phase), snapshot->mix);
    Point head = {area.x + (float)phase * area.width, area.y + (float)(1.0 - gain) * area.height};
    Color headColor = accent(snapshot);
    hairline(editor, head.x, plotRect.y + 1.0f, head.x, plotRect.y + plotRect.height - 1.0f, headColor);
    canvasStrokeCircle(&editor->canvas, head, 9.0f, 1.0f, colorWithAlpha(headColor, 0.45f));
    canvasFillCircle(&editor->canvas, head, 4.0f, headColor);

    drawTag(editor, plotRect.x + 1.0f, plotRect.y + 1.0f, "100%", colorFromRgb(ColorDeep), ink(0.6f));
    drawTag(editor, plotRect.x + 1.0f, plotRect.y + plotRect.height - 17.0f, "0%", colorFromRgb(ColorDeep), ink(0.6f));
    snprintf(text, sizeof(text), "%.2f", phase);
    float tagWidth = textMeasure(&small, text) + 10.0f;
    float tagX =
        fminf(fmaxf(head.x - tagWidth * 0.5f, plotRect.x + 1.0f), plotRect.x + plotRect.width - tagWidth - 1.0f);
    drawTag(editor, tagX, plotRect.y + 1.0f, text, headColor, colorFromRgb(ColorBackground));

    static const char* const ticks[] = {"0", "1/4", "1/2", "3/4", "1"};
    for (int index = 0; index < 5; ++index) {
        float x = plotRect.x + plotRect.width * index / 4.0f;
        TextAlign align = index == 0 ? TextAlignLeft : (index == 4 ? TextAlignRight : TextAlignCenter);
        textDraw(&editor->canvas, &small, x, plotRect.y + plotRect.height + 16.0f, align, ticks[index]);
    }
}

static Point knobPoint(double value, float radius) {
    float angle = knobStartAngle + knobSweep * (float)value;
    return (Point){knobCenter.x + cosf(angle) * radius, knobCenter.y - sinf(angle) * radius};
}

static void drawMix(Editor* editor, const Snapshot* snapshot) {
    enum { TickCount = 41 };
    char text[32];
    drawSectionLabel(editor, snapshot, sideLeft, 98.0f, "02", "MIX");

    for (int tick = 0; tick < TickCount; ++tick) {
        double position = (double)tick / (TickCount - 1);
        bool major = tick % 4 == 0;
        bool lit = position <= snapshot->mix + 1e-6;
        canvasStrokeLine(&editor->canvas, knobPoint(position, 70.0f), knobPoint(position, major ? 78.0f : 74.0f),
                         major ? 1.5f : 1.0f, ink(lit ? (major ? 1.0f : 0.8f) : 0.25f));
    }

    canvasStrokeArc(&editor->canvas, knobCenter, knobRadius, 1.0f, knobStartAngle, knobSweep, ink(0.28f));
    canvasStrokeArc(&editor->canvas, knobCenter, knobRadius, 3.0f, knobStartAngle, knobSweep * (float)snapshot->mix,
                    accent(snapshot));

    bool active = editor->draggingKnob || editor->hovered.kind == TargetKnob;
    canvasFillCircle(&editor->canvas, knobCenter, 52.0f, colorFromRgb(ColorDeep));
    canvasStrokeCircle(&editor->canvas, knobCenter, 52.0f, 1.0f, ink(active ? 0.7f : 0.2f));
    canvasStrokeLine(&editor->canvas, knobPoint(snapshot->mix, 36.0f), knobPoint(snapshot->mix, 48.0f), 2.5f,
                     ink(1.0f));

    TextStyle value = textStyle(editor->display, 48.0f, 0.5f, ink(1.0f));
    TextStyle unit = textStyle(editor->monoMedium, 10.0f, 0.0f, ink(0.6f));
    snprintf(text, sizeof(text), "%.0f", snapshot->mix * 100.0);
    float capHeight = fontCapHeight(editor->display, value.size);
    float baseline = knobCenter.y + capHeight * 0.5f;
    float width = textMeasure(&value, text);
    textDraw(&editor->canvas, &value, knobCenter.x, baseline, TextAlignCenter, text);
    textDraw(&editor->canvas, &unit, knobCenter.x + width * 0.5f + 3.0f, baseline - capHeight + 8.0f, TextAlignLeft,
             "%");

    float gain = snapshot->meter.gain < 0.0f ? 0.0f : (snapshot->meter.gain > 1.0f ? 1.0f : snapshot->meter.gain);
    TextStyle label = textStyle(editor->mono, 9.0f, 1.2f, ink(0.55f));
    TextStyle reading = textStyle(editor->monoMedium, 9.0f, 1.0f, ink(1.0f));
    if (gain > 0.0001f) {
        snprintf(text, sizeof(text), "%.1f DB", 20.0 * log10(gain));
    } else {
        snprintf(text, sizeof(text), "-INF DB");
    }
    textDraw(&editor->canvas, &label, sideLeft, meterRect.y - 7.0f, TextAlignLeft, "GAIN");
    textDraw(&editor->canvas, &reading, contentRight, meterRect.y - 7.0f, TextAlignRight, text);
    canvasFillRect(&editor->canvas, meterRect, ink(0.18f));
    Rect reduction = meterRect;
    reduction.width *= 1.0f - gain;
    canvasFillRect(&editor->canvas, reduction, accent(snapshot));
}

static void drawRate(Editor* editor, const Snapshot* snapshot) {
    drawSectionLabel(editor, snapshot, contentLeft, 370.0f, "03", "RATE");
    TextStyle label = textStyle(editor->monoMedium, 12.0f, 0.6f, ink(1.0f));
    float capHeight = fontCapHeight(editor->monoMedium, label.size);

    for (int index = 0; index < RateCount; ++index) {
        Rect cell = rateRect(index);
        bool selected = index == snapshot->rate;
        bool hovered = editor->hovered.kind == TargetRate && editor->hovered.index == index;
        if (selected) {
            canvasFillRect(&editor->canvas, cell, ink(1.0f));
        } else if (hovered) {
            canvasFillRect(&editor->canvas, cell, ink(0.12f));
        }
        if (index > 0) {
            hairline(editor, cell.x, cell.y, cell.x, cell.y + cell.height, ink(0.35f));
        }
        label.color = selected ? colorFromRgb(ColorBackground) : ink(1.0f);
        textDraw(&editor->canvas, &label, cell.x + cell.width * 0.5f, cell.y + (cell.height + capHeight) * 0.5f,
                 TextAlignCenter, syncRates[index].name);
    }
    Rect first = rateRect(0), last = rateRect(RateCount - 1);
    Rect group = {first.x, first.y, last.x + last.width - first.x, first.height};
    canvasStrokeRoundRect(&editor->canvas, group, 0.0f, 1.0f / editor->canvas.scale, ink(0.5f));
}

static void drawSync(Editor* editor, const Snapshot* snapshot) {
    char text[32];
    drawSectionLabel(editor, snapshot, sideLeft, 360.0f, "04", "SYNC");
    Point dot = {sideLeft + 3.5f, 375.5f};
    if (snapshot->meter.followsHost) {
        canvasFillCircle(&editor->canvas, dot, 3.5f, accent(snapshot));
    } else {
        canvasStrokeCircle(&editor->canvas, dot, 3.0f, 1.0f, ink(0.55f));
    }
    TextStyle status = textStyle(editor->mono, 9.0f, 1.2f, ink(0.7f));
    textDraw(&editor->canvas, &status, sideLeft + 13.0f, 379.0f, TextAlignLeft,
             snapshot->meter.followsHost ? "HOST SYNC" : "FREE RUN");

    TextStyle unit = textStyle(editor->mono, 9.0f, 1.2f, ink(0.55f));
    TextStyle tempo = textStyle(editor->display, 30.0f, 0.5f, ink(1.0f));
    snprintf(text, sizeof(text), "%.1f", snapshot->meter.tempo);
    float unitWidth = textMeasure(&unit, "BPM");
    textDraw(&editor->canvas, &unit, contentRight, 379.0f, TextAlignRight, "BPM");
    textDraw(&editor->canvas, &tempo, contentRight - unitWidth - 6.0f, 379.0f, TextAlignRight, text);
}

static void drawCurves(Editor* editor, const Snapshot* snapshot) {
    enum { Resolution = 48 };
    drawSectionLabel(editor, snapshot, contentLeft, 422.0f, "05", "CURVE");
    TextStyle hint = textStyle(editor->mono, 9.0f, 1.2f, ink(0.55f));
    textDraw(&editor->canvas, &hint, contentRight, 422.0f, TextAlignRight, "CLICK THE ENVELOPE TO CYCLE");

    for (int index = 0; index < CurveCount; ++index) {
        Rect tile = curveRect(index);
        bool selected = index == snapshot->curve;
        bool hovered = editor->hovered.kind == TargetCurve && editor->hovered.index == index;
        Color content = selected ? colorFromRgb(ColorBackground) : ink(hovered ? 1.0f : 0.8f);
        if (selected) {
            canvasFillRoundRect(&editor->canvas, tile, 2.0f, ink(1.0f));
        } else {
            canvasStrokeRoundRect(&editor->canvas, tile, 2.0f, 1.0f / editor->canvas.scale,
                                  ink(hovered ? 0.9f : 0.35f));
        }

        char number[4];
        char name[16];
        snprintf(number, sizeof(number), "%02d", index + 1);
        snprintf(name, sizeof(name), "%s", curveShapes[index].name);
        uppercase(name);
        TextStyle meta = textStyle(editor->mono, 9.0f, 1.0f, colorWithAlpha(content, 0.7f));
        TextStyle title = textStyle(editor->monoMedium, 9.0f, 1.2f, content);
        textDraw(&editor->canvas, &meta, tile.x + 8.0f, tile.y + 15.0f, TextAlignLeft, number);
        textDraw(&editor->canvas, &title, tile.x + 8.0f, tile.y + tile.height - 8.0f, TextAlignLeft, name);

        Rect thumbnail = {tile.x + 8.0f, tile.y + 22.0f, tile.width - 16.0f, 22.0f};
        Point points[Resolution];
        envelopePoints(thumbnail, index, 1.0, points, Resolution);
        canvasDitherBelow(&editor->canvas, points, Resolution, thumbnail.y + thumbnail.height, 16.0f, 0.5f, 0.12f,
                          colorWithAlpha(content, 0.8f));
        canvasStrokePolyline(&editor->canvas, points, Resolution, 1.5f, content);
    }
}

static void drawFooter(Editor* editor) {
    TextStyle style = textStyle(editor->mono, 9.0f, 1.2f, ink(0.55f));
    textDraw(&editor->canvas, &style, contentLeft, 532.0f, TextAlignLeft,
             "OPENKICK \xC2\xB7 FREE AND OPEN SOURCE \xC2\xB7 MIT");
    textDraw(&editor->canvas, &style, contentRight, 532.0f, TextAlignRight,
             "DRAG \xC2\xB7 SHIFT FOR FINE \xC2\xB7 DOUBLE-CLICK TO RESET");
}

static void drawRules(Editor* editor) {
    Color rule = ink(0.3f);
    hairline(editor, 0.0f, headerBottom, EditorWidth, headerBottom, rule);
    hairline(editor, 0.0f, mainBottom, EditorWidth, mainBottom, rule);
    hairline(editor, 0.0f, rateBottom, EditorWidth, rateBottom, rule);
    hairline(editor, 0.0f, footerTop, EditorWidth, footerTop, rule);
    hairline(editor, columnDivider, headerBottom, columnDivider, rateBottom, rule);
}

static void takeSnapshot(Editor* editor, Snapshot* snapshot) {
    void* context = editor->host.context;
    snapshot->mix = editor->host.getParam(context, ParamMix);
    snapshot->curve = paramStep(ParamCurve, editor->host.getParam(context, ParamCurve));
    snapshot->rate = paramStep(ParamRate, editor->host.getParam(context, ParamRate));
    snapshot->bypassed = paramStep(ParamBypass, editor->host.getParam(context, ParamBypass)) != 0;
    editor->host.readMeter(context, &snapshot->meter);
    if (snapshot->meter.hasSignal) {
        editor->playheadPhase = snapshot->meter.phase;
        editor->meterGain = snapshot->meter.gain;
    }
    snapshot->meter.phase = editor->playheadPhase;
    snapshot->meter.gain = editor->meterGain;
}

const uint32_t* editorRender(Editor* editor, float scale, int* width, int* height) {
    if (!canvasResize(&editor->canvas, EditorWidth, EditorHeight, scale)) {
        return NULL;
    }
    Snapshot snapshot;
    takeSnapshot(editor, &snapshot);

    canvasClear(&editor->canvas, colorFromRgb(ColorBackground));
    drawRules(editor);
    drawHeader(editor, &snapshot);
    drawEnvelope(editor, &snapshot);
    drawMix(editor, &snapshot);
    drawRate(editor, &snapshot);
    drawSync(editor, &snapshot);
    drawCurves(editor, &snapshot);
    drawFooter(editor);

    *width = editor->canvas.width;
    *height = editor->canvas.height;
    return editor->canvas.pixels;
}

static void commitParam(Editor* editor, ParamId id, double value) {
    editor->host.beginEdit(editor->host.context, id);
    editor->host.performEdit(editor->host.context, id, paramClamp(value));
    editor->host.endEdit(editor->host.context, id);
}

static void stepParam(Editor* editor, ParamId id, int direction, bool wrap) {
    int choices = paramInfos[id].stepCount + 1;
    int step = paramStep(id, editor->host.getParam(editor->host.context, id)) + direction;
    if (wrap) {
        step = (step % choices + choices) % choices;
    } else if (step < 0 || step >= choices) {
        return;
    }
    commitParam(editor, id, paramFromStep(id, step));
}

void editorMouseDown(Editor* editor, float x, float y, int clickCount, EditorModifiers modifiers) {
    Target target = hitTest(x, y);
    editor->hovered = target;
    switch (target.kind) {
    case TargetKnob:
        if (clickCount >= 2) {
            commitParam(editor, ParamMix, paramInfos[ParamMix].defaultValue);
            return;
        }
        editor->draggingKnob = true;
        editor->dragFine = (modifiers & EditorModifierFine) != 0;
        editor->dragOriginY = y;
        editor->dragOriginValue = editor->host.getParam(editor->host.context, ParamMix);
        editor->host.beginEdit(editor->host.context, ParamMix);
        break;
    case TargetBypass:
        stepParam(editor, ParamBypass, 1, true);
        break;
    case TargetPlot:
        stepParam(editor, ParamCurve, 1, true);
        break;
    case TargetRate:
        commitParam(editor, ParamRate, paramFromStep(ParamRate, target.index));
        break;
    case TargetCurve:
        commitParam(editor, ParamCurve, paramFromStep(ParamCurve, target.index));
        break;
    case TargetNone:
        break;
    }
}

void editorMouseDrag(Editor* editor, float x, float y, EditorModifiers modifiers) {
    (void)x;
    if (!editor->draggingKnob) {
        return;
    }
    bool fine = (modifiers & EditorModifierFine) != 0;
    if (fine != editor->dragFine) {
        editor->dragFine = fine;
        editor->dragOriginY = y;
        editor->dragOriginValue = editor->host.getParam(editor->host.context, ParamMix);
    }
    double travel = fine ? 800.0 : 200.0;
    double value = paramClamp(editor->dragOriginValue + (editor->dragOriginY - y) / travel);
    editor->host.performEdit(editor->host.context, ParamMix, value);
}

void editorMouseUp(Editor* editor) {
    if (editor->draggingKnob) {
        editor->draggingKnob = false;
        editor->host.endEdit(editor->host.context, ParamMix);
    }
}

void editorMouseMove(Editor* editor, float x, float y) {
    Target target = hitTest(x, y);
    if (!sameTarget(target, editor->hovered)) {
        editor->hovered = target;
    }
}

void editorMouseExit(Editor* editor) {
    editor->hovered = (Target){TargetNone, 0};
}

void editorMouseWheel(Editor* editor, float x, float y, float delta, EditorModifiers modifiers) {
    Target target = hitTest(x, y);
    int direction = delta > 0.0f ? 1 : -1;
    switch (target.kind) {
    case TargetKnob: {
        double step = (modifiers & EditorModifierFine) ? 0.005 : 0.02;
        commitParam(editor, ParamMix, editor->host.getParam(editor->host.context, ParamMix) + step * delta);
        break;
    }
    case TargetRate:
        stepParam(editor, ParamRate, direction, false);
        break;
    case TargetCurve:
    case TargetPlot:
        stepParam(editor, ParamCurve, direction, false);
        break;
    default:
        break;
    }
}
