#include "plugin.h"

#include <math.h>
#include <string.h>

enum { RenderChunk = 256 };

static const double signalThreshold = 0.0001;
static const double signalHoldSeconds = 0.2;

PLUGIN_UNKNOWN_METHODS(processor, processor)
PLUGIN_UNKNOWN_METHODS(contextRequirements, contextRequirements)

static Steinberg_tresult SMTG_STDMETHODCALLTYPE processorSetBusArrangements(void* self,
                                                                            Steinberg_Vst_SpeakerArrangement* inputs,
                                                                            Steinberg_int32 inputCount,
                                                                            Steinberg_Vst_SpeakerArrangement* outputs,
                                                                            Steinberg_int32 outputCount) {
    Plugin* plugin = PLUGIN_FROM(self, processor);
    if (inputCount != 1 || outputCount != 1 || inputs[0] != outputs[0]) {
        return Steinberg_kResultFalse;
    }
    if (inputs[0] == Steinberg_Vst_SpeakerArr_kStereo) {
        plugin->channelCount = 2;
    } else if (inputs[0] == Steinberg_Vst_SpeakerArr_kMono) {
        plugin->channelCount = 1;
    } else {
        return Steinberg_kResultFalse;
    }
    return Steinberg_kResultTrue;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE
processorGetBusArrangement(void* self, Steinberg_Vst_BusDirection direction, Steinberg_int32 index,
                           Steinberg_Vst_SpeakerArrangement* arrangement) {
    Plugin* plugin = PLUGIN_FROM(self, processor);
    (void)direction;
    if (index != 0 || !arrangement) {
        return Steinberg_kInvalidArgument;
    }
    *arrangement = plugin->channelCount == 1 ? Steinberg_Vst_SpeakerArr_kMono : Steinberg_Vst_SpeakerArr_kStereo;
    return Steinberg_kResultOk;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE processorCanProcessSampleSize(void* self, Steinberg_int32 size) {
    (void)self;
    bool supported =
        size == Steinberg_Vst_SymbolicSampleSizes_kSample32 || size == Steinberg_Vst_SymbolicSampleSizes_kSample64;
    return supported ? Steinberg_kResultTrue : Steinberg_kResultFalse;
}

static Steinberg_uint32 SMTG_STDMETHODCALLTYPE processorGetLatencySamples(void* self) {
    (void)self;
    return 0;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE processorSetupProcessing(void* self,
                                                                         struct Steinberg_Vst_ProcessSetup* setup) {
    shaperPrepare(&PLUGIN_FROM(self, processor)->shaper, setup->sampleRate);
    return Steinberg_kResultOk;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE processorSetProcessing(void* self, Steinberg_TBool state) {
    (void)self;
    (void)state;
    return Steinberg_kResultOk;
}

static void applyParameterChanges(Plugin* plugin, struct Steinberg_Vst_IParameterChanges* changes) {
    if (!changes) {
        return;
    }
    Steinberg_int32 count = changes->lpVtbl->getParameterCount(changes);
    for (Steinberg_int32 index = 0; index < count; ++index) {
        struct Steinberg_Vst_IParamValueQueue* queue = changes->lpVtbl->getParameterData(changes, index);
        if (!queue) {
            continue;
        }
        Steinberg_Vst_ParamID id = queue->lpVtbl->getParameterId(queue);
        Steinberg_int32 points = queue->lpVtbl->getPointCount(queue);
        Steinberg_int32 offset;
        Steinberg_Vst_ParamValue value;
        if (id < ParamCount && points > 0 &&
            queue->lpVtbl->getPoint(queue, points - 1, &offset, &value) == Steinberg_kResultOk) {
            pluginSetParam(plugin, (ParamId)id, value);
        }
    }
}

static bool followTransport(Plugin* plugin, const struct Steinberg_Vst_ProcessContext* context) {
    if (!context) {
        return false;
    }
    if (context->state & Steinberg_Vst_ProcessContext_StatesAndFlags_kTempoValid) {
        shaperSetTempo(&plugin->shaper, context->tempo);
    }
    bool playing = (context->state & Steinberg_Vst_ProcessContext_StatesAndFlags_kPlaying) != 0;
    bool positioned = (context->state & Steinberg_Vst_ProcessContext_StatesAndFlags_kProjectTimeMusicValid) != 0;
    if (playing && positioned) {
        shaperLocate(&plugin->shaper, context->projectTimeMusic);
        return true;
    }
    return false;
}

static void applyGains32(Steinberg_Vst_Sample32** inputs, Steinberg_Vst_Sample32** outputs, int channels, int offset,
                         const double* gains, int frames) {
    for (int channel = 0; channel < channels; ++channel) {
        const Steinberg_Vst_Sample32* input = inputs[channel] + offset;
        Steinberg_Vst_Sample32* output = outputs[channel] + offset;
        for (int frame = 0; frame < frames; ++frame) {
            output[frame] = (Steinberg_Vst_Sample32)(input[frame] * gains[frame]);
        }
    }
}

static void applyGains64(Steinberg_Vst_Sample64** inputs, Steinberg_Vst_Sample64** outputs, int channels, int offset,
                         const double* gains, int frames) {
    for (int channel = 0; channel < channels; ++channel) {
        const Steinberg_Vst_Sample64* input = inputs[channel] + offset;
        Steinberg_Vst_Sample64* output = outputs[channel] + offset;
        for (int frame = 0; frame < frames; ++frame) {
            output[frame] = input[frame] * gains[frame];
        }
    }
}

static void clearChannels(struct Steinberg_Vst_AudioBusBuffers* bus, int from, int frames, bool doublePrecision) {
    for (int channel = from; channel < bus->numChannels; ++channel) {
        if (doublePrecision) {
            memset(bus->Steinberg_Vst_AudioBusBuffers_channelBuffers64[channel], 0,
                   sizeof(Steinberg_Vst_Sample64) * (size_t)frames);
        } else {
            memset(bus->Steinberg_Vst_AudioBusBuffers_channelBuffers32[channel], 0,
                   sizeof(Steinberg_Vst_Sample32) * (size_t)frames);
        }
    }
}

static double inputPeak(const struct Steinberg_Vst_AudioBusBuffers* bus, int frames, bool doublePrecision) {
    double peak = 0.0;
    for (int channel = 0; channel < bus->numChannels; ++channel) {
        for (int frame = 0; frame < frames; ++frame) {
            double sample = doublePrecision ? bus->Steinberg_Vst_AudioBusBuffers_channelBuffers64[channel][frame]
                                            : bus->Steinberg_Vst_AudioBusBuffers_channelBuffers32[channel][frame];
            peak = fmax(peak, fabs(sample));
        }
    }
    return peak;
}

static bool detectSignal(Plugin* plugin, const struct Steinberg_Vst_AudioBusBuffers* input, int frames,
                         bool doublePrecision) {
    if (inputPeak(input, frames, doublePrecision) > signalThreshold) {
        plugin->silentSeconds = 0.0;
    } else if (plugin->silentSeconds < signalHoldSeconds) {
        plugin->silentSeconds += frames / plugin->shaper.sampleRate;
    }
    return plugin->silentSeconds < signalHoldSeconds;
}

static void publishMeter(Plugin* plugin, bool followsHost, bool hasSignal) {
    atomic_store(&plugin->meter.phase, (float)plugin->shaper.phase);
    atomic_store(&plugin->meter.gain, (float)plugin->shaper.gain);
    atomic_store(&plugin->meter.tempo, (float)plugin->shaper.tempo);
    atomic_store(&plugin->meter.followsHost, followsHost);
    atomic_store(&plugin->meter.hasSignal, hasSignal);
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE processorProcess(void* self, struct Steinberg_Vst_ProcessData* data) {
    Plugin* plugin = PLUGIN_FROM(self, processor);
    applyParameterChanges(plugin, data->inputParameterChanges);
    if (data->numSamples <= 0 || data->numInputs < 1 || data->numOutputs < 1) {
        return Steinberg_kResultOk;
    }

    bool followsHost = followTransport(plugin, data->processContext);
    double params[ParamCount];
    for (int id = 0; id < ParamCount; ++id) {
        params[id] = pluginGetParam(plugin, (ParamId)id);
    }
    ShaperSettings settings;
    settingsFromParams(params, &settings);

    struct Steinberg_Vst_AudioBusBuffers* input = &data->inputs[0];
    struct Steinberg_Vst_AudioBusBuffers* output = &data->outputs[0];
    bool doublePrecision = data->symbolicSampleSize == Steinberg_Vst_SymbolicSampleSizes_kSample64;
    int channels = input->numChannels < output->numChannels ? input->numChannels : output->numChannels;
    double gains[RenderChunk];
    bool hasSignal = detectSignal(plugin, input, data->numSamples, doublePrecision);

    for (int offset = 0; offset < data->numSamples; offset += RenderChunk) {
        int frames = data->numSamples - offset < RenderChunk ? data->numSamples - offset : RenderChunk;
        shaperRender(&plugin->shaper, &settings, gains, frames);
        if (doublePrecision) {
            applyGains64(input->Steinberg_Vst_AudioBusBuffers_channelBuffers64,
                         output->Steinberg_Vst_AudioBusBuffers_channelBuffers64, channels, offset, gains, frames);
        } else {
            applyGains32(input->Steinberg_Vst_AudioBusBuffers_channelBuffers32,
                         output->Steinberg_Vst_AudioBusBuffers_channelBuffers32, channels, offset, gains, frames);
        }
    }
    clearChannels(output, channels, data->numSamples, doublePrecision);
    output->silenceFlags = input->silenceFlags;
    publishMeter(plugin, followsHost, hasSignal);
    return Steinberg_kResultOk;
}

static Steinberg_uint32 SMTG_STDMETHODCALLTYPE processorGetTailSamples(void* self) {
    (void)self;
    return 0;
}

static Steinberg_uint32 SMTG_STDMETHODCALLTYPE contextRequirementsGet(void* self) {
    (void)self;
    return Steinberg_Vst_IProcessContextRequirements_Flags_kNeedProjectTimeMusic |
           Steinberg_Vst_IProcessContextRequirements_Flags_kNeedTempo |
           Steinberg_Vst_IProcessContextRequirements_Flags_kNeedTransportState;
}

Steinberg_Vst_IAudioProcessorVtbl processorVtbl = {
    .queryInterface = processorQueryInterface,
    .addRef = processorAddRef,
    .release = processorRelease,
    .setBusArrangements = processorSetBusArrangements,
    .getBusArrangement = processorGetBusArrangement,
    .canProcessSampleSize = processorCanProcessSampleSize,
    .getLatencySamples = processorGetLatencySamples,
    .setupProcessing = processorSetupProcessing,
    .setProcessing = processorSetProcessing,
    .process = processorProcess,
    .getTailSamples = processorGetTailSamples,
};

Steinberg_Vst_IProcessContextRequirementsVtbl contextRequirementsVtbl = {
    .queryInterface = contextRequirementsQueryInterface,
    .addRef = contextRequirementsAddRef,
    .release = contextRequirementsRelease,
    .getProcessContextRequirements = contextRequirementsGet,
};
