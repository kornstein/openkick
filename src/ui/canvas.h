#ifndef OPENKICK_CANVAS_H
#define OPENKICK_CANVAS_H

#include <stdbool.h>
#include <stdint.h>

typedef uint32_t Color;

typedef struct {
    float x;
    float y;
} Point;

typedef struct {
    float x;
    float y;
    float width;
    float height;
} Rect;

typedef struct {
    uint32_t* pixels;
    float* mask;
    int width;
    int height;
    float scale;
} Canvas;

static inline Color colorFromRgb(uint32_t rgb) {
    return 0xFF000000u | rgb;
}

static inline Color colorWithAlpha(Color color, float alpha) {
    float base = (float)(color >> 24) / 255.0f;
    uint32_t value = (uint32_t)(base * alpha * 255.0f + 0.5f);
    return (value > 255 ? 255u : value) << 24 | (color & 0x00FFFFFFu);
}

static inline Rect rectInset(Rect rect, float dx, float dy) {
    return (Rect){rect.x + dx, rect.y + dy, rect.width - 2.0f * dx, rect.height - 2.0f * dy};
}

static inline bool rectContains(Rect rect, float x, float y) {
    return x >= rect.x && x < rect.x + rect.width && y >= rect.y && y < rect.y + rect.height;
}

bool canvasResize(Canvas* canvas, int logicalWidth, int logicalHeight, float scale);
void canvasRelease(Canvas* canvas);

void canvasClear(Canvas* canvas, Color color);
void canvasFillRect(Canvas* canvas, Rect rect, Color color);
void canvasFillRoundRect(Canvas* canvas, Rect rect, float radius, Color color);
void canvasStrokeRoundRect(Canvas* canvas, Rect rect, float radius, float thickness, Color color);
void canvasFillCircle(Canvas* canvas, Point center, float radius, Color color);
void canvasStrokeCircle(Canvas* canvas, Point center, float radius, float thickness, Color color);
void canvasStrokeArc(Canvas* canvas, Point center, float radius, float thickness, float startAngle, float sweep,
                     Color color);
void canvasStrokeLine(Canvas* canvas, Point from, Point to, float thickness, Color color);
void canvasStrokePolyline(Canvas* canvas, const Point* points, int count, float thickness, Color color);
void canvasDitherBelow(Canvas* canvas, const Point* points, int count, float bottom, float fadeDistance,
                       float edgeDensity, float floorDensity, Color color);
void canvasBlendCoverage(Canvas* canvas, int x, int y, float coverage, Color color);

#endif
