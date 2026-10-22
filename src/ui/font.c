#include "font.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

enum { AsciiFirst = 32, AsciiCount = 95, ExtraCount = 4, GlyphCount = AsciiCount + ExtraCount, AtlasLimit = 16 };

static const int extraCodepoints[ExtraCount] = {0x00B7, 0x00D7, 0x2014, 0x2192};

typedef struct {
    float pixelSize;
    int width;
    int height;
    unsigned char* pixels;
    stbtt_packedchar glyphs[GlyphCount];
} Atlas;

struct Font {
    stbtt_fontinfo info;
    Atlas atlases[AtlasLimit];
    int atlasCount;
    int nextEviction;
};

Font* fontCreate(const unsigned char* data) {
    Font* font = calloc(1, sizeof(Font));
    if (!font) {
        return NULL;
    }
    if (!stbtt_InitFont(&font->info, data, stbtt_GetFontOffsetForIndex(data, 0))) {
        free(font);
        return NULL;
    }
    return font;
}

void fontDestroy(Font* font) {
    if (!font) {
        return;
    }
    for (int index = 0; index < font->atlasCount; ++index) {
        free(font->atlases[index].pixels);
    }
    free(font);
}

static bool packAtlas(Font* font, Atlas* atlas) {
    stbtt_pack_range ranges[2] = {
        {STBTT_POINT_SIZE(atlas->pixelSize), AsciiFirst, NULL, AsciiCount, atlas->glyphs, 0, 0},
        {STBTT_POINT_SIZE(atlas->pixelSize), 0, (int*)extraCodepoints, ExtraCount, atlas->glyphs + AsciiCount, 0, 0},
    };
    int cell = (int)ceilf(atlas->pixelSize * 1.4f) + 2;
    atlas->width = 16 * cell;
    atlas->height = 4 * cell;

    for (int attempt = 0; attempt < 4; ++attempt, atlas->height *= 2) {
        free(atlas->pixels);
        atlas->pixels = calloc((size_t)atlas->width * (size_t)atlas->height, 1);
        if (!atlas->pixels) {
            return false;
        }
        stbtt_pack_context context;
        if (!stbtt_PackBegin(&context, atlas->pixels, atlas->width, atlas->height, 0, 1, NULL)) {
            return false;
        }
        stbtt_PackSetOversampling(&context, 1, 1);
        int packed = stbtt_PackFontRanges(&context, font->info.data, 0, ranges, 2);
        stbtt_PackEnd(&context);
        if (packed) {
            return true;
        }
    }
    return false;
}

static Atlas* atlasForSize(Font* font, float pixelSize) {
    pixelSize = roundf(pixelSize * 4.0f) / 4.0f;
    for (int index = 0; index < font->atlasCount; ++index) {
        if (font->atlases[index].pixelSize == pixelSize) {
            return &font->atlases[index];
        }
    }
    Atlas* atlas;
    if (font->atlasCount < AtlasLimit) {
        atlas = &font->atlases[font->atlasCount++];
    } else {
        atlas = &font->atlases[font->nextEviction];
        font->nextEviction = (font->nextEviction + 1) % AtlasLimit;
        free(atlas->pixels);
    }
    memset(atlas, 0, sizeof(*atlas));
    atlas->pixelSize = pixelSize;
    if (!packAtlas(font, atlas)) {
        free(atlas->pixels);
        atlas->pixels = NULL;
    }
    return atlas;
}

static int decodeUtf8(const char** cursor) {
    const unsigned char* bytes = (const unsigned char*)*cursor;
    int codepoint = bytes[0];
    int length = 1;
    if (codepoint >= 0xF0 && bytes[1] && bytes[2] && bytes[3]) {
        codepoint = (bytes[0] & 0x07) << 18 | (bytes[1] & 0x3F) << 12 | (bytes[2] & 0x3F) << 6 | (bytes[3] & 0x3F);
        length = 4;
    } else if (codepoint >= 0xE0 && bytes[1] && bytes[2]) {
        codepoint = (bytes[0] & 0x0F) << 12 | (bytes[1] & 0x3F) << 6 | (bytes[2] & 0x3F);
        length = 3;
    } else if (codepoint >= 0xC0 && bytes[1]) {
        codepoint = (bytes[0] & 0x1F) << 6 | (bytes[1] & 0x3F);
        length = 2;
    }
    *cursor += length;
    return codepoint;
}

static int glyphIndex(int codepoint) {
    if (codepoint >= AsciiFirst && codepoint < AsciiFirst + AsciiCount) {
        return codepoint - AsciiFirst;
    }
    for (int index = 0; index < ExtraCount; ++index) {
        if (extraCodepoints[index] == codepoint) {
            return AsciiCount + index;
        }
    }
    return -1;
}

float fontCapHeight(Font* font, float size) {
    int top = 0;
    stbtt_GetCodepointBox(&font->info, 'H', NULL, NULL, NULL, &top);
    return top * stbtt_ScaleForMappingEmToPixels(&font->info, size);
}

float textMeasure(const TextStyle* style, const char* text) {
    float scale = stbtt_ScaleForMappingEmToPixels(&style->font->info, style->size);
    float width = 0.0f;
    int glyphs = 0;
    while (*text) {
        int codepoint = decodeUtf8(&text);
        if (glyphIndex(codepoint) < 0) {
            continue;
        }
        int advance = 0;
        stbtt_GetCodepointHMetrics(&style->font->info, codepoint, &advance, NULL);
        width += advance * scale;
        ++glyphs;
    }
    return glyphs > 0 ? width + style->tracking * (glyphs - 1) : 0.0f;
}

static void blitGlyph(Canvas* canvas, const Atlas* atlas, const stbtt_aligned_quad* quad, Color color) {
    int left = (int)quad->x0, top = (int)quad->y0;
    int width = (int)(quad->x1 - quad->x0), height = (int)(quad->y1 - quad->y0);
    int sourceX = (int)(quad->s0 * atlas->width + 0.5f);
    int sourceY = (int)(quad->t0 * atlas->height + 0.5f);
    for (int row = 0; row < height; ++row) {
        const unsigned char* source = &atlas->pixels[(size_t)(sourceY + row) * (size_t)atlas->width + sourceX];
        for (int column = 0; column < width; ++column) {
            if (source[column]) {
                canvasBlendCoverage(canvas, left + column, top + row, source[column] / 255.0f, color);
            }
        }
    }
}

void textDraw(Canvas* canvas, const TextStyle* style, float x, float baseline, TextAlign align, const char* text) {
    Atlas* atlas = atlasForSize(style->font, style->size * canvas->scale);
    if (!atlas->pixels) {
        return;
    }
    if (align != TextAlignLeft) {
        float width = textMeasure(style, text);
        x -= align == TextAlignCenter ? width * 0.5f : width;
    }
    float penX = roundf(x * canvas->scale);
    float penY = roundf(baseline * canvas->scale);
    while (*text) {
        int index = glyphIndex(decodeUtf8(&text));
        if (index < 0) {
            continue;
        }
        stbtt_aligned_quad quad;
        stbtt_GetPackedQuad(atlas->glyphs, atlas->width, atlas->height, index, &penX, &penY, &quad, 1);
        blitGlyph(canvas, atlas, &quad, style->color);
        penX += style->tracking * canvas->scale;
    }
}
