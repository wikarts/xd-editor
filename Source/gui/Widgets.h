#pragma once
#include "Look.h"
#include "../PluginProcessor.h"

/** Base for every control bound to one program parameter. */
class ParamControl : public juce::Component, public juce::SettableTooltipClient
{
public:
    ParamControl (XDProcessor& p, const juce::String& key);
    ~ParamControl() override;
    virtual void refresh() = 0;
    const xd::ParamDef& def() const { return xd::ParamTable::get()[pi]; }
    bool isGreen() const { return ! def().isRealtime(); }
    void applyNonCcMode (int mode);
    juce::String title;

    static juce::Array<ParamControl*>& registry();
    static void refreshAll();

protected:
    void showInfo() { proc.showParamInfo (pi); }
    XDProcessor& proc;
    int pi;
    bool updating = false;
    juce::Colour titleColour() const { return isGreen() ? col::green : col::text; }
};

class Knob : public ParamControl, private juce::Slider::Listener
{
public:
    Knob (XDProcessor&, const juce::String& key, const juce::String& title = {}, bool showValue = true);
    void refresh() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { showInfo(); }

private:
    void sliderValueChanged (juce::Slider*) override;
    void sliderDragStarted (juce::Slider*) override;
    void sliderDragEnded (juce::Slider*) override;
    struct S : juce::Slider { std::function<void()> onDown; void mouseDown (const juce::MouseEvent& e) override { if (onDown) onDown(); Slider::mouseDown (e); } } slider;
    bool showValue;
};

/** Hardware-like selector: options listed vertically with an LED, click cycles. */
class Switch : public ParamControl
{
public:
    Switch (XDProcessor&, const juce::String& key, const juce::String& title = {});
    void refresh() override { repaint(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
};

class Choice : public ParamControl
{
public:
    Choice (XDProcessor&, const juce::String& key, const juce::String& title = {}, bool labelLeft = false);
    void refresh() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { showInfo(); }
    int labelWidth = 120;

private:
    juce::ComboBox box;
    bool labelLeft;
    std::vector<int> values; // value for each combo item
};

/** Label + horizontal bar with formatted value, for the dense program tab. */
class Bar : public ParamControl, private juce::Slider::Listener
{
public:
    Bar (XDProcessor&, const juce::String& key, const juce::String& title = {});
    void refresh() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { showInfo(); }
    int labelWidth = 130;

private:
    void sliderValueChanged (juce::Slider*) override;
    void sliderDragStarted (juce::Slider*) override;
    void sliderDragEnded (juce::Slider*) override;
    juce::Slider slider;
};

/** Generic factory: Switch for 2-4 labels, Choice for longer lists, Bar otherwise. */
std::unique_ptr<ParamControl> makeRowControl (XDProcessor&, const juce::String& key);

class PanelBox : public juce::Component
{
public:
    explicit PanelBox (juce::String t) : titleText (std::move (t)) { setInterceptsMouseClicks (false, true); }
    void paint (juce::Graphics&) override;
    juce::Rectangle<int> content() const { return getLocalBounds().reduced (8).withTrimmedTop (16); }
    juce::String titleText;
};

class EnvelopeGraph : public juce::Component, public XDProcessor::Listener
{
public:
    EnvelopeGraph (XDProcessor&, bool adsr);
    ~EnvelopeGraph() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void xdChanged (int what) override { if (what & XDProcessor::cParam) repaint(); }

private:
    XDProcessor& proc;
    bool adsr;
    int keys[4] {};
    int dragNode = -1;
    juce::Array<juce::Point<float>> nodes() const;
    juce::Rectangle<float> area() const { return getLocalBounds().toFloat().reduced (6.0f, 6.0f); }
};

class LcdDisplay : public juce::Component, public XDProcessor::Listener
{
public:
    explicit LcdDisplay (XDProcessor&);
    ~LcdDisplay() override;
    void paint (juce::Graphics&) override;
    void xdChanged (int what) override;
    std::function<void()> onRetry;
    void mouseDown (const juce::MouseEvent&) override;

private:
    XDProcessor& proc;
    juce::Rectangle<int> retryArea;
};

/** Plain value knob not bound to a program parameter (bank/program selectors, morph...). */
class FreeKnob : public juce::Component
{
public:
    FreeKnob (juce::String t, double min, double max, double step);
    juce::Slider slider;
    juce::String titleText;
    std::function<juce::String (double)> valueText;
    void resized() override;
    void paint (juce::Graphics&) override;
};
