#include "Look.h"

XDLook::XDLook()
{
    setColour (juce::ResizableWindow::backgroundColourId, col::bg);
    setColour (juce::Label::textColourId, col::text);
    setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2d3035));
    setColour (juce::TextButton::buttonOnColourId, col::accent);
    setColour (juce::TextButton::textColourOffId, col::text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff111214));
    setColour (juce::ComboBox::textColourId, col::text);
    setColour (juce::ComboBox::outlineColourId, col::panelEdge);
    setColour (juce::ComboBox::arrowColourId, col::accent);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1b1c1f));
    setColour (juce::PopupMenu::textColourId, col::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::accent.withAlpha (0.85f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
    setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0f1012));
    setColour (juce::TextEditor::textColourId, col::text);
    setColour (juce::TextEditor::outlineColourId, col::panelEdge);
    setColour (juce::TextEditor::focusedOutlineColourId, col::accent);
    setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff111214));
    setColour (juce::ScrollBar::thumbColourId, juce::Colour (0xff4a4d54));
    setColour (juce::Slider::textBoxTextColourId, col::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::AlertWindow::backgroundColourId, juce::Colour (0xff1f2023));
    setColour (juce::AlertWindow::textColourId, col::text);
    setColour (juce::AlertWindow::outlineColourId, col::accent);
    setColour (juce::ToggleButton::textColourId, col::text);
    setColour (juce::ToggleButton::tickColourId, col::accent);
}

juce::Font XDLook::mono (float size)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size, juce::Font::plain));
}

void XDLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
    const float size = juce::jmin (b.getWidth(), b.getHeight());
    auto c = b.withSizeKeepingCentre (size, size);
    const float r = size * 0.5f;
    const auto centre = c.getCentre();
    const bool green = s.getProperties()["green"];
    const auto ring = ! s.isEnabled() ? col::dim.withAlpha (0.4f) : (green ? col::green : col::accent);
    const bool bipolar = s.getProperties()["bipolar"];

    // track
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, r - 2.0f, r - 2.0f, 0.0f, start, end, true);
    g.setColour (juce::Colour (0xff3a3d43));
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // value arc
    const float angle = start + pos * (end - start);
    const float from = bipolar ? (start + end) * 0.5f : start;
    juce::Path arc;
    arc.addCentredArc (centre.x, centre.y, r - 2.0f, r - 2.0f, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
    g.setColour (ring);
    g.strokePath (arc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // knob body
    const float kr = r - 7.0f;
    juce::ColourGradient grad (juce::Colour (0xff3b3e44), centre.x, centre.y - kr, col::knob, centre.x, centre.y + kr, false);
    g.setGradientFill (grad);
    g.fillEllipse (centre.x - kr, centre.y - kr, kr * 2, kr * 2);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawEllipse (centre.x - kr, centre.y - kr, kr * 2, kr * 2, 1.0f);

    // pointer
    juce::Path ptr;
    ptr.addRoundedRectangle (-1.5f, -kr + 3.0f, 3.0f, kr * 0.55f, 1.5f);
    g.setColour (col::text);
    g.fillPath (ptr, juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));
}

void XDLook::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bgc, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto c = b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId) : bgc;
    if (! b.isEnabled()) c = c.withAlpha (0.4f);
    if (down) c = c.brighter (0.2f);
    else if (over) c = c.brighter (0.08f);
    g.setColour (c);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

void XDLook::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat();
    auto box = r.removeFromLeft (r.getHeight()).reduced (4.0f);
    g.setColour (juce::Colour (0xff0f1012));
    g.fillRoundedRectangle (box, 3.0f);
    g.setColour (over ? col::accent : col::panelEdge);
    g.drawRoundedRectangle (box, 3.0f, 1.0f);
    if (b.getToggleState())
    {
        g.setColour (col::accent);
        g.fillRoundedRectangle (box.reduced (3.5f), 2.0f);
    }
    g.setColour (b.isEnabled() ? col::text : col::dim);
    g.setFont (juce::FontOptions (13.0f));
    g.drawFittedText (b.getButtonText(), r.toNearestInt().withTrimmedLeft (2), juce::Justification::centredLeft, 2);
}

void XDLook::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (box.hasKeyboardFocus (true) ? col::accent : box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
    juce::Path p;
    const float ax = (float) w - 12.0f, ay = (float) h * 0.5f;
    p.addTriangle (ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3);
    g.setColour (box.isEnabled() ? col::accent : col::dim);
    g.fillPath (p);
}

void XDLook::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                               juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style != juce::Slider::LinearBar)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s);
        return;
    }
    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    g.setColour (juce::Colour (0xff0f1012));
    g.fillRoundedRectangle (r, 3.0f);
    const bool green = s.getProperties()["green"];
    g.setColour ((green ? col::green : col::accent).withAlpha (s.isEnabled() ? 0.55f : 0.2f));
    g.fillRoundedRectangle (r.withRight (pos), 3.0f);
    g.setColour (col::panelEdge);
    g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
}
