#include "Tabs.h"

using namespace xd;
static juce::String u (const char* s) { return juce::String::fromUTF8 (s); }

namespace
{
struct ToggleDef { const char* text; bool Settings::* member; const char* tip; };
const ToggleDef toggleDefs[] = {
    { "Bank autosave", &Settings::bankAutoSave, "Zmiany w bankach od razu zapisywane do pliku .syx banku." },
    { "Actions confirm", &Settings::actionsConfirm, "Potwierdzenia operacji librariana." },
    { "Save OK confirm", &Settings::saveOkConfirm, "Komunikat po udanym zapisie." },
    { "Load confirm", &Settings::loadConfirm, "Pytanie przed wczytaniem programu z banku." },
    { "Load OK confirm", &Settings::loadOkConfirm, "Komunikat po wczytaniu." },
    { "Receive confirm", &Settings::receiveConfirm, "Pytanie przed odbiorem z syntezatora." },
    { "Receive OK confirm", &Settings::receiveOkConfirm, "Komunikat po odbiorze." },
    { "Send confirm (small Send)", &Settings::sendConfirm, "Pytanie przed wysłaniem małymi przyciskami Send." },
    { "Send OK confirm (small Send)", &Settings::sendOkConfirm, "Komunikat po wysłaniu małymi przyciskami Send." },
    { "Welcome message", &Settings::welcome, "Komunikat powitalny na LCD." },
    { "Auto-send dump for green params", &Settings::autoSendDump, "Po zmianie parametru bez CC/NRPN program jest automatycznie wysyłany (z opóźnieniem 250 ms)." },
    { "Send patch on project load", &Settings::sendOnRecall, "Po otwarciu projektu DAW zapisany w nim patch jest wysyłany do syntezatora." },
    { "Send patch when editor opens", &Settings::sendOnEditorOpen, "Otwarcie okna wtyczki na danej ścieżce wysyła jej patch do syntezatora." },
    { "Thru: host notes -> synth", &Settings::hostThru, "Nuty z DAW przechodzą do syntezatora (przez port urządzenia albo wyjście MIDI wtyczki)." },
    { "Host MIDI in = synth feedback", &Settings::hostInputAsSynth, "CC/NRPN/SysEx docierające z DAW traktuj jako odpowiedź syntezatora." },
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
    nonCcMode.addItemList ({ u ("Zielone: widoczne i aktywne"), u ("Zielone: wyłączone"), u ("Zielone: ukryte") }, 1);
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
    route.addItemList ({ u ("Wyjście: port MIDI urządzenia"), u ("Wyjście: przez DAW (wyjście MIDI wtyczki)"), u ("Wyjście: oba") }, 1);
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
    about.setText (u ("XD Editor 1.0 - edytor i librarian Korg minilogue xd (VST3 / AU / standalone).\n\n"
                      "Funkcje: wszystkie parametry programu (CC / NRPN w czasie rzeczywistym, pozostałe przez zrzut programu), "
                      "parametry globalne, 3 banki po 500 programów (Disk 1, Disk 2, Minilogue XD), kopiowanie / przenoszenie / zamiana / "
                      "kopiowanie wielu, porównania, morphing z blokadą sekcji (parametr automatyzacji 'Morph' i CC 100), "
                      "pliki .syx, .mnlgxdprog, .mnlgxdlib, .prog_bin, nazwy modułów USER, podgląd sekwencera.\n\n"
                      "W DAW: stan wtyczki (bieżący patch, banki, ustawienia panelu) zapisuje się w projekcie, a po jego otwarciu "
                      "patch jest automatycznie wysyłany do syntezatora. Wszystkie parametry są dostępne do automatyzacji."));
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
        ed.chooseFile (true, u ("Eksport informacji o programie"), "*.txt",
                       juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ((name.isEmpty() ? juce::String ("program") : name) + "_info.txt"),
                       [this, text] (juce::File f)
        {
            const bool ok = f.withFileExtension (".txt").replaceWithText (text);
            ed.inform (true, "Export", ok ? u ("Zapisano.") : u ("Nie można zapisać pliku."), ! ok);
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
    midiIn.addItem (u ("(brak)"), 1);
    midiOut.addItem (u ("(brak)"), 1);
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
    st << (proc.isConnected() ? u ("Połączono: minilogue xd, OS ") + proc.getOsVersion() : u ("Brak odpowiedzi na Device Inquiry.")) << "\n";
    st << "In: " << (proc.getMidi().hasInput() ? proc.getMidi().inputName() : u ("(brak)"))
       << "   Out: " << (proc.getMidi().hasOutput() ? proc.getMidi().outputName() : u ("(brak)")) << "\n";
    st << u ("USB: wejście 'minilogue xd KBD/KNOB', wyjście 'minilogue xd SOUND'.");
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

    midiIn.setBounds (426, 30, 300, 24);
    refreshDevices.setBounds (732, 30, 92, 24);
    midiOut.setBounds (426, 60, 300, 24);
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
