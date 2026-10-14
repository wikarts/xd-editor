#include "Widgets.h"

using namespace xd;
static juce::String u (const char* s) { return juce::String::fromUTF8 (s); }

// ======================================================================= ParamControl
juce::Array<ParamControl*>& ParamControl::registry()
{
    static juce::Array<ParamControl*> r;
    return r;
}

void ParamControl::refreshAll()
{
    for (auto* c : registry()) c->refresh();
}

ParamControl::ParamControl (XDProcessor& p, const juce::String& key) : proc (p), pi (ParamTable::get().indexOf (key))
{
    jassert (pi >= 0);
    title = def().name;
    registry().add (this);
    setTooltip (def().desc);
}

ParamControl::~ParamControl() { registry().removeFirstMatchingValue (this); }

void ParamControl::applyNonCcMode (int mode)
{
    if (! isGreen()) return;
    setVisible (mode != 2);
    setEnabled (mode == 0);
}

// ======================================================================= Knob
Knob::Knob (XDProcessor& p, const juce::String& key, const juce::String& t, bool sv) : ParamControl (p, key), showValue (sv)
{
    if (t.isNotEmpty()) title = t;
    auto& d = def();
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    slider.setRange (d.minV, d.maxV, 1.0);
    slider.setDoubleClickReturnValue (true, d.defV);
    slider.setMouseDragSensitivity (d.range() > 200 ? 300 : 150);
    slider.setVelocityBasedMode (false);
    slider.getProperties().set ("green", isGreen());
    slider.getProperties().set ("bipolar", d.defV > d.minV && d.defV < d.maxV && std::abs ((d.defV - d.minV) * 2 - d.range()) <= 2);
    slider.setPopupDisplayEnabled (false, false, nullptr);
    slider.onDown = [this] { showInfo(); };
    slider.addListener (this);
    addAndMakeVisible (slider);
    refresh();
}

void Knob::refresh()
{
    const juce::ScopedValueSetter<bool> s (updating, true);
    slider.setValue (proc.getValue (pi), juce::dontSendNotification);
    repaint();
}

void Knob::resized()
{
    auto r = getLocalBounds();
    r.removeFromBottom (showValue ? 28 : 15);
    slider.setBounds (r);
}

void Knob::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    auto bottom = r.removeFromBottom (showValue ? 28 : 15);
    g.setColour (titleColour().withAlpha (isEnabled() ? 1.0f : 0.4f));
    g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    g.drawFittedText (title.toUpperCase(), bottom.removeFromTop (14), juce::Justification::centred, 1, 0.7f);
    if (showValue)
    {
        g.setColour (col::dim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawFittedText (formatInContext (def(), proc.getCurrent(), &proc.getSettings().userNames),
                          bottom, juce::Justification::centred, 1, 0.7f);
    }
}

void Knob::sliderValueChanged (juce::Slider*)
{
    if (updating) return;
    proc.setValueFromUI (pi, (int) slider.getValue());
    repaint();
}

void Knob::sliderDragStarted (juce::Slider*) { proc.beginGesture (pi); showInfo(); }
void Knob::sliderDragEnded (juce::Slider*) { proc.endGesture (pi); }

// ======================================================================= Switch
Switch::Switch (XDProcessor& p, const juce::String& key, const juce::String& t) : ParamControl (p, key)
{
    if (t.isNotEmpty()) title = t;
    setRepaintsOnMouseActivity (true);
}

void Switch::paint (juce::Graphics& g)
{
    auto& d = def();
    auto labels = labelsFor (d, &proc.getSettings().userNames);
    const int v = proc.getValue (pi) - d.valueBase;
    auto r = getLocalBounds();
    auto head = r.removeFromBottom (15);
    g.setColour (titleColour().withAlpha (isEnabled() ? 1.0f : 0.4f));
    g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    g.drawFittedText (title.toUpperCase(), head, juce::Justification::centred, 1, 0.7f);

    const int n = juce::jmax (1, labels.size());
    const int rowH = juce::jmin (16, r.getHeight() / n);
    auto list = r.withSizeKeepingCentre (r.getWidth(), rowH * n);
    for (int i = n - 1; i >= 0; --i) // top = highest value, like the hardware
    {
        auto row = list.removeFromTop (rowH);
        const bool on = i == v;
        auto led = row.removeFromLeft (14).toFloat().withSizeKeepingCentre (7.0f, 7.0f);
        g.setColour (on ? (isGreen() ? col::green : col::accent) : juce::Colour (0xff3a3d43));
        g.fillEllipse (led);
        g.setColour (on ? col::text : col::dim);
        g.setFont (juce::FontOptions (11.0f, on ? juce::Font::bold : juce::Font::plain));
        g.drawFittedText (labels[i], row, juce::Justification::centredLeft, 1, 0.6f);
    }
    if (isMouseOver())
    {
        g.setColour (col::accent.withAlpha (0.25f));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 3.0f, 1.0f);
    }
}

void Switch::mouseDown (const juce::MouseEvent& e)
{
    showInfo();
    if (e.y >= getHeight() - 15) return; // clicking the label only shows info
    auto& d = def();
    int v = proc.getValue (pi) + (e.mods.isRightButtonDown() ? -1 : 1);
    if (v > d.maxV) v = d.minV;
    if (v < d.minV) v = d.maxV;
    proc.beginGesture (pi);
    proc.setValueFromUI (pi, v);
    proc.endGesture (pi);
    repaint();
}

// ======================================================================= Choice
Choice::Choice (XDProcessor& p, const juce::String& key, const juce::String& t, bool left) : ParamControl (p, key), labelLeft (left)
{
    if (t.isNotEmpty()) title = t;
    addAndMakeVisible (box);
    box.onChange = [this]
    {
        if (updating || box.getSelectedId() == 0) return;
        proc.beginGesture (pi);
        const int idx = box.getSelectedId() - 1;
        if (juce::isPositiveAndBelow (idx, (int) values.size())) proc.setValueFromUI (pi, values[(size_t) idx]);
        proc.endGesture (pi);
    };
    refresh();
}

void Choice::refresh()
{
    const juce::ScopedValueSetter<bool> s (updating, true);
    auto& d = def();
    juce::StringArray items;
    std::vector<int> vals;
    if (d.disp == Disp::Labels)
    {
        items = labelsFor (d, &proc.getSettings().userNames);
        for (int i = 0; i < items.size(); ++i) vals.push_back (d.minV + i);
    }
    else
        for (int v = d.minV; v <= d.maxV; ++v)
        {
            if (d.disp == Disp::MicroTuning && v > 22 && v < 128) continue; // undefined values
            items.add (formatValue (d, v, &proc.getSettings().userNames));
            vals.push_back (v);
        }
    bool same = box.getNumItems() == items.size();
    for (int i = 0; same && i < items.size(); ++i) same = box.getItemText (i) == items[i];
    if (! same)
    {
        box.clear (juce::dontSendNotification);
        for (int i = 0; i < items.size(); ++i) box.addItem (items[i], i + 1);
    }
    values = vals;
    const auto it = std::find (values.begin(), values.end(), proc.getValue (pi));
    box.setSelectedId (it == values.end() ? 0 : (int) (it - values.begin()) + 1, juce::dontSendNotification);
}

void Choice::resized()
{
    auto r = getLocalBounds();
    if (labelLeft) r.removeFromLeft (labelWidth);
    else r.removeFromBottom (15);
    box.setBounds (labelLeft ? r.reduced (0, 2) : r.withSizeKeepingCentre (r.getWidth(), juce::jmin (24, r.getHeight())));
}

void Choice::paint (juce::Graphics& g)
{
    g.setColour (titleColour().withAlpha (isEnabled() ? 1.0f : 0.4f));
    if (labelLeft)
    {
        g.setFont (juce::FontOptions (12.5f));
        g.drawFittedText (title, getLocalBounds().removeFromLeft (labelWidth - 6), juce::Justification::centredLeft, 1, 0.7f);
    }
    else
    {
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawFittedText (title.toUpperCase(), getLocalBounds().removeFromBottom (15), juce::Justification::centred, 1, 0.7f);
    }
}

// ======================================================================= Bar
Bar::Bar (XDProcessor& p, const juce::String& key, const juce::String& t) : ParamControl (p, key)
{
    if (t.isNotEmpty()) title = t;
    auto& d = def();
    slider.setSliderStyle (juce::Slider::LinearBar);
    slider.setRange (d.minV, d.maxV, 1.0);
    slider.setDoubleClickReturnValue (true, d.defV);
    slider.getProperties().set ("green", isGreen());
    slider.textFromValueFunction = [this] (double v)
    {
        return formatInContext (def(), proc.getCurrent(), &proc.getSettings().userNames, (int) v);
    };
    slider.setColour (juce::Slider::textBoxTextColourId, col::text);
    slider.addListener (this);
    addAndMakeVisible (slider);
    refresh();
}

void Bar::refresh()
{
    const juce::ScopedValueSetter<bool> s (updating, true);
    slider.setValue (proc.getValue (pi), juce::dontSendNotification);
    slider.updateText();
}

void Bar::resized() { slider.setBounds (getLocalBounds().withTrimmedLeft (labelWidth).reduced (0, 2)); }

void Bar::paint (juce::Graphics& g)
{
    g.setColour (titleColour().withAlpha (isEnabled() ? 1.0f : 0.4f));
    g.setFont (juce::FontOptions (12.5f));
    g.drawFittedText (title, getLocalBounds().removeFromLeft (labelWidth - 6), juce::Justification::centredLeft, 1, 0.7f);
}

void Bar::sliderValueChanged (juce::Slider*)
{
    if (! updating) proc.setValueFromUI (pi, (int) slider.getValue());
}
void Bar::sliderDragStarted (juce::Slider*) { proc.beginGesture (pi); showInfo(); }
void Bar::sliderDragEnded (juce::Slider*) { proc.endGesture (pi); }

std::unique_ptr<ParamControl> makeRowControl (XDProcessor& p, const juce::String& key)
{
    auto* d = ParamTable::get().byKey (key);
    jassert (d != nullptr);
    if (d->disp == Disp::Labels || d->disp == Disp::MicroTuning)
        return std::make_unique<Choice> (p, key, juce::String(), true);
    return std::make_unique<Bar> (p, key);
}

// ======================================================================= Section
void PanelBox::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (col::panel);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (col::panelEdge);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
    g.setColour (col::dim);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (titleText.toUpperCase(), getLocalBounds().reduced (10, 4).removeFromTop (14), juce::Justification::centredLeft);
}

// ======================================================================= Envelope
EnvelopeGraph::EnvelopeGraph (XDProcessor& p, bool a) : proc (p), adsr (a)
{
    auto& t = ParamTable::get();
    if (adsr)
    {
        keys[0] = t.indexOf ("amp_attack"); keys[1] = t.indexOf ("amp_decay");
        keys[2] = t.indexOf ("amp_sustain"); keys[3] = t.indexOf ("amp_release");
    }
    else
    {
        keys[0] = t.indexOf ("eg_attack"); keys[1] = t.indexOf ("eg_decay"); keys[2] = keys[3] = -1;
    }
    proc.addXDListener (this);
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

EnvelopeGraph::~EnvelopeGraph() { proc.removeXDListener (this); }

juce::Array<juce::Point<float>> EnvelopeGraph::nodes() const
{
    auto a = area();
    const float seg = a.getWidth() / (adsr ? 4.0f : 2.0f);
    auto v = [this] (int k) { return keys[k] >= 0 ? (float) proc.getValue (keys[k]) / 1023.0f : 0.0f; };
    juce::Array<juce::Point<float>> n;
    n.add ({ a.getX(), a.getBottom() });
    const float ax = a.getX() + v (0) * seg;
    n.add ({ ax, a.getY() });
    if (adsr)
    {
        const float sy = a.getBottom() - v (2) * a.getHeight();
        const float dx = ax + v (1) * seg;
        n.add ({ dx, sy });
        const float sx = dx + seg * 0.6f;
        n.add ({ sx, sy });
        n.add ({ sx + v (3) * seg, a.getBottom() });
    }
    else
        n.add ({ ax + v (1) * seg, a.getBottom() });
    return n;
}

void EnvelopeGraph::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (col::lcdBg);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (col::panelEdge);
    g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
    auto n = nodes();
    juce::Path p;
    p.startNewSubPath (n[0]);
    for (int i = 1; i < n.size(); ++i) p.lineTo (n[i]);
    juce::Path fill (p);
    fill.lineTo (n.getLast().x, area().getBottom());
    fill.closeSubPath();
    g.setColour (col::accent2.withAlpha (0.12f));
    g.fillPath (fill);
    g.setColour (col::accent2);
    g.strokePath (p, juce::PathStrokeType (1.6f));
    const int handles[] = { 1, 2, 3, 4 };
    for (auto h : handles)
        if (h < n.size())
        {
            g.setColour (h - 1 == dragNode ? col::accent : col::lcdText);
            g.fillEllipse (n[h].x - 3.5f, n[h].y - 3.5f, 7.0f, 7.0f);
        }
}

void EnvelopeGraph::mouseDown (const juce::MouseEvent& e)
{
    auto n = nodes();
    float best = 1.0e9f;
    dragNode = -1;
    for (int h = 1; h < n.size(); ++h)
    {
        if (adsr && h == 3) continue; // sustain end is not a separate parameter
        auto d = n[h].getDistanceFrom (e.position);
        if (d < best) { best = d; dragNode = h - 1; }
    }
    // node -> parameter: ADSR handles 1:A 2:D(+S) 4:R (dragNode 0,1,3); AD handles 1:A 2:D
    if (dragNode >= 0)
    {
        const int k = dragNode;
        proc.beginGesture (keys[juce::jmin (k, 3)]);
        if (adsr && k == 1) proc.beginGesture (keys[2]);
        proc.showParamInfo (keys[juce::jmin (k, 3)]);
    }
    repaint();
}

void EnvelopeGraph::mouseDrag (const juce::MouseEvent& e)
{
    if (dragNode < 0) return;
    auto a = area();
    const float seg = a.getWidth() / (adsr ? 4.0f : 2.0f);
    auto n = nodes();
    auto toVal = [] (float f) { return juce::jlimit (0, 1023, juce::roundToInt (f * 1023.0f)); };
    if (dragNode == 0)
        proc.setValueFromUI (keys[0], toVal ((e.position.x - a.getX()) / seg));
    else if (dragNode == 1)
    {
        proc.setValueFromUI (keys[1], toVal ((e.position.x - n[1].x) / seg));
        if (adsr) proc.setValueFromUI (keys[2], toVal ((a.getBottom() - e.position.y) / a.getHeight()));
    }
    else if (dragNode == 3 && adsr)
        proc.setValueFromUI (keys[3], toVal ((e.position.x - n[3].x) / seg));
    repaint();
}

void EnvelopeGraph::mouseUp (const juce::MouseEvent&)
{
    if (dragNode >= 0)
    {
        proc.endGesture (keys[juce::jmin (dragNode, 3)]);
        if (adsr && dragNode == 1) proc.endGesture (keys[2]);
    }
    dragNode = -1;
    repaint();
}

// ======================================================================= LCD
LcdDisplay::LcdDisplay (XDProcessor& p) : proc (p) { proc.addXDListener (this); }
LcdDisplay::~LcdDisplay() { proc.removeXDListener (this); }

void LcdDisplay::xdChanged (int what)
{
    if (what & (XDProcessor::cLcd | XDProcessor::cProgram | XDProcessor::cSelection | XDProcessor::cStatus | XDProcessor::cBanks | XDProcessor::cParam))
        repaint();
}

void LcdDisplay::mouseDown (const juce::MouseEvent& e)
{
    if (retryArea.contains (e.getPosition()) && onRetry) onRetry();
}

void LcdDisplay::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (col::lcdBg);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (juce::Colour (0xff2a3036));
    g.drawRoundedRectangle (r.reduced (1.0f), 6.0f, 2.0f);
    retryArea = {};
    auto area = getLocalBounds().reduced (12, 8);

    if (proc.lcdOff)
    {
        g.setColour (col::dim.withAlpha (0.4f));
        g.setFont (XDLook::mono (12.0f));
        g.drawText ("LCD OFF", area, juce::Justification::bottomRight);
        return;
    }

    auto& cur = proc.getCurrent();
    const bool modified = cur != proc.getSaved();
    auto top = area.removeFromTop (20);
    g.setFont (XDLook::mono (15.0f).boldened());
    g.setColour (col::lcdText);
    g.drawText (cur.getName() + (modified ? " *" : ""), top, juce::Justification::centredLeft);
    g.setFont (XDLook::mono (12.0f));
    g.setColour (proc.isConnected() ? col::accent2 : col::warn);
    g.drawText (proc.isConnected() ? "XD OS " + proc.getOsVersion() : juce::String ("OFFLINE"), top, juce::Justification::centredRight);

    g.setColour (juce::Colour (0xff1f262c));
    g.fillRect (area.removeFromTop (6).withSizeKeepingCentre (area.getWidth(), 1));

    auto bottom = area.removeFromBottom (18);
    auto& lcd = proc.getLcd();
    if (lcd.message.isNotEmpty())
    {
        g.setColour (col::yellow);
        g.setFont (XDLook::mono (14.0f));
        g.drawFittedText (lcd.message, area, juce::Justification::centredLeft, 3);
    }
    else if (! proc.isConnected() && lcd.param < 0)
    {
        g.setColour (col::warn);
        g.setFont (XDLook::mono (13.0f));
        auto line = area.removeFromTop (22);
        g.drawText (u ("No connection to the minilogue xd."), line, juce::Justification::centredLeft);
        retryArea = line.removeFromRight (90).reduced (2);
        g.setColour (col::warn.withAlpha (0.25f));
        g.fillRoundedRectangle (retryArea.toFloat(), 3.0f);
        g.setColour (col::text);
        g.drawText (u ("Retry"), retryArea, juce::Justification::centred);
        g.setColour (col::dim);
        g.setFont (XDLook::mono (12.0f));
        g.drawFittedText (u ("MIDI setup: Settings & Info tab."), area, juce::Justification::topLeft, 2);
    }
    else if (lcd.param >= 0)
    {
        auto& d = ParamTable::get()[lcd.param];
        juce::String tx;
        switch (d.tx)
        {
            case Tx::None: tx = "No CC (program dump)"; break;
            case Tx::NRPN7: case Tx::NRPN8: case Tx::NRPN10: tx = "NRPN " + juce::String (d.number); break;
            case Tx::CCCtx: tx = "CC 96"; break;
            case Tx::CC10: case Tx::CC7: case Tx::CCStep: case Tx::CCBool:
            default: tx = "CC " + juce::String (d.number); break;
        }
        auto line = area.removeFromTop (20);
        g.setFont (XDLook::mono (14.0f).boldened());
        g.setColour (d.isRealtime() ? col::accent2 : col::green);
        g.drawText (d.name, line, juce::Justification::centredLeft);
        g.setFont (XDLook::mono (12.0f));
        g.setColour (col::dim);
        g.drawText (juce::String (d.minV) + "~" + juce::String (d.maxV) + "  " + tx, line, juce::Justification::centredRight);
        auto values = area.removeFromBottom (18);
        g.setColour (col::lcdText.withAlpha (0.8f));
        g.drawFittedText (d.desc, area, juce::Justification::topLeft, 2);
        auto& names = proc.getSettings().userNames;
        g.setFont (XDLook::mono (13.0f));
        g.setColour (col::lcdText);
        g.drawText (u ("Current: ") + formatInContext (d, cur, &names), values.removeFromLeft (values.getWidth() / 2), juce::Justification::centredLeft);
        g.setColour (col::dim);
        g.drawText (u ("Saved: ") + formatInContext (d, proc.getSaved(), &names), values, juce::Justification::centredLeft);
    }

    // selector line
    g.setFont (XDLook::mono (12.5f));
    juce::String sel;
    if (proc.directMode)
    {
        sel = "DIRECT  ";
        sel << (proc.slotSel == 0 ? juce::String (u ("000  synth edit buffer")) : juce::String (proc.slotSel).paddedLeft ('0', 3) + u ("  (program on the synth)"));
    }
    else
    {
        auto& b = const_cast<XDProcessor&> (proc).bank (proc.sourceBank);
        sel = bankTitle ((BankId) proc.sourceBank) + (b.dirty ? "*" : "") + "  ";
        if (proc.slotSel == 0) sel << u ("000  .syx / .mnlgxdprog file / XD buffer");
        else if (! b.loaded) sel << juce::String (proc.slotSel).paddedLeft ('0', 3) << u ("  (bank not loaded)");
        else sel << juce::String (proc.slotSel).paddedLeft ('0', 3) << "  " << b.progs[(size_t) proc.slotSel - 1].getName();
    }
    g.setColour (col::accent);
    g.drawText (sel, bottom, juce::Justification::centredLeft);
}

// ======================================================================= FreeKnob
FreeKnob::FreeKnob (juce::String t, double mn, double mx, double step) : titleText (std::move (t))
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    slider.setRange (mn, mx, step);
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible (slider);
}

void FreeKnob::resized() { slider.setBounds (getLocalBounds().withTrimmedBottom (28)); }

void FreeKnob::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().removeFromBottom (28);
    g.setColour (col::text);
    g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    g.drawFittedText (titleText.toUpperCase(), b.removeFromTop (14), juce::Justification::centred, 1);
    g.setColour (col::dim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawFittedText (valueText ? valueText (slider.getValue()) : juce::String ((int) slider.getValue()), b, juce::Justification::centred, 1);
}
