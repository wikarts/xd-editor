#include "PluginEditor.h"
#include "gui/Tabs.h"

using namespace xd;
static juce::String u (const char* s) { return juce::String::fromUTF8 (s); }

XDEditor::XDEditor (XDProcessor& p) : AudioProcessorEditor (p), proc (p), lcd (p)
{
    const int initialZoom = proc.getSettings().zoomPct; // read before any resize can overwrite it
    resizingFromZoom = true;
    setLookAndFeel (&look);
    addAndMakeVisible (content);

    const char* tabNames[] = { "Main", "Program & Global", "Librarian & Morph", "Sequencer", "Settings & Info" };
    for (int i = 0; i < 5; ++i)
    {
        tabButtons[i].setButtonText (tabNames[i]);
        tabButtons[i].setClickingTogglesState (false);
        tabButtons[i].onClick = [this, i] { showTab (i); };
        content.addAndMakeVisible (tabButtons[i]);
    }
    lcdOffButton.setClickingTogglesState (true);
    lcdOffButton.setTooltip (u ("Wygasza wyświetlacz (np. przy automatyce wielu parametrów albo morphingu)."));
    lcdOffButton.onClick = [this] { proc.lcdOff = lcdOffButton.getToggleState(); proc.uiNotify (XDProcessor::cLcd); };
    content.addAndMakeVisible (lcdOffButton);

    lcd.onRetry = [this] { proc.deviceInquiry(); };
    content.addAndMakeVisible (lcd);

    directButton.setClickingTogglesState (true);
    directButton.setTooltip (u ("Tryb Direct: pokrętła Bank/Program przełączają programy bezpośrednio w syntezatorze."));
    directButton.onClick = [this]
    {
        proc.setDirectMode (directButton.getToggleState());
        if (directButton.getToggleState() && ! proc.directMode)
            inform (true, "Direct", u ("Tryb Direct wymaga połączenia z syntezatorem."), true);
        refreshSelectors();
    };
    content.addAndMakeVisible (directButton);

    const char* src[] = { "DISK 1", "DISK 2", "MINILOGUE" };
    for (int i = 0; i < 3; ++i)
    {
        sourceButtons[i].setButtonText (src[i]);
        sourceButtons[i].setRadioGroupId (77);
        sourceButtons[i].setClickingTogglesState (true);
        sourceButtons[i].onClick = [this, i] { if (sourceButtons[i].getToggleState()) { proc.sourceBank = i; proc.uiNotify (XDProcessor::cSelection); } };
        content.addAndMakeVisible (sourceButtons[i]);
    }

    bankKnob.valueText = [] (double v) { return juce::String ((int) v); };
    progKnob.valueText = [] (double v) { return v < 0.5 ? juce::String ("000 (file)") : juce::String ((int) v).paddedLeft ('0', 3); };
    bankKnob.slider.onValueChange = [this] { bankKnob.repaint(); selectorsChanged(); };
    progKnob.slider.onValueChange = [this] { progKnob.repaint(); selectorsChanged(); };
    bankKnob.slider.setTooltip (u ("Bank 0-4 (po 100 programów)."));
    progKnob.slider.setTooltip (u ("Program 1-100. Pozycja 000 (skrajnie w lewo) = operacje na pliku lub buforze syntezatora."));
    content.addAndMakeVisible (bankKnob);
    content.addAndMakeVisible (progKnob);

    loadButton.onClick = [this] { doLoad(); };
    saveButton.onClick = [this] { doSave(); };
    receiveButton.onClick = [this] { doReceive(); };
    sendButton.onClick = [this] { doSend (true); };
    renameButton.onClick = [this] { doRename(); };
    initButton.onClick = [this] { doInit(); };
    for (auto* b : { &loadButton, &saveButton, &receiveButton, &sendButton, &renameButton, &initButton })
        content.addAndMakeVisible (*b);
    loadButton.setTooltip (u ("Wczytaj program z pliku (pozycja 000) albo z wybranego banku i wyślij do bufora syntezatora."));
    saveButton.setTooltip (u ("Zapisz program do pliku (pozycja 000) albo do wybranego slotu banku."));
    receiveButton.setTooltip (u ("Odbierz program z syntezatora (bufor lub wybrany slot)."));
    sendButton.setTooltip (u ("Wyślij bieżący program do bufora syntezatora."));

    mainTab = std::make_unique<MainTab> (*this);
    programTab = std::make_unique<ProgramTab> (*this);
    librarianTab = std::make_unique<LibrarianTab> (*this);
    sequencerTab = std::make_unique<SequencerTab> (proc);
    settingsTab = std::make_unique<SettingsTab> (*this);
    tabs[0] = mainTab.get(); tabs[1] = programTab.get(); tabs[2] = librarianTab.get(); tabs[3] = sequencerTab.get(); tabs[4] = settingsTab.get();
    for (auto* t : tabs) content.addChildComponent (t);

    proc.addXDListener (this);
    proc.makeActive();
    if (proc.getSettings().sendOnEditorOpen) proc.sendCurrentToSynth();

    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) baseW / baseH);
    setResizeLimits (baseW / 2, baseH / 2, baseW * 2, baseH * 2);
    resizingFromZoom = false;
    setZoom (initialZoom);

    lcdOffButton.setToggleState (proc.lcdOff, juce::dontSendNotification);
    showTab (proc.uiTab);
    refreshSelectors();
    xdChanged (XDProcessor::cStatus);

    if (proc.getSettings().welcome && ! proc.welcomeShown)
    {
        proc.welcomeShown = true;
        proc.lcdMessage (u ("XD Editor 1.0 - edytor i librarian minilogue xd.  ")
                         + (proc.isConnected() ? u ("Połączono (OS ") + proc.getOsVersion() + ")" : u ("Szukam syntezatora...")), 5000);
    }
}

XDEditor::~XDEditor()
{
    proc.removeXDListener (this);
    setLookAndFeel (nullptr);
}

void XDEditor::paint (juce::Graphics& g) { g.fillAll (col::bg); }

void XDEditor::setZoom (int pct)
{
    pct = juce::jlimit (50, 200, pct);
    auto& s = proc.getSettings();
    if (s.zoomPct != pct) { s.zoomPct = pct; s.save(); }
    const juce::ScopedValueSetter<bool> sv (resizingFromZoom, true);
    setSize (juce::roundToInt (baseW * pct / 100.0), juce::roundToInt (baseH * pct / 100.0));
    resized();
}

void XDEditor::resized()
{
    const float z = (float) getWidth() / (float) baseW;
    content.setBounds (0, 0, baseW, baseH);
    content.setTransform (juce::AffineTransform::scale (z));
    if (! resizingFromZoom)
    {
        const int pct = juce::roundToInt (z * 100.0f);
        if (pct != proc.getSettings().zoomPct) { proc.getSettings().zoomPct = pct; proc.getSettings().save(); }
    }

    for (int i = 0; i < 5; ++i) tabButtons[i].setBounds (8, 8 + i * 27, 160, 24);
    lcdOffButton.setBounds (8, 8 + 5 * 27 + 4, 160, 20);
    lcd.setBounds (178, 8, 584, 152);
    directButton.setBounds (772, 10, 84, 26);
    for (int i = 0; i < 3; ++i) sourceButtons[i].setBounds (772, 46 + i * 26, 84, 22);
    bankKnob.setBounds (862, 14, 70, 100);
    progKnob.setBounds (934, 14, 76, 100);
    const int bx = 1020, bw = 82, bh = 34;
    loadButton.setBounds (bx, 14, bw, bh);
    saveButton.setBounds (bx + bw + 6, 14, bw, bh);
    receiveButton.setBounds (bx + 2 * (bw + 6), 14, bw, bh);
    sendButton.setBounds (bx, 58, bw, bh);
    renameButton.setBounds (bx + bw + 6, 58, bw, bh);
    initButton.setBounds (bx + 2 * (bw + 6), 58, bw, bh);
    for (auto* t : tabs) if (t != nullptr) t->setBounds (0, 168, baseW, baseH - 168);
}

void XDEditor::showTab (int i)
{
    i = juce::jlimit (0, 4, i);
    proc.uiTab = i;
    for (int k = 0; k < 5; ++k)
    {
        tabButtons[k].setToggleState (k == i, juce::dontSendNotification);
        if (tabs[k] != nullptr) tabs[k]->setVisible (k == i);
    }
}

void XDEditor::xdChanged (int what)
{
    if (what & XDProcessor::cParam) ParamControl::refreshAll();
    if (what & (XDProcessor::cSelection | XDProcessor::cBanks)) refreshSelectors();
    if (what & XDProcessor::cStatus)
    {
        const int mode = proc.getSettings().nonCcMode;
        for (auto* c : ParamControl::registry()) c->applyNonCcMode (mode);
        ParamControl::refreshAll();
    }
    if (what & XDProcessor::cLcd) lcdOffButton.setToggleState (proc.lcdOff, juce::dontSendNotification);
}

void XDEditor::refreshSelectors()
{
    const juce::ScopedValueSetter<bool> s (updatingSelectors, true);
    directButton.setToggleState (proc.directMode, juce::dontSendNotification);
    for (int i = 0; i < 3; ++i)
    {
        auto& b = proc.bank (i);
        sourceButtons[i].setToggleState (proc.sourceBank == i, juce::dontSendNotification);
        sourceButtons[i].setEnabled (! proc.directMode);
        sourceButtons[i].setButtonText (juce::String (i == 0 ? "DISK 1" : i == 1 ? "DISK 2" : "MINILOGUE") + (b.dirty ? " *" : ""));
        sourceButtons[i].setAlpha (b.loaded ? 1.0f : 0.55f);
    }
    if (proc.slotSel == 0) progKnob.slider.setValue (0, juce::dontSendNotification);
    else
    {
        bankKnob.slider.setValue ((proc.slotSel - 1) / 100, juce::dontSendNotification);
        progKnob.slider.setValue ((proc.slotSel - 1) % 100 + 1, juce::dontSendNotification);
    }
    bankKnob.repaint();
    progKnob.repaint();
}

void XDEditor::selectorsChanged()
{
    if (updatingSelectors) return;
    const int prog = (int) progKnob.slider.getValue();
    const int bank = (int) bankKnob.slider.getValue();
    proc.setSlotSel (prog == 0 ? 0 : bank * 100 + prog);
}

// ======================================================================= dialogs
void XDEditor::confirm (bool ask, const juce::String& title, const juce::String& msg, std::function<void()> then)
{
    if (! ask) { then(); return; }
    juce::Component::SafePointer<XDEditor> safe (this);
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, title, msg, "OK", "Anuluj", this,
        juce::ModalCallbackFunction::create ([safe, then] (int r) { if (r != 0 && safe != nullptr) then(); }));
}

void XDEditor::inform (bool show, const juce::String& title, const juce::String& msg, bool error)
{
    if (! show && ! error) { proc.lcdMessage (msg); return; }
    juce::AlertWindow::showMessageBoxAsync (error ? juce::MessageBoxIconType::WarningIcon : juce::MessageBoxIconType::InfoIcon,
                                            title, msg, "OK", this);
}

void XDEditor::askText (const juce::String& title, const juce::String& msg, const juce::String& initial, int maxLen,
                        std::function<void (juce::String)> then)
{
    auto* w = new juce::AlertWindow (title, msg, juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("text", initial);
    if (auto* te = w->getTextEditor ("text")) { te->setInputRestrictions (maxLen); te->selectAll(); }
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Anuluj", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<XDEditor> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([w, safe, then] (int r)
    {
        if (r == 1 && safe != nullptr) then (w->getTextEditorContents ("text"));
    }), true);
}

void XDEditor::chooseFile (bool save, const juce::String& title, const juce::String& patterns, const juce::File& initial,
                           std::function<void (juce::File)> then)
{
    chooser = std::make_unique<juce::FileChooser> (title, initial, patterns);
    auto chooserFlags = save ? (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting)
                      : (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles);
    juce::Component::SafePointer<XDEditor> safe (this);
    chooser->launchAsync (chooserFlags, [safe, then] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (safe != nullptr && f != juce::File()) then (f);
    });
}

// ======================================================================= main operations
static juce::File dirOr (const juce::String& d)
{
    juce::File f (d);
    return f.isDirectory() ? f : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
}

static juce::String slotTitle (int bank, int slot)
{
    return bankTitle ((BankId) bank) + " " + juce::String (slot + 1).paddedLeft ('0', 3);
}

void XDEditor::loadProgramFile (const juce::File& f, std::function<void (bool)> after)
{
    Program p; AddInfo info;
    auto r = files::loadProgram (f, p, info);
    if (! r.ok) { inform (true, "Load", r.message, true); if (after) after (false); return; }
    proc.getSettings().lastProgramDir = f.getParentDirectory().getFullPathName();
    const bool canSend = proc.canTalkToSynth();
    proc.loadProgram (p, info, canSend);
    if (! canSend) inform (true, "Load", u ("Program wczytany do panelu, ale NIE do syntezatora (brak połączenia)."), true);
    else inform (proc.getSettings().loadOkConfirm, "Load", u ("Wczytano \"") + p.getName() + u ("\" i wysłano do bufora minilogue xd."));
    if (after) after (true);
}

void XDEditor::doLoad()
{
    auto& s = proc.getSettings();
    if (proc.directMode) { doReceive(); return; }
    if (proc.slotSel == 0)
    {
        chooseFile (false, u ("Wczytaj program minilogue xd"), "*.syx;*.mnlgxdprog;*.prog_bin;*.mnlgxdlib",
                    dirOr (s.lastProgramDir), [this] (juce::File f) { loadProgramFile (f); });
        return;
    }
    auto& b = proc.bank (proc.sourceBank);
    if (! b.loaded) { inform (true, "Load", u ("Bank ") + bankTitle ((BankId) proc.sourceBank) + u (" nie jest wczytany (zakładka Librarian)."), true); return; }
    const int slot = proc.slotSel - 1;
    const auto& prog = b.progs[(size_t) slot];
    confirm (s.loadConfirm, "Load", u ("Wczytać program ") + slotTitle (proc.sourceBank, slot) + " \"" + prog.getName() + "\"?",
             [this, slot]
             {
                 auto& bank = proc.bank (proc.sourceBank);
                 const bool canSend = proc.canTalkToSynth();
                 proc.loadProgram (bank.progs[(size_t) slot], bank.info[(size_t) slot], canSend);
                 if (! canSend) inform (true, "Load", u ("Program wczytany do panelu, ale NIE do syntezatora (brak połączenia)."), true);
                 else inform (proc.getSettings().loadOkConfirm, "Load", u ("Wczytano i wysłano do bufora minilogue xd."));
             });
}

void XDEditor::doSave()
{
    auto& s = proc.getSettings();
    proc.currentInfo().date = juce::Time::getCurrentTime().toISO8601 (true);

    if (proc.slotSel == 0 || proc.directMode)
    {
        const auto base = proc.getCurrent().getName().trim().replaceCharacters ("/\\:*?\"<>|", "_________");
        const bool syx = s.saveMode != 1;
        auto initial = dirOr (s.lastProgramDir).getChildFile ((base.isEmpty() ? juce::String ("program") : base) + (syx ? ".syx" : ".mnlgxdprog"));
        chooseFile (true, u ("Zapisz program"), syx ? "*.syx" : "*.mnlgxdprog", initial, [this, syx] (juce::File f)
        {
            auto& st = proc.getSettings();
            st.lastProgramDir = f.getParentDirectory().getFullPathName();
            st.lastSysexName = f.getFileName();
            IoResult r;
            if (syx)
            {
                f = f.withFileExtension (".syx");
                r = files::saveProgramSyx (f, proc.getCurrent(), proc.currentInfo(), st.channel);
                if (r.ok && st.saveMode == 2)
                    r = files::saveProgramXdProg (f.withFileExtension (".mnlgxdprog"), proc.getCurrent(), proc.currentInfo());
            }
            else r = files::saveProgramXdProg (f.withFileExtension (".mnlgxdprog"), proc.getCurrent(), proc.currentInfo());
            st.save();
            if (! r.ok) { inform (true, "Save", r.message, true); return; }
            proc.markCurrentSaved();
            inform (st.saveOkConfirm, "Save", u ("Zapisano: ") + f.getFileNameWithoutExtension());
        });
        return;
    }

    auto& b = proc.bank (proc.sourceBank);
    if (! b.loaded) { inform (true, "Save", u ("Bank ") + bankTitle ((BankId) proc.sourceBank) + u (" nie jest wczytany. Utwórz pusty bank albo wczytaj plik w zakładce Librarian."), true); return; }
    const int slot = proc.slotSel - 1;
    juce::String msg = u ("Zapisać \"") + proc.getCurrent().getName() + u ("\" w ") + slotTitle (proc.sourceBank, slot)
                     + u (" (zastąpi \"") + b.progs[(size_t) slot].getName() + "\")?";
    if (proc.sourceBank == (int) BankId::Synth) msg << u ("\n\nProgram zostanie też zapisany w pamięci syntezatora.");
    confirm (s.actionsConfirm, "Save", msg, [this, slot]
    {
        auto m = lib::putProgram (proc, proc.sourceBank, slot, proc.getCurrent(), proc.currentInfo());
        proc.markCurrentSaved();
        inform (proc.getSettings().saveOkConfirm, "Save", m);
    });
}

void XDEditor::doReceive()
{
    auto& s = proc.getSettings();
    const bool buffer = proc.directMode || proc.slotSel == 0;
    juce::String what = buffer ? u ("bieżący program (bufor) z syntezatora")
                               : u ("program ") + juce::String (proc.slotSel).paddedLeft ('0', 3) + u (" z pamięci syntezatora");
    confirm (s.receiveConfirm, "Receive", u ("Odebrać ") + what + u ("?\nNiezapisane zmiany w panelu zostaną zastąpione."), [this, buffer]
    {
        juce::Component::SafePointer<XDEditor> safe (this);
        auto done = [safe] (bool ok, juce::String msg)
        {
            if (safe == nullptr) return;
            if (ok) safe->inform (safe->proc.getSettings().receiveOkConfirm, "Receive", u ("Odebrano: ") + msg);
            else safe->inform (true, "Receive", msg, true);
        };
        if (buffer) proc.requestCurrentProgram (done);
        else proc.requestSlotToPanel (proc.slotSel - 1, done);
    });
}

void XDEditor::doSend (bool mainButton)
{
    auto& s = proc.getSettings();
    confirm (mainButton || s.sendConfirm, "Send", u ("Wysłać \"") + proc.getCurrent().getName() + u ("\" do bufora minilogue xd?"), [this, mainButton]
    {
        if (! proc.sendCurrentToSynth()) { inform (true, "Send", u ("Brak połączenia z syntezatorem."), true); return; }
        inform (mainButton || proc.getSettings().sendOkConfirm, "Send", u ("Program wysłany do bufora."));
    });
}

void XDEditor::doRename()
{
    askText ("Rename", u ("Nazwa programu (maks. 12 znaków ASCII):"), proc.getCurrent().getName(), 12,
             [this] (juce::String name) { proc.renameCurrent (name); });
}

void XDEditor::doAuthor()
{
    askText ("Author", u ("Autor programu (maks. 15 znaków):"), proc.currentInfo().author, 15, [this] (juce::String a)
    {
        proc.currentInfo().author = a.trim();
        proc.infoEdited();
    });
}

void XDEditor::doInit()
{
    confirm (proc.getSettings().actionsConfirm, "Init", u ("Wczytać program Init? Niezapisane zmiany zostaną utracone."), [this]
    {
        AddInfo info;
        info.author = proc.currentInfo().author; // keep the author, like the original panel
        proc.loadProgram (Program(), info, proc.canTalkToSynth());
    });
}
