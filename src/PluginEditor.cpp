#include "PluginEditor.h"
#include "GuiLayout.h"

namespace chroma
{
using APVTS = juce::AudioProcessorValueTreeState;

struct ChromaEditor::Control
{
    juce::String section;
    juce::Label label;
    std::unique_ptr<juce::Component> widget;
    std::unique_ptr<APVTS::SliderAttachment> sliderAtt;
    std::unique_ptr<APVTS::ComboBoxAttachment> comboAtt;
    std::unique_ptr<APVTS::ButtonAttachment> buttonAtt;
};

juce::String ChromaEditor::aboutText()
{
    return juce::String (CHROMA_PRODUCT_NAME) + " v" + CHROMA_VERSION_STRING + "\n" + CHROMA_COMPANY_NAME;
}

ChromaEditor::ChromaEditor (ChromaProcessor& p) : AudioProcessorEditor (p)
{
    auto& apvts = p.getState();

    for (const auto& e : gui::params)
    {
        auto* param = apvts.getParameter (e.id);
        if (param == nullptr)
            continue;

        auto c = std::make_unique<Control>();
        c->section = e.section;
        c->label.setText (e.name, juce::dontSendNotification);
        c->label.setJustificationType (juce::Justification::centred);
        const juce::String type (e.type);

        if (type == "choice")
        {
            auto box = std::make_unique<juce::ComboBox>();
            box->addItemList (param->getAllValueStrings(), 1);
            c->comboAtt = std::make_unique<APVTS::ComboBoxAttachment> (apvts, e.id, *box);
            c->widget = std::move (box);
        }
        else if (type == "bool")
        {
            auto btn = std::make_unique<juce::ToggleButton>();
            c->buttonAtt = std::make_unique<APVTS::ButtonAttachment> (apvts, e.id, *btn);
            c->widget = std::move (btn);
        }
        else
        {
            auto s = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
            c->sliderAtt = std::make_unique<APVTS::SliderAttachment> (apvts, e.id, *s);
            c->widget = std::move (s);
        }

        addAndMakeVisible (c->label);
        addAndMakeVisible (*c->widget);
        controls.push_back (std::move (c));
    }

    aboutButton.onClick = [this]
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "About", aboutText(), {}, this);
    };
    addAndMakeVisible (aboutButton);

    setResizable (true, true);
    setSize (720, 420);
}

ChromaEditor::~ChromaEditor() = default;

void ChromaEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff15161a));
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText (CHROMA_PRODUCT_NAME, 16, 8, 300, 32, juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (13.0f));
    g.setColour (juce::Colours::grey);
    g.drawText (CHROMA_COMPANY_NAME, 16, 36, 300, 18, juce::Justification::centredLeft);

    int y = 64;
    for (const auto* s : gui::sections)
    {
        g.setColour (juce::Colours::lightgrey);
        g.drawText (s, 16, y, 300, 20, juce::Justification::centredLeft);
        y += 130;
    }
}

void ChromaEditor::resized()
{
    aboutButton.setBounds (getWidth() - 96, 12, 80, 26);

    int y = 64;
    for (const auto* s : gui::sections)
    {
        int x = 16;
        for (auto& c : controls)
        {
            if (c->section != s)
                continue;
            c->label.setBounds (x, y + 22, 96, 18);
            c->widget->setBounds (x, y + 42, 96, c->comboAtt || c->buttonAtt ? 24 : 80);
            x += 104;
        }
        y += 130;
    }
}
} // namespace chroma
