#include "Tabs.h"

using namespace xd;
static juce::String u (const char* s) { return juce::String::fromUTF8 (s); }

namespace
{
struct ToggleDef { const char* text; bool Settings::* member; const char* tip; };
const ToggleDef toggleDefs[] = {
    { "Bank autosave", &Settings::bankAutoSave, "Bank changes are written to the bank's .syx file immediately." },
    { "Actions confirm", &Settings::actionsConfirm, "Ask before librarian operations." },
    { "Save OK confirm", &Settings::saveOkConfirm, "Show a message after a successful save." },
    { "Load confirm", &Settings::loadConfirm, "Ask before loading a program from a bank." },
    { "Load OK confirm", &Settings::loadOkConfirm, "Show a message after loading." },
    { "Receive confirm", &Settings::receiveConfirm, "Ask before receiving from the synth." },
    { "Receive OK confirm", &Settings::receiveOkConfirm, "Show a message after receiving." },
    { "Send confirm (small Send)", &Settings::sendConfirm, "Ask before sending with the small Send buttons." },
    { "Send OK confirm (small Send)", &Settings::sendOkConfirm, "Show a message after sending with the small Send buttons." },
    { "Welcome message", &Settings::welcome, "Welcome message on the LCD." },
    { "Auto-send dump for green params", &Settings::autoSendDump, "After changing a parameter without CC/NRPN the program is sent automatically (250 ms later)." },
    { "Send patch on project load", &Settings::sendOnRecall, "When a DAW project is opened, the patch stored in it is sent to the synth." },
    { "Send patch when editor opens", &Settings::sendOnEditorOpen, "Opening the plugin window on a track sends that track's patch to the synth." },
    { "Thru: host notes -> synth", &Settings::hostThru, "Notes from the DAW are passed to the synth (via the device port or the plugin's MIDI output)." },
    { "Host MIDI in = synth feedback", &Settings::hostInputAsSynth, "Treat CC/NRPN/SysEx arriving from the DAW as coming from the synth." },
};
}

SettingsTab::SettingsTab (XDEditor& e) : ed (e), proc (e.proc)
{
    auto& s = proc.getSettings();
    for (auto& d : toggleDefs)
    {
        auto* t = toggles.add (new juce::ToggleButton (d.text));
        t->setTooltip (juce::String::fromUTF8 (d.tip));
        auto member = d.member;
        t->onClick = [this, t, member] { proc.getSettings().*member = t->getToggleState(); proc.getSettings().changed(); };
        addAndMakeVisible (t);
    }
    saveMode.addItemList ({ "Save mode: Sysex (.syx)", "Save mode: Korg Librarian (.mnlgxdprog)", "Save mode: Both" }, 1);
    saveMode.onChange = [this] { proc.getSettings().saveMode = saveMode.getSelectedId() - 1; proc.getSettings().changed(); };
    nonCcMode.addItemList ({ u ("Green params: visible and enabled"), u ("Green params: disabled"), u ("Green params: hidden") }, 1);
    nonCcMode.onChange = [this] { proc.getSettings().nonCcMode = nonCcMode.getSelectedId() - 1; proc.getSettings().changed(); };
    transfer.setSliderStyle (juce::Slider::LinearBar);
    transfer.setRange (20, 1000, 10);
    transfer.setDoubleClickReturnValue (true, 200);
    transfer.textFromValueFunction = [] (double v) { return "Transfer speed: " + juce::String ((int) v) + " ms"; };
    transfer.onValueChange = [this] { proc.getSettings().transferMs = (int) transfer.getValue(); proc.getSettings().changed(); };
    zoomIn.onClick = [this] { ed.setZoom (proc.getSettings().zoomPct + 5); refresh(); };
    zoomOut.onClick = [this] { ed.setZoom (proc.getSettings().zoomPct - 5); refresh(); };
    for (juce::Component* c : { (juce::Component*) &saveMode, (juce::Component*) &nonCcMode, (juce::Component*) &transfer,
                                (juce::Component*) &zoomIn, (juce::Component*) &zoomOut, (juce::Component*) &zoomLabel })
        addAndMakeVisible (c);

    // MIDI
    midiIn.onChange = [this] { const int i = midiIn.getSelectedId() - 2; proc.getSettings().midiInId = i >= 0 ? inIds[i] : juce::String(); proc.getSettings().changed(); };
    midiOut.onChange = [this]
    {
        const int i = midiOut.getSelectedId() - 2;
        proc.getSettings().midiOutId = i >= 0 ? outIds[i] : juce::String();
        proc.getSettings().changed();
        proc.deviceInquiry();
    };
    for (int c = 1; c <= 16; ++c) channel.addItem ("Channel " + juce::String (c), c);
    channel.onChange = [this] { proc.getSettings().channel = channel.getSelectedId() - 1; proc.getSettings().changed(); };
    route.addItemList ({ u ("Output: MIDI device port"), u ("Output: via the DAW (plugin MIDI output)"), u ("Output: both") }, 1);
    route.onChange = [this] { proc.getSettings().outputRoute = route.getSelectedId() - 1; proc.getSettings().changed(); };
    refreshDevices.onClick = [this] { fillDevices(); };
    inquiry.onClick = [this] { proc.deviceInquiry(); };
    loadNames.onClick = [this]
    {
        juce::Component::SafePointer<SettingsTab> safe (this);
        proc.loadUserNames ([safe] (bool ok, juce::String msg) { if (safe != nullptr) safe->ed.inform (true, "USER", msg, ! ok); });
    };
    resetNames.onClick = [this] { proc.resetUserNames(); };
    statusLabel.setJustificationType (juce::Justification::topLeft);
    statusLabel.setFont (juce::FontOptions (13.0f));
    for (juce::Component* c : { (juce::Component*) &midiIn, (juce::Component*) &midiOut, (juce::Component*) &channel, (juce::Component*) &route,
                                (juce::Component*) &refreshDevices, (juce::Component*) &inquiry, (juce::Component*) &loadNames,
                                (juce::Component*) &resetNames, (juce::Component*) &statusLabel })
        addAndMakeVisible (c);

    about.setMultiLine (true);
    about.setReadOnly (true);
    about.setFont (juce::FontOptions (12.5f));
    about.setText (u ("XD Editor 1.0 - editor & librarian for the Korg minilogue xd (VST3 / AU / standalone).\n\n"
                      "Features: every program parameter (real time via CC / NRPN, the rest via program dump), "
                      "global parameters, 3 banks of 500 programs (Disk 1, Disk 2, Minilogue XD), copy / move / swap / "
                      "multi-copy, comparisons, morphing with section locks ('Morph' automation parameter and CC 100), "
                      ".syx, .mnlgxdprog, .mnlgxdlib and .prog_bin files, USER module names, sequencer view.\n\n"
                      "In a DAW the plugin state (current patch, banks, panel settings) is saved with the project; when the project is opened "
                      "the patch is sent to the synth automatically. Every parameter can be automated."));
    addAndMakeVisible (about);

    // program info
    reportKind.addItemList (reportKindNames(), 1);
    reportKind.setSelectedId (1, juce::dontSendNotification);
    display.onClick = [this] { report.setText (proc.buildInfoReport ((ReportKind) (reportKind.getSelectedId() - 1)), false); };
    exportButton.onClick = [this]
    {
        display.onClick();
        auto text = report.getText();
        auto name = proc.getCurrent().getName().trim();
        ed.chooseFile (true, u ("Export program information"), "*.txt",
                       juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ((name.isEmpty() ? juce::String ("program") : name) + "_info.txt"),
                       [this, text] (juce::File f)
        {
            const bool ok = f.withFileExtension (".txt").replaceWithText (text);
            ed.inform (true, "Export", ok ? u ("Saved.") : u ("Cannot write the file."), ! ok);
        });
    };
    report.setMultiLine (true);
    report.setReadOnly (true);
    report.setScrollbarsShown (true);
    report.setFont (XDLook::mono (12.0f));
    for (juce::Component* c : { (juce::Component*) &reportKind, (juce::Component*) &display, (juce::Component*) &exportButton, (juce::Component*) &report })
        addAndMakeVisible (c);

    proc.addXDListener (this);
    fillDevices();
    refresh();
    juce::ignoreUnused (s);
}

SettingsTab::~SettingsTab() { proc.removeXDListener (this); }

void SettingsTab::fillDevices()
{
    auto& s = proc.getSettings();
    midiIn.clear (juce::dontSendNotification);
    midiOut.clear (juce::dontSendNotification);
    inIds.clear(); outIds.clear();
    midiIn.addItem (u ("(none)"), 1);
    midiOut.addItem (u ("(none)"), 1);
    for (auto& d : juce::MidiInput::getAvailableDevices()) { inIds.add (d.identifier); midiIn.addItem (d.name, inIds.size() + 1); }
    for (auto& d : juce::MidiOutput::getAvailableDevices()) { outIds.add (d.identifier); midiOut.addItem (d.name, outIds.size() + 1); }
    midiIn.setSelectedId (inIds.indexOf (s.midiInId) + 2, juce::dontSendNotification);
    midiOut.setSelectedId (outIds.indexOf (s.midiOutId) + 2, juce::dontSendNotification);
}

void SettingsTab::refresh()
{
    auto& s = proc.getSettings();
    for (int i = 0; i < toggles.size(); ++i) toggles[i]->setToggleState (s.*(toggleDefs[i].member), juce::dontSendNotification);
    saveMode.setSelectedId (s.saveMode + 1, juce::dontSendNotification);
    nonCcMode.setSelectedId (s.nonCcMode + 1, juce::dontSendNotification);
    transfer.setValue (s.transferMs, juce::dontSendNotification);
    transfer.updateText();
    zoomLabel.setText ("Zoom " + juce::String (s.zoomPct) + "%", juce::dontSendNotification);
    channel.setSelectedId (s.channel + 1, juce::dontSendNotification);
    route.setSelectedId (s.outputRoute + 1, juce::dontSendNotification);

    juce::String st;
    st << (proc.isConnected() ? u ("Connected: minilogue xd, OS ") + proc.getOsVersion() : u ("No reply to Device Inquiry.")) << "\n";
    st << "In: " << (proc.getMidi().hasInput() ? proc.getMidi().inputName() : u ("(none)"))
       << "   Out: " << (proc.getMidi().hasOutput() ? proc.getMidi().outputName() : u ("(none)")) << "\n";
    st << u ("USB: input 'minilogue xd KBD/KNOB', output 'minilogue xd SOUND' (Windows may name them 'minilogue xd' / 'MIDIIN2').");
    statusLabel.setText (st, juce::dontSendNotification);
}

void SettingsTab::xdChanged (int what)
{
    if (what & XDProcessor::cStatus) refresh();
    if (what & XDProcessor::cReport) report.setText (proc.lastReport, false);
}

void SettingsTab::paint (juce::Graphics& g)
{
    auto drawBox = [&] (juce::Rectangle<int> r, const char* t)
    {
        g.setColour (col::panel); g.fillRoundedRectangle (r.toFloat(), 6.0f);
        g.setColour (col::panelEdge); g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 6.0f, 1.0f);
        g.setColour (col::dim); g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (juce::String (t).toUpperCase(), r.reduced (10, 4).removeFromTop (14), juce::Justification::centredLeft);
    };
    drawBox ({ 8, 8, 400, 636 }, "Panel settings");
    drawBox ({ 416, 8, 420, 636 }, "MIDI & synth");
    drawBox ({ 844, 8, 428, 636 }, "Program information");
    g.setColour (col::text);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText ("MIDI In", 426, 30, 58, 24, juce::Justification::centredLeft);
    g.drawText ("MIDI Out", 426, 60, 58, 24, juce::Justification::centredLeft);
}

void SettingsTab::resized()
{
    int y = 30;
    for (auto* t : toggles) { t->setBounds (18, y, 380, 24); y += 26; }
    y += 6;
    saveMode.setBounds (18, y, 380, 24); y += 30;
    nonCcMode.setBounds (18, y, 380, 24); y += 30;
    transfer.setBounds (18, y, 380, 24); y += 32;
    zoomOut.setBounds (18, y, 32, 24);
    zoomLabel.setBounds (56, y, 120, 24);
    zoomIn.setBounds (180, y, 32, 24);

    midiIn.setBounds (486, 30, 240, 24);
    refreshDevices.setBounds (732, 30, 92, 24);
    midiOut.setBounds (486, 60, 240, 24);
    channel.setBounds (426, 90, 150, 24);
    route.setBounds (426, 120, 398, 24);
    inquiry.setBounds (426, 152, 150, 26);
    statusLabel.setBounds (426, 182, 398, 70);
    loadNames.setBounds (426, 258, 190, 26);
    resetNames.setBounds (622, 258, 190, 26);
    about.setBounds (426, 294, 398, 340);

    reportKind.setBounds (854, 30, 250, 24);
    display.setBounds (1110, 30, 76, 24);
    exportButton.setBounds (1190, 30, 72, 24);
    report.setBounds (854, 62, 408, 572);
}
