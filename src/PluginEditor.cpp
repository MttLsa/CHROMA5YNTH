#include "PluginEditor.h"
#include "GuiLayout.h"

namespace chroma
{
using APVTS = juce::AudioProcessorValueTreeState;

namespace
{
constexpr int knobW = 76, knobH = 96, segW = 120, segH = 40;
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
        content.addAndMakeVisible (*panels.back());
    }

    for (const auto& e : gui::params)
    {
        if (apvts.getParameter (e.id) == nullptr)
            continue;

        auto c = std::make_unique<Control>();
        c->section = e.section;
        const juce::String type (e.type);

        if (type == "choice" && e.numChoices > 4)
        {
            auto combo = std::make_unique<juce::ComboBox>();
            combo->addItemList (apvts.getParameter (e.id)->getAllValueStrings(), 1);
            combo->setTooltip (e.name);
            comboAttachments.push_back (std::make_unique<APVTS::ComboBoxAttachment> (apvts, e.id, *combo));
            c->widget = std::move (combo);
            c->compact = true;
        }
        else if (type == "choice" || type == "bool")
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

        content.addAndMakeVisible (*c->widget);
        controls.push_back (std::move (c));
    }

    aboutButton.onClick = [this]
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "About", aboutText(), {}, this);
    };
    addAndMakeVisible (aboutButton);

    wtEditor = std::make_unique<WavetableEditor> (p.getSynth());
    wtEditor->onClose = [this] { wtEditor->setVisible (false); wtButton.setToggleState (false, juce::dontSendNotification); };
    addChildComponent (*wtEditor);
    wtButton.setTooltip ("Editor delle wavetable utente (engine Wavetable).");
    wtButton.setClickingTogglesState (true);
    wtButton.onClick = [this] { wtEditor->setVisible (wtButton.getToggleState()); };
    addAndMakeVisible (wtButton);

    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
    setResizable (true, true);
    setResizeLimits (480, 320, 2400, 1800);
    setSize (980, 720);
}

ChromaEditor::~ChromaEditor()
{
    setLookAndFeel (nullptr);
}

void ChromaEditor::paint (juce::Graphics& g)
{
    g.fillAll (style::bg);
    const auto bar = juce::Rectangle<float> ((float) style::margin, 6.0f, (float) (getWidth() - 2 * style::margin - 190),
                                             (float) style::titleH - 12.0f);
    drawHeader (g, bar, "multi-engine layered synth", juce::String ("v") + CHROMA_VERSION_STRING);
}

void ChromaEditor::resized()
{
    aboutButton.setBounds (getWidth() - style::margin - 80, (style::titleH - 24) / 2, 80, 24);
    wtButton.setBounds (aboutButton.getX() - style::margin - 90, aboutButton.getY(), 90, 24);
    wtEditor->setBounds (getLocalBounds().withTrimmedTop (style::titleH));
    viewport.setBounds (getLocalBounds().withTrimmedTop (style::titleH));
    const int w = viewport.getWidth() - viewport.getScrollBarThickness();
    content.setSize (w, layoutContent (w));
}

// Panels stacked vertically; controls wrap into rows inside each panel.
int ChromaEditor::layoutContent (int width)
{
    const int rowH = knobH + style::margin;
    const int innerW = width - 2 * style::margin - 12;
    int y = 0;
    for (size_t i = 0; i < panels.size(); ++i)
    {
        int x = 0, rows = 1;
        for (auto& c : controls)
        {
            if (c->section != gui::sections[i]) continue;
            const int cw = c->compact ? segW : knobW;
            if (x > 0 && x + cw > innerW) { x = 0; ++rows; }
            x += cw + style::margin;
        }
        const int h = style::panelHeaderH + rows * rowH + 8;
        auto& panel = *panels[i];
        panel.setBounds (style::margin, y, width - 2 * style::margin, h);
        const auto area = panel.content() + panel.getPosition();
        x = 0;
        int row = 0;
        for (auto& c : controls)
        {
            if (c->section != gui::sections[i]) continue;
            const int cw = c->compact ? segW : knobW;
            if (x > 0 && x + cw > area.getWidth()) { x = 0; ++row; }
            const juce::Rectangle<int> cell (area.getX() + x, area.getY() + row * rowH, cw, knobH);
            c->widget->setBounds (c->compact ? cell.withSizeKeepingCentre (segW, segH) : cell);
            x += cw + style::margin;
        }
        y += h + style::margin;
    }
    return y;
}
} // namespace chroma
