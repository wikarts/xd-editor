#include "Tabs.h"

using namespace xd;
static juce::String u (const char* s) { return juce::String::fromUTF8 (s); }

static const char* colA[] = { "program_level", "program_transpose", "program_tuning", "scale_key", "micro_tuning", "octave",
                              "key_trig", "arp_gate", "arp_rate", "amp_velocity", "cutoff_velocity", "eg_legato",
                              "portamento_mode", "portamento_bpm", "lfo_key_sync", "lfo_voice_sync", "lfo_target_osc" };
static const char* colB[] = { "bend_plus", "bend_minus", "joy_assign_plus", "joy_range_plus", "joy_assign_minus", "joy_range_minus",
                              "cv_mode", "cv1_assign", "cv1_range", "cv2_assign", "cv2_range", "aftertouch_assign",
                              "seq_bpm", "seq_length", "seq_resolution", "seq_swing", "seq_gate" };

ProgramTab::ProgramTab (XDEditor& e) : ed (e), proc (e.proc)
{
    for (auto* s : { "Program", "Controllers & Sequencer", "Program info", "Global parameters" })
        addAndMakeVisible (sections.add (new PanelBox (s)));

    for (auto* k : colA) addAndMakeVisible (rows.add (makeRowControl (proc, k).release()));
    for (auto* k : colB) addAndMakeVisible (rows.add (makeRowControl (proc, k).release()));

    for (auto* b : { &send1, &send2 })
    {
        b->setTooltip (u ("Sends the whole program - needed after changing green parameters (no CC/NRPN)."));
        b->onClick = [this] { ed.doSend (false); };
        b->setColour (juce::TextButton::buttonColourId, col::green.darker (0.6f));
        addAndMakeVisible (b);
    }

    description.setMultiLine (true, true);
    description.setReturnKeyStartsNewLine (false);
    description.setTextToShowWhenEmpty (u ("Program description (Enter to confirm)"), col::dim);
    description.onReturnKey = [this] { proc.currentInfo().description = description.getText(); proc.infoEdited(); };
    description.onFocusLost = description.onReturnKey;
    addAndMakeVisible (description);

    authorButton.onClick = [this] { ed.doAuthor(); };
    addAndMakeVisible (authorButton);
    auto cats = categories();
    for (int i = 0; i < cats.size(); ++i) category.addItem (i == 0 ? u ("(no category)") : cats[i], i + 1);
    category.onChange = [this]
    {
        const int i = category.getSelectedId() - 1;
        proc.currentInfo().category = i <= 0 ? juce::String() : categories()[i];
        proc.infoEdited();
    };
    addAndMakeVisible (category);
    dateLabel.setColour (juce::Label::textColourId, col::dim);
    addAndMakeVisible (dateLabel);

    requestGlobals.onClick = [this]
    {
        juce::Component::SafePointer<ProgramTab> safe (this);
        proc.requestGlobals ([safe] (bool ok, juce::String msg)
        {
            if (safe == nullptr) return;
            if (ok) safe->proc.lcdMessage (u ("Global parameters received."));
            else safe->ed.inform (true, "Global", msg, true);
        });
    };
    sendGlobals.onClick = [this]
    {
        if (! proc.hasGlobals()) { ed.inform (true, "Global", u ("Receive the global parameters first (Request)."), true); return; }
        ed.confirm (true, "Global", u ("Send the global parameters to the synth?"), [this]
        {
            ed.inform (proc.getSettings().sendOkConfirm, "Global", proc.sendGlobals() ? u ("Sent.") : u ("No connection."), false);
        });
    };
    addAndMakeVisible (requestGlobals);
    addAndMakeVisible (sendGlobals);

    for (auto& f : globalFields())
    {
        auto* l = globalLabels.add (new juce::Label ({}, f.name));
        l->setFont (juce::FontOptions (12.5f));
        addAndMakeVisible (l);
        auto* b = globalBoxes.add (new juce::ComboBox());
        for (int v = f.minV; v <= f.maxV; ++v) b->addItem (formatGlobal (f, v), v - f.minV + 1);
        const auto* fp = &f;
        b->onChange = [this, b, fp] { if (b->getSelectedId() > 0) proc.setGlobal (*fp, b->getSelectedId() - 1 + fp->minV); };
        addAndMakeVisible (b);
    }

    proc.addXDListener (this);
    refreshInfo();
    refreshGlobals();
}

ProgramTab::~ProgramTab() { proc.removeXDListener (this); }

void ProgramTab::paint (juce::Graphics& g)
{
    g.setColour (col::dim);
    g.setFont (juce::FontOptions (11.5f));
    g.drawFittedText (u ("Green parameters have no CC/NRPN - the synth picks them up when the program is sent (Send button or auto-send in Settings)."),
                      juce::Rectangle<int> (16, 612, 600, 30), juce::Justification::centredLeft, 2);
}

void ProgramTab::resized()
{
    sections[0]->setBounds (8, 8, 306, 600);
    sections[1]->setBounds (322, 8, 306, 600);
    sections[2]->setBounds (636, 8, 636, 236);
    sections[3]->setBounds (636, 252, 636, 392);

    const int rowH = 30;
    for (int i = 0; i < 17; ++i)
    {
        rows[i]->setBounds (18, 32 + i * rowH, 286, 26);
        rows[17 + i]->setBounds (332, 32 + i * rowH, 286, 26);
    }
    for (auto* r : rows)
    {
        if (auto* c = dynamic_cast<Choice*> (r)) c->labelWidth = 130;
        r->resized();
    }
    send1.setBounds (18, 32 + 17 * rowH + 6, 120, 26);
    send2.setBounds (332, 32 + 17 * rowH + 6, 120, 26);

    authorButton.setBounds (648, 32, 110, 26);
    category.setBounds (770, 32, 200, 26);
    dateLabel.setBounds (980, 32, 280, 26);
    description.setBounds (648, 66, 612, 166);

    requestGlobals.setBounds (1080, 258, 90, 22);
    sendGlobals.setBounds (1176, 258, 86, 22);
    const int n = globalBoxes.size();
    const int half = (n + 1) / 2;
    for (int i = 0; i < n; ++i)
    {
        const int col = i / half, row = i % half;
        const int x = 648 + col * 312, y = 286 + row * 25;
        globalLabels[i]->setBounds (x, y, 140, 22);
        globalBoxes[i]->setBounds (x + 140, y, 160, 22);
    }
}

void ProgramTab::xdChanged (int what)
{
    if (what & (XDProcessor::cInfo | XDProcessor::cProgram)) refreshInfo();
    if (what & XDProcessor::cGlobals) refreshGlobals();
}

void ProgramTab::refreshInfo()
{
    auto& in = proc.currentInfo();
    authorButton.setButtonText (in.author.isEmpty() ? juce::String ("Author: ?") : "Author: " + in.author);
    if (! description.hasKeyboardFocus (true)) description.setText (in.description, juce::dontSendNotification);
    const int ci = categories().indexOf (in.category);
    category.setSelectedId (ci <= 0 ? 1 : ci + 1, juce::dontSendNotification);
    dateLabel.setText (in.date.isEmpty() ? u ("Saved: -") : u ("Saved: ") + in.date.substring (0, 16).replaceCharacter ('T', ' '),
                       juce::dontSendNotification);
}

void ProgramTab::refreshGlobals()
{
    const bool have = proc.hasGlobals();
    auto& f = globalFields();
    for (int i = 0; i < globalBoxes.size(); ++i)
    {
        globalBoxes[i]->setEnabled (have);
        if (have) globalBoxes[i]->setSelectedId (proc.getGlobal (f[(size_t) i]) - f[(size_t) i].minV + 1, juce::dontSendNotification);
        else globalBoxes[i]->setSelectedId (0, juce::dontSendNotification);
    }
    sendGlobals.setEnabled (have);
}
