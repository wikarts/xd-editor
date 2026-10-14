#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace col
{
const juce::Colour bg        { 0xff17181a };
const juce::Colour panel     { 0xff222428 };
const juce::Colour panelEdge { 0xff34373d };
const juce::Colour text      { 0xffe4dfd5 };
const juce::Colour dim       { 0xff8e8a82 };
const juce::Colour accent    { 0xfff0a23b }; // amber knob ring
const juce::Colour accent2   { 0xff5fc4e8 }; // LCD cyan
const juce::Colour green     { 0xff9fd89a }; // parameters without CC/NRPN (dump only)
const juce::Colour lcdBg     { 0xff07090b };
const juce::Colour lcdText   { 0xffdcecff };
const juce::Colour warn      { 0xffe2574c };
const juce::Colour yellow    { 0xfff5d76e };
const juce::Colour knob      { 0xff0e0f10 };
}

class XDLook : public juce::LookAndFeel_V4
{
public:
    XDLook();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::Font (juce::FontOptions (13.0f)); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return juce::Font (juce::FontOptions (13.0f, juce::Font::bold)); }
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float min, float max, juce::Slider::SliderStyle, juce::Slider&) override;
    static juce::Font mono (float size);
};
