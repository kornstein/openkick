#include <stdio.h>
#include <string.h>

#include "platform/platform.h"
#include "plugin.h"
#include "version.h"

#if defined(_WIN32)
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

EXPORT Steinberg_IPluginFactory* SMTG_STDMETHODCALLTYPE GetPluginFactory(void);
#if defined(_WIN32)
EXPORT bool InitDll(void);
EXPORT bool ExitDll(void);
#else
EXPORT bool bundleEntry(void* bundle);
EXPORT bool bundleExit(void);
#endif

static const char* const classCategory = "Audio Module Class";
static const char* const classSubCategories = "Fx|Dynamics";
static const char* const sdkVersion = "VST 3.7.12";

static Steinberg_tresult SMTG_STDMETHODCALLTYPE factoryQueryInterface(void* self, const Steinberg_TUID iid,
                                                                      void** object) {
    if (uidEquals(iid, Steinberg_FUnknown_iid) || uidEquals(iid, Steinberg_IPluginFactory_iid) ||
        uidEquals(iid, Steinberg_IPluginFactory2_iid) || uidEquals(iid, Steinberg_IPluginFactory3_iid)) {
        *object = self;
        return Steinberg_kResultOk;
    }
    *object = NULL;
    return Steinberg_kNoInterface;
}

static Steinberg_uint32 SMTG_STDMETHODCALLTYPE factoryAddRef(void* self) {
    (void)self;
    return 1;
}

static Steinberg_uint32 SMTG_STDMETHODCALLTYPE factoryRelease(void* self) {
    (void)self;
    return 1;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE factoryGetFactoryInfo(void* self, struct Steinberg_PFactoryInfo* info) {
    (void)self;
    memset(info, 0, sizeof(*info));
    snprintf(info->vendor, sizeof(info->vendor), "%s", OPENKICK_VENDOR);
    snprintf(info->url, sizeof(info->url), "%s", OPENKICK_URL);
    info->flags = Steinberg_PFactoryInfo_FactoryFlags_kUnicode;
    return Steinberg_kResultOk;
}

static Steinberg_int32 SMTG_STDMETHODCALLTYPE factoryCountClasses(void* self) {
    (void)self;
    return 1;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE factoryGetClassInfo(void* self, Steinberg_int32 index,
                                                                    struct Steinberg_PClassInfo* info) {
    (void)self;
    if (index != 0 || !info) {
        return Steinberg_kInvalidArgument;
    }
    memset(info, 0, sizeof(*info));
    memcpy(info->cid, pluginClassId, sizeof(Steinberg_TUID));
    info->cardinality = Steinberg_PClassInfo_ClassCardinality_kManyInstances;
    snprintf(info->category, sizeof(info->category), "%s", classCategory);
    snprintf(info->name, sizeof(info->name), "%s", OPENKICK_NAME);
    return Steinberg_kResultOk;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE factoryGetClassInfo2(void* self, Steinberg_int32 index,
                                                                     struct Steinberg_PClassInfo2* info) {
    (void)self;
    if (index != 0 || !info) {
        return Steinberg_kInvalidArgument;
    }
    memset(info, 0, sizeof(*info));
    memcpy(info->cid, pluginClassId, sizeof(Steinberg_TUID));
    info->cardinality = Steinberg_PClassInfo_ClassCardinality_kManyInstances;
    snprintf(info->category, sizeof(info->category), "%s", classCategory);
    snprintf(info->name, sizeof(info->name), "%s", OPENKICK_NAME);
    snprintf(info->subCategories, sizeof(info->subCategories), "%s", classSubCategories);
    snprintf(info->vendor, sizeof(info->vendor), "%s", OPENKICK_VENDOR);
    snprintf(info->version, sizeof(info->version), "%s", OPENKICK_VERSION);
    snprintf(info->sdkVersion, sizeof(info->sdkVersion), "%s", sdkVersion);
    return Steinberg_kResultOk;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE factoryGetClassInfoUnicode(void* self, Steinberg_int32 index,
                                                                           struct Steinberg_PClassInfoW* info) {
    (void)self;
    if (index != 0 || !info) {
        return Steinberg_kInvalidArgument;
    }
    memset(info, 0, sizeof(*info));
    memcpy(info->cid, pluginClassId, sizeof(Steinberg_TUID));
    info->cardinality = Steinberg_PClassInfo_ClassCardinality_kManyInstances;
    snprintf(info->category, sizeof(info->category), "%s", classCategory);
    snprintf(info->subCategories, sizeof(info->subCategories), "%s", classSubCategories);
    utf16FromAscii(info->name, OPENKICK_NAME, 64);
    utf16FromAscii(info->vendor, OPENKICK_VENDOR, 64);
    utf16FromAscii(info->version, OPENKICK_VERSION, 64);
    utf16FromAscii(info->sdkVersion, sdkVersion, 64);
    return Steinberg_kResultOk;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE factorySetHostContext(void* self, struct Steinberg_FUnknown* context) {
    (void)self;
    (void)context;
    return Steinberg_kResultOk;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE factoryCreateInstance(void* self, Steinberg_FIDString classId,
                                                                      Steinberg_FIDString iid, void** object) {
    (void)self;
    *object = NULL;
    if (!uidEquals(classId, pluginClassId)) {
        return Steinberg_kNoInterface;
    }
    Plugin* plugin = pluginCreate();
    if (!plugin) {
        return Steinberg_kOutOfMemory;
    }
    Steinberg_tresult result = pluginQueryInterface(plugin, (const Steinberg_int8*)iid, object);
    pluginRelease(plugin);
    return result;
}

static Steinberg_IPluginFactory3Vtbl factoryVtbl = {
    .queryInterface = factoryQueryInterface,
    .addRef = factoryAddRef,
    .release = factoryRelease,
    .getFactoryInfo = factoryGetFactoryInfo,
    .countClasses = factoryCountClasses,
    .getClassInfo = factoryGetClassInfo,
    .createInstance = factoryCreateInstance,
    .getClassInfo2 = factoryGetClassInfo2,
    .getClassInfoUnicode = factoryGetClassInfoUnicode,
    .setHostContext = factorySetHostContext,
};

static Steinberg_IPluginFactory3 factory = {&factoryVtbl};

EXPORT Steinberg_IPluginFactory* SMTG_STDMETHODCALLTYPE GetPluginFactory(void) {
    return (Steinberg_IPluginFactory*)&factory;
}

#if defined(_WIN32)
EXPORT bool InitDll(void) {
    return true;
}

EXPORT bool ExitDll(void) {
    platformModuleExit();
    return true;
}
#else
EXPORT bool bundleEntry(void* bundle) {
    (void)bundle;
    return true;
}

EXPORT bool bundleExit(void) {
    platformModuleExit();
    return true;
}
#endif
