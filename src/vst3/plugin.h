#ifndef OPENKICK_PLUGIN_H
#define OPENKICK_PLUGIN_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>

#include "dsp/shaper.h"
#include "params.h"
#include "vst3_c_api.h"

typedef struct {
    _Atomic float phase;
    _Atomic float gain;
    _Atomic float tempo;
    atomic_bool followsHost;
    atomic_bool hasSignal;
} PluginMeter;

typedef struct {
    Steinberg_Vst_IComponent component;
    Steinberg_Vst_IAudioProcessor processor;
    Steinberg_Vst_IEditController controller;
    Steinberg_Vst_IProcessContextRequirements contextRequirements;
    atomic_uint referenceCount;
    _Atomic double params[ParamCount];
    Steinberg_Vst_IComponentHandler* componentHandler;
    Shaper shaper;
    Steinberg_int32 channelCount;
    double silentSeconds;
    PluginMeter meter;
} Plugin;

#define PLUGIN_FROM(pointer, member) ((Plugin*)((char*)(pointer) - offsetof(Plugin, member)))

#define PLUGIN_UNKNOWN_METHODS(prefix, member)                                                                         \
    static Steinberg_tresult SMTG_STDMETHODCALLTYPE prefix##QueryInterface(void* self, const Steinberg_TUID iid,       \
                                                                           void** object) {                            \
        return pluginQueryInterface(PLUGIN_FROM(self, member), iid, object);                                           \
    }                                                                                                                  \
    static Steinberg_uint32 SMTG_STDMETHODCALLTYPE prefix##AddRef(void* self) {                                        \
        return pluginAddRef(PLUGIN_FROM(self, member));                                                                \
    }                                                                                                                  \
    static Steinberg_uint32 SMTG_STDMETHODCALLTYPE prefix##Release(void* self) {                                       \
        return pluginRelease(PLUGIN_FROM(self, member));                                                               \
    }

extern const Steinberg_TUID pluginClassId;
extern Steinberg_Vst_IComponentVtbl componentVtbl;
extern Steinberg_Vst_IAudioProcessorVtbl processorVtbl;
extern Steinberg_Vst_IEditControllerVtbl controllerVtbl;
extern Steinberg_Vst_IProcessContextRequirementsVtbl contextRequirementsVtbl;

Plugin* pluginCreate(void);
Steinberg_tresult pluginQueryInterface(Plugin* plugin, const Steinberg_TUID iid, void** object);
Steinberg_uint32 pluginAddRef(Plugin* plugin);
Steinberg_uint32 pluginRelease(Plugin* plugin);

double pluginGetParam(Plugin* plugin, ParamId id);
void pluginSetParam(Plugin* plugin, ParamId id, double value);
void pluginSetComponentHandler(Plugin* plugin, Steinberg_Vst_IComponentHandler* handler);

Steinberg_tresult pluginWriteState(Plugin* plugin, struct Steinberg_IBStream* stream);
Steinberg_tresult pluginReadState(Plugin* plugin, struct Steinberg_IBStream* stream);

struct Steinberg_IPlugView* plugViewCreate(Plugin* plugin);

bool uidEquals(const void* a, const void* b);
void utf16FromAscii(Steinberg_char16* destination, const char* source, int capacity);
void asciiFromUtf16(char* destination, const Steinberg_char16* source, int capacity);

#endif
