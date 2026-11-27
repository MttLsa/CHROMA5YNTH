#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/catch_approx.hpp>
#include "PluginProcessor.h"
#include "ParamIDs.h"
#include "GuiLayout.h"
#include "gui/StyleLookAndFeel.h"
#include "core/LayerParamStore.h"
#include <cstring>

TEST_CASE ("Every generated parameter exists in the processor", "[params]")
{
    chroma::ChromaProcessor proc;
    auto& apvts = proc.getState();

    REQUIRE (proc.getParameters().size() == chroma::ParamIDs::count);
    for (const auto& e : chroma::gui::params)
        REQUIRE (apvts.getParameter (e.id) != nullptr);
}

TEST_CASE ("Parameter defaults match YAML", "[params]")
{
    chroma::ChromaProcessor proc;
    auto& apvts = proc.getState();
    REQUIRE_THAT (apvts.getRawParameterValue (chroma::ParamIDs::masterVolume)->load(), Catch::Matchers::WithinAbs (0.0, 1e-6));
    REQUIRE (apvts.getParameter (chroma::ParamIDs::masterPolyphony)->getCurrentValueAsText() == "24");
}

TEST_CASE ("Params: v1.0 Mod Matrix destinations are remapped after the Analog mixer removal", "[params]")
{
    using chroma::LayerParamStore;
    auto find = [] (const char* n) { for (int i = 0; i < chroma::lp::list_modDestinations_size; ++i) if (std::strcmp (chroma::lp::list_modDestinations[i], n) == 0) return i; return -1; };
    const int a = find ("OSC 1 Fine");
    REQUIRE (a > 0);
    CHECK (LayerParamStore::upgradeDestV1 (a - 1) == a - 1);              // before the Analog block: unchanged
    CHECK (LayerParamStore::upgradeDestV1 (a + 27) == 0);                 // Mixer OSC 1 Volume -> Off
    CHECK (LayerParamStore::upgradeDestV1 (a + 38) == find ("Output Level")); // Osc Master Level
    CHECK (LayerParamStore::upgradeDestV1 (a + 37) == find ("Sub Level"));
    CHECK (LayerParamStore::upgradeDestV1 (a + 43) == find ("Op 1 Ratio")); // after the block: shifted by 6
    juce::ValueTree old ("LAYER");
    old.setProperty ("mm1_dest", a + 43 + 5, nullptr);                    // v1.0 "Op 1 Output Level"
    LayerParamStore st;
    st.fromTree (old);
    CHECK ((int) st.get (chroma::lp::mmDest[0]) == find ("Op 1 Output Level"));
}

TEST_CASE ("GUI: values typed with the keyboard are parsed in the unit shown", "[params][gui]")
{
    chroma::Binding t;
    t.kind = chroma::Binding::Kind::Float; t.unit = "s"; t.min = 0.0f; t.max = 10.0f; t.def = 0.3f;
    t.get = [] { return 0.3f; };                       // readout shows "300 ms"
    CHECK (*t.parse ("250") == Catch::Approx (0.25f));
    CHECK (*t.parse ("1.5 s") == Catch::Approx (1.5f));
    CHECK (*t.parse ("20ms") == Catch::Approx (0.02f));
    chroma::Binding hz = t;
    hz.unit = "Hz"; hz.min = 20.0f; hz.max = 20000.0f; hz.get = [] { return 440.0f; };
    CHECK (*hz.parse ("1.2k") == Catch::Approx (1200.0f));
    CHECK (*hz.parse ("880") == Catch::Approx (880.0f));
    CHECK (*hz.parse ("99999") == Catch::Approx (20000.0f));   // clamped
    chroma::Binding pc = t;
    pc.unit = "%"; pc.min = 0.0f; pc.max = 1.0f; pc.get = [] { return 0.5f; };
    CHECK (*pc.parse ("45") == Catch::Approx (0.45f));
    chroma::Binding ch;
    ch.kind = chroma::Binding::Kind::Choice; ch.choices = { "Sine", "Saw", "Square" }; ch.max = 2.0f;
    CHECK (*ch.parse ("saw") == Catch::Approx (1.0f));
    CHECK (*ch.parse ("3") == Catch::Approx (2.0f));
    CHECK_FALSE (ch.parse ("zzz").has_value());
    CHECK_FALSE (t.parse ("abc").has_value());
}
