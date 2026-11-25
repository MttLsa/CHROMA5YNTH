#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "PluginProcessor.h"
#include "ParamIDs.h"
#include "GuiLayout.h"

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
