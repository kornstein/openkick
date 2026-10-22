#include "plugin.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
    StateMagic = 0x4B434B4F,
    StateVersion = 1,
    StateHeaderSize = 8,
    StateSize = StateHeaderSize + 8 * ParamCount,
};

const Steinberg_TUID pluginClassId = SMTG_INLINE_UID(0x4F4B4B31, 0x8E2D4C1A, 0x9B7F3E26, 0xD15C0A77);

Plugin* pluginCreate(void) {
    Plugin* plugin = calloc(1, sizeof(Plugin));
    if (!plugin) {
        return NULL;
    }
    plugin->component.lpVtbl = &componentVtbl;
    plugin->processor.lpVtbl = &processorVtbl;
    plugin->controller.lpVtbl = &controllerVtbl;
    plugin->contextRequirements.lpVtbl = &contextRequirementsVtbl;
    atomic_init(&plugin->referenceCount, 1);
    for (int id = 0; id < ParamCount; ++id) {
        atomic_init(&plugin->params[id], paramInfos[id].defaultValue);
    }
    plugin->channelCount = 2;
    shaperPrepare(&plugin->shaper, 44100.0);
    shaperReset(&plugin->shaper);
    atomic_init(&plugin->meter.phase, 0.0f);
    atomic_init(&plugin->meter.gain, 1.0f);
    atomic_init(&plugin->meter.tempo, (float)plugin->shaper.tempo);
    atomic_init(&plugin->meter.followsHost, false);
    atomic_init(&plugin->meter.hasSignal, false);
    return plugin;
}

bool uidEquals(const void* a, const void* b) {
    return memcmp(a, b, sizeof(Steinberg_TUID)) == 0;
}

Steinberg_tresult pluginQueryInterface(Plugin* plugin, const Steinberg_TUID iid, void** object) {
    if (uidEquals(iid, Steinberg_FUnknown_iid) || uidEquals(iid, Steinberg_IPluginBase_iid) ||
        uidEquals(iid, Steinberg_Vst_IComponent_iid)) {
        *object = &plugin->component;
    } else if (uidEquals(iid, Steinberg_Vst_IAudioProcessor_iid)) {
        *object = &plugin->processor;
    } else if (uidEquals(iid, Steinberg_Vst_IEditController_iid)) {
        *object = &plugin->controller;
    } else if (uidEquals(iid, Steinberg_Vst_IProcessContextRequirements_iid)) {
        *object = &plugin->contextRequirements;
    } else {
        *object = NULL;
        return Steinberg_kNoInterface;
    }
    pluginAddRef(plugin);
    return Steinberg_kResultOk;
}

Steinberg_uint32 pluginAddRef(Plugin* plugin) {
    return atomic_fetch_add(&plugin->referenceCount, 1) + 1;
}

Steinberg_uint32 pluginRelease(Plugin* plugin) {
    Steinberg_uint32 remaining = atomic_fetch_sub(&plugin->referenceCount, 1) - 1;
    if (remaining == 0) {
        pluginSetComponentHandler(plugin, NULL);
        free(plugin);
    }
    return remaining;
}

double pluginGetParam(Plugin* plugin, ParamId id) {
    return atomic_load(&plugin->params[id]);
}

void pluginSetParam(Plugin* plugin, ParamId id, double value) {
    atomic_store(&plugin->params[id], paramClamp(value));
}

void pluginSetComponentHandler(Plugin* plugin, Steinberg_Vst_IComponentHandler* handler) {
    if (handler == plugin->componentHandler) {
        return;
    }
    if (handler) {
        handler->lpVtbl->addRef(handler);
    }
    if (plugin->componentHandler) {
        plugin->componentHandler->lpVtbl->release(plugin->componentHandler);
    }
    plugin->componentHandler = handler;
}

static void storeLittleEndian(unsigned char* bytes, uint64_t value, int size) {
    for (int index = 0; index < size; ++index) {
        bytes[index] = (unsigned char)(value >> (8 * index));
    }
}

static uint64_t loadLittleEndian(const unsigned char* bytes, int size) {
    uint64_t value = 0;
    for (int index = 0; index < size; ++index) {
        value |= (uint64_t)bytes[index] << (8 * index);
    }
    return value;
}

Steinberg_tresult pluginWriteState(Plugin* plugin, struct Steinberg_IBStream* stream) {
    if (!stream) {
        return Steinberg_kInvalidArgument;
    }
    unsigned char bytes[StateSize];
    storeLittleEndian(bytes, StateMagic, 4);
    storeLittleEndian(bytes + 4, StateVersion, 4);
    for (int id = 0; id < ParamCount; ++id) {
        double value = pluginGetParam(plugin, (ParamId)id);
        uint64_t bits;
        memcpy(&bits, &value, sizeof(bits));
        storeLittleEndian(bytes + StateHeaderSize + 8 * id, bits, 8);
    }
    Steinberg_int32 written = 0;
    Steinberg_tresult result = stream->lpVtbl->write(stream, bytes, StateSize, &written);
    return result == Steinberg_kResultOk && written == StateSize ? Steinberg_kResultOk : Steinberg_kResultFalse;
}

Steinberg_tresult pluginReadState(Plugin* plugin, struct Steinberg_IBStream* stream) {
    if (!stream) {
        return Steinberg_kInvalidArgument;
    }
    unsigned char bytes[StateSize];
    Steinberg_int32 total = 0;
    while (total < StateSize) {
        Steinberg_int32 read = 0;
        if (stream->lpVtbl->read(stream, bytes + total, StateSize - total, &read) != Steinberg_kResultOk || read <= 0) {
            break;
        }
        total += read;
    }
    if (total < StateHeaderSize || loadLittleEndian(bytes, 4) != StateMagic || loadLittleEndian(bytes + 4, 4) < 1) {
        return Steinberg_kResultFalse;
    }
    for (int id = 0; id < ParamCount && StateHeaderSize + 8 * (id + 1) <= total; ++id) {
        uint64_t bits = loadLittleEndian(bytes + StateHeaderSize + 8 * id, 8);
        double value;
        memcpy(&value, &bits, sizeof(value));
        pluginSetParam(plugin, (ParamId)id, value);
    }
    return Steinberg_kResultOk;
}

void utf16FromAscii(Steinberg_char16* destination, const char* source, int capacity) {
    int index = 0;
    for (; source[index] && index < capacity - 1; ++index) {
        destination[index] = (Steinberg_char16)(unsigned char)source[index];
    }
    destination[index] = 0;
}

void asciiFromUtf16(char* destination, const Steinberg_char16* source, int capacity) {
    int index = 0;
    for (; source[index] && index < capacity - 1; ++index) {
        destination[index] = source[index] > 0 && source[index] < 128 ? (char)source[index] : '?';
    }
    destination[index] = 0;
}
