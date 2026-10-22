#include "canvas.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265358979323846f

typedef struct {
    int left;
    int top;
    int right;
    int bottom;
} PixelBounds;

static inline float clampUnit(float value) {
    return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

static inline float coverageFromDistance(float distance) {
    return clampUnit(0.5f - distance);
}

bool canvasResize(Canvas* canvas, int logicalWidth, int logicalHeight, float scale) {
    int width = (int)ceilf(logicalWidth * scale);
    int height = (int)ceilf(logicalHeight * scale);
    if (canvas->pixels && canvas->width == width && canvas->height == height) {
        canvas->scale = scale;
        return true;
    }
    canvasRelease(canvas);
    size_t count = (size_t)width * (size_t)height;
    canvas->pixels = malloc(count * sizeof(uint32_t));
    canvas->mask = calloc(count, sizeof(float));
    if (!canvas->pixels || !canvas->mask) {
        canvasRelease(canvas);
        return false;
    }
    canvas->width = width;
    canvas->height = height;
    canvas->scale = scale;
    return true;
}

void canvasRelease(Canvas* canvas) {
    free(canvas->pixels);
    free(canvas->mask);
    memset(canvas, 0, sizeof(*canvas));
}

void canvasBlendCoverage(Canvas* canvas, int x, int y, float coverage, Color color) {
    if (x < 0 || y < 0 || x >= canvas->width || y >= canvas->height) {
        return;
    }
    uint32_t alpha = (uint32_t)((float)(color >> 24) * coverage * (256.0f / 255.0f) + 0.5f);
    if (alpha == 0) {
        return;
    }
    if (alpha > 256) {
        alpha = 256;
    }
    uint32_t* pixel = &canvas->pixels[(size_t)y * (size_t)canvas->width + (size_t)x];
    uint32_t inverse = 256 - alpha;
    uint32_t destination = *pixel;
    uint32_t red = (((color >> 16) & 0xFF) * alpha + ((destination >> 16) & 0xFF) * inverse) >> 8;
    uint32_t green = (((color >> 8) & 0xFF) * alpha + ((destination >> 8) & 0xFF) * inverse) >> 8;
    uint32_t blue = ((color & 0xFF) * alpha + (destination & 0xFF) * inverse) >> 8;
    *pixel = 0xFF000000u | red << 16 | green << 8 | blue;
}

static bool pixelBounds(const Canvas* canvas, float left, float top, float right, float bottom, PixelBounds* bounds) {
    bounds->left = (int)floorf(left);
    bounds->top = (int)floorf(top);
    bounds->right = (int)ceilf(right);
    bounds->bottom = (int)ceilf(bottom);
    if (bounds->left < 0) {
        bounds->left = 0;
    }
    if (bounds->top < 0) {
        bounds->top = 0;
    }
    if (bounds->right > canvas->width) {
        bounds->right = canvas->width;
    }
    if (bounds->bottom > canvas->height) {
        bounds->bottom = canvas->height;
    }
    return bounds->left < bounds->right && bounds->top < bounds->bottom;
}

void canvasClear(Canvas* canvas, Color color) {
    size_t count = (size_t)canvas->width * (size_t)canvas->height;
    for (size_t index = 0; index < count; ++index) {
        canvas->pixels[index] = color | 0xFF000000u;
    }
}

void canvasFillRect(Canvas* canvas, Rect rect, Color color) {
    float s = canvas->scale;
    float left = rect.x * s, top = rect.y * s;
    float right = (rect.x + rect.width) * s, bottom = (rect.y + rect.height) * s;
    PixelBounds bounds;
    if (!pixelBounds(canvas, left, top, right, bottom, &bounds)) {
        return;
    }
    for (int y = bounds.top; y < bounds.bottom; ++y) {
        float vertical = clampUnit(fminf(y + 1.0f, bottom) - fmaxf((float)y, top));
        for (int x = bounds.left; x < bounds.right; ++x) {
            float horizontal = clampUnit(fminf(x + 1.0f, right) - fmaxf((float)x, left));
            canvasBlendCoverage(canvas, x, y, vertical * horizontal, color);
        }
    }
}

static float roundRectDistance(float x, float y, float centerX, float centerY, float halfWidth, float halfHeight,
                               float radius) {
    float qx = fabsf(x - centerX) - (halfWidth - radius);
    float qy = fabsf(y - centerY) - (halfHeight - radius);
    float ox = fmaxf(qx, 0.0f);
    float oy = fmaxf(qy, 0.0f);
    return sqrtf(ox * ox + oy * oy) + fminf(fmaxf(qx, qy), 0.0f) - radius;
}

static void paintRoundRect(Canvas* canvas, Rect rect, float radius, float thickness, Color color) {
    float s = canvas->scale;
    float halfWidth = rect.width * s * 0.5f, halfHeight = rect.height * s * 0.5f;
    float centerX = rect.x * s + halfWidth, centerY = rect.y * s + halfHeight;
    float r = fminf(radius * s, fminf(halfWidth, halfHeight));
    float stroke = thickness * s * 0.5f;
    PixelBounds bounds;
    if (!pixelBounds(canvas, centerX - halfWidth - 1, centerY - halfHeight - 1, centerX + halfWidth + 1,
                     centerY + halfHeight + 1, &bounds)) {
        return;
    }
    for (int y = bounds.top; y < bounds.bottom; ++y) {
        for (int x = bounds.left; x < bounds.right; ++x) {
            float d = roundRectDistance(x + 0.5f, y + 0.5f, centerX, centerY, halfWidth, halfHeight, r);
            float coverage =
                thickness > 0.0f ? coverageFromDistance(fabsf(d + stroke) - stroke) : coverageFromDistance(d);
            canvasBlendCoverage(canvas, x, y, coverage, color);
        }
    }
}

void canvasFillRoundRect(Canvas* canvas, Rect rect, float radius, Color color) {
    paintRoundRect(canvas, rect, radius, 0.0f, color);
}

void canvasStrokeRoundRect(Canvas* canvas, Rect rect, float radius, float thickness, Color color) {
    paintRoundRect(canvas, rect, radius, thickness, color);
}

static void paintCircle(Canvas* canvas, Point center, float radius, float thickness, Color color) {
    float s = canvas->scale;
    float cx = center.x * s, cy = center.y * s, r = radius * s, stroke = thickness * s * 0.5f;
    float reach = r + stroke + 1.0f;
    PixelBounds bounds;
    if (!pixelBounds(canvas, cx - reach, cy - reach, cx + reach, cy + reach, &bounds)) {
        return;
    }
    for (int y = bounds.top; y < bounds.bottom; ++y) {
        for (int x = bounds.left; x < bounds.right; ++x) {
            float d = hypotf(x + 0.5f - cx, y + 0.5f - cy) - r;
            float coverage = thickness > 0.0f ? coverageFromDistance(fabsf(d) - stroke) : coverageFromDistance(d);
            canvasBlendCoverage(canvas, x, y, coverage, color);
        }
    }
}

void canvasFillCircle(Canvas* canvas, Point center, float radius, Color color) {
    paintCircle(canvas, center, radius, 0.0f, color);
}

void canvasStrokeCircle(Canvas* canvas, Point center, float radius, float thickness, Color color) {
    paintCircle(canvas, center, radius, thickness, color);
}

static float wrapAngle(float angle) {
    float turn = 2.0f * PI;
    angle = fmodf(angle, turn);
    return angle < 0.0f ? angle + turn : angle;
}

void canvasStrokeArc(Canvas* canvas, Point center, float radius, float thickness, float startAngle, float sweep,
                     Color color) {
    if (fabsf(sweep) < 1e-4f) {
        return;
    }
    float s = canvas->scale;
    float cx = center.x * s, cy = center.y * s, r = radius * s, stroke = thickness * s * 0.5f;
    float endAngle = startAngle + sweep;
    Point start = {cx + cosf(startAngle) * r, cy - sinf(startAngle) * r};
    Point end = {cx + cosf(endAngle) * r, cy - sinf(endAngle) * r};
    float reach = r + stroke + 1.0f;
    PixelBounds bounds;
    if (!pixelBounds(canvas, cx - reach, cy - reach, cx + reach, cy + reach, &bounds)) {
        return;
    }
    for (int y = bounds.top; y < bounds.bottom; ++y) {
        for (int x = bounds.left; x < bounds.right; ++x) {
            float px = x + 0.5f - cx, py = y + 0.5f - cy;
            float angle = atan2f(-py, px);
            float travelled = sweep < 0.0f ? wrapAngle(startAngle - angle) : wrapAngle(angle - startAngle);
            float d;
            if (travelled <= fabsf(sweep)) {
                d = fabsf(hypotf(px, py) - r);
            } else {
                float toStart = hypotf(x + 0.5f - start.x, y + 0.5f - start.y);
                float toEnd = hypotf(x + 0.5f - end.x, y + 0.5f - end.y);
                d = fminf(toStart, toEnd);
            }
            canvasBlendCoverage(canvas, x, y, coverageFromDistance(d - stroke), color);
        }
    }
}

static float segmentDistance(float x, float y, Point a, Point b) {
    float dx = b.x - a.x, dy = b.y - a.y;
    float lengthSquared = dx * dx + dy * dy;
    float t = lengthSquared > 0.0f ? clampUnit(((x - a.x) * dx + (y - a.y) * dy) / lengthSquared) : 0.0f;
    return hypotf(x - (a.x + t * dx), y - (a.y + t * dy));
}

void canvasStrokePolyline(Canvas* canvas, const Point* points, int count, float thickness, Color color) {
    if (count < 2) {
        return;
    }
    float s = canvas->scale;
    float stroke = thickness * s * 0.5f;
    PixelBounds dirty = {canvas->width, canvas->height, 0, 0};

    for (int index = 1; index < count; ++index) {
        Point a = {points[index - 1].x * s, points[index - 1].y * s};
        Point b = {points[index].x * s, points[index].y * s};
        PixelBounds bounds;
        if (!pixelBounds(canvas, fminf(a.x, b.x) - stroke - 1, fminf(a.y, b.y) - stroke - 1,
                         fmaxf(a.x, b.x) + stroke + 1, fmaxf(a.y, b.y) + stroke + 1, &bounds)) {
            continue;
        }
        for (int y = bounds.top; y < bounds.bottom; ++y) {
            float* row = &canvas->mask[(size_t)y * (size_t)canvas->width];
            for (int x = bounds.left; x < bounds.right; ++x) {
                float coverage = coverageFromDistance(segmentDistance(x + 0.5f, y + 0.5f, a, b) - stroke);
                if (coverage > row[x]) {
                    row[x] = coverage;
                }
            }
        }
        dirty.left = bounds.left < dirty.left ? bounds.left : dirty.left;
        dirty.top = bounds.top < dirty.top ? bounds.top : dirty.top;
        dirty.right = bounds.right > dirty.right ? bounds.right : dirty.right;
        dirty.bottom = bounds.bottom > dirty.bottom ? bounds.bottom : dirty.bottom;
    }

    for (int y = dirty.top; y < dirty.bottom; ++y) {
        float* row = &canvas->mask[(size_t)y * (size_t)canvas->width];
        for (int x = dirty.left; x < dirty.right; ++x) {
            if (row[x] > 0.0f) {
                canvasBlendCoverage(canvas, x, y, row[x], color);
                row[x] = 0.0f;
            }
        }
    }
}

void canvasStrokeLine(Canvas* canvas, Point from, Point to, float thickness, Color color) {
    Point points[2] = {from, to};
    canvasStrokePolyline(canvas, points, 2, thickness, color);
}

void canvasDitherBelow(Canvas* canvas, const Point* points, int count, float bottom, float fadeDistance,
                       float edgeDensity, float floorDensity, Color color) {
    static const float bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    if (count < 2) {
        return;
    }
    float s = canvas->scale;
    int cell = s < 1.5f ? 1 : (int)lroundf(s);
    int firstColumn = (int)ceilf(points[0].x * s);
    int lastColumn = (int)floorf(points[count - 1].x * s);
    int baseline = (int)floorf(bottom * s);
    float fade = fmaxf(fadeDistance * s, 1.0f);
    int segment = 1;

    for (int x = firstColumn < 0 ? 0 : firstColumn; x < lastColumn && x < canvas->width; ++x) {
        float logicalX = (x + 0.5f) / s;
        while (segment < count - 1 && points[segment].x < logicalX) {
            ++segment;
        }
        Point a = points[segment - 1], b = points[segment];
        float t = b.x > a.x ? clampUnit((logicalX - a.x) / (b.x - a.x)) : 0.0f;
        float top = (a.y + (b.y - a.y) * t) * s;
        for (int y = (int)ceilf(top); y < baseline && y < canvas->height; ++y) {
            float falloff = 1.0f - clampUnit((y - top) / fade);
            float density = floorDensity + (edgeDensity - floorDensity) * falloff * falloff;
            float threshold = (bayer[(y / cell) & 3][(x / cell) & 3] + 0.5f) / 16.0f;
            if (density > threshold) {
                canvasBlendCoverage(canvas, x, y, 1.0f, color);
            }
        }
    }
}
