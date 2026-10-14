#include "Tabs.h"

using namespace xd;

template <typename T>
T* MainTab::add (const juce::String& key, const juce::String& title)
{
    auto* c = new T (proc, key, title);
    controls.add (c);
    byKey[key] = c;
    addAndMakeVisible (c);
    return c;
}

PanelBox* MainTab::section (const juce::String& name, juce::Rectangle<int> r)
{
    auto* s = sections.add (new PanelBox (name));
    s->setBounds (r);
    addAndMakeVisible (s);
    s->toBack();
    return s;
}

void MainTab::place (const juce::String& key, juce::Rectangle<int> r)
{
    if (auto it = byKey.find (key); it != byKey.end()) it->second->setBounds (r);
}

MainTab::MainTab (XDEditor& e) : proc (e.proc)
{
    // voice mode buttons (POLY / UNISON / CHORD / ARP-LATCH = stored 4 / 3 / 2 / 1)
    const char* vm[] = { "POLY", "UNISON", "CHORD", "ARP/LATCH" };
    for (int i = 0; i < 4; ++i)
    {
        auto* b = voiceButtons.add (new juce::TextButton (vm[i]));
        b->setClickingTogglesState (false);
        b->setTooltip (juce::String::fromUTF8 ("Voice mode (sent via NRPN)."));
        const int value = 4 - i;
        b->onClick = [this, value]
        {
            auto pi = ParamTable::get().indexOf ("voice_mode");
            proc.beginGesture (pi);
            proc.setValueFromUI (pi, value);
            proc.endGesture (pi);
            proc.showParamInfo (pi);
        };
        addAndMakeVisible (b);
    }
    add<Knob> ("voice_depth", "Depth");
    add<Knob> ("portamento");

    add<Switch> ("vco1_wave", "Wave"); add<Switch> ("vco1_octave", "Octave");
    add<Knob> ("vco1_pitch", "Pitch"); add<Knob> ("vco1_shape", "Shape");
    add<Switch> ("vco2_wave", "Wave"); add<Switch> ("vco2_octave", "Octave");
    add<Knob> ("vco2_pitch", "Pitch"); add<Knob> ("vco2_shape", "Shape");
    add<Switch> ("sync"); add<Switch> ("ring");
    add<Knob> ("cross_mod", "Cross Mod");

    add<Switch> ("multi_type", "Type"); add<Switch> ("multi_octave", "Octave"); add<Switch> ("multi_routing", "Routing");
    add<Choice> ("select_noise", "Noise"); add<Choice> ("select_vpm", "VPM"); add<Choice> ("select_user", "User Osc");
    add<Knob> ("shape_noise", "Shape"); add<Knob> ("shape_vpm", "Shape"); add<Knob> ("shape_user", "Shape");
    add<Knob> ("shift_shape_noise", "Shift Shape"); add<Knob> ("shift_shape_vpm", "Shift Shape"); add<Knob> ("shift_shape_user", "Shift Shape");

    add<Knob> ("vco1_level", "VCO 1"); add<Knob> ("vco2_level", "VCO 2"); add<Knob> ("multi_level", "Multi");

    add<Knob> ("cutoff"); add<Knob> ("resonance");
    add<Switch> ("drive"); add<Switch> ("keytrack", "Key Track");

    ampGraph = std::make_unique<EnvelopeGraph> (proc, true);
    egGraph = std::make_unique<EnvelopeGraph> (proc, false);
    addAndMakeVisible (*ampGraph);
    addAndMakeVisible (*egGraph);
    add<Knob> ("amp_attack", "Attack"); add<Knob> ("amp_decay", "Decay"); add<Knob> ("amp_sustain", "Sustain"); add<Knob> ("amp_release", "Release");
    add<Knob> ("eg_attack", "Attack"); add<Knob> ("eg_decay", "Decay"); add<Knob> ("eg_int", "EG Int"); add<Switch> ("eg_target", "Target");

    add<Switch> ("lfo_wave", "Wave"); add<Switch> ("lfo_mode", "Mode"); add<Switch> ("lfo_target", "Target");
    add<Knob> ("lfo_rate", "Rate"); add<Knob> ("lfo_int", "Int");

    for (int i = 1; i <= 6; ++i)
    {
        add<Knob> ("vpm_param" + juce::String (i), juce::StringArray { "Feedback", "Noise Dp", "ShapeMod", "Mod Atk", "Mod Dcy", "Mod KeyT" }[i - 1]);
        add<Knob> ("user_param" + juce::String (i), "Param " + juce::String (i));
    }

    add<Switch> ("modfx_on", "On"); add<Switch> ("modfx_type", "Type");
    add<Choice> ("modfx_chorus", "Sub Type"); add<Choice> ("modfx_ensemble", "Sub Type"); add<Choice> ("modfx_phaser", "Sub Type");
    add<Choice> ("modfx_flanger", "Sub Type"); add<Choice> ("modfx_user", "Sub Type");
    add<Knob> ("modfx_time", "Time"); add<Knob> ("modfx_depth", "Depth");

    add<Switch> ("delay_on", "On"); add<Choice> ("delay_type", "Type");
    add<Knob> ("delay_time", "Time"); add<Knob> ("delay_depth", "Depth"); add<Knob> ("delay_drywet", "Dry/Wet");
    add<Switch> ("reverb_on", "On"); add<Choice> ("reverb_type", "Type");
    add<Knob> ("reverb_time", "Time"); add<Knob> ("reverb_depth", "Depth"); add<Knob> ("reverb_drywet", "Dry/Wet");

    // sections (drawn behind)
    section ("Voice Mode", { 8, 8, 190, 232 });
    section ("VCO 1", { 206, 8, 200, 232 });
    section ("VCO 2", { 414, 8, 200, 232 });
    section ("Modulation", { 622, 8, 150, 232 });
    section ("Multi Engine", { 780, 8, 300, 232 });
    section ("Mixer", { 1088, 8, 184, 232 });
    section ("Filter", { 8, 248, 230, 226 });
    section ("Amp EG", { 246, 248, 300, 226 });
    section ("EG", { 554, 248, 260, 226 });
    section ("LFO", { 822, 248, 250, 226 });
    vpmSection = section ("VPM Params", { 1080, 248, 192, 226 });
    section ("Mod FX", { 8, 482, 400, 162 });
    section ("Delay", { 416, 482, 430, 162 });
    section ("Reverb", { 854, 482, 418, 162 });

    proc.addXDListener (this);
    updateContext();
}

MainTab::~MainTab() { proc.removeXDListener (this); }

void MainTab::resized()
{
    const int kw = 66, kh = 90, sw = 76, sh = 84;
    // voice mode
    for (int i = 0; i < 4; ++i) voiceButtons[i]->setBounds (18 + (i % 2) * 88, 32 + (i / 2) * 30, 84, 26);
    place ("voice_depth", { 22, 110, kw + 10, kh });
    place ("portamento", { 108, 110, kw + 10, kh });
    // VCOs
    for (int v = 0; v < 2; ++v)
    {
        const int x = v == 0 ? 206 : 414;
        const juce::String p = v == 0 ? "vco1_" : "vco2_";
        place (p + "wave", { x + 16, 30, sw, sh });
        place (p + "octave", { x + 104, 30, sw, sh });
        place (p + "pitch", { x + 16, 128, kw + 10, kh + 4 });
        place (p + "shape", { x + 104, 128, kw + 10, kh + 4 });
    }
    // modulation
    place ("sync", { 632, 30, 64, 70 });
    place ("ring", { 700, 30, 64, 70 });
    place ("cross_mod", { 660, 128, kw + 10, kh + 4 });
    // multi
    place ("multi_type", { 790, 30, 72, 70 });
    place ("multi_octave", { 866, 30, 72, 84 });
    place ("multi_routing", { 942, 30, 90, 70 });
    for (auto k : { "select_noise", "select_vpm", "select_user" }) place (k, { 792, 118, 130, 42 });
    for (auto k : { "shape_noise", "shape_vpm", "shape_user" }) place (k, { 932, 112, kw + 6, kh + 4 });
    for (auto k : { "shift_shape_noise", "shift_shape_vpm", "shift_shape_user" }) place (k, { 1006, 112, kw + 6, kh + 4 });
    // mixer
    place ("vco1_level", { 1096, 30, kw + 10, kh + 4 });
    place ("vco2_level", { 1182, 30, kw + 10, kh + 4 });
    place ("multi_level", { 1139, 132, kw + 10, kh + 4 });
    // filter
    place ("cutoff", { 22, 270, kw + 20, kh + 10 });
    place ("resonance", { 128, 270, kw + 20, kh + 10 });
    place ("drive", { 30, 382, sw, 80 });
    place ("keytrack", { 130, 382, sw, 80 });
    // amp eg
    ampGraph->setBounds (258, 270, 276, 90);
    const char* amp[] = { "amp_attack", "amp_decay", "amp_sustain", "amp_release" };
    for (int i = 0; i < 4; ++i) place (amp[i], { 256 + i * 70, 368, kw, kh + 6 });
    // eg
    egGraph->setBounds (566, 270, 236, 90);
    place ("eg_attack", { 562, 368, kw - 4, kh + 6 });
    place ("eg_decay", { 624, 368, kw - 4, kh + 6 });
    place ("eg_int", { 686, 368, kw - 4, kh + 6 });
    place ("eg_target", { 748, 370, 64, 84 });
    // lfo
    place ("lfo_wave", { 830, 270, 76, 70 });
    place ("lfo_mode", { 908, 270, 80, 70 });
    place ("lfo_target", { 990, 270, 78, 70 });
    place ("lfo_rate", { 852, 366, kw + 16, kh + 6 });
    place ("lfo_int", { 960, 366, kw + 16, kh + 6 });
    // vpm / user params
    for (int i = 0; i < 6; ++i)
    {
        juce::Rectangle<int> r (1088 + (i % 3) * 60, 270 + (i / 3) * 100, 60, kh + 4);
        place ("vpm_param" + juce::String (i + 1), r);
        place ("user_param" + juce::String (i + 1), r);
    }
    // effects
    place ("modfx_on", { 18, 506, 50, 60 });
    place ("modfx_type", { 72, 500, 86, 100 });
    for (auto k : { "modfx_chorus", "modfx_ensemble", "modfx_phaser", "modfx_flanger", "modfx_user" }) place (k, { 164, 530, 100, 42 });
    place ("modfx_time", { 268, 510, kw, kh + 6 });
    place ("modfx_depth", { 336, 510, kw, kh + 6 });

    place ("delay_on", { 426, 506, 50, 60 });
    place ("delay_type", { 480, 530, 130, 42 });
    place ("delay_time", { 616, 510, kw + 6, kh + 6 });
    place ("delay_depth", { 690, 510, kw + 6, kh + 6 });
    place ("delay_drywet", { 764, 510, kw + 6, kh + 6 });

    place ("reverb_on", { 864, 506, 50, 60 });
    place ("reverb_type", { 918, 530, 120, 42 });
    place ("reverb_time", { 1044, 510, kw + 6, kh + 6 });
    place ("reverb_depth", { 1118, 510, kw + 6, kh + 6 });
    place ("reverb_drywet", { 1192, 510, kw + 6, kh + 6 });
}

void MainTab::xdChanged (int what)
{
    if (what & (XDProcessor::cParam | XDProcessor::cProgram | XDProcessor::cStatus)) updateContext();
}

void MainTab::updateContext()
{
    auto& t = ParamTable::get();
    const int nonCc = proc.getSettings().nonCcMode;
    auto showIf = [&] (const juce::String& key, bool on)
    {
        if (auto it = byKey.find (key); it != byKey.end())
            it->second->setVisible (on && ! (it->second->isGreen() && nonCc == 2));
    };
    const int mt = juce::jlimit (0, 2, proc.getValue (t.indexOf ("multi_type")));
    const char* suffix[] = { "noise", "vpm", "user" };
    for (int i = 0; i < 3; ++i)
    {
        showIf (juce::String ("select_") + suffix[i], i == mt);
        showIf (juce::String ("shape_") + suffix[i], i == mt);
        showIf (juce::String ("shift_shape_") + suffix[i], i == mt);
    }
    for (int i = 1; i <= 6; ++i)
    {
        showIf ("vpm_param" + juce::String (i), mt != 2);
        showIf ("user_param" + juce::String (i), mt == 2);
        if (auto it = byKey.find ("vpm_param" + juce::String (i)); it != byKey.end()) it->second->setEnabled (mt == 1);
    }
    if (vpmSection != nullptr)
    {
        vpmSection->titleText = mt == 2 ? "User Osc Params" : "VPM Params";
        vpmSection->repaint();
    }
    const int fx = juce::jlimit (1, 5, proc.getValue (t.indexOf ("modfx_type")));
    const char* fxKeys[] = { "modfx_chorus", "modfx_ensemble", "modfx_phaser", "modfx_flanger", "modfx_user" };
    for (int i = 0; i < 5; ++i) showIf (fxKeys[i], i == fx - 1);

    const int vmode = proc.getValue (t.indexOf ("voice_mode"));
    for (int i = 0; i < 4; ++i) voiceButtons[i]->setToggleState (4 - i == vmode, juce::dontSendNotification);
}
