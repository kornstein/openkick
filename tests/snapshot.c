#include <stdio.h>
#include <stdlib.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_WINDOWS_UTF8
#include "stb_image_write.h"

#include "ui/editor.h"

typedef struct {
    double params[ParamCount];
} FakeHost;

static double getParam(void* context, ParamId id) {
    return ((FakeHost*)context)->params[id];
}

static void beginEdit(void* context, ParamId id) {
    (void)context;
    (void)id;
}

static void performEdit(void* context, ParamId id, double value) {
    ((FakeHost*)context)->params[id] = value;
}

static void endEdit(void* context, ParamId id) {
    (void)context;
    (void)id;
}

static void readMeter(void* context, EditorMeter* meter) {
    FakeHost* host = context;
    ShaperSettings settings;
    settingsFromParams(host->params, &settings);
    meter->phase = 0.28f;
    meter->gain = (float)applyDepth(curveGain(settings.curve, meter->phase), settings.depth);
    meter->tempo = 124.0f;
    meter->followsHost = true;
    meter->hasSignal = true;
}

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "openkick.png";
    float scale = argc > 2 ? (float)atof(argv[2]) : 2.0f;

    FakeHost fake = {{0}};
    for (int id = 0; id < ParamCount; ++id) {
        fake.params[id] = paramInfos[id].defaultValue;
    }
    fake.params[ParamMix] = 0.85;

    EditorHost host = {&fake, getParam, beginEdit, performEdit, endEdit, readMeter};
    Editor* editor = editorCreate(&host);
    if (!editor) {
        fprintf(stderr, "snapshot: failed to create the editor\n");
        return 1;
    }
    editorMouseMove(editor, 420.0f, 470.0f);

    int width = 0, height = 0;
    const uint32_t* pixels = editorRender(editor, scale, &width, &height);
    if (!pixels) {
        fprintf(stderr, "snapshot: render failed\n");
        editorDestroy(editor);
        return 1;
    }

    unsigned char* rgba = malloc((size_t)width * (size_t)height * 4);
    for (int index = 0; index < width * height; ++index) {
        uint32_t pixel = pixels[index];
        rgba[index * 4 + 0] = (unsigned char)(pixel >> 16);
        rgba[index * 4 + 1] = (unsigned char)(pixel >> 8);
        rgba[index * 4 + 2] = (unsigned char)pixel;
        rgba[index * 4 + 3] = 0xFF;
    }
    int written = stbi_write_png(path, width, height, 4, rgba, width * 4);
    free(rgba);
    editorDestroy(editor);

    if (!written) {
        fprintf(stderr, "snapshot: cannot write %s\n", path);
        return 1;
    }
    printf("snapshot: %dx%d editor written to %s\n", width, height, path);
    return 0;
}
