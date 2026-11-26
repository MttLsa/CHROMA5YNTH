#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParamIDs.h"

namespace chroma
{
ChromaProcessor::ChromaProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "CHROMA5YNTH", createParameterLayout())
{
    history.capture = [this] { return captureState (false); };
    history.restore = [this] (const juce::ValueTree& t) { restoreState (t, true); };
    history.reset();
    markClean();
    addListener (this);
    if (juce::MessageManager::getInstanceWithoutCreating() != nullptr)
        startTimerHz (30);
}

ChromaProcessor::~ChromaProcessor()
{
    stopTimer();
    removeListener (this);
}

void ChromaProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    synth.prepare (sampleRate, samplesPerBlock, getTotalNumOutputChannels());
    setLatencySamples (synth.getLatencySamples());
}

bool ChromaProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void ChromaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm())
                bpm = *b;
    synth.process (buffer, midi, bpm);
}

juce::AudioProcessorEditor* ChromaProcessor::createEditor()
{
    return new ChromaEditor (*this);
}

void ChromaProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = captureState (true).createXml())
        copyXmlToBinary (*xml, destData);
}

void ChromaProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (state.state.getType()))
        {
            restoreState (juce::ValueTree::fromXml (*xml), false);
            history.reset();
            markClean();
        }
}

// ------------------------------------------------------------------------------------------------ §5/§6
bool ChromaProcessor::isGlobalParam (const juce::String& id)
{
    for (const auto* g : { ParamIDs::masterTune, ParamIDs::oversamplingRealtime, ParamIDs::oversamplingOffline,
                           ParamIDs::mpeOn, ParamIDs::mpeZone, ParamIDs::mpeBendRange })
        if (id == g) return true;
    return false;
}

juce::ValueTree ChromaProcessor::captureState (bool includeGlobal)
{
    auto tree = state.copyState();
    tree.removeChild (tree.getChildWithName ("CORE"), nullptr);
    if (! includeGlobal)
        for (int i = tree.getNumChildren(); --i >= 0;)
            if (isGlobalParam (tree.getChild (i).getProperty ("id").toString()))
                tree.removeChild (i, nullptr);
    tree.appendChild (synth.saveState (includeGlobal), nullptr);
    return tree;
}

void ChromaProcessor::restoreState (const juce::ValueTree& source, bool keepGlobal)
{
    if (! source.hasType (state.state.getType())) return;
    const auto core = source.getChildWithName ("CORE");
    // Tree in the live order: missing parameters go back to their defaults,
    // Global ones keep their current value when keepGlobal.
    juce::ValueTree tree (source.getType());
    tree.copyPropertiesFrom (source, nullptr);
    for (const auto& cur : state.state)   // keep the live tree's child order (stable snapshots)
        if (auto* p = state.getParameter (cur.getProperty ("id").toString()))
        {
            const bool keep = keepGlobal && isGlobalParam (p->getParameterID());
            const auto child = source.getChildWithProperty ("id", p->getParameterID());
            juce::ValueTree c ("PARAM");
            c.setProperty ("id", p->getParameterID(), nullptr);
            c.setProperty ("value", ! keep && child.isValid() ? child.getProperty ("value")
                                                               : juce::var (p->convertFrom0to1 (keep ? p->getValue() : p->getDefaultValue())), nullptr);
            tree.appendChild (c, nullptr);
        }
    state.replaceState (tree);
    synth.loadState (core, ! keepGlobal);
}

void ChromaProcessor::markClean()
{
    lastSeenEdits = editCounter();
    pendingCommit = false;
}

void ChromaProcessor::idle()
{
    synth.pollMidiLearn();
    synth.flushMidiToHost();
    const auto now = juce::Time::getMillisecondCounterHiRes();
    if (const auto n = editCounter(); n != lastSeenEdits)
    {
        lastSeenEdits = n;
        lastEditMs = now;
        pendingCommit = true;
    }
    if (pendingCommit && now - lastEditMs > 400.0)
        commitHistory();
}

void ChromaProcessor::commitHistory()
{
    history.commit();
    markClean();
}

bool ChromaProcessor::undo()
{
    const bool ok = history.undo();
    markClean();
    return ok;
}

bool ChromaProcessor::redo()
{
    const bool ok = history.redo();
    markClean();
    return ok;
}

void ChromaProcessor::selectAB (int slot)
{
    history.selectSlot (slot);
    markClean();
}

int ChromaProcessor::randomize (const RandomizeScope& scope, float amount, juce::int64 seed)
{
    juce::Random rnd (seed != 0 ? seed : juce::Time::currentTimeMillis());
    history.commit();
    const int n = Randomizer::apply (synth, state, scope, amount, rnd);
    commitHistory();
    return n;
}

void ChromaProcessor::initAll()
{
    history.commit();
    juce::ValueTree empty (state.state.getType());
    restoreState (empty, true);
    synth.resetAll();
    currentPresetName = "Init";
    commitHistory();
}

PresetManager& ChromaProcessor::getPresets()
{
    if (presets == nullptr)
        presets = std::make_unique<PresetManager> (presetRoot == juce::File() ? PresetManager::defaultRoot() : presetRoot);
    return *presets;
}

void ChromaProcessor::setPresetRoot (const juce::File& root)
{
    presetRoot = root;
    presets.reset();
}

juce::File ChromaProcessor::saveMultiPreset (const PresetMeta& meta)
{
    const auto f = getPresets().save (PresetKind::Multi, captureState (false), meta);
    if (f != juce::File()) currentPresetName = meta.name;
    return f;
}

bool ChromaProcessor::loadMultiPreset (const juce::File& f)
{
    PresetInfo info;
    auto content = PresetManager::load (f, &info);
    if (! content || info.kind != PresetKind::Multi || ! content->hasType (state.state.getType())) return false;
    history.commit();
    restoreState (*content, true);
    currentPresetName = info.meta.name;
    commitHistory();
    return true;
}

juce::File ChromaProcessor::saveLayerPreset (int layer, const PresetMeta& meta)
{
    return getPresets().save (PresetKind::Layer, synth.copyLayer (layer), meta);
}

bool ChromaProcessor::loadLayerPreset (int layer, const juce::File& f)
{
    PresetInfo info;
    auto content = PresetManager::load (f, &info);
    if (! content || ! content->hasType ("LAYER_PRESET")) return false;
    history.commit();
    synth.pasteLayer (layer, *content);
    commitHistory();
    return true;
}
} // namespace chroma

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new chroma::ChromaProcessor();
}
