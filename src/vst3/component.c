#include "plugin.h"

#include <string.h>

PLUGIN_UNKNOWN_METHODS(component, component)

static Steinberg_tresult SMTG_STDMETHODCALLTYPE componentInitialize(void* self, struct Steinberg_FUnknown* context) {
    (void)self;
    (void)context;
    return Steinberg_kResultOk;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE componentTerminate(void* self) {
    (void)self;
    return Steinberg_kResultOk;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE componentGetControllerClassId(void* self, Steinberg_TUID classId) {
    (void)self;
    (void)classId;
    return Steinberg_kNotImplemented;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE componentSetIoMode(void* self, Steinberg_Vst_IoMode mode) {
    (void)self;
    (void)mode;
    return Steinberg_kResultOk;
}

static Steinberg_int32 SMTG_STDMETHODCALLTYPE componentGetBusCount(void* self, Steinberg_Vst_MediaType type,
                                                                   Steinberg_Vst_BusDirection direction) {
    (void)self;
    (void)direction;
    return type == Steinberg_Vst_MediaTypes_kAudio ? 1 : 0;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE componentGetBusInfo(void* self, Steinberg_Vst_MediaType type,
                                                                    Steinberg_Vst_BusDirection direction,
                                                                    Steinberg_int32 index,
                                                                    struct Steinberg_Vst_BusInfo* bus) {
    Plugin* plugin = PLUGIN_FROM(self, component);
    if (type != Steinberg_Vst_MediaTypes_kAudio || index != 0 || !bus) {
        return Steinberg_kInvalidArgument;
    }
    memset(bus, 0, sizeof(*bus));
    bus->mediaType = type;
    bus->direction = direction;
    bus->channelCount = plugin->channelCount;
    bus->busType = Steinberg_Vst_BusTypes_kMain;
    bus->flags = Steinberg_Vst_BusInfo_BusFlags_kDefaultActive;
    utf16FromAscii(bus->name, direction == Steinberg_Vst_BusDirections_kInput ? "Input" : "Output", 128);
    return Steinberg_kResultOk;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE componentGetRoutingInfo(void* self,
                                                                        struct Steinberg_Vst_RoutingInfo* input,
                                                                        struct Steinberg_Vst_RoutingInfo* output) {
    (void)self;
    (void)input;
    (void)output;
    return Steinberg_kResultFalse;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE componentActivateBus(void* self, Steinberg_Vst_MediaType type,
                                                                     Steinberg_Vst_BusDirection direction,
                                                                     Steinberg_int32 index, Steinberg_TBool state) {
    (void)self;
    (void)direction;
    (void)state;
    return type == Steinberg_Vst_MediaTypes_kAudio && index == 0 ? Steinberg_kResultOk : Steinberg_kInvalidArgument;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE componentSetActive(void* self, Steinberg_TBool state) {
    if (state) {
        shaperReset(&PLUGIN_FROM(self, component)->shaper);
    }
    return Steinberg_kResultOk;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE componentSetState(void* self, struct Steinberg_IBStream* state) {
    return pluginReadState(PLUGIN_FROM(self, component), state);
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE componentGetState(void* self, struct Steinberg_IBStream* state) {
    return pluginWriteState(PLUGIN_FROM(self, component), state);
}

Steinberg_Vst_IComponentVtbl componentVtbl = {
    .queryInterface = componentQueryInterface,
    .addRef = componentAddRef,
    .release = componentRelease,
    .initialize = componentInitialize,
    .terminate = componentTerminate,
    .getControllerClassId = componentGetControllerClassId,
    .setIoMode = componentSetIoMode,
    .getBusCount = componentGetBusCount,
    .getBusInfo = componentGetBusInfo,
    .getRoutingInfo = componentGetRoutingInfo,
    .activateBus = componentActivateBus,
    .setActive = componentSetActive,
    .setState = componentSetState,
    .getState = componentGetState,
};
