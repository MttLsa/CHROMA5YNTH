#pragma once
#include "LayerParamStore.h"
#include "ParamIDs.h"
#include "shared/Limiter.h"
#include "shared/Smoothing.h"
#include "shared/TripleBuffer.h"
#include "shared/Voice.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>

namespace chroma
{
inline constexpr int numMacros = 8;
inline constexpr int numAutomationSlots = 256;
inline constexpr int maxMacroAssignments = 128;

// Target of a macro assignment / automation slot.
struct ParamTarget
{
    static constexpr int none = -2, host = -1;
    int layer = none;     // 0..11 = internal layer parameter, host = host parameter index
    int id = 0;           // lp::Id or host parameter index
    bool isValid() const noexcept { return layer != none; }
    bool operator== (const ParamTarget& o) const noexcept { return layer == o.layer && id == o.id; }
};

struct MacroAssignment { int macro = 0; ParamTarget target; float min = 0.0f, max = 1.0f; };  // min/max normalised
struct MacroMap { int count = 0; std::array<MacroAssignment, maxMacroAssignments> items {}; };
struct AutomationMap { std::array<ParamTarget, numAutomationSlots> slots {}; };

// Core synth (§1/§2): 12 layers, shared voice pool, SCALE, MPE, macros, automation slots,
// layer mixer, master level and true-peak limiter.
class Synth
{
public:
    Synth (juce::AudioProcessor&, juce::AudioProcessorValueTreeState&);
    ~Synth();

    void prepare (double sampleRate, int maxBlockSize, int numOutputChannels);
    void process (juce::AudioBuffer<float>&, const juce::MidiBuffer&, double bpm) noexcept;
    int getLatencySamples() const noexcept { return limiter.getLatencySamples(); }

    // ---- message thread
    LayerParamStore& layerStore (int layer) noexcept { return stores[(size_t) layer]; }
    bool assignMacro (int macro, ParamTarget, float minNorm, float maxNorm);
    void clearMacroAssignments (int macro);
    const MacroMap& getMacroMap() const noexcept { return macroMap.latest(); }
    int assignAutomation (int layer, int lpId);        // returns slot or -1
    void clearAutomation (int slot);
    ParamTarget getAutomationTarget (int slot) const noexcept { return automationMap.latest().slots[(size_t) slot]; }
    int hostParamIndex (const juce::String& id) const;

    juce::ValueTree copyLayer (int layer) const;
    void pasteLayer (int layer, const juce::ValueTree&);
    void initLayer (int layer);
    bool saveLayerPreset (int layer, const juce::File&) const;
    bool loadLayerPreset (int layer, const juce::File&);

    juce::ValueTree saveState() const;
    void loadState (const juce::ValueTree&);

    // ---- queries (tests / meters)
    struct VoiceInfo { bool active = false; int layer = 0, note = 0; bool gate = false; float level = 0.0f; };
    VoiceInfo voiceInfo (int i) const noexcept;
    int activeVoiceCount() const noexcept;
    int poolSize() const noexcept { return pool; }

private:
    struct HostLayer
    {
        juce::RangedAudioParameter *on, *engine, *volume, *pan, *mute, *solo, *transpose,
                                   *keyLo, *keyHi, *velLo, *velHi, *midiCh, *maxVoices;
    };
    struct MonoEntry { int channel, note; float velocity; uint64_t order; };
    struct LayerState
    {
        bool on = false, audible = false, active = false;
        float volumeGain = 1.0f, pan = 0.0f;
        int keyLo = 0, keyHi = 127, velLo = 1, velHi = 127, midiCh = 0, maxVoices = 36;
        LinearRamp gainL, gainR;
        std::array<MonoEntry, 32> stack {};
        int stackSize = 0;
        int monoVoice = -1;
        float lastPitch = -1.0f;
        std::vector<float> bufL, bufR;
    };

    float hostValue (const juce::RangedAudioParameter*) const noexcept;
    void readHostParams() noexcept;
    void updateLayerBases (int samples) noexcept;
    void controlTick() noexcept;
    void handleMidi (const juce::MidiMessage&) noexcept;
    void noteOn (int ch, int note, float vel) noexcept;
    void noteOff (int ch, int note, float relVel) noexcept;
    void layerNoteOn (int layer, int ch, int note, float vel) noexcept;
    void monoNoteOff (int layer, int ch, int note, float relVel) noexcept;
    void releaseVoice (Voice&, float relVel) noexcept;
    void pedal (int ch, bool sustain, bool down) noexcept;
    int allocateVoice (int layer) noexcept;
    int pickSteal (int layer) const noexcept;
    void startVoice (int v, const VoiceStart&) noexcept;
    void renderChunk (float* const* out, int numChannels, int start, int n) noexcept;
    int layerVoiceCount (int layer) const noexcept;
    bool layerMono (int layer) const noexcept;

    juce::AudioProcessor& processor;
    juce::AudioProcessorValueTreeState& apvts;
    std::vector<juce::RangedAudioParameter*> hostParams;
    std::array<HostLayer, numLayers> hostLayers {};
    juce::RangedAudioParameter *masterVolume, *polyphony, *scaleOn, *mpeOn, *mpeZone, *mpeBend;
    std::array<juce::RangedAudioParameter*, 12> scaleSlots {};
    std::array<juce::RangedAudioParameter*, numMacros> macros {};
    std::array<juce::RangedAudioParameter*, numAutomationSlots> autoSlots {};

    std::array<LayerParamStore, numLayers> stores;
    std::array<LayerRuntime, numLayers> runtime {};
    std::array<LayerState, numLayers> layers;
    std::array<std::unique_ptr<IEngine>, numLayers> analogEngines, sineEngines;
    std::array<int, numLayers> engineIndex {};
    std::array<Voice, maxPoolVoices> voices {};
    MidiState midi;
    VoiceGlobals globals;

    TripleBuffer<MacroMap> macroMap;
    TripleBuffer<AutomationMap> automationMap;
    std::vector<float> hostOverride;            // normalised override per host param (-1 = none)
    struct LayerOverride { int layer, id; float plain; };
    std::array<LayerOverride, numAutomationSlots + maxMacroAssignments> layerOverrides {};
    int numLayerOverrides = 0;

    bool scaleEnabled = false;
    uint16_t scaleMasks[12] {};
    int pool = 24;
    bool anySolo = false;
    std::vector<float> scratchL, scratchR, bypassL, bypassR;
    OnePole paramSmoother;
    LinearRamp masterGain;
    TruePeakLimiter limiter;
    double sampleRate = 48000.0;
    int tickRemaining = 0;
    uint64_t sampleClock = 0, noteOrder = 0;
    int sampleClockOffset = 0;
    bool firstBlock = true;
};
} // namespace chroma
