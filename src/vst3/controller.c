#include "plugin.h"

#include <string.h>

PLUGIN_UNKNOWN_METHODS(controller, controller)

static bool isKnownParam(Steinberg_Vst_ParamID id) {
    return id < ParamCount;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE controllerInitialize(void* self, struct Steinberg_FUnknown* context) {
    (void)self;
    (void)context;
    return Steinberg_kResultOk;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE controllerTerminate(void* self) {
    pluginSetComponentHandler(PLUGIN_FROM(self, controller), NULL);
    return Steinberg_kResultOk;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE controllerSetComponentState(void* self,
                                                                            struct Steinberg_IBStream* state) {
    return pluginReadState(PLUGIN_FROM(self, controller), state);
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE controllerSetState(void* self, struct Steinberg_IBStream* state) {
    (void)self;
    (void)state;
    return Steinberg_kResultOk;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE controllerGetState(void* self, struct Steinberg_IBStream* state) {
    (void)self;
    (void)state;
    return Steinberg_kResultOk;
}

static Steinberg_int32 SMTG_STDMETHODCALLTYPE controllerGetParameterCount(void* self) {
    (void)self;
    return ParamCount;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE controllerGetParameterInfo(void* self, Steinberg_int32 index,
                                                                           struct Steinberg_Vst_ParameterInfo* info) {
    (void)self;
    if (index < 0 || index >= ParamCount || !info) {
        return Steinberg_kInvalidArgument;
    }
    const ParamInfo* param = &paramInfos[index];
    memset(info, 0, sizeof(*info));
    info->id = (Steinberg_Vst_ParamID)index;
    info->stepCount = param->stepCount;
    info->defaultNormalizedValue = param->defaultValue;
    info->flags = Steinberg_Vst_ParameterInfo_ParameterFlags_kCanAutomate;
    if (param->isList) {
        info->flags |= Steinberg_Vst_ParameterInfo_ParameterFlags_kIsList;
    }
    if (param->isBypass) {
        info->flags |= Steinberg_Vst_ParameterInfo_ParameterFlags_kIsBypass;
    }
    utf16FromAscii(info->title, param->title, 128);
    utf16FromAscii(info->shortTitle, param->title, 128);
    utf16FromAscii(info->units, param->units, 128);
    return Steinberg_kResultOk;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE controllerGetParamStringByValue(void* self, Steinberg_Vst_ParamID id,
                                                                                Steinberg_Vst_ParamValue value,
                                                                                Steinberg_Vst_String128 string) {
    (void)self;
    if (!isKnownParam(id)) {
        return Steinberg_kInvalidArgument;
    }
    char text[64];
    paramFormat((ParamId)id, value, text, sizeof(text));
    utf16FromAscii(string, text, 128);
    return Steinberg_kResultOk;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE controllerGetParamValueByString(void* self, Steinberg_Vst_ParamID id,
                                                                                Steinberg_Vst_TChar* string,
                                                                                Steinberg_Vst_ParamValue* value) {
    (void)self;
    if (!isKnownParam(id) || !string || !value) {
        return Steinberg_kInvalidArgument;
    }
    char text[128];
    asciiFromUtf16(text, string, sizeof(text));
    return paramParse((ParamId)id, text, value) ? Steinberg_kResultOk : Steinberg_kResultFalse;
}

static Steinberg_Vst_ParamValue SMTG_STDMETHODCALLTYPE
controllerNormalizedParamToPlain(void* self, Steinberg_Vst_ParamID id, Steinberg_Vst_ParamValue value) {
    (void)self;
    return isKnownParam(id) ? paramToPlain((ParamId)id, value) : value;
}

static Steinberg_Vst_ParamValue SMTG_STDMETHODCALLTYPE
controllerPlainParamToNormalized(void* self, Steinberg_Vst_ParamID id, Steinberg_Vst_ParamValue value) {
    (void)self;
    return isKnownParam(id) ? paramFromPlain((ParamId)id, value) : value;
}

static Steinberg_Vst_ParamValue SMTG_STDMETHODCALLTYPE controllerGetParamNormalized(void* self,
                                                                                    Steinberg_Vst_ParamID id) {
    return isKnownParam(id) ? pluginGetParam(PLUGIN_FROM(self, controller), (ParamId)id) : 0.0;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE controllerSetParamNormalized(void* self, Steinberg_Vst_ParamID id,
                                                                             Steinberg_Vst_ParamValue value) {
    if (!isKnownParam(id)) {
        return Steinberg_kInvalidArgument;
    }
    pluginSetParam(PLUGIN_FROM(self, controller), (ParamId)id, value);
    return Steinberg_kResultOk;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE
controllerSetComponentHandler(void* self, struct Steinberg_Vst_IComponentHandler* handler) {
    pluginSetComponentHandler(PLUGIN_FROM(self, controller), handler);
    return Steinberg_kResultOk;
}

static struct Steinberg_IPlugView* SMTG_STDMETHODCALLTYPE controllerCreateView(void* self, Steinberg_FIDString name) {
    if (!name || strcmp(name, Steinberg_Vst_ViewType_kEditor) != 0) {
        return NULL;
    }
    return plugViewCreate(PLUGIN_FROM(self, controller));
}

Steinberg_Vst_IEditControllerVtbl controllerVtbl = {
    .queryInterface = controllerQueryInterface,
    .addRef = controllerAddRef,
    .release = controllerRelease,
    .initialize = controllerInitialize,
    .terminate = controllerTerminate,
    .setComponentState = controllerSetComponentState,
    .setState = controllerSetState,
    .getState = controllerGetState,
    .getParameterCount = controllerGetParameterCount,
    .getParameterInfo = controllerGetParameterInfo,
    .getParamStringByValue = controllerGetParamStringByValue,
    .getParamValueByString = controllerGetParamValueByString,
    .normalizedParamToPlain = controllerNormalizedParamToPlain,
    .plainParamToNormalized = controllerPlainParamToNormalized,
    .getParamNormalized = controllerGetParamNormalized,
    .setParamNormalized = controllerSetParamNormalized,
    .setComponentHandler = controllerSetComponentHandler,
    .createView = controllerCreateView,
};
