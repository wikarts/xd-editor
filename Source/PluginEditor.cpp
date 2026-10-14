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
    lcdOffButton.setTooltip (u ("Blanks the display (useful while automating many parameters or morphing)."));
    lcdOffButton.onClick = [this] { proc.lcdOff = lcdOffButton.getToggleState(); proc.uiNotify (XDProcessor::cLcd); };
    content.addAndMakeVisible (lcdOffButton);

    lcd.onRetry = [this] { proc.deviceInquiry(); };
    content.addAndMakeVisible (lcd);

    directButton.setClickingTogglesState (true);
    directButton.setTooltip (u ("Direct mode: the Bank/Program knobs switch programs directly on the synth."));
    directButton.onClick = [this]
    {
        proc.setDirectMode (directButton.getToggleState());
        if (directButton.getToggleState() && ! proc.directMode)
            inform (true, "Direct", u ("Direct mode needs a connection to the synth."), true);
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
    bankKnob.slider.setTooltip (u ("Bank 0-4 (100 programs each)."));
    progKnob.slider.setTooltip (u ("Program 1-100. Position 000 (fully left) = work with a file or the synth's edit buffer."));
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
    loadButton.setTooltip (u ("Load a program from a file (position 000) or from the selected bank and send it to the synth's edit buffer."));
    saveButton.setTooltip (u ("Save the program to a file (position 000) or to the selected bank slot."));
    receiveButton.setTooltip (u ("Receive a program from the synth (edit buffer or selected slot)."));
    sendButton.setTooltip (u ("Send the current program to the synth's edit buffer."));

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
        proc.lcdMessage (u ("XD Editor 1.0 - minilogue xd editor & librarian.  ")
                         + (proc.isConnected() ? u ("Connected (OS ") + proc.getOsVersion() + ")" : u ("Looking for the synth...")), 5000);
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
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, title, msg, "OK", "Cancel", this,
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
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
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
    if (! canSend) inform (true, "Load", u ("Program loaded into the panel but NOT into the synth (no connection)."), true);
    else inform (proc.getSettings().loadOkConfirm, "Load", u ("Loaded \"") + p.getName() + u ("\" and sent to the minilogue xd edit buffer."));
    if (after) after (true);
}

void XDEditor::doLoad()
{
    auto& s = proc.getSettings();
    if (proc.directMode) { doReceive(); return; }
    if (proc.slotSel == 0)
    {
        chooseFile (false, u ("Load minilogue xd program"), "*.syx;*.mnlgxdprog;*.prog_bin;*.mnlgxdlib",
                    dirOr (s.lastProgramDir), [this] (juce::File f) { loadProgramFile (f); });
        return;
    }
    auto& b = proc.bank (proc.sourceBank);
    if (! b.loaded) { inform (true, "Load", u ("Bank ") + bankTitle ((BankId) proc.sourceBank) + u (" is not loaded (see the Librarian tab)."), true); return; }
    const int slot = proc.slotSel - 1;
    const auto& prog = b.progs[(size_t) slot];
    confirm (s.loadConfirm, "Load", u ("Load program ") + slotTitle (proc.sourceBank, slot) + " \"" + prog.getName() + "\"?",
             [this, slot]
             {
                 auto& bank = proc.bank (proc.sourceBank);
                 const bool canSend = proc.canTalkToSynth();
                 proc.loadProgram (bank.progs[(size_t) slot], bank.info[(size_t) slot], canSend);
                 if (! canSend) inform (true, "Load", u ("Program loaded into the panel but NOT into the synth (no connection)."), true);
                 else inform (proc.getSettings().loadOkConfirm, "Load", u ("Loaded and sent to the minilogue xd edit buffer."));
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
        chooseFile (true, u ("Save program"), syx ? "*.syx" : "*.mnlgxdprog", initial, [this, syx] (juce::File f)
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
            inform (st.saveOkConfirm, "Save", u ("Saved: ") + f.getFileNameWithoutExtension());
        });
        return;
    }

    auto& b = proc.bank (proc.sourceBank);
    if (! b.loaded) { inform (true, "Save", u ("Bank ") + bankTitle ((BankId) proc.sourceBank) + u (" is not loaded. Create an empty bank or load a file in the Librarian tab."), true); return; }
    const int slot = proc.slotSel - 1;
    juce::String msg = u ("Save \"") + proc.getCurrent().getName() + u ("\" to ") + slotTitle (proc.sourceBank, slot)
                     + u (" (replaces \"") + b.progs[(size_t) slot].getName() + "\")?";
    if (proc.sourceBank == (int) BankId::Synth) msg << u ("\n\nThe program will also be written to the synth's memory.");
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
    juce::String what = buffer ? u ("the current program (edit buffer) from the synth")
                               : u ("program ") + juce::String (proc.slotSel).paddedLeft ('0', 3) + u (" from the synth's memory");
    confirm (s.receiveConfirm, "Receive", u ("Receive ") + what + u ("?\nUnsaved changes in the panel will be replaced."), [this, buffer]
    {
        juce::Component::SafePointer<XDEditor> safe (this);
        auto done = [safe] (bool ok, juce::String msg)
        {
            if (safe == nullptr) return;
            if (ok) safe->inform (safe->proc.getSettings().receiveOkConfirm, "Receive", u ("Received: ") + msg);
            else safe->inform (true, "Receive", msg, true);
        };
        if (buffer) proc.requestCurrentProgram (done);
        else proc.requestSlotToPanel (proc.slotSel - 1, done);
    });
}

void XDEditor::doSend (bool mainButton)
{
    auto& s = proc.getSettings();
    confirm (mainButton || s.sendConfirm, "Send", u ("Send \"") + proc.getCurrent().getName() + u ("\" to the minilogue xd edit buffer?"), [this, mainButton]
    {
        if (! proc.sendCurrentToSynth()) { inform (true, "Send", u ("No connection to the synth."), true); return; }
        inform (mainButton || proc.getSettings().sendOkConfirm, "Send", u ("Program sent to the edit buffer."));
    });
}

void XDEditor::doRename()
{
    askText ("Rename", u ("Program name (max. 12 ASCII characters):"), proc.getCurrent().getName(), 12,
             [this] (juce::String name) { proc.renameCurrent (name); });
}

void XDEditor::doAuthor()
{
    askText ("Author", u ("Program author (max. 15 characters):"), proc.currentInfo().author, 15, [this] (juce::String a)
    {
        proc.currentInfo().author = a.trim();
        proc.infoEdited();
    });
}

void XDEditor::doInit()
{
    confirm (proc.getSettings().actionsConfirm, "Init", u ("Load the Init program? Unsaved changes will be lost."), [this]
    {
        AddInfo info;
        info.author = proc.currentInfo().author; // keep the author, like the original panel
        proc.loadProgram (Program(), info, proc.canTalkToSynth());
    });
}
