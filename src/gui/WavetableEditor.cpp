#include "WavetableEditor.h"
#include "core/Synth.h"

namespace chroma
{
namespace
{
constexpr int barH = 26, viewHarmonics = 64;

void framePanel (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title)
{
    g.setColour (style::panel);
    g.fillRoundedRectangle (r, style::panelRadius);
    g.setColour (style::outline);
    g.drawRoundedRectangle (r.reduced (0.5f), style::panelRadius, 1.0f);
    auto h = r.removeFromTop ((float) style::panelHeaderH);
    g.setColour (style::header);
    g.fillRoundedRectangle (h, style::panelRadius);
    g.setColour (style::accent);
    g.fillRect (h.removeFromLeft (3.0f));
    g.setColour (style::text);
    g.setFont (style::font (12.0f, true));
    g.drawText (title.toUpperCase(), h.withTrimmedLeft (6.0f), juce::Justification::centredLeft);
}

juce::Rectangle<float> body (juce::Rectangle<float> r) { return r.withTrimmedTop ((float) style::panelHeaderH).reduced (6.0f, 4.0f); }
} // namespace

// ------------------------------------------------------------------ 2D view + Draw mode
struct WavetableEditor::WaveView final : public juce::Component
{
    explicit WaveView (WavetableEditor& o) : owner (o) { setTooltip(); }
    void setTooltip() { setHelpText ("Disegna con il mouse sul frame corrente (Draw mode)."); }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        framePanel (g, r, "Frame " + juce::String (owner.current + 1) + " / " + juce::String (owner.doc.numFrames()));
        const auto b = body (r);
        g.setColour (style::track);
        g.drawHorizontalLine ((int) b.getCentreY(), b.getX(), b.getRight());
        const float* x = owner.doc.frame (owner.current);
        juce::Path p;
        const int step = std::max (1, wt::frameSize / (int) std::max (8.0f, b.getWidth()));
        for (int i = 0; i <= wt::frameSize; i += step)
        {
            const float v = x[i % wt::frameSize];
            const float px = b.getX() + b.getWidth() * (float) i / (float) wt::frameSize;
            const float py = b.getCentreY() - juce::jlimit (-1.0f, 1.0f, v) * b.getHeight() * 0.48f;
            if (i == 0) p.startNewSubPath (px, py); else p.lineTo (px, py);
        }
        g.setColour (style::accent);
        g.strokePath (p, juce::PathStrokeType (1.5f));
    }

    juce::Point<float> toWave (juce::Point<float> pt) const
    {
        const auto b = body (getLocalBounds().toFloat());
        return { juce::jlimit (0.0f, 1.0f, (pt.x - b.getX()) / b.getWidth()),
                 juce::jlimit (-1.0f, 1.0f, (b.getCentreY() - pt.y) / (b.getHeight() * 0.48f)) };
    }
    void mouseDown (const juce::MouseEvent& e) override { last = toWave (e.position); mouseDrag (e); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto p = toWave (e.position);
        owner.doc.drawLine (owner.current, last.x, last.y, p.x, p.y);
        last = p;
        owner.changed (false);
    }

    WavetableEditor& owner;
    juce::Point<float> last;
};

// ------------------------------------------------------------------ 3D view
struct WavetableEditor::View3D final : public juce::Component
{
    explicit View3D (WavetableEditor& o) : owner (o) {}

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        framePanel (g, r, "3D");
        const auto b = body (r);
        const int n = owner.doc.numFrames();
        const float depthX = b.getWidth() * 0.25f, depthY = b.getHeight() * 0.45f;
        const float w = b.getWidth() - depthX, amp = b.getHeight() * 0.22f;
        const int every = std::max (1, n / 48);
        const auto drawFrame = [&] (int f, juce::Colour c, float thick)
        {
            const float t = n > 1 ? (float) f / (float) (n - 1) : 0.0f;
            const float ox = b.getX() + depthX * t, oy = b.getBottom() - amp - depthY * t;
            const float* x = owner.doc.frame (f);
            juce::Path p;
            for (int i = 0; i <= 128; ++i)
            {
                const float v = juce::jlimit (-1.0f, 1.0f, x[(i * wt::frameSize / 128) % wt::frameSize]);
                const float px = ox + w * (float) i / 128.0f, py = oy - v * amp;
                if (i == 0) p.startNewSubPath (px, py); else p.lineTo (px, py);
            }
            g.setColour (c);
            g.strokePath (p, juce::PathStrokeType (thick));
        };
        for (int f = n - 1; f >= 0; f -= every)
            drawFrame (f, style::textDim.withAlpha (0.25f + 0.5f * (1.0f - (float) f / (float) std::max (1, n))), 1.0f);
        drawFrame (owner.current, style::accent2, 1.8f);
    }

    void mouseDown (const juce::MouseEvent& e) override { pick (e); }
    void mouseDrag (const juce::MouseEvent& e) override { pick (e); }
    void pick (const juce::MouseEvent& e)
    {
        const auto b = body (getLocalBounds().toFloat());
        const float t = juce::jlimit (0.0f, 1.0f, (b.getBottom() - e.position.y) / b.getHeight());
        owner.frameSlider.setValue (1.0 + std::round (t * (owner.doc.numFrames() - 1)));
    }

    WavetableEditor& owner;
};

// ------------------------------------------------------------------ harmonic editor
struct WavetableEditor::HarmonicView final : public juce::Component
{
    explicit HarmonicView (WavetableEditor& o) : owner (o) {}

    void refresh()
    {
        owner.doc.harmonics (owner.current, viewHarmonics, amp, phase);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        framePanel (g, r, showPhase ? "Harmonics · Phase" : "Harmonics · Amplitude");
        const auto b = body (r);
        if (amp.empty()) return;
        const float bw = b.getWidth() / (float) viewHarmonics;
        for (int k = 0; k < viewHarmonics; ++k)
        {
            const float v = showPhase ? (phase[(size_t) k] / juce::MathConstants<float>::pi) * 0.5f + 0.5f
                                      : juce::jlimit (0.0f, 1.0f, amp[(size_t) k]);
            const auto bar = juce::Rectangle<float> (b.getX() + bw * (float) k + 1.0f, b.getBottom() - v * b.getHeight(),
                                                     std::max (1.0f, bw - 2.0f), v * b.getHeight());
            g.setColour ((k % 2) == 0 ? style::accent : style::accent.withAlpha (0.75f));
            g.fillRect (bar);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override { edit (e); }
    void mouseDrag (const juce::MouseEvent& e) override { edit (e); }
    void edit (const juce::MouseEvent& e)
    {
        const auto b = body (getLocalBounds().toFloat());
        const int k = juce::jlimit (0, viewHarmonics - 1, (int) ((e.position.x - b.getX()) / (b.getWidth() / viewHarmonics)));
        const float v = juce::jlimit (0.0f, 1.0f, (b.getBottom() - e.position.y) / b.getHeight());
        if (amp.empty()) return;
        if (showPhase) phase[(size_t) k] = (v - 0.5f) * 2.0f * juce::MathConstants<float>::pi;
        else amp[(size_t) k] = v;
        owner.doc.setHarmonic (owner.current, k + 1, amp[(size_t) k], phase[(size_t) k]);
        owner.changed (false);
    }

    WavetableEditor& owner;
    bool showPhase = false;
    std::vector<float> amp, phase;
};

// ------------------------------------------------------------------ editor
void WavetableEditor::addButton (juce::TextButton& b, const juce::String& tip, std::function<void()> fn)
{
    b.setTooltip (tip);
    b.onClick = std::move (fn);
    addAndMakeVisible (b);
}

WavetableEditor::WavetableEditor (Synth& s) : synth (s)
{
    wave = std::make_unique<WaveView> (*this);
    view3d = std::make_unique<View3D> (*this);
    harm = std::make_unique<HarmonicView> (*this);
    addAndMakeVisible (*wave);
    addAndMakeVisible (*view3d);
    addAndMakeVisible (*harm);

    for (int l = 1; l <= numLayers; ++l) layerBox.addItem ("Layer " + juce::String (l), l);
    for (int u = 1; u <= wt::numUserSlots; ++u) slotBox.addItem ("User " + juce::String (u), u);
    factoryBox.setTextWhenNothingSelected ("Factory…");
    for (int i = 0; i < wt::numFactoryTables(); ++i) factoryBox.addItem (wt::factoryName (i), i + 1);
    interpBox.addItemList ({ "None", "Linear", "Spectral" }, 1);
    interpBox.setSelectedId (3, juce::dontSendNotification);
    layerBox.setSelectedId (1, juce::dontSendNotification);
    slotBox.setSelectedId (1, juce::dontSendNotification);
    layerBox.setTooltip ("Layer di destinazione.");
    slotBox.setTooltip ("Slot utente: User N viene assegnato a WT OSC N con Apply.");
    factoryBox.setTooltip ("Carica una tabella di fabbrica come punto di partenza.");
    interpBox.setTooltip ("Interpolazione per Resample e Morph: nessuna, lineare o spettrale (ampiezza + fase).");
    layerBox.onChange = slotBox.onChange = [this] { loadSlot(); };
    factoryBox.onChange = [this]
    {
        if (factoryBox.getSelectedId() <= 0) return;
        int nf = 0;
        auto fr = wt::factoryFrames (factoryBox.getSelectedId() - 1, nf);
        doc.setFrames (std::move (fr), nf);
        changed (true);
    };
    for (auto* c : { &layerBox, &slotBox, &factoryBox, &interpBox })
        addAndMakeVisible (*c);

    const auto setupSlider = [this] (juce::Slider& sl, juce::Label& lb, const juce::String& name, double lo, double hi,
                                     double v, const juce::String& tip)
    {
        sl.setSliderStyle (juce::Slider::LinearHorizontal);
        sl.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 20);
        sl.setRange (lo, hi, 1.0);
        sl.setValue (v, juce::dontSendNotification);
        sl.setTooltip (tip);
        lb.setText (name, juce::dontSendNotification);
        lb.setFont (style::font (11.0f));
        lb.setColour (juce::Label::textColourId, style::textDim);
        addAndMakeVisible (sl);
        addAndMakeVisible (lb);
    };
    setupSlider (frameSlider, frameLabel, "Frame", 1, 1, 1, "Frame corrente.");
    setupSlider (countSlider, countLabel, "Frames", 1, wt::maxFrames, 32, "Numero di frame per Resample.");
    setupSlider (morphSlider, morphLabel, "Morph To", 1, 1, 1, "Frame di arrivo del Morph (dal frame corrente).");
    setupSlider (smoothSlider, smoothLabel, "Smooth", 1, 100, 30, "Quantità di smoothing (%).");
    frameSlider.onValueChange = [this]
    {
        current = juce::jlimit (0, doc.numFrames() - 1, (int) frameSlider.getValue() - 1);
        changed (false);
    };

    addButton (importBtn, "Importa un file WAV (2048 campioni per frame, fino a 256 frame).", [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Import WAV", juce::File(), "*.wav");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (f == juce::File()) return;
            juce::String err;
            if (! doc.importWav (f, err))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Import", err, {}, this);
            changed (true);
        });
    });
    addButton (exportBtn, "Esporta la tabella come WAV (float 32 bit, 2048 campioni per frame).", [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Export WAV", juce::File(), "*.wav");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (f != juce::File()) doc.exportWav (f.withFileExtension ("wav"));
        });
    });
    addButton (addBtn, "Aggiunge un frame (copia del corrente) dopo il frame corrente.", [this] { doc.addFrame (current); ++current; changed (true); });
    addButton (removeBtn, "Rimuove il frame corrente.", [this] { doc.removeFrame (current); changed (true); });
    addButton (dupBtn, "Duplica il frame corrente.", [this] { doc.duplicateFrame (current); changed (true); });
    addButton (resampleBtn, "Ricampiona la tabella al numero di frame indicato con l'interpolazione scelta.",
               [this] { doc.resample ((int) countSlider.getValue(), interp()); changed (true); });
    addButton (morphBtn, "Riscrive i frame tra il corrente e Morph To interpolando dall'uno all'altro.",
               [this] { doc.morph (current, (int) morphSlider.getValue() - 1, interp()); changed (true); });
    addButton (smoothBtn, "Smussa il frame corrente (Shift = tutti i frame).", [this]
    {
        const bool all = juce::ModifierKeys::currentModifiers.isShiftDown();
        doc.smooth (all ? -1 : current, (float) smoothSlider.getValue() * 0.01f);
        changed (false);
    });
    addButton (normBtn, "Rimuove il DC e normalizza ogni frame al picco (Shift = guadagno unico per la tabella).", [this]
    {
        doc.normalize (! juce::ModifierKeys::currentModifiers.isShiftDown());
        changed (false);
    });
    addButton (phaseBtn, "Editor armonico: ampiezza o fase.", [this]
    {
        harm->showPhase = ! harm->showPhase;
        phaseBtn.setButtonText (harm->showPhase ? "Amplitude" : "Phase");
        harm->repaint();
    });
    addButton (applyBtn, "Invia la tabella allo slot User del Layer e la seleziona su WT OSC corrispondente.", [this] { apply(); });
    addButton (closeBtn, "Chiude l'editor.", [this] { if (onClose) onClose(); });

    loadSlot();
}

WavetableEditor::~WavetableEditor() = default;

wt::WavetableDoc::Interp WavetableEditor::interp() const
{
    return (wt::WavetableDoc::Interp) juce::jlimit (0, 2, interpBox.getSelectedId() - 1);
}

void WavetableEditor::loadSlot()
{
    const int layer = layerBox.getSelectedId() - 1, slot = slotBox.getSelectedId() - 1;
    const auto& u = synth.layerStore (layer).userTable (slot);
    if (u.numFrames > 0) doc.setFrames (u.frames, u.numFrames);
    else doc = wt::WavetableDoc();
    current = 0;
    changed (true);
}

void WavetableEditor::changed (bool structure)
{
    const int n = doc.numFrames();
    current = juce::jlimit (0, n - 1, current);
    if (structure)
    {
        frameSlider.setRange (1.0, std::max (2.0, (double) n), 1.0);
        morphSlider.setRange (1.0, std::max (2.0, (double) n), 1.0);
        morphSlider.setValue (n, juce::dontSendNotification);
        frameSlider.setEnabled (n > 1);
    }
    frameSlider.setValue (current + 1, juce::dontSendNotification);
    harm->refresh();
    wave->repaint();
    view3d->repaint();
}

void WavetableEditor::apply()
{
    const int layer = layerBox.getSelectedId() - 1, slot = slotBox.getSelectedId() - 1;
    synth.setUserWavetable (layer, slot, doc.frames(), doc.numFrames());
    auto& st = synth.layerStore (layer);
    st.set (lp::wtOscTable[(size_t) slot], (float) (wt::numFactoryTables() + slot));
    st.set (lp::wtOscOn[(size_t) slot], 1.0f);
}

void WavetableEditor::paint (juce::Graphics& g)
{
    g.fillAll (style::bg);
}

void WavetableEditor::resized()
{
    auto r = getLocalBounds().reduced (style::margin);
    auto row1 = r.removeFromTop (barH);
    const auto place = [] (juce::Rectangle<int>& row, juce::Component& c, int w)
    {
        c.setBounds (row.removeFromLeft (w));
        row.removeFromLeft (style::margin / 2);
    };
    place (row1, layerBox, 90);
    place (row1, slotBox, 80);
    place (row1, factoryBox, 150);
    place (row1, importBtn, 64);
    place (row1, exportBtn, 64);
    place (row1, applyBtn, 64);
    closeBtn.setBounds (row1.removeFromRight (64));
    r.removeFromTop (style::margin / 2);

    auto row2 = r.removeFromTop (barH);
    place (row2, addBtn, 52);
    place (row2, removeBtn, 64);
    place (row2, dupBtn, 76);
    place (row2, interpBox, 90);
    place (row2, countLabel, 44);
    place (row2, countSlider, 130);
    place (row2, resampleBtn, 76);
    place (row2, morphLabel, 60);
    place (row2, morphSlider, 120);
    place (row2, morphBtn, 60);
    r.removeFromTop (style::margin / 2);

    auto row3 = r.removeFromTop (barH);
    place (row3, frameLabel, 44);
    place (row3, frameSlider, 220);
    place (row3, smoothLabel, 50);
    place (row3, smoothSlider, 130);
    place (row3, smoothBtn, 64);
    place (row3, normBtn, 80);
    phaseBtn.setBounds (row3.removeFromRight (90));
    r.removeFromTop (style::margin);

    auto bottom = r.removeFromBottom (r.getHeight() * 2 / 5);
    harm->setBounds (bottom);
    r.removeFromBottom (style::margin);
    wave->setBounds (r.removeFromLeft (r.getWidth() / 2 - style::margin / 2));
    r.removeFromLeft (style::margin);
    view3d->setBounds (r);
}
} // namespace chroma
