#pragma once
#include "LayerParamStore.h"
#include "ParamIDs.h"
#include "shared/Limiter.h"
#include "shared/Smoothing.h"
#include "shared/Tuning.h"
#include "shared/TripleBuffer.h"
#include "shared/Voice.h"
#include "shared/fx/FxChain.h"
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

// §5 MIDI learn: CC (channel -1 = any) -> parameter.
inline constexpr int maxMidiMappings = 256;
struct MidiMapping { int cc = 0, channel = -1; ParamTarget target; };
struct MidiMap { int count = 0; std::array<MidiMapping, maxMidiMappings> items {}; };

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
    // §3D editor: publish a user wavetable (frames of wt::frameSize samples) to slot 0..2 of a layer.
    void setUserWavetable (int layer, int slot, std::vector<float> frames, int numFrames);
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

    // Global data (MIDI map, microtuning) is saved in the host state but not in Multi presets:
    // loadState (core, false) keeps the current global data.
    juce::ValueTree saveState (bool includeGlobal = true) const;
    void loadState (const juce::ValueTree&, bool loadGlobal = true);
    void resetAll();                                   // init all layers, clear macros + automation slots

    // §5 MIDI learn (message thread, except where noted)
    void startMidiLearn (ParamTarget);                 // next CC received (audio thread) is mapped to target
    void cancelMidiLearn() noexcept { learnArmed.store (false); }
    bool isMidiLearning() const noexcept { return learnArmed.load(); }
    bool pollMidiLearn();                              // finalises a pending learn; true when a mapping was added
    void flushMidiToHost();                            // pushes CC-driven host parameter values to the host
    bool addMidiMapping (int cc, int channel, ParamTarget);
    void removeMidiMapping (ParamTarget);
    void clearMidiMap();
    int midiMappingFor (ParamTarget) const noexcept;   // CC number or -1
    const MidiMap& getMidiMap() const noexcept { return midiMap.latest(); }
    juce::ValueTree midiMapToTree() const;
    void midiMapFromTree (const juce::ValueTree&);
    bool saveMidiMap (const juce::File&) const;
    bool loadMidiMap (const juce::File&);

    // §5 Microtuning (.scl/.tun); master tune is the host parameter master_tune.
    void setTuning (const TuningTable&, const juce::String& name);
    void resetTuning();
    bool loadTuningFile (const juce::File&, juce::String* error = nullptr);
    const juce::String& getTuningName() const noexcept { return tuningName; }
    float tunedPitch (int note) const noexcept;        // note + offset currently applied (tests / GUI)
    int currentOversampling() const noexcept { return oversampling; }

    // Monotonic counter bumped by every edit of non-host state (undo history).
    uint32_t editCount() const noexcept;

    // ---- queries (tests / meters)
    struct VoiceInfo { bool active = false; int layer = 0, note = 0; bool gate = false; float level = 0.0f; };
    VoiceInfo voiceInfo (int i) const noexcept;
    int activeVoiceCount() const noexcept;
    int poolSize() const noexcept { return pool; }

    // GUI meters (lock-free): peaks held by the audio thread until the GUI takes them.
    float takeLayerPeak (int layer) noexcept { return meterLayer[(size_t) layer].exchange (0.0f, std::memory_order_relaxed); }
    float takeMasterPeak (int ch) noexcept { return meterMaster[(size_t) (ch != 0)].exchange (0.0f, std::memory_order_relaxed); }
    int meterVoiceCount (int layer) const noexcept { return meterVoices[(size_t) layer].load (std::memory_order_relaxed); }

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
    void updateMeters (const juce::AudioBuffer<float>&, int numCh, int numSamples) noexcept;
    void updateLayerFx() noexcept;
    void updateMasterFx() noexcept;
    int layerVoiceCount (int layer) const noexcept;
    bool layerMono (int layer) const noexcept;

    juce::AudioProcessor& processor;
    juce::AudioProcessorValueTreeState& apvts;
    std::vector<juce::RangedAudioParameter*> hostParams;
    std::array<HostLayer, numLayers> hostLayers {};
    juce::RangedAudioParameter *masterVolume, *polyphony, *scaleOn, *mpeOn, *mpeZone, *mpeBend;
    juce::RangedAudioParameter *masterTune, *osRealtime, *osOffline;
    std::array<juce::RangedAudioParameter*, 12> scaleSlots {};
    std::array<juce::RangedAudioParameter*, numMacros> macros {};
    std::array<juce::RangedAudioParameter*, numAutomationSlots> autoSlots {};

    std::array<LayerParamStore, numLayers> stores;
    std::array<LayerRuntime, numLayers> runtime {};
    std::array<LayerState, numLayers> layers;
    std::array<float, numLayers> blockPeak {};
    std::array<std::atomic<float>, numLayers> meterLayer {};
    std::array<std::atomic<float>, 2> meterMaster {};
    std::array<std::atomic<int>, numLayers> meterVoices {};
    std::array<std::unique_ptr<IEngine>, numLayers> analogEngines, addEngines, fmEngines, wtEngines, pdEngines;
    std::array<int, numLayers> engineIndex {};
    std::array<Voice, maxPoolVoices> voices {};
    MidiState midi;
    VoiceGlobals globals;

    TripleBuffer<MacroMap> macroMap;
    TripleBuffer<MidiMap> midiMap;
    const MidiMap* liveMidiMap = nullptr;       // audio thread, read once per block
    std::atomic<bool> learnArmed { false };
    std::atomic<int> learnedCC { 0 };           // (channel << 8 | cc) + 1, 0 = none
    ParamTarget learnTarget;
    std::unique_ptr<std::atomic<float>[]> midiHostPending;   // per host param, -1 = none
    std::atomic<bool> midiHostDirty { false };
    void applyMidiCC (int ch, int cc, int value) noexcept;
    TripleBuffer<TuningTable> tuningTable;
    std::array<float, 128> tuningFinal {};
    juce::String tuningName;
    int oversampling = 2;
    std::atomic<uint32_t> edits { 0 };
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

    // LAYER FX (§2) and MASTER FX (§4), shared modules in src/shared/fx.
    std::array<fx::LayerFxChain, numLayers> layerFx;
    fx::MasterFxChain masterFx;
    std::vector<juce::RangedAudioParameter*> masterFxParams;   // ordered as in params/master_fx.yaml
    std::vector<float> monoScratch;
    double sampleRate = 48000.0;
    int tickRemaining = 0;
    uint64_t sampleClock = 0, noteOrder = 0;
    int sampleClockOffset = 0;
    bool firstBlock = true;
};
} // namespace chroma
