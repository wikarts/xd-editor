#include "Tabs.h"

using namespace xd;
static juce::String u (const char* s) { return juce::String::fromUTF8 (s); }
static juce::String slotName (int bank, int slot) { return bankTitle ((BankId) bank) + " " + juce::String (slot + 1).paddedLeft ('0', 3); }

// ======================================================================= bank list
class BankList : public juce::Component
{
public:
    BankList (LibrarianTab& t, XDProcessor& p, int b) : tab (t), proc (p), bank (b), inner (*this)
    {
        vp.setViewedComponent (&inner, false);
        vp.setScrollBarsShown (true, false);
        vp.setSingleStepSizes (0, rowH * 3);
        addAndMakeVisible (vp);
    }
    void resized() override
    {
        vp.setBounds (getLocalBounds());
        inner.setSize (getWidth() - vp.getScrollBarThickness(), rowH * numSlots);
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff111214));
        if (! proc.bank (bank).loaded)
        {
            g.setColour (col::dim);
            g.setFont (juce::FontOptions (13.0f));
            g.drawFittedText (u ("Bank pusty.\nKliknij nagłówek, wybierz akcję\nbanku i naciśnij Proceed."), getLocalBounds().reduced (12), juce::Justification::centred, 4);
        }
    }
    void refresh() { const bool l = proc.bank (bank).loaded; inner.setVisible (l); vp.setVisible (l); inner.repaint(); repaint(); }
    void scrollTo (int slot) { vp.setViewPosition (0, juce::jmax (0, slot * rowH - vp.getHeight() / 2)); }

private:
    static constexpr int rowH = 18;
    LibrarianTab& tab;
    XDProcessor& proc;
    int bank;
    int anchor = -1;
    juce::Viewport vp;

    struct Inner : juce::Component
    {
        explicit Inner (BankList& o) : owner (o) {}
        BankList& owner;
        void paint (juce::Graphics& g) override
        {
            auto& p = owner.proc;
            auto& b = p.bank (owner.bank);
            const auto clip = g.getClipBounds();
            const int first = juce::jmax (0, clip.getY() / rowH), last = juce::jmin (numSlots - 1, clip.getBottom() / rowH);
            const bool srcBank = p.libSrcBank == owner.bank;
            auto targets = owner.tab.previewTargets (owner.bank);
            for (int i = first; i <= last; ++i)
            {
                juce::Rectangle<int> r (0, i * rowH, getWidth(), rowH);
                if (i % 2) { g.setColour (juce::Colour (0xff15171a)); g.fillRect (r); }
                const bool isSrc = srcBank && p.libSrcSlots.contains (i);
                const bool isDst = p.libDstBank == owner.bank && p.libDstSlot == i;
                if (targets.contains (i)) { g.setColour (col::warn.withAlpha (0.25f)); g.fillRect (r); }
                if (isSrc) { g.setColour (col::accent.withAlpha (0.45f)); g.fillRect (r); }
                if (isDst) { g.setColour (col::accent2); g.drawRect (r, 2); }
                const bool hl = b.highlighted.count (i) > 0;
                g.setColour (hl ? col::yellow : (b.progs[(size_t) i].isInitProgram() ? col::dim : col::text));
                g.setFont (XDLook::mono (12.5f));
                g.drawText (juce::String (i + 1).paddedLeft ('0', 3), r.withTrimmedLeft (4).removeFromLeft (34), juce::Justification::centredLeft);
                g.drawText (b.progs[(size_t) i].getName(), r.withTrimmedLeft (40).removeFromLeft (110), juce::Justification::centredLeft);
                auto& in = b.info[(size_t) i];
                if (in.category.isNotEmpty() || in.author.isNotEmpty())
                {
                    g.setColour (col::dim);
                    g.setFont (juce::FontOptions (11.0f));
                    g.drawText (in.category.isNotEmpty() ? in.category : in.author, r.withTrimmedLeft (152).withTrimmedRight (4), juce::Justification::centredRight);
                }
            }
        }
        void mouseDown (const juce::MouseEvent& e) override
        {
            const int row = juce::jlimit (0, numSlots - 1, e.y / rowH);
            auto& p = owner.proc;
            p.makeActive();
            if (e.mods.isPopupMenu()) { owner.tab.selectDestination (owner.bank, row); return; }
            juce::Array<int> sel = p.libSrcBank == owner.bank ? p.libSrcSlots : juce::Array<int>();
            if (e.mods.isShiftDown() && owner.anchor >= 0 && p.libSrcBank == owner.bank)
            {
                sel.clear();
                for (int i = juce::jmin (owner.anchor, row); i <= juce::jmax (owner.anchor, row); ++i) sel.add (i);
            }
            else if (e.mods.isCommandDown() || e.mods.isCtrlDown())
            {
                if (sel.contains (row)) sel.removeFirstMatchingValue (row); else sel.add (row);
                owner.anchor = row;
            }
            else { sel = { row }; owner.anchor = row; }
            owner.tab.selectSource (owner.bank, sel);
        }
        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu()) return;
            const int row = juce::jlimit (0, numSlots - 1, e.y / rowH);
            auto& p = owner.proc;
            auto& b = p.bank (owner.bank);
            p.loadProgram (b.progs[(size_t) row], b.info[(size_t) row], p.canTalkToSynth());
            p.lcdMessage (u ("Wczytano ") + slotName (owner.bank, row) + " " + b.progs[(size_t) row].getName());
        }
    } inner;
};

// ======================================================================= tab
LibrarianTab::LibrarianTab (XDEditor& e) : ed (e), proc (e.proc)
{
    for (int i = 0; i < 3; ++i)
    {
        headers[i].setClickingTogglesState (false);
        headers[i].onClick = [this, i] { bankHeaderClicked (i); };
        headers[i].setTooltip (u ("Kliknij, aby wybrać bank dla akcji banku."));
        addAndMakeVisible (headers[i]);
        addAndMakeVisible (lists.add (new BankList (*this, proc, i)));
    }

    bankAction.addItemList (lib::bankActionNames(), 1);
    bankAction.setSelectedId (1, juce::dontSendNotification);
    programAction.addItemList (lib::programActionNames(), 1);
    programAction.setSelectedId (8, juce::dontSendNotification);
    programAction.onChange = [this] { for (auto* l : lists) l->refresh(); };
    bankProceed.onClick = [this] { runBankAction(); };
    programProceed.onClick = [this] { runProgramAction(); };
    cancelTransfer.onClick = [this] { proc.cancelTransfer(); };
    for (juce::Component* c : { (juce::Component*) &bankAction, (juce::Component*) &programAction, (juce::Component*) &bankProceed,
                                (juce::Component*) &programProceed, (juce::Component*) &cancelTransfer, (juce::Component*) &selectionInfo })
        addAndMakeVisible (c);
    selectionInfo.setJustificationType (juce::Justification::topLeft);
    selectionInfo.setFont (juce::FontOptions (12.5f));

    // morph
    morphKnob.valueText = [] (double v) { return juce::String ((int) v) + "%"; };
    morphKnob.slider.onValueChange = [this]
    {
        morphKnob.repaint();
        if (updatingMorph) return;
        if (! proc.hasMorphTarget()) { ed.inform (true, "Morph", u ("Wybierz program docelowy prawym kliknięciem na liście."), true); updatingMorph = true; morphKnob.slider.setValue (0); updatingMorph = false; return; }
        proc.setMorph (morphKnob.slider.getValue() / 100.0);
    };
    morphKnob.slider.setTooltip (u ("Morphing z bieżącego programu do programu docelowego. Automatyzacja: parametr 'Morph' lub CC 100."));
    addAndMakeVisible (morphKnob);
    for (int s = 0; s < (int) Section::Sequencer; ++s)
    {
        auto* b = lockButtons.add (new juce::TextButton (sectionName ((Section) s)));
        b->setClickingTogglesState (true);
        b->onClick = [this, s, b] { proc.morphLocks[(size_t) s] = b->getToggleState(); };
        addAndMakeVisible (b);
    }
    lockAll.onClick = [this] { proc.morphLocks.fill (true); refreshAll(); };
    unlockAll.onClick = [this] { proc.morphLocks.fill (false); refreshAll(); };
    morph0.onClick = [this] { if (proc.hasMorphTarget()) proc.setMorph (0.0, false, true); };
    morph100.onClick = [this] { if (proc.hasMorphTarget()) proc.setMorph (1.0, false, true); };
    for (auto* b : { &lockAll, &unlockAll, &morph0, &morph100 }) addAndMakeVisible (b);
    morphInfo.setFont (juce::FontOptions (12.5f));
    morphInfo.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (morphInfo);

    proc.addXDListener (this);
    refreshAll();
}

LibrarianTab::~LibrarianTab() { proc.removeXDListener (this); }

void LibrarianTab::paint (juce::Graphics& g)
{
    for (int i = 0; i < 3; ++i)
    {
        auto& b = proc.bank (i);
        g.setColour (col::dim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawFittedText (b.file == juce::File() ? (b.loaded ? u ("(nie zapisany)") : juce::String ("-")) : b.file.getFileName(),
                          juce::Rectangle<int> (8 + i * 256, 38, 248, 18), juce::Justification::centredLeft, 1);
    }
    auto drawBox = [&] (juce::Rectangle<int> r, const char* t)
    {
        g.setColour (col::panel); g.fillRoundedRectangle (r.toFloat(), 6.0f);
        g.setColour (col::panelEdge); g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 6.0f, 1.0f);
        g.setColour (col::dim); g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (juce::String (t).toUpperCase(), r.reduced (10, 4).removeFromTop (14), juce::Justification::centredLeft);
    };
    drawBox ({ 780, 8, 492, 96 }, "Bank action");
    drawBox ({ 780, 112, 492, 166 }, "Program action");
    drawBox ({ 780, 286, 492, 358 }, "Morphing");
    g.setColour (col::dim);
    g.setFont (juce::FontOptions (11.5f));
    g.drawFittedText (u ("Lewy klik = źródło (Shift/Ctrl = wiele), prawy klik = cel, dwuklik = wczytaj do panelu."),
                      juce::Rectangle<int> (792, 250, 470, 24), juce::Justification::centredLeft, 2);
}

void LibrarianTab::resized()
{
    for (int i = 0; i < 3; ++i)
    {
        headers[i].setBounds (8 + i * 256, 8, 248, 28);
        lists[i]->setBounds (8 + i * 256, 58, 248, 586);
    }
    bankAction.setBounds (792, 32, 300, 26);
    bankProceed.setBounds (1100, 32, 80, 26);
    cancelTransfer.setBounds (1186, 32, 74, 26);
    programAction.setBounds (792, 136, 300, 26);
    programProceed.setBounds (1100, 136, 80, 26);
    selectionInfo.setBounds (792, 168, 470, 80);

    morphKnob.setBounds (800, 310, 110, 130);
    morphInfo.setBounds (920, 310, 340, 70);
    lockAll.setBounds (920, 386, 80, 24); unlockAll.setBounds (1006, 386, 80, 24);
    morph0.setBounds (1092, 386, 80, 24); morph100.setBounds (1178, 386, 84, 24);
    for (int i = 0; i < lockButtons.size(); ++i)
        lockButtons[i]->setBounds (792 + (i % 3) * 158, 452 + (i / 3) * 30, 150, 26);
}

void LibrarianTab::xdChanged (int what)
{
    if (what & (XDProcessor::cBanks | XDProcessor::cSelection | XDProcessor::cMorph | XDProcessor::cStatus)) refreshAll();
}

void LibrarianTab::refreshAll()
{
    for (int i = 0; i < 3; ++i)
    {
        auto& b = proc.bank (i);
        headers[i].setButtonText (bankTitle ((BankId) i) + (b.dirty ? " *" : ""));
        headers[i].setToggleState (selectedBank == i, juce::dontSendNotification);
        lists[i]->refresh();
    }
    for (int s = 0; s < lockButtons.size(); ++s)
    {
        lockButtons[s]->setToggleState (proc.morphLocks[(size_t) s], juce::dontSendNotification);
        lockButtons[s]->setButtonText ((proc.morphLocks[(size_t) s] ? juce::String ("LOCKED: ") : juce::String()) + sectionName ((Section) s));
    }
    cancelTransfer.setEnabled (proc.isTransferring());
    bankProceed.setEnabled (! proc.isTransferring());

    juce::String s;
    const int src = proc.libSrcFirst();
    if (src >= 0 && juce::isPositiveAndBelow (proc.libSrcBank, 3))
    {
        s << u ("Źródło: ") << slotName (proc.libSrcBank, src) << "  " << proc.bank (proc.libSrcBank).progs[(size_t) src].getName();
        if (proc.libSrcSlots.size() > 1) s << "  (+" << (proc.libSrcSlots.size() - 1) << u (" zaznaczonych)");
    }
    else s << u ("Źródło: -");
    s << "\n";
    if (proc.libDstSlot >= 0 && juce::isPositiveAndBelow (proc.libDstBank, 3))
        s << u ("Cel: ") << slotName (proc.libDstBank, proc.libDstSlot) << "  " << proc.bank (proc.libDstBank).progs[(size_t) proc.libDstSlot].getName();
    else s << u ("Cel: -");
    selectionInfo.setText (s, juce::dontSendNotification);

    updatingMorph = true;
    morphKnob.slider.setValue (juce::roundToInt (proc.getMorph() * 100.0), juce::dontSendNotification);
    morphKnob.repaint();
    updatingMorph = false;
    morphInfo.setText (proc.hasMorphTarget()
                           ? u ("Z: bieżący program (") + proc.getCurrent().getName() + u (")\nDo: ") + slotName (proc.libDstBank, proc.libDstSlot) + "  "
                                 + proc.bank (proc.libDstBank).progs[(size_t) proc.libDstSlot].getName()
                                 + u ("\nZablokowane sekcje nie są zmieniane.")
                           : u ("Wybierz program docelowy (prawy klik na liście).\nWłącz LCD OFF i kręć pokrętłem Morph."),
                       juce::dontSendNotification);
}

void LibrarianTab::bankHeaderClicked (int b)
{
    selectedBank = b;
    refreshAll();
}

void LibrarianTab::selectSource (int bank, const juce::Array<int>& slots)
{
    proc.libSrcBank = bank;
    proc.libSrcSlots = slots;
    proc.uiNotify (XDProcessor::cSelection);
}

void LibrarianTab::selectDestination (int bank, int slot)
{
    proc.libDstBank = bank;
    proc.libDstSlot = slot;
    proc.morphTargetChanged();
    proc.uiNotify (XDProcessor::cSelection);
}

juce::Array<int> LibrarianTab::previewTargets (int bank) const
{
    const auto a = (lib::ProgramAction) (programAction.getSelectedId() - 1);
    if (bank != proc.libDstBank || proc.libDstSlot < 0) return {};
    if (a == lib::ProgramAction::CopyMultiAsIs || a == lib::ProgramAction::CopyMultiCollated)
        return lib::multiCopyTargets (proc.libSrcSlots, proc.libDstSlot, a == lib::ProgramAction::CopyMultiCollated);
    return {};
}

bool LibrarianTab::needsSource (lib::ProgramAction a) const
{
    using A = lib::ProgramAction;
    return a == A::LoadToPanel || a == A::SaveSrcToFile || a == A::Move || a == A::Copy || a == A::CopyMultiAsIs
        || a == A::CopyMultiCollated || a == A::Swap || a == A::CompareFull || a == A::CompareDiff;
}

bool LibrarianTab::needsDest (lib::ProgramAction a) const
{
    using A = lib::ProgramAction;
    return a == A::LoadFileToDest || a == A::InitDest || a == A::BufferToDest || a == A::Move || a == A::Copy
        || a == A::CopyMultiAsIs || a == A::CopyMultiCollated || a == A::Swap || a == A::CompareFull || a == A::CompareDiff;
}

// ======================================================================= program actions
void LibrarianTab::runProgramAction()
{
    using A = lib::ProgramAction;
    const auto a = (A) (programAction.getSelectedId() - 1);
    const int sb = proc.libSrcBank, ss = proc.libSrcFirst(), db = proc.libDstBank, ds = proc.libDstSlot;
    auto& s = proc.getSettings();

    if (needsSource (a) && (ss < 0 || ! juce::isPositiveAndBelow (sb, 3) || ! proc.bank (sb).loaded))
    { ed.inform (true, "Librarian", u ("Wybierz program źródłowy (lewy klik)."), true); return; }
    if (needsDest (a) && (ds < 0 || ! juce::isPositiveAndBelow (db, 3) || ! proc.bank (db).loaded))
    { ed.inform (true, "Librarian", u ("Wybierz program docelowy (prawy klik)."), true); return; }

    const bool toSynth = db == (int) BankId::Synth;
    const juce::String synthNote = toSynth ? u ("\n\nCel jest w banku Minilogue XD - program w syntezatorze zostanie nadpisany.") : juce::String();

    switch (a)
    {
        case A::LoadToPanel:
            ed.confirm (s.loadConfirm, "Load", u ("Wczytać ") + slotName (sb, ss) + u (" do panelu i bufora syntezatora?"), [this, sb, ss]
            {
                const bool send = proc.canTalkToSynth();
                proc.loadProgram (proc.bank (sb).progs[(size_t) ss], proc.bank (sb).info[(size_t) ss], send);
                ed.inform (proc.getSettings().loadOkConfirm, "Load", send ? u ("Wczytano i wysłano do bufora.") : u ("Wczytano do panelu (brak połączenia z syntezatorem)."), ! send);
            });
            break;
        case A::InitToPanel: ed.doInit(); break;
        case A::LoadFileToDest:
            ed.chooseFile (false, u ("Program do wstawienia w ") + slotName (db, ds), "*.syx;*.mnlgxdprog;*.prog_bin",
                           juce::File (s.lastProgramDir), [this, db, ds, synthNote] (juce::File f)
            {
                auto p = std::make_shared<Program>();
                auto info = std::make_shared<AddInfo>();
                auto r = files::loadProgram (f, *p, *info);
                if (! r.ok) { ed.inform (true, "Load", r.message, true); return; }
                ed.confirm (proc.getSettings().actionsConfirm, "Load", u ("Wstawić \"") + p->getName() + u ("\" do ") + slotName (db, ds) + "?" + synthNote,
                            [this, db, ds, p, info] { proc.lcdMessage (lib::putProgram (proc, db, ds, *p, *info)); });
            });
            break;
        case A::InitDest:
            ed.confirm (s.actionsConfirm, "Init", u ("Wstawić program Init do ") + slotName (db, ds) + "?" + synthNote,
                        [this, db, ds] { proc.lcdMessage (lib::initSlot (proc, db, ds)); });
            break;
        case A::SaveSrcToFile:
        {
            auto& b = proc.bank (sb);
            auto name = b.progs[(size_t) ss].getName().trim();
            auto initial = juce::File (s.lastProgramDir).getChildFile ((name.isEmpty() ? juce::String ("program") : name) + (s.saveMode == 1 ? ".mnlgxdprog" : ".syx"));
            ed.chooseFile (true, u ("Zapisz ") + slotName (sb, ss), "*.syx;*.mnlgxdprog", initial, [this, sb, ss] (juce::File f)
            {
                auto& bk = proc.bank (sb);
                auto r = f.hasFileExtension (".mnlgxdprog") ? files::saveProgramXdProg (f, bk.progs[(size_t) ss], bk.info[(size_t) ss])
                                                            : files::saveProgramSyx (f.withFileExtension (".syx"), bk.progs[(size_t) ss], bk.info[(size_t) ss], proc.getSettings().channel);
                ed.inform (proc.getSettings().saveOkConfirm || ! r.ok, "Save", r.ok ? u ("Zapisano: ") + f.getFileName() : r.message, ! r.ok);
            });
            break;
        }
        case A::BufferToDest:
            ed.confirm (s.actionsConfirm, "Buffer", u ("Odebrać bufor syntezatora i zapisać w ") + slotName (db, ds) + "?" + synthNote, [this, db, ds]
            {
                juce::Component::SafePointer<LibrarianTab> safe (this);
                proc.requestBufferToBank ((BankId) db, ds, [safe] (bool ok, juce::String msg)
                {
                    if (safe != nullptr) safe->ed.inform (! ok || safe->proc.getSettings().saveOkConfirm, "Buffer", ok ? u ("Zapisano: ") + msg : msg, ! ok);
                });
            });
            break;
        case A::Move:
            ed.confirm (s.actionsConfirm, "Move", u ("Przenieść ") + slotName (sb, ss) + " -> " + slotName (db, ds) + u ("?\nŹródło zostanie zastąpione programem Init.") + synthNote,
                        [this, sb, ss, db, ds] { proc.lcdMessage (lib::move (proc, sb, ss, db, ds)); });
            break;
        case A::Copy:
            ed.confirm (s.actionsConfirm, "Copy", u ("Kopiować ") + slotName (sb, ss) + " -> " + slotName (db, ds) + "?" + synthNote,
                        [this, sb, ss, db, ds] { proc.lcdMessage (lib::copy (proc, sb, { ss }, db, ds, true)); });
            break;
        case A::CopyMultiAsIs:
        case A::CopyMultiCollated:
        {
            const bool collated = a == A::CopyMultiCollated;
            auto targets = lib::multiCopyTargets (proc.libSrcSlots, ds, collated);
            auto slots = proc.libSrcSlots;
            ed.confirm (s.actionsConfirm, "Copy", u ("Kopiować ") + juce::String (slots.size()) + u (" program(ów) do ") + bankTitle ((BankId) db)
                            + u (" od slotu ") + juce::String (ds + 1).paddedLeft ('0', 3) + u ("?\nNadpisanych zostanie ") + juce::String (targets.size()) + u (" slotów.") + synthNote,
                        [this, sb, slots, db, ds, collated] { ed.inform (proc.getSettings().saveOkConfirm, "Copy", lib::copy (proc, sb, slots, db, ds, collated)); });
            break;
        }
        case A::Swap:
            ed.confirm (s.actionsConfirm, "Swap", u ("Zamienić ") + slotName (sb, ss) + " <-> " + slotName (db, ds) + "?"
                            + ((sb == (int) BankId::Synth || toSynth) ? u ("\n\nZmiana obejmie pamięć syntezatora.") : juce::String()),
                        [this, sb, ss, db, ds] { proc.lcdMessage (lib::swap (proc, sb, ss, db, ds)); });
            break;
        case A::CompareFull:
        case A::CompareDiff:
            proc.lastReport = proc.buildInfoReport (a == A::CompareFull ? ReportKind::LibraryFullComparison : ReportKind::LibraryDifferences);
            proc.uiNotify (XDProcessor::cReport);
            ed.showTab (4);
            break;
    }
}

// ======================================================================= bank actions
void LibrarianTab::runBankAction()
{
    using A = lib::BankAction;
    const auto a = (A) (bankAction.getSelectedId() - 1);
    const int b = selectedBank;
    auto& bank = proc.bank (b);
    auto& s = proc.getSettings();
    const auto title = bankTitle ((BankId) b);
    const auto initialDir = juce::File (s.lastBankDir).isDirectory() ? juce::File (s.lastBankDir)
                                                                    : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    juce::Component::SafePointer<LibrarianTab> safe (this);

    switch (a)
    {
        case A::LoadFromDisk:
            ed.confirm (bank.dirty && s.actionsConfirm, title, u ("Bank ma niezapisane zmiany. Kontynuować?"), [this, b, title, initialDir]
            {
                ed.chooseFile (false, u ("Wczytaj bank do ") + title, "*.syx;*.mnlgxdlib", initialDir, [this, b, title] (juce::File f)
                {
                    auto doLoad = [this, b, f, title]
                    {
                        auto r = proc.loadBankFile ((BankId) b, f);
                        if (! r.ok) { ed.inform (true, title, r.message, true); return; }
                        if (b == (int) BankId::Synth && proc.canTalkToSynth())
                        {
                            juce::Component::SafePointer<LibrarianTab> sf (this);
                            proc.startSendBank (BankId::Synth, [sf] (bool ok, juce::String msg) { if (sf != nullptr) sf->ed.inform (true, "Minilogue XD", msg, ! ok); });
                        }
                        else ed.inform (proc.getSettings().loadOkConfirm, title, r.message);
                    };
                    if (b == (int) BankId::Synth)
                        ed.confirm (true, title, u ("UWAGA: wczytanie do banku Minilogue XD wyśle wszystkie 500 programów do syntezatora i nadpisze jego pamięć. Kontynuować?"), doLoad);
                    else doLoad();
                });
            });
            break;
        case A::SaveToDisk:
        case A::SaveAsKorgLib:
        {
            if (! bank.loaded) { ed.inform (true, title, u ("Bank jest pusty."), true); return; }
            const bool korg = a == A::SaveAsKorgLib;
            auto initial = bank.file != juce::File() ? bank.file.withFileExtension (korg ? ".mnlgxdlib" : ".syx")
                                                     : initialDir.getChildFile (title.removeCharacters (" ") + (korg ? ".mnlgxdlib" : ".syx"));
            ed.chooseFile (true, u ("Zapisz bank ") + title, korg ? "*.mnlgxdlib" : "*.syx", initial, [this, b, korg, title] (juce::File f)
            {
                auto r = proc.saveBankFile ((BankId) b, f.withFileExtension (korg ? ".mnlgxdlib" : ".syx"));
                ed.inform (proc.getSettings().saveOkConfirm || ! r.ok, title, r.ok ? u ("Bank zapisany: ") + f.getFileName() : r.message, ! r.ok);
            });
            break;
        }
        case A::InitBank:
            ed.confirm (s.actionsConfirm, title, u ("Utworzyć pusty bank (500 x Init)?") + (b == (int) BankId::Synth ? u ("\nBank NIE zostanie wysłany do syntezatora.") : juce::String()),
                        [this, b] { proc.initBank ((BankId) b); });
            break;
        case A::ReceiveFromSynth:
            if (b != (int) BankId::Synth) { selectedBank = (int) BankId::Synth; refreshAll(); }
            ed.confirm (s.receiveConfirm, "Minilogue XD", u ("Odebrać wszystkie 500 programów z syntezatora?\nTrwa to ok. ")
                            + juce::String (s.transferMs * 500 / 1000 + 10) + u (" s (szybkość transferu w Settings)."), [this, safe]
            {
                proc.startReceiveBank ([safe] (bool ok, juce::String msg)
                {
                    if (safe == nullptr) return;
                    safe->ed.inform (true, "Minilogue XD", msg + u ("\n\nNie zapomnij zapisać banku na dysku (Zapisz na dysk)."), ! ok);
                });
            });
            break;
        case A::SendToSynth:
            if (! bank.loaded) { ed.inform (true, title, u ("Bank jest pusty."), true); return; }
            ed.confirm (true, title, u ("Wysłać cały bank ") + title + u (" do syntezatora?\nWSZYSTKIE 500 programów w syntezatorze zostanie nadpisanych."), [this, b, safe]
            {
                proc.startSendBank ((BankId) b, [safe] (bool ok, juce::String msg) { if (safe != nullptr) safe->ed.inform (true, "Minilogue XD", msg, ! ok); });
            });
            break;
        case A::Clear:
            ed.confirm (bank.dirty || s.actionsConfirm, title, u ("Zamknąć bank") + (bank.dirty ? u (" (są niezapisane zmiany)") : juce::String()) + "?",
                        [this, b] { proc.clearBank ((BankId) b); });
            break;
        case A::ExportList:
            if (! bank.loaded) { ed.inform (true, title, u ("Bank jest pusty."), true); return; }
            ed.chooseFile (true, u ("Eksport listy programów"), "*.txt", initialDir.getChildFile (title.removeCharacters (" ") + "_list.txt"),
                           [this, b, title] (juce::File f)
            {
                auto r = files::exportBankList (f.withFileExtension (".txt"), proc.bank (b), title);
                ed.inform (true, title, r.ok ? u ("Lista zapisana.") : r.message, ! r.ok);
            });
            break;
    }
}
