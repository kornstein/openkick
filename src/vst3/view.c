#include <stdlib.h>
#include <string.h>

#include "platform/platform.h"
#include "plugin.h"
#include "ui/editor.h"

#if defined(_WIN32)
#define PLATFORM_VIEW_TYPE Steinberg_kPlatformTypeHWND
#define SUPPORTS_CONTENT_SCALE 1
#else
#define PLATFORM_VIEW_TYPE Steinberg_kPlatformTypeNSView
#define SUPPORTS_CONTENT_SCALE 0
#endif

typedef struct {
    Steinberg_IPlugView plugView;
    Steinberg_IPlugViewContentScaleSupport scaleSupport;
    atomic_uint referenceCount;
    Plugin* plugin;
    Editor* editor;
    PlatformView* platformView;
    float scale;
} PlugView;

#define VIEW_FROM(pointer, member) ((PlugView*)((char*)(pointer) - offsetof(PlugView, member)))

static double hostGetParam(void* context, ParamId id) {
    return pluginGetParam(context, id);
}

static void hostBeginEdit(void* context, ParamId id) {
    Steinberg_Vst_IComponentHandler* handler = ((Plugin*)context)->componentHandler;
    if (handler) {
        handler->lpVtbl->beginEdit(handler, (Steinberg_Vst_ParamID)id);
    }
}

static void hostPerformEdit(void* context, ParamId id, double value) {
    Plugin* plugin = context;
    pluginSetParam(plugin, id, value);
    if (plugin->componentHandler) {
        plugin->componentHandler->lpVtbl->performEdit(plugin->componentHandler, (Steinberg_Vst_ParamID)id,
                                                      pluginGetParam(plugin, id));
    }
}

static void hostEndEdit(void* context, ParamId id) {
    Steinberg_Vst_IComponentHandler* handler = ((Plugin*)context)->componentHandler;
    if (handler) {
        handler->lpVtbl->endEdit(handler, (Steinberg_Vst_ParamID)id);
    }
}

static void hostReadMeter(void* context, EditorMeter* meter) {
    PluginMeter* source = &((Plugin*)context)->meter;
    meter->phase = atomic_load(&source->phase);
    meter->gain = atomic_load(&source->gain);
    meter->tempo = atomic_load(&source->tempo);
    meter->followsHost = atomic_load(&source->followsHost);
    meter->hasSignal = atomic_load(&source->hasSignal);
}

static void closeEditor(PlugView* view) {
    if (view->platformView) {
        platformViewDetach(view->platformView);
        view->platformView = NULL;
    }
    if (view->editor) {
        editorDestroy(view->editor);
        view->editor = NULL;
    }
}

static Steinberg_tresult queryView(PlugView* view, const Steinberg_TUID iid, void** object) {
    if (uidEquals(iid, Steinberg_FUnknown_iid) || uidEquals(iid, Steinberg_IPlugView_iid)) {
        *object = &view->plugView;
    } else if (SUPPORTS_CONTENT_SCALE && uidEquals(iid, Steinberg_IPlugViewContentScaleSupport_iid)) {
        *object = &view->scaleSupport;
    } else {
        *object = NULL;
        return Steinberg_kNoInterface;
    }
    atomic_fetch_add(&view->referenceCount, 1);
    return Steinberg_kResultOk;
}

static Steinberg_uint32 retainView(PlugView* view) {
    return atomic_fetch_add(&view->referenceCount, 1) + 1;
}

static Steinberg_uint32 releaseView(PlugView* view) {
    Steinberg_uint32 remaining = atomic_fetch_sub(&view->referenceCount, 1) - 1;
    if (remaining == 0) {
        closeEditor(view);
        pluginRelease(view->plugin);
        free(view);
    }
    return remaining;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE plugViewQueryInterface(void* self, const Steinberg_TUID iid,
                                                                       void** object) {
    return queryView(VIEW_FROM(self, plugView), iid, object);
}

static Steinberg_uint32 SMTG_STDMETHODCALLTYPE plugViewAddRef(void* self) {
    return retainView(VIEW_FROM(self, plugView));
}

static Steinberg_uint32 SMTG_STDMETHODCALLTYPE plugViewRelease(void* self) {
    return releaseView(VIEW_FROM(self, plugView));
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE plugViewIsPlatformTypeSupported(void* self, Steinberg_FIDString type) {
    (void)self;
    return type && strcmp(type, PLATFORM_VIEW_TYPE) == 0 ? Steinberg_kResultTrue : Steinberg_kResultFalse;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE plugViewAttached(void* self, void* parent, Steinberg_FIDString type) {
    PlugView* view = VIEW_FROM(self, plugView);
    if (!parent || plugViewIsPlatformTypeSupported(self, type) != Steinberg_kResultTrue) {
        return Steinberg_kResultFalse;
    }
    closeEditor(view);
    EditorHost host = {view->plugin, hostGetParam, hostBeginEdit, hostPerformEdit, hostEndEdit, hostReadMeter};
    view->editor = editorCreate(&host);
    view->platformView = view->editor ? platformViewAttach(parent, view->editor, view->scale) : NULL;
    if (!view->platformView) {
        closeEditor(view);
        return Steinberg_kResultFalse;
    }
    return Steinberg_kResultOk;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE plugViewRemoved(void* self) {
    closeEditor(VIEW_FROM(self, plugView));
    return Steinberg_kResultOk;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE plugViewOnWheel(void* self, float distance) {
    (void)self;
    (void)distance;
    return Steinberg_kResultFalse;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE plugViewOnKey(void* self, Steinberg_char16 key, Steinberg_int16 keyCode,
                                                              Steinberg_int16 modifiers) {
    (void)self;
    (void)key;
    (void)keyCode;
    (void)modifiers;
    return Steinberg_kResultFalse;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE plugViewGetSize(void* self, struct Steinberg_ViewRect* size) {
    PlugView* view = VIEW_FROM(self, plugView);
    if (!size) {
        return Steinberg_kInvalidArgument;
    }
    size->left = 0;
    size->top = 0;
    size->right = (Steinberg_int32)(EditorWidth * view->scale + 0.5f);
    size->bottom = (Steinberg_int32)(EditorHeight * view->scale + 0.5f);
    return Steinberg_kResultOk;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE plugViewOnSize(void* self, struct Steinberg_ViewRect* size) {
    (void)self;
    (void)size;
    return Steinberg_kResultOk;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE plugViewOnFocus(void* self, Steinberg_TBool state) {
    (void)self;
    (void)state;
    return Steinberg_kResultOk;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE plugViewSetFrame(void* self, struct Steinberg_IPlugFrame* frame) {
    (void)self;
    (void)frame;
    return Steinberg_kResultOk;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE plugViewCanResize(void* self) {
    (void)self;
    return Steinberg_kResultFalse;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE plugViewCheckSizeConstraint(void* self,
                                                                            struct Steinberg_ViewRect* rect) {
    return plugViewGetSize(self, rect);
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE scaleSupportQueryInterface(void* self, const Steinberg_TUID iid,
                                                                           void** object) {
    return queryView(VIEW_FROM(self, scaleSupport), iid, object);
}

static Steinberg_uint32 SMTG_STDMETHODCALLTYPE scaleSupportAddRef(void* self) {
    return retainView(VIEW_FROM(self, scaleSupport));
}

static Steinberg_uint32 SMTG_STDMETHODCALLTYPE scaleSupportRelease(void* self) {
    return releaseView(VIEW_FROM(self, scaleSupport));
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE
scaleSupportSetContentScaleFactor(void* self, Steinberg_IPlugViewContentScaleSupport_ScaleFactor factor) {
    PlugView* view = VIEW_FROM(self, scaleSupport);
    if (factor < 0.5f || factor > 4.0f) {
        return Steinberg_kResultFalse;
    }
    view->scale = factor;
    if (view->platformView) {
        platformViewSetScale(view->platformView, factor);
    }
    return Steinberg_kResultOk;
}

static Steinberg_IPlugViewVtbl plugViewVtbl = {
    .queryInterface = plugViewQueryInterface,
    .addRef = plugViewAddRef,
    .release = plugViewRelease,
    .isPlatformTypeSupported = plugViewIsPlatformTypeSupported,
    .attached = plugViewAttached,
    .removed = plugViewRemoved,
    .onWheel = plugViewOnWheel,
    .onKeyDown = plugViewOnKey,
    .onKeyUp = plugViewOnKey,
    .getSize = plugViewGetSize,
    .onSize = plugViewOnSize,
    .onFocus = plugViewOnFocus,
    .setFrame = plugViewSetFrame,
    .canResize = plugViewCanResize,
    .checkSizeConstraint = plugViewCheckSizeConstraint,
};

static Steinberg_IPlugViewContentScaleSupportVtbl scaleSupportVtbl = {
    .queryInterface = scaleSupportQueryInterface,
    .addRef = scaleSupportAddRef,
    .release = scaleSupportRelease,
    .setContentScaleFactor = scaleSupportSetContentScaleFactor,
};

struct Steinberg_IPlugView* plugViewCreate(Plugin* plugin) {
    PlugView* view = calloc(1, sizeof(PlugView));
    if (!view) {
        return NULL;
    }
    view->plugView.lpVtbl = &plugViewVtbl;
    view->scaleSupport.lpVtbl = &scaleSupportVtbl;
    atomic_init(&view->referenceCount, 1);
    view->plugin = plugin;
    view->scale = 1.0f;
    pluginAddRef(plugin);
    return &view->plugView;
}
