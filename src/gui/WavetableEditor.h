#pragma once
#include "StyleLookAndFeel.h"
#include "engines/wavetable/WavetableDoc.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace chroma
{
class Synth;

// WAVETABLE EDITOR (§3D): edits a user table (WavetableDoc) and publishes it to a layer's User slot.
// Views: 2D frame (Draw mode), 3D stack of frames, harmonic editor (FFT amplitude / phase).
class WavetableEditor final : public juce::Component
{
public:
    explicit WavetableEditor (Synth&);
    ~WavetableEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onClose;

private:
    struct WaveView;
    struct View3D;
    struct HarmonicView;

    void loadSlot();
    void changed (bool structure);   // doc edited -> refresh views / controls
    void apply();
    wt::WavetableDoc::Interp interp() const;
    void addButton (juce::TextButton&, const juce::String& tip, std::function<void()>);

    Synth& synth;
    wt::WavetableDoc doc;
    int current = 0;

    juce::ComboBox layerBox, slotBox, factoryBox, interpBox;
    juce::TextButton importBtn { "Import" }, exportBtn { "Export" }, addBtn { "Add" }, removeBtn { "Remove" },
                     dupBtn { "Duplicate" }, resampleBtn { "Resample" }, morphBtn { "Morph" }, smoothBtn { "Smooth" },
                     normBtn { "Normalize" }, applyBtn { "Apply" }, closeBtn { "Close" }, phaseBtn { "Phase" };
    juce::Slider frameSlider, countSlider, morphSlider, smoothSlider;
    juce::Label frameLabel, countLabel, morphLabel, smoothLabel;
    std::unique_ptr<WaveView> wave;
    std::unique_ptr<View3D> view3d;
    std::unique_ptr<HarmonicView> harm;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WavetableEditor)
};
} // namespace chroma
