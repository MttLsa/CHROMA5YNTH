#include "PluginEditor.h"
#include "GuiLayout.h"

namespace chroma
{
using APVTS = juce::AudioProcessorValueTreeState;

namespace
{
constexpr int knobW = 76, knobH = 96, segW = 120, segH = 40, panelH = style::panelHeaderH + knobH + 12;
}

struct ChromaEditor::Control
{
    juce::String section;
    bool compact = false;                          // segmented selector instead of a knob
    std::unique_ptr<juce::Component> widget;
};

juce::String ChromaEditor::aboutText()
{
    return juce::String (CHROMA_PRODUCT_NAME) + " v" + CHROMA_VERSION_STRING + "\n" + CHROMA_COMPANY_NAME;
}

ChromaEditor::ChromaEditor (ChromaProcessor& p) : AudioProcessorEditor (p)
{
    setLookAndFeel (&lookAndFeel);
    auto& apvts = p.getState();

    for (const auto* s : gui::sections)
    {
        panels.push_back (std::make_unique<Panel> (s));
        addAndMakeVisible (*panels.back());
    }

    for (const auto& e : gui::params)
    {
        if (apvts.getParameter (e.id) == nullptr)
            continue;

        auto c = std::make_unique<Control>();
        c->section = e.section;
        const juce::String type (e.type);

        if (type == "choice" || type == "bool")
        {
            auto seg = std::make_unique<Segmented>();
            seg->attach (apvts, e.id);
            c->widget = std::move (seg);
            c->compact = true;
        }
        else
        {
            auto knob = std::make_unique<Knob> (e.name);
            knob->attach (apvts, e.id);
            c->widget = std::move (knob);
        }

        addAndMakeVisible (*c->widget);
        controls.push_back (std::move (c));
    }

    aboutButton.onClick = [this]
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "About", aboutText(), {}, this);
    };
    addAndMakeVisible (aboutButton);

    const int h = style::titleH + (int) gui::sections.size() * (panelH + style::margin) + style::margin;
    setResizable (true, true);
    setResizeLimits (480, h, 2000, 1600);
    setSize (720, h);
}

ChromaEditor::~ChromaEditor()
{
    setLookAndFeel (nullptr);
}

void ChromaEditor::paint (juce::Graphics& g)
{
    g.fillAll (style::bg);
    const auto bar = juce::Rectangle<float> ((float) style::margin, 6.0f, (float) (getWidth() - 2 * style::margin - 90),
                                             (float) style::titleH - 12.0f);
    drawHeader (g, bar, "multi-engine layered synth", juce::String ("v") + CHROMA_VERSION_STRING);
}

void ChromaEditor::resized()
{
    aboutButton.setBounds (getWidth() - style::margin - 80, (style::titleH - 24) / 2, 80, 24);

    int y = style::titleH;
    for (size_t i = 0; i < panels.size(); ++i)
    {
        auto& panel = *panels[i];
        panel.setBounds (style::margin, y, getWidth() - 2 * style::margin, panelH);
        auto area = panel.content() + panel.getPosition();
        for (auto& c : controls)
        {
            if (c->section != gui::sections[i])
                continue;
            auto cell = area.removeFromLeft (c->compact ? segW : knobW);
            c->widget->setBounds (c->compact ? cell.withSizeKeepingCentre (segW, segH) : cell);
            area.removeFromLeft (style::margin);
        }
        y += panelH + style::margin;
    }
}
} // namespace chroma
