// Phase 13 – final QA: full-load CPU, host automation, host state save/restore.
#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <chrono>
#include <random>
#include <thread>
#include "TestHelpers.h"
#include "LayerParams.h"
namespace lp = chroma::lp;

namespace
{
constexpr double qaFs = 48000.0;
constexpr int qaBlock = 256;

void prepare (chroma::ChromaProcessor& p, double fs = qaFs, int block = qaBlock)
{
    p.setPlayConfigDetails (0, 2, fs, block);
    p.prepareToPlay (fs, block);
}

// 12 layers on, engines cycled Analog/Additive/FM6/Wavetable/PD, pool of 36 voices.
void fullLoad (chroma::ChromaProcessor& p, bool layerFx)
{
    test::setParam (p, chroma::ParamIDs::masterPolyphony, 2.0f);   // "36"
    for (int l = 0; l < 12; ++l)
    {
        test::setParam (p, chroma::ParamIDs::layerOn[(size_t) l], 1.0f);
        test::setParam (p, chroma::ParamIDs::layerEngine[(size_t) l], (float) (l % 5));
        auto& st = p.getSynth().layerStore (l);
        st.set (lp::env1Release, 2.0f);
        if (layerFx)
        {
            st.set (lp::fxChorusOn, 1.0f);
            st.set (lp::fxDelayOn, 1.0f);
            st.set (lp::fxReverbOn, 1.0f);
        }
    }
}

int activeVoices (chroma::ChromaProcessor& p)
{
    int n = 0;
    for (int i = 0; i < chroma::maxPoolVoices; ++i)
        n += p.getSynth().voiceInfo (i).active ? 1 : 0;
    return n;
}

bool bounded (const juce::AudioBuffer<float>& b, float lim = 4.0f)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int s = 0; s < b.getNumSamples(); ++s)
        {
            const float x = b.getSample (ch, s);
            if (! std::isfinite (x) || std::abs (x) > lim) return false;
        }
    return true;
}

// Measures 2 s of audio with 3 held notes (3 × 12 layers = 36 voices). Returns % of one core.
double measureFullLoad (bool layerFx)
{
    chroma::ChromaProcessor p;
    prepare (p);
    fullLoad (p, layerFx);
    juce::AudioBuffer<float> buf (2, qaBlock);
    juce::MidiBuffer midi;
    for (int n : { 48, 55, 64 })
        midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);
    p.processBlock (buf, midi);
    midi.clear();
    for (int i = 0; i < 20; ++i) p.processBlock (buf, midi);   // warm-up
    REQUIRE (activeVoices (p) == 36);

    const int blocks = (int) (2.0 * qaFs / qaBlock);
    bool ok = true;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < blocks; ++i)
    {
        p.processBlock (buf, midi);
        ok = ok && bounded (buf);
    }
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    REQUIRE (ok);
    REQUIRE (activeVoices (p) == 36);
    return secs / 2.0 * 100.0;
}
} // namespace

TEST_CASE ("QA: CPU with 12 active layers and 36 voices", "[qa]")
{
    const double dry = measureFullLoad (false);
    const double fx  = measureFullLoad (true);
    WARN ("12 layers × 5 engines, 36 voices, 2× OS @48 kHz/256: " << dry << "% of one core; + Chorus/Delay/Reverb per layer: " << fx << "%");
    // Must run faster than real time on a CI runner (Debug-free Release build).
    CHECK (dry < 100.0);
    CHECK (fx < 100.0);
}

TEST_CASE ("QA: voice stealing at full load stays bounded", "[qa]")
{
    chroma::ChromaProcessor p;
    prepare (p);
    fullLoad (p, false);
    juce::AudioBuffer<float> buf (2, qaBlock);
    std::mt19937 rng (7);
    bool ok = true;
    for (int i = 0; i < 300; ++i)
    {
        juce::MidiBuffer midi;
        const int note = 36 + (int) (rng() % 48);
        midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) (20 + rng() % 107)), (int) (rng() % qaBlock));
        if (i % 3 == 0) midi.addEvent (juce::MidiMessage::noteOff (1, 36 + (int) (rng() % 48)), (int) (rng() % qaBlock));
        p.processBlock (buf, midi);
        ok = ok && bounded (buf) && activeVoices (p) <= 36;
    }
    REQUIRE (ok);
}

TEST_CASE ("QA: host automation of every automatable parameter", "[qa]")
{
    chroma::ChromaProcessor p;
    prepare (p);
    juce::AudioBuffer<float> buf (2, qaBlock);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    midi.addEvent (juce::MidiMessage::noteOn (1, 67, (juce::uint8) 100), 0);
    p.processBlock (buf, midi);
    midi.clear();

    std::mt19937 rng (13);
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    int automatable = 0;
    for (auto* prm : p.getParameters())
    {
        if (! prm->isAutomatable()) continue;
        ++automatable;
        auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm);
        REQUIRE (rp != nullptr);
        INFO (rp->getParameterID());
        // Value <-> text round trip (what hosts display and type in).
        for (float v : { 0.0f, 0.5f, 1.0f })
        {
            const auto txt = rp->getText (v, 64);
            REQUIRE (txt.isNotEmpty());
            const float back = rp->getValueForText (txt);
            REQUIRE (std::isfinite (back));
            REQUIRE (back >= 0.0f);
            REQUIRE (back <= 1.0f);
        }
        // Sweep: extremes, jumps and a ramp, as a host lane would send.
        bool ok = true;
        for (float v : { 0.0f, 1.0f, 0.0f, u (rng), u (rng) })
        {
            rp->setValueNotifyingHost (v);
            p.processBlock (buf, midi);
            ok = ok && bounded (buf, 8.0f);
        }
        for (int i = 0; i <= 8; ++i)
        {
            rp->setValueNotifyingHost ((float) i / 8.0f);
            p.processBlock (buf, midi);
            ok = ok && bounded (buf, 8.0f);
        }
        rp->setValueNotifyingHost (rp->getDefaultValue());
        p.processBlock (buf, midi);
        REQUIRE (ok);
    }
    WARN ("automatable host parameters swept: " << automatable);
    REQUIRE (automatable > 0);
}

TEST_CASE ("QA: automation from another thread while rendering", "[qa]")
{
    chroma::ChromaProcessor p;
    prepare (p);
    fullLoad (p, true);
    std::vector<juce::RangedAudioParameter*> params;
    for (auto* prm : p.getParameters())
        if (prm->isAutomatable())
            params.push_back (dynamic_cast<juce::RangedAudioParameter*> (prm));

    std::atomic<bool> stop { false };
    std::thread host ([&]
    {
        std::mt19937 rng (21);
        while (! stop.load())
        {
            params[rng() % params.size()]->setValueNotifyingHost ((float) (rng() % 1001) / 1000.0f);
            std::this_thread::yield();
        }
    });
    juce::AudioBuffer<float> buf (2, qaBlock);
    bool ok = true;
    for (int i = 0; i < 400; ++i)
    {
        juce::MidiBuffer midi;
        if (i % 40 == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 48 + (i / 40) % 24, (juce::uint8) 100), 0);
        if (i % 40 == 30) midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
        p.processBlock (buf, midi);
        ok = ok && bounded (buf, 8.0f);
    }
    stop = true;
    host.join();
    REQUIRE (ok);
}

TEST_CASE ("QA: host state save/restore", "[qa]")
{
    // Random host + layer parameters, as after a session of editing.
    chroma::ChromaProcessor a;
    prepare (a);
    std::mt19937 rng (5);
    for (auto* prm : a.getParameters())
        prm->setValueNotifyingHost ((float) (rng() % 1001) / 1000.0f);
    for (int l = 0; l < 12; ++l)
    {
        auto& st = a.getSynth().layerStore (l);
        for (int i = 0; i < (int) lp::count; ++i)
        {
            const auto& d = lp::descs[(size_t) i];
            float v = d.min + (d.max - d.min) * (float) (rng() % 1001) / 1000.0f;
            if (d.type != lp::Type::Float) v = std::round (v);
            st.set ((lp::Id) i, v);
        }
    }

    juce::MemoryBlock s1;
    a.getStateInformation (s1);
    REQUIRE (s1.getSize() > 0);

    SECTION ("new instance restores everything, save is idempotent")
    {
        chroma::ChromaProcessor b;
        b.setStateInformation (s1.getData(), (int) s1.getSize());
        prepare (b, 44100.0, 512);   // host reopens the project at another rate / block size
        for (size_t i = 0; i < (size_t) a.getParameters().size(); ++i)
        {
            INFO (dynamic_cast<juce::RangedAudioParameter*> (a.getParameters()[(int) i])->getParameterID()
                  << " a=" << a.getParameters()[(int) i]->getValue() << " b=" << b.getParameters()[(int) i]->getValue());
            auto* pa = a.getParameters()[(int) i];
            auto* pb = b.getParameters()[(int) i];
            if (pa->isDiscrete() || pa->isBoolean())
                REQUIRE (pa->getCurrentValueAsText() == pb->getCurrentValueAsText());   // hosts may send unsnapped values
            else
                REQUIRE (std::abs (pa->getValue() - pb->getValue()) < 1e-5f);
        }
        for (int l = 0; l < 12; ++l)
            for (int i = 0; i < (int) lp::count; ++i)
            {
                INFO ("layer " << l << " " << lp::descs[(size_t) i].id);
                REQUIRE (std::abs (a.getSynth().layerStore (l).get ((lp::Id) i) - b.getSynth().layerStore (l).get ((lp::Id) i)) < 1e-4f);
            }
        juce::MemoryBlock s2;
        b.getStateInformation (s2);
        REQUIRE (s1 == s2);

        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        bool ok = true;
        for (int i = 0; i < 50; ++i) { b.processBlock (buf, midi); midi.clear(); ok = ok && bounded (buf, 8.0f); }
        REQUIRE (ok);
    }

    SECTION ("restore while playing, with the editor open")
    {
        chroma::ChromaProcessor b;
        prepare (b);
        std::unique_ptr<juce::AudioProcessorEditor> ed (b.createEditor());
        juce::AudioBuffer<float> buf (2, qaBlock);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        b.processBlock (buf, midi);
        midi.clear();
        bool ok = true;
        for (int i = 0; i < 5; ++i)
        {
            b.setStateInformation (s1.getData(), (int) s1.getSize());
            for (int k = 0; k < 10; ++k) { b.processBlock (buf, midi); ok = ok && bounded (buf, 8.0f); }
        }
        REQUIRE (ok);
        ed.reset();
    }

    SECTION ("garbage / truncated / empty state is ignored safely")
    {
        chroma::ChromaProcessor b;
        juce::MemoryBlock before;
        b.getStateInformation (before);
        b.setStateInformation (s1.getData(), (int) s1.getSize() / 2);
        const char junk[] = "not a chroma state";
        b.setStateInformation (junk, (int) sizeof (junk));
        b.setStateInformation (nullptr, 0);
        juce::MemoryBlock after;
        b.getStateInformation (after);
        REQUIRE (before == after);
    }

    SECTION ("missing parameters (older project) fall back to defaults")
    {
        auto xml = juce::AudioProcessor::getXmlFromBinary (s1.getData(), (int) s1.getSize());
        REQUIRE (xml != nullptr);
        for (auto* c = xml->getFirstChildElement(); c != nullptr;)
        {
            auto* next = c->getNextElement();
            if (c->getStringAttribute ("id") == chroma::ParamIDs::masterVolume) xml->removeChildElement (c, true);
            c = next;
        }
        juce::MemoryBlock old;
        juce::AudioProcessor::copyXmlToBinary (*xml, old);
        chroma::ChromaProcessor b;
        b.setStateInformation (old.getData(), (int) old.getSize());
        auto* mv = b.getState().getParameter (chroma::ParamIDs::masterVolume);
        REQUIRE (std::abs (mv->getValue() - mv->getDefaultValue()) < 1e-6f);
    }
}
