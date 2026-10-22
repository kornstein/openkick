#ifndef OPENKICK_FONT_H
#define OPENKICK_FONT_H

#include "canvas.h"

typedef struct Font Font;

typedef enum {
    TextAlignLeft,
    TextAlignCenter,
    TextAlignRight,
} TextAlign;

typedef struct {
    Font* font;
    float size;
    float tracking;
    Color color;
} TextStyle;

Font* fontCreate(const unsigned char* data);
void fontDestroy(Font* font);
float fontCapHeight(Font* font, float size);

float textMeasure(const TextStyle* style, const char* text);
void textDraw(Canvas* canvas, const TextStyle* style, float x, float baseline, TextAlign align, const char* text);

#endif
