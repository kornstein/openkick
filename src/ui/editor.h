#ifndef OPENKICK_EDITOR_H
#define OPENKICK_EDITOR_H

#include <stdbool.h>
#include <stdint.h>

#include "params.h"

enum { EditorWidth = 760, EditorHeight = 540 };

typedef enum {
    EditorModifierNone = 0,
    EditorModifierFine = 1 << 0,
} EditorModifiers;

typedef struct {
    float phase;
    float gain;
    float tempo;
    bool followsHost;
    bool hasSignal;
} EditorMeter;

typedef struct {
    void* context;
    double (*getParam)(void* context, ParamId id);
    void (*beginEdit)(void* context, ParamId id);
    void (*performEdit)(void* context, ParamId id, double value);
    void (*endEdit)(void* context, ParamId id);
    void (*readMeter)(void* context, EditorMeter* meter);
} EditorHost;

typedef struct Editor Editor;

Editor* editorCreate(const EditorHost* host);
void editorDestroy(Editor* editor);

const uint32_t* editorRender(Editor* editor, float scale, int* width, int* height);

void editorMouseDown(Editor* editor, float x, float y, int clickCount, EditorModifiers modifiers);
void editorMouseDrag(Editor* editor, float x, float y, EditorModifiers modifiers);
void editorMouseUp(Editor* editor);
void editorMouseMove(Editor* editor, float x, float y);
void editorMouseExit(Editor* editor);
void editorMouseWheel(Editor* editor, float x, float y, float delta, EditorModifiers modifiers);

#endif
