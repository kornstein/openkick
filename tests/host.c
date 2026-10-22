#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "params.h"
#include "vst3_c_api.h"

enum { BlockSize = 256, MaxQueues = 8 };

static const double sampleRate = 48000.0;

static int failureCount;

static void expect(bool condition, const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    printf("  %s  ", condition ? "pass" : "FAIL");
    vprintf(format, arguments);
    printf("\n");
    va_end(arguments);
    if (!condition) {
        ++failureCount;
    }
}

typedef struct {
    void* handle;
    Steinberg_IPluginFactory* (*getFactory)(void);
    bool (*enter)(void*);
    bool (*leave)(void);
} Module;

static void* moduleSymbol(void* handle, const char* name) {
#if defined(_WIN32)
    return (void*)GetProcAddress((HMODULE)handle, name);
#else
    return dlsym(handle, name);
#endif
}

static bool moduleOpen(Module* module, const char* path) {
#if defined(_WIN32)
    module->handle = LoadLibraryA(path);
    const char* enterName = "InitDll";
    const char* leaveName = "ExitDll";
#else
    module->handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    const char* enterName = "bundleEntry";
    const char* leaveName = "bundleExit";
#endif
    if (!module->handle) {
        return false;
    }
    module->getFactory = (Steinberg_IPluginFactory * (*)(void)) moduleSymbol(module->handle, "GetPluginFactory");
    module->enter = (bool (*)(void*))moduleSymbol(module->handle, enterName);
    module->leave = (bool (*)(void))moduleSymbol(module->handle, leaveName);
    return true;
}

static void moduleClose(Module* module) {
#if defined(_WIN32)
    FreeLibrary((HMODULE)module->handle);
#else
    dlclose(module->handle);
#endif
}

typedef struct {
    Steinberg_Vst_IParamValueQueue queue;
    Steinberg_Vst_ParamID id;
    double value;
} ValueQueue;

typedef struct {
    Steinberg_Vst_IParameterChanges changes;
    ValueQueue queues[MaxQueues];
    int count;
} ParameterChanges;

static Steinberg_tresult SMTG_STDMETHODCALLTYPE noInterface(void* self, const Steinberg_TUID iid, void** object) {
    (void)self;
    (void)iid;
    *object = NULL;
    return Steinberg_kNoInterface;
}

static Steinberg_uint32 SMTG_STDMETHODCALLTYPE staticReference(void* self) {
    (void)self;
    return 1;
}

static Steinberg_Vst_ParamID SMTG_STDMETHODCALLTYPE queueParameterId(void* self) {
    return ((ValueQueue*)self)->id;
}

static Steinberg_int32 SMTG_STDMETHODCALLTYPE queuePointCount(void* self) {
    (void)self;
    return 1;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE queueGetPoint(void* self, Steinberg_int32 index,
                                                              Steinberg_int32* offset,
                                                              Steinberg_Vst_ParamValue* value) {
    (void)index;
    *offset = 0;
    *value = ((ValueQueue*)self)->value;
    return Steinberg_kResultOk;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE queueAddPoint(void* self, Steinberg_int32 offset,
                                                              Steinberg_Vst_ParamValue value, Steinberg_int32* index) {
    (void)self;
    (void)offset;
    (void)value;
    (void)index;
    return Steinberg_kResultFalse;
}

static Steinberg_int32 SMTG_STDMETHODCALLTYPE changesCount(void* self) {
    return ((ParameterChanges*)self)->count;
}

static struct Steinberg_Vst_IParamValueQueue* SMTG_STDMETHODCALLTYPE changesData(void* self, Steinberg_int32 index) {
    return &((ParameterChanges*)self)->queues[index].queue;
}

static struct Steinberg_Vst_IParamValueQueue* SMTG_STDMETHODCALLTYPE changesAdd(void* self,
                                                                                const Steinberg_Vst_ParamID* id,
                                                                                Steinberg_int32* index) {
    (void)self;
    (void)id;
    (void)index;
    return NULL;
}

static Steinberg_Vst_IParamValueQueueVtbl queueVtbl = {
    noInterface, staticReference, staticReference, queueParameterId, queuePointCount, queueGetPoint, queueAddPoint,
};

static Steinberg_Vst_IParameterChangesVtbl changesVtbl = {
    noInterface, staticReference, staticReference, changesCount, changesData, changesAdd,
};

static void changesPush(ParameterChanges* changes, ParamId id, double value) {
    changes->changes.lpVtbl = &changesVtbl;
    ValueQueue* queue = &changes->queues[changes->count++];
    queue->queue.lpVtbl = &queueVtbl;
    queue->id = (Steinberg_Vst_ParamID)id;
    queue->value = value;
}

typedef struct {
    Steinberg_IBStream stream;
    unsigned char bytes[1024];
    int size;
    int position;
} MemoryStream;

static Steinberg_tresult SMTG_STDMETHODCALLTYPE streamRead(void* self, void* buffer, Steinberg_int32 count,
                                                           Steinberg_int32* read) {
    MemoryStream* stream = self;
    int available = stream->size - stream->position;
    int length = count < available ? count : available;
    memcpy(buffer, stream->bytes + stream->position, (size_t)length);
    stream->position += length;
    if (read) {
        *read = length;
    }
    return Steinberg_kResultOk;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE streamWrite(void* self, void* buffer, Steinberg_int32 count,
                                                            Steinberg_int32* written) {
    MemoryStream* stream = self;
    if (stream->position + count > (int)sizeof(stream->bytes)) {
        return Steinberg_kResultFalse;
    }
    memcpy(stream->bytes + stream->position, buffer, (size_t)count);
    stream->position += count;
    stream->size = stream->position > stream->size ? stream->position : stream->size;
    if (written) {
        *written = count;
    }
    return Steinberg_kResultOk;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE streamSeek(void* self, Steinberg_int64 position, Steinberg_int32 mode,
                                                           Steinberg_int64* result) {
    (void)mode;
    ((MemoryStream*)self)->position = (int)position;
    if (result) {
        *result = position;
    }
    return Steinberg_kResultOk;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE streamTell(void* self, Steinberg_int64* position) {
    *position = ((MemoryStream*)self)->position;
    return Steinberg_kResultOk;
}

static Steinberg_IBStreamVtbl streamVtbl = {
    noInterface, staticReference, staticReference, streamRead, streamWrite, streamSeek, streamTell,
};

typedef struct {
    Steinberg_Vst_IAudioProcessor* processor;
    double position;
    double tempo;
    bool doublePrecision;
} Session;

static int runBeat(Session* session, ParameterChanges* changes, double* gains) {
    int total = (int)(sampleRate * 60.0 / session->tempo);
    float input32[2][BlockSize], output32[2][BlockSize];
    double input64[2][BlockSize], output64[2][BlockSize];
    float* inputs32[2] = {input32[0], input32[1]};
    float* outputs32[2] = {output32[0], output32[1]};
    double* inputs64[2] = {input64[0], input64[1]};
    double* outputs64[2] = {output64[0], output64[1]};
    for (int frame = 0; frame < BlockSize; ++frame) {
        input32[0][frame] = input32[1][frame] = 1.0f;
        input64[0][frame] = input64[1][frame] = 1.0;
    }

    for (int done = 0; done < total; done += BlockSize) {
        int frames = total - done < BlockSize ? total - done : BlockSize;
        struct Steinberg_Vst_AudioBusBuffers input = {0}, output = {0};
        input.numChannels = output.numChannels = 2;
        if (session->doublePrecision) {
            input.Steinberg_Vst_AudioBusBuffers_channelBuffers64 = inputs64;
            output.Steinberg_Vst_AudioBusBuffers_channelBuffers64 = outputs64;
        } else {
            input.Steinberg_Vst_AudioBusBuffers_channelBuffers32 = inputs32;
            output.Steinberg_Vst_AudioBusBuffers_channelBuffers32 = outputs32;
        }
        struct Steinberg_Vst_ProcessContext context;
        memset(&context, 0, sizeof(context));
        context.state = Steinberg_Vst_ProcessContext_StatesAndFlags_kPlaying |
                        Steinberg_Vst_ProcessContext_StatesAndFlags_kTempoValid |
                        Steinberg_Vst_ProcessContext_StatesAndFlags_kProjectTimeMusicValid;
        context.sampleRate = sampleRate;
        context.tempo = session->tempo;
        context.projectTimeMusic = session->position;

        struct Steinberg_Vst_ProcessData data;
        memset(&data, 0, sizeof(data));
        data.symbolicSampleSize = session->doublePrecision ? Steinberg_Vst_SymbolicSampleSizes_kSample64
                                                           : Steinberg_Vst_SymbolicSampleSizes_kSample32;
        data.numSamples = frames;
        data.numInputs = data.numOutputs = 1;
        data.inputs = &input;
        data.outputs = &output;
        data.processContext = &context;
        data.inputParameterChanges = done == 0 && changes ? &changes->changes : NULL;
        if (session->processor->lpVtbl->process(session->processor, &data) != Steinberg_kResultOk) {
            return -1;
        }
        for (int frame = 0; frame < frames; ++frame) {
            gains[done + frame] = session->doublePrecision ? output64[0][frame] : output32[0][frame];
        }
        session->position += frames * session->tempo / 60.0 / sampleRate;
    }
    return total;
}

static void range(const double* values, int from, int to, double* minimum, double* maximum) {
    *minimum = INFINITY;
    *maximum = -INFINITY;
    for (int index = from; index < to; ++index) {
        *minimum = fmin(*minimum, values[index]);
        *maximum = fmax(*maximum, values[index]);
    }
}

static void testFactory(Steinberg_IPluginFactory* factory, struct Steinberg_PClassInfo* classInfo) {
    printf("Factory\n");
    struct Steinberg_PFactoryInfo factoryInfo;
    factory->lpVtbl->getFactoryInfo(factory, &factoryInfo);
    expect(strcmp(factoryInfo.vendor, "kornstein") == 0, "vendor is %s", factoryInfo.vendor);
    expect(factory->lpVtbl->countClasses(factory) == 1, "exposes a single class");
    factory->lpVtbl->getClassInfo(factory, 0, classInfo);
    expect(strcmp(classInfo->category, "Audio Module Class") == 0, "class %s is an audio module", classInfo->name);

    Steinberg_IPluginFactory2* factory2 = NULL;
    factory->lpVtbl->queryInterface(factory, Steinberg_IPluginFactory2_iid, (void**)&factory2);
    struct Steinberg_PClassInfo2 classInfo2;
    bool hasInfo2 = factory2 && factory2->lpVtbl->getClassInfo2(factory2, 0, &classInfo2) == Steinberg_kResultOk;
    expect(hasInfo2 && strcmp(classInfo2.vendor, "kornstein") == 0, "IPluginFactory2 reports %s by %s, version %s",
           hasInfo2 ? classInfo2.subCategories : "?", hasInfo2 ? classInfo2.vendor : "?",
           hasInfo2 ? classInfo2.version : "?");

    Steinberg_IPluginFactory3* factory3 = NULL;
    factory->lpVtbl->queryInterface(factory, Steinberg_IPluginFactory3_iid, (void**)&factory3);
    struct Steinberg_PClassInfoW classInfoW;
    bool hasInfoW = factory3 && factory3->lpVtbl->getClassInfoUnicode(factory3, 0, &classInfoW) == Steinberg_kResultOk;
    char vendor[64] = "?";
    if (hasInfoW) {
        int length;
        for (length = 0; classInfoW.vendor[length] && length < 63; ++length) {
            vendor[length] = (char)classInfoW.vendor[length];
        }
        vendor[length] = 0;
    }
    expect(hasInfoW && strcmp(vendor, "kornstein") == 0, "IPluginFactory3 reports vendor %s in UTF-16", vendor);
}

static void testParameters(Steinberg_Vst_IEditController* controller) {
    printf("Parameters\n");
    expect(controller->lpVtbl->getParameterCount(controller) == ParamCount, "%d parameters", ParamCount);
    for (int index = 0; index < ParamCount; ++index) {
        struct Steinberg_Vst_ParameterInfo info;
        controller->lpVtbl->getParameterInfo(controller, index, &info);
        Steinberg_Vst_String128 text;
        controller->lpVtbl->getParamStringByValue(controller, info.id, info.defaultNormalizedValue, text);
        Steinberg_Vst_ParamValue parsed = -1.0;
        controller->lpVtbl->getParamValueByString(controller, info.id, text, &parsed);
        char title[64], value[64];
        int length;
        for (length = 0; info.title[length] && length < 63; ++length) {
            title[length] = (char)info.title[length];
        }
        title[length] = 0;
        for (length = 0; text[length] && length < 63; ++length) {
            value[length] = (char)text[length];
        }
        value[length] = 0;
        expect(fabs(parsed - info.defaultNormalizedValue) < 1e-9, "%-6s default \"%s\" round-trips through text", title,
               value);
    }
}

static void testProcessing(Steinberg_Vst_IComponent* component, Steinberg_Vst_IAudioProcessor* processor,
                           bool doublePrecision) {
    printf("Processing (%s)\n", doublePrecision ? "64-bit" : "32-bit");
    Steinberg_Vst_SpeakerArrangement stereo = Steinberg_Vst_SpeakerArr_kStereo;
    expect(processor->lpVtbl->setBusArrangements(processor, &stereo, 1, &stereo, 1) == Steinberg_kResultTrue,
           "accepts stereo in and out");
    Steinberg_int32 sampleSize =
        doublePrecision ? Steinberg_Vst_SymbolicSampleSizes_kSample64 : Steinberg_Vst_SymbolicSampleSizes_kSample32;
    expect(processor->lpVtbl->canProcessSampleSize(processor, sampleSize) == Steinberg_kResultTrue,
           "supports the sample size");
    struct Steinberg_Vst_ProcessSetup setup = {0, sampleSize, BlockSize, sampleRate};
    processor->lpVtbl->setupProcessing(processor, &setup);
    component->lpVtbl->setActive(component, 1);
    processor->lpVtbl->setProcessing(processor, 1);

    Session session = {processor, 0.0, 120.0, doublePrecision};
    double* gains = malloc(sizeof(double) * (size_t)sampleRate);
    double minimum, maximum;
    ParameterChanges defaults = {0};
    changesPush(&defaults, ParamMix, 1.0);
    changesPush(&defaults, ParamCurve, 0.0);
    changesPush(&defaults, ParamRate, paramFromStep(ParamRate, 2));
    changesPush(&defaults, ParamBypass, 0.0);
    runBeat(&session, &defaults, gains);
    int frames = runBeat(&session, NULL, gains);

    range(gains, 0, frames / 20, &minimum, &maximum);
    expect(minimum < 0.05, "ducks at the start of each beat (min %.3f)", minimum);
    range(gains, (int)(frames * 0.62), frames, &minimum, &maximum);
    expect(minimum > 0.99, "recovers to unity before the next beat (min %.3f)", minimum);
    double expected = curveGain(0, 0.25);
    expect(fabs(gains[frames / 4] - expected) < 0.02, "follows the Classic curve (%.3f vs %.3f)", gains[frames / 4],
           expected);
    double largestStep = 0.0;
    for (int frame = 1; frame < frames; ++frame) {
        largestStep = fmax(largestStep, fabs(gains[frame] - gains[frame - 1]));
    }
    expect(largestStep < 0.06, "never steps faster than the 0.4 ms attack ramp (largest step %.4f)", largestStep);

    ParameterChanges halfMix = {0};
    changesPush(&halfMix, ParamMix, 0.5);
    runBeat(&session, &halfMix, gains);
    frames = runBeat(&session, NULL, gains);
    range(gains, 0, frames, &minimum, &maximum);
    expect(fabs(minimum - 0.5) < 0.03, "mix 50%% limits the duck (min %.3f)", minimum);

    ParameterChanges eighths = {0};
    changesPush(&eighths, ParamMix, 1.0);
    changesPush(&eighths, ParamRate, paramFromStep(ParamRate, 3));
    runBeat(&session, &eighths, gains);
    frames = runBeat(&session, NULL, gains);
    range(gains, frames / 2, frames / 2 + frames / 40, &minimum, &maximum);
    expect(minimum < 0.05, "rate 1/8 ducks on the off-beat (min %.3f)", minimum);

    ParameterChanges bypass = {0};
    changesPush(&bypass, ParamBypass, 1.0);
    runBeat(&session, &bypass, gains);
    frames = runBeat(&session, NULL, gains);
    range(gains, 0, frames, &minimum, &maximum);
    expect(minimum > 0.999, "bypass leaves the signal untouched (min %.4f)", minimum);

    processor->lpVtbl->setProcessing(processor, 0);
    component->lpVtbl->setActive(component, 0);
    free(gains);
}

static void testState(Steinberg_IPluginFactory* factory, const struct Steinberg_PClassInfo* classInfo,
                      Steinberg_Vst_IEditController* controller, Steinberg_Vst_IComponent* component) {
    printf("State\n");
    controller->lpVtbl->setParamNormalized(controller, ParamMix, 0.73);
    controller->lpVtbl->setParamNormalized(controller, ParamCurve, paramFromStep(ParamCurve, 5));
    MemoryStream stream = {{&streamVtbl}, {0}, 0, 0};
    expect(component->lpVtbl->getState(component, &stream.stream) == Steinberg_kResultOk, "saves %d bytes",
           stream.size);

    Steinberg_Vst_IComponent* restored = NULL;
    factory->lpVtbl->createInstance(factory, (Steinberg_FIDString)classInfo->cid,
                                    (Steinberg_FIDString)Steinberg_Vst_IComponent_iid, (void**)&restored);
    restored->lpVtbl->initialize(restored, NULL);
    stream.position = 0;
    expect(restored->lpVtbl->setState(restored, &stream.stream) == Steinberg_kResultOk, "loads into a new instance");
    Steinberg_Vst_IEditController* restoredController = NULL;
    restored->lpVtbl->queryInterface(restored, Steinberg_Vst_IEditController_iid, (void**)&restoredController);
    double mix = restoredController->lpVtbl->getParamNormalized(restoredController, ParamMix);
    int curve = paramStep(ParamCurve, restoredController->lpVtbl->getParamNormalized(restoredController, ParamCurve));
    expect(fabs(mix - 0.73) < 1e-9 && curve == 5, "restores mix %.2f and curve %s", mix, curveShapes[curve].name);
    restoredController->lpVtbl->release(restoredController);
    restored->lpVtbl->terminate(restored);
    restored->lpVtbl->release(restored);
}

static void testEditor(Steinberg_Vst_IEditController* controller) {
    printf("Editor\n");
    Steinberg_IPlugView* view = controller->lpVtbl->createView(controller, "editor");
    expect(view != NULL, "creates the editor view");
    if (!view) {
        return;
    }
    struct Steinberg_ViewRect size;
    view->lpVtbl->getSize(view, &size);
#if defined(_WIN32)
    const char* platformType = "HWND";
#else
    const char* platformType = "NSView";
#endif
    expect(view->lpVtbl->isPlatformTypeSupported(view, platformType) == Steinberg_kResultTrue, "supports %s at %dx%d",
           platformType, size.right - size.left, size.bottom - size.top);
    view->lpVtbl->release(view);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <plugin binary>\n", argv[0]);
        return 2;
    }
    Module module = {0};
    if (!moduleOpen(&module, argv[1])) {
        fprintf(stderr, "cannot load %s\n", argv[1]);
        return 1;
    }
    printf("Module\n");
    expect(module.getFactory && module.enter && module.leave, "exports the VST3 entry points");
    if (!module.getFactory || !module.enter || !module.leave) {
        return 1;
    }
    expect(module.enter(NULL), "module entry succeeds");

    Steinberg_IPluginFactory* factory = module.getFactory();
    struct Steinberg_PClassInfo classInfo;
    testFactory(factory, &classInfo);

    Steinberg_Vst_IComponent* component = NULL;
    factory->lpVtbl->createInstance(factory, (Steinberg_FIDString)classInfo.cid,
                                    (Steinberg_FIDString)Steinberg_Vst_IComponent_iid, (void**)&component);
    expect(component != NULL, "creates a component");
    if (!component) {
        return 1;
    }
    component->lpVtbl->initialize(component, NULL);
    Steinberg_Vst_IAudioProcessor* processor = NULL;
    Steinberg_Vst_IEditController* controller = NULL;
    component->lpVtbl->queryInterface(component, Steinberg_Vst_IAudioProcessor_iid, (void**)&processor);
    component->lpVtbl->queryInterface(component, Steinberg_Vst_IEditController_iid, (void**)&controller);
    expect(processor && controller, "exposes processor and controller on the same object");
    if (!processor || !controller) {
        return 1;
    }
    controller->lpVtbl->initialize(controller, NULL);

    testParameters(controller);
    testProcessing(component, processor, false);
    testProcessing(component, processor, true);
    testState(factory, &classInfo, controller, component);
    testEditor(controller);

    controller->lpVtbl->terminate(controller);
    component->lpVtbl->terminate(component);
    processor->lpVtbl->release(processor);
    controller->lpVtbl->release(controller);
    expect(component->lpVtbl->release(component) == 0, "releases the instance cleanly");
    module.leave();
    moduleClose(&module);

    printf(failureCount ? "\n%d check(s) failed\n" : "\nAll checks passed\n", failureCount);
    return failureCount ? 1 : 0;
}
