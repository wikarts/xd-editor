#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace xd;

static int msNow() { return (int) juce::Time::getMillisecondCounter(); }

XDProcessor::XDProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    auto& table = ParamTable::get();
    dirty.reset (new std::atomic<bool>[(size_t) table.size()]);
    for (auto& d : table.all())
    {
        dirty[(size_t) d.index] = false;
        auto def = d; // copy for the lambda
        auto attrs = juce::AudioParameterIntAttributes().withStringFromValueFunction (
            [this, def] (int v, int) { return formatValue (def, v, &settings->userNames); });
        auto* p = new juce::AudioParameterInt (juce::ParameterID { d.key, 1 }, d.name, d.minV, d.maxV, d.defV, attrs);
        addParameter (p);
        params.push_back (p);
        p->addListener (this);
    }
    morphParam = new juce::AudioParameterFloat (juce::ParameterID { "morph", 1 }, "Morph", 0.0f, 1.0f, 0.0f);
    addParameter (morphParam);
    morphParam->addListener (this);

    morphLocks.fill (false);
    for (auto& b : banks) b.clear();

    settings->addChangeListener (this);
    midi->addClient (this);
    applyMidiSettings();
    startTimer (20);
    deviceInquiry();
}

XDProcessor::~XDProcessor()
{
    stopTimer();
    cancelPendingUpdate();
    settings->removeChangeListener (this);
    midi->removeClient (this);
    for (auto* p : params) p->removeListener (this);
    morphParam->removeListener (this);
}

juce::AudioProcessorEditor* XDProcessor::createEditor() { return new XDEditor (*this); }

void XDProcessor::applyMidiSettings()
{
    midi->open (settings->midiInId, settings->midiOutId);
}

void XDProcessor::makeActive() { midi->setActive (this); }

bool XDProcessor::canTalkToSynth() const
{
    return settings->outputRoute != 0 || midi->hasOutput();
}

void XDProcessor::notify (int what)
{
    listeners.call ([what] (Listener& l) { l.xdChanged (what); });
}

// ======================================================================= audio / host MIDI
static bool isSynthCC (int cc)
{
    switch (cc)
    {
        case 0: case 6: case 32: case 54: case 63: case 96: case 98: case 99: case 100: case 103: case 104: return true;
        default: return ParamTable::get().paramForCC (cc) >= 0;
    }
}

void XDProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    buffer.clear();
    const bool parseHost = settings->hostInputAsSynth;
    const int route = settings->outputRoute;

    juce::MidiBuffer thru;
    for (const auto meta : midiMessages)
    {
        const auto msg = meta.getMessage();
        const bool synthMsg = msg.isSysEx() || msg.isProgramChange() || (msg.isController() && isSynthCC (msg.getControllerNumber()));
        if (parseHost && synthMsg)
        {
            queueInbound (msg);
            continue;
        }
        thru.addEvent (msg, meta.samplePosition);
    }
    midiMessages.clear();

    if (settings->hostThru)
    {
        if (route == 1) midiMessages.addEvents (thru, 0, -1, 0);
        else midi->sendBlockFromAudioThread (thru, currentSampleRate);
    }

    if (route != 0)
    {
        const juce::SpinLock::ScopedTryLockType sl (hostOutLock);
        if (sl.isLocked() && ! hostOut.isEmpty())
        {
            midiMessages.addEvents (hostOut, 0, -1, 0);
            hostOut.clear();
        }
    }
}

void XDProcessor::queueInbound (const juce::MidiMessage& m)
{
    {
        const juce::ScopedLock sl (inboundLock);
        inbound.push_back (m);
    }
    triggerAsyncUpdate();
}

void XDProcessor::deviceMidiReceived (const juce::MidiMessage& m)
{
    if (m.isSysEx() || m.isController() || m.isProgramChange())
        queueInbound (m);
}

// ======================================================================= parameters
void XDProcessor::parameterValueChanged (int index, float)
{
    if (index >= 0 && index < (int) params.size()) dirty[(size_t) index] = true;
    else morphDirty = true;
    triggerAsyncUpdate();
}

void XDProcessor::handleAsyncUpdate()
{
    std::vector<juce::MidiMessage> msgs;
    {
        const juce::ScopedLock sl (inboundLock);
        msgs.swap (inbound);
    }
    for (auto& m : msgs) handleInbound (m);

    int changed = 0;
    for (size_t i = 0; i < params.size(); ++i)
    {
        if (! dirty[i].exchange (false)) continue;
        const int v = params[i]->get();
        const auto& d = ParamTable::get()[(int) i];
        // values outside the documented range (seen in real patches) are clamped by the host parameter:
        // don't let that round trip silently rewrite the program
        if (v == juce::jlimit (d.minV, d.maxV, current.get ((int) i))) continue;
        current.set ((int) i, v);
        sendParam ((int) i);
        if (! lcdOff) lcd.param = (int) i;
        changed |= cParam | cLcd;
    }
    if (morphDirty.exchange (false))
    {
        const double m = morphParam->get();
        if (std::abs (m - morphAmount) > 1.0e-4) setMorph (m, true);
    }
    if (changed) notify (changed);
}

void XDProcessor::pushToHost (int pi)
{
    auto* p = params[(size_t) pi];
    const int v = current.get (pi);
    if (p->get() != v)
        p->setValueNotifyingHost (p->convertTo0to1 ((float) v));
}

void XDProcessor::pushAllToHost()
{
    for (int i = 0; i < (int) params.size(); ++i) pushToHost (i);
}

void XDProcessor::setValueFromUI (int pi, int value)
{
    auto& d = ParamTable::get()[pi];
    value = juce::jlimit (d.minV, d.maxV, value);
    if (value == current.get (pi)) return;
    current.set (pi, value);
    sendParam (pi);
    pushToHost (pi);
    if (! lcdOff) lcd.param = pi;
    makeActive();
    notify (cParam | cLcd);
}

void XDProcessor::beginGesture (int pi) { if (juce::isPositiveAndBelow (pi, (int) params.size())) params[(size_t) pi]->beginChangeGesture(); }
void XDProcessor::endGesture (int pi) { if (juce::isPositiveAndBelow (pi, (int) params.size())) params[(size_t) pi]->endChangeGesture(); }

void XDProcessor::showParamInfo (int pi)
{
    lcd.param = pi;
    notify (cLcd);
}

// ======================================================================= sending
void XDProcessor::send (const juce::MidiMessage& m)
{
    const int route = settings->outputRoute;
    if (route == 0 || route == 2) midi->sendNow (m);
    if (route == 1 || route == 2)
    {
        const juce::SpinLock::ScopedLockType sl (hostOutLock);
        hostOut.addEvent (m, 0);
    }
}

void XDProcessor::sendSysex (const std::vector<uint8_t>& bytes) { send (sysex::toMessage (bytes)); }

void XDProcessor::sendCC (int cc, int v)
{
    send (juce::MidiMessage::controllerEvent (settings->channel + 1, cc, juce::jlimit (0, 127, v)));
}

void XDProcessor::sendNRPN (int lsb, int value, int mode)
{
    sendCC (99, 0);
    sendCC (98, lsb);
    if (mode == 10) { sendCC (63, value & 7); sendCC (6, (value >> 3) & 0x7F); }
    else if (mode == 8) { sendCC (63, value & 7); sendCC (6, (value >> 3) & 0x1F); }
    else sendCC (6, value & 0x7F);
}

void XDProcessor::sendParam (int pi)
{
    if (! canTalkToSynth()) return;
    auto& t = ParamTable::get();
    auto& d = t[pi];
    const int v = current.get (pi);
    switch (d.tx)
    {
        case Tx::CC10: { const int x = juce::jmin (1023, v); sendCC (63, x & 7); sendCC (d.number, (x >> 3) & 0x7F); break; }
        case Tx::CC7: sendCC (d.number, v); break;
        case Tx::CCStep:
        {
            const int idx = v - d.valueBase;
            sendCC (d.number, juce::jmin (127, (2 * idx + 1) * 64 / d.steps));
            break;
        }
        case Tx::CCBool: sendCC (d.number, v ? 127 : 0); break;
        case Tx::CCCtx:
        {
            static const char* keys[] = { "modfx_chorus", "modfx_ensemble", "modfx_phaser", "modfx_flanger", "modfx_user" };
            const int type = current.get (t.indexOf ("modfx_type"));
            if (juce::isPositiveAndBelow (type - 1, 5) && d.key == keys[type - 1])
                sendCC (96, juce::jmin (127, (2 * v + 1) * 64 / d.steps));
            else if (settings->autoSendDump)
                requestDump();
            break;
        }
        case Tx::NRPN7:
        {
            int x = v;
            if (d.key == "program_level") x = v - 12;
            else if (d.key == "micro_tuning" && v >= 128) x = v - 16; // user scales 128..139 -> 112..123 over NRPN
            sendNRPN (d.number, x, 7);
            break;
        }
        case Tx::NRPN10: sendNRPN (d.number, v, 10); break;
        case Tx::NRPN8: sendNRPN (d.number, v, 8); break;
        case Tx::None:
        default:
            if (settings->autoSendDump) requestDump();
            break;
    }
}

bool XDProcessor::sendCurrentToSynth()
{
    if (! canTalkToSynth()) return false;
    sendSysex (sysex::currentProgramDump (settings->channel, current));
    dumpPendingAt = 0;
    return true;
}

// ======================================================================= program level
void XDProcessor::loadProgram (const Program& p, const AddInfo& info, bool sendToSynth, bool markSaved)
{
    morphAmount = 0.0;
    morphFromValid = false;
    if (morphParam->get() > 0.0f) morphParam->setValueNotifyingHost (0.0f);

    current = p;
    curInfo = info;
    if (markSaved) { saved = p; savedInfo = info; }
    pushAllToHost();
    if (sendToSynth) sendCurrentToSynth();
    notify (cProgram | cParam | cLcd | cInfo | cMorph);
}

void XDProcessor::renameCurrent (const juce::String& name)
{
    current.setName (name);
    if (canTalkToSynth())
    {
        auto n = current.getName();
        for (int i = 0; i < 12; ++i)
            sendNRPN (i + 1, i < n.length() ? (int) n[i] : 32, 7); // NRPN 0x01..0x0C
    }
    notify (cProgram | cLcd);
}

// ======================================================================= incoming
void XDProcessor::handleInbound (const juce::MidiMessage& m)
{
    if (m.isSysEx()) { handleSysex (m); return; }
    if (m.isProgramChange())
    {
        auto e = parser.programChange (m.getProgramChangeNumber());
        if (directMode)
        {
            slotSel = juce::jlimit (1, numSlots, e.value + 1);
            notify (cSelection | cLcd);
            requestCurrentProgram (nullptr);
        }
        return;
    }
    if (! m.isController()) return;
    if (m.getChannel() != settings->channel + 1) return;

    auto& t = ParamTable::get();
    auto e = parser.handle (m.getControllerNumber(), m.getControllerValue());
    switch (e.type)
    {
        case CcParser::Event::Param: applyIncomingParam (e.param, e.value); break;
        case CcParser::Event::Morph: setMorph (e.value / 127.0); break;
        case CcParser::Event::NameChar:
        {
            auto n = current.getName().paddedRight (' ', 12);
            n = n.substring (0, e.param) + juce::String::charToString ((juce::juce_wchar) (e.value ? e.value : ' ')) + n.substring (e.param + 1);
            current.setName (n.trimEnd());
            notify (cProgram);
            break;
        }
        case CcParser::Event::ContextCC:
        {
            const int mt = current.get (t.indexOf ("multi_type"));
            static const char* shape[] = { "shape_noise", "shape_vpm", "shape_user" };
            static const char* shift[] = { "shift_shape_noise", "shift_shape_vpm", "shift_shape_user" };
            static const char* sel[] = { "select_noise", "select_vpm", "select_user" };
            static const char* fx[] = { "modfx_chorus", "modfx_ensemble", "modfx_phaser", "modfx_flanger", "modfx_user" };
            const int m3 = juce::jlimit (0, 2, mt);
            if (e.cc == 54) applyIncomingParam (t.indexOf (shape[m3]), e.value);
            else if (e.cc == 104) applyIncomingParam (t.indexOf (shift[m3]), e.value);
            else if (e.cc == 103)
            {
                auto pi = t.indexOf (sel[m3]);
                applyIncomingParam (pi, juce::jmin (t[pi].steps - 1, e.value * t[pi].steps / 128));
            }
            else if (e.cc == 96)
            {
                const int type = juce::jlimit (1, 5, current.get (t.indexOf ("modfx_type")));
                auto pi = t.indexOf (fx[type - 1]);
                applyIncomingParam (pi, juce::jmin (t[pi].steps - 1, e.value * t[pi].steps / 128));
            }
            break;
        }
        case CcParser::Event::None:
        case CcParser::Event::ProgramChange:
        default: break;
    }
}

void XDProcessor::applyIncomingParam (int pi, int value)
{
    if (pi < 0) return;
    auto& d = ParamTable::get()[pi];
    value = juce::jlimit (d.minV, d.maxV, value);
    if (value == current.get (pi)) return;
    current.set (pi, value);
    pushToHost (pi);
    if (! lcdOff) lcd.param = pi;
    notify (cParam | cLcd);
}

void XDProcessor::handleSysex (const juce::MidiMessage& m)
{
    const auto* d = m.getSysExData();
    const int n = m.getSysExDataSize();

    auto id = sysex::parseIdentity (d, n);
    if (id.valid)
    {
        midi->connected = true;
        midi->osVersion = juce::String (id.major) + "." + juce::String (id.minor).paddedLeft ('0', 2);
        ++midi->stateCounter;
        if (settings->channel != id.channel) { settings->channel = id.channel; settings->changed(); }
        if (pending.type == Pending::Inquiry) finishPending (true, "minilogue xd OS " + midi->osVersion);
        notify (cStatus);
        return;
    }

    auto p = sysex::parse (d, n);
    if (! p.valid) return;

    switch (p.func)
    {
        case sysex::CurrentProgramDump:
        {
            Program prog;
            if (! prog.loadFrom (p.payload.data(), p.payload.size())) return;
            if (pending.type == Pending::BufferToBank)
            {
                auto& b = banks[(size_t) pending.bank];
                b.progs[(size_t) pending.slot] = prog;
                b.info[(size_t) pending.slot] = {};
                bankModified ((BankId) pending.bank, { pending.slot }, true);
                finishPending (true, prog.getName());
            }
            else
            {
                loadProgram (prog, {}, false);
                if (pending.type == Pending::CurrentToPanel) finishPending (true, prog.getName());
                else lcdMessage (juce::String::fromUTF8 ("Odebrano program: ") + prog.getName());
            }
            break;
        }
        case sysex::ProgramDump:
        {
            Program prog;
            if (! prog.loadFrom (p.payload.data(), p.payload.size())) return;
            if (job.type == Job::ReceiveBank && job.waiting && job.pos < job.slots.size() && job.slots[job.pos] == p.slot)
            {
                job.received[job.pos] = prog;
                job.receivedOk[job.pos] = true;
                ++job.pos;
                job.waiting = false;
                job.retries = 0;
                job.nextAt = (juce::uint32) msNow() + (juce::uint32) settings->transferMs;
            }
            else if (pending.type == Pending::SlotToPanel && pending.slot == p.slot)
            {
                auto& xdBank = banks[(size_t) BankId::Synth];
                AddInfo info = xdBank.loaded ? xdBank.info[(size_t) p.slot] : AddInfo();
                if (xdBank.loaded && xdBank.progs[(size_t) p.slot] != prog)
                {
                    xdBank.progs[(size_t) p.slot] = prog;
                    bankModified (BankId::Synth, { p.slot }, false);
                }
                loadProgram (prog, info, false);
                finishPending (true, prog.getName());
            }
            break;
        }
        case sysex::GlobalDump:
            globals = p.payload;
            if (pending.type == Pending::Globals) finishPending (true, {});
            notify (cGlobals);
            break;
        case sysex::UserSlotStatusReply:
            if (job.type == Job::UserNames && job.waiting && job.pos < job.slots.size())
            {
                const int e = job.slots[job.pos];
                const int module = e / 100, slot = e % 100;
                auto name = sysex::userSlotName (p);
                const int kind = module == 4 ? 0 : module - 1; // UserNames order: osc, modfx, delay, reverb
                auto& arr = settings->userNames.kind (kind);
                if (juce::isPositiveAndBelow (slot, arr.size()))
                    arr.set (slot, name.isNotEmpty() ? name : "EMPTY" + juce::String (slot + 1));
                ++job.pos;
                job.waiting = false;
                job.nextAt = (juce::uint32) msNow() + 30;
            }
            break;
        case sysex::DataLoadError:
        case sysex::DataFormatError:
        case 0x2C: case 0x2D: case 0x2F:
            if (job.type == Job::UserNames && job.waiting && job.pos < job.slots.size())
            {
                const int e = job.slots[job.pos];
                const int kind = e / 100 == 4 ? 0 : e / 100 - 1;
                auto& arr = settings->userNames.kind (kind);
                if (juce::isPositiveAndBelow (e % 100, arr.size())) arr.set (e % 100, "EMPTY" + juce::String (e % 100 + 1));
                ++job.pos;
                job.waiting = false;
                job.nextAt = (juce::uint32) msNow() + 30;
                break;
            }
            lcdMessage (juce::String::fromUTF8 ("Syntezator zgłosił błąd odbioru danych."));
            break;
        default: break;
    }
}

// ======================================================================= requests
void XDProcessor::startPending (Pending::Type t, int bankIdx, int slot, Done done, int timeoutMs)
{
    if (pending.type != Pending::None) finishPending (false, juce::String::fromUTF8 ("Przerwano poprzednie żądanie."));
    pending.type = t;
    pending.bank = bankIdx;
    pending.slot = slot;
    pending.done = std::move (done);
    pending.deadline = (juce::uint32) msNow() + (juce::uint32) timeoutMs;
}

void XDProcessor::finishPending (bool ok, const juce::String& msg)
{
    auto done = std::move (pending.done);
    pending = {};
    if (done) done (ok, msg);
}

void XDProcessor::deviceInquiry()
{
    if (! canTalkToSynth()) { midi->connected = false; ++midi->stateCounter; notify (cStatus); return; }
    startPending (Pending::Inquiry, 0, 0, [this] (bool ok, juce::String)
    {
        if (! ok) { midi->connected = false; ++midi->stateCounter; }
        notify (cStatus);
    }, 1500);
    makeActive();
    send (sysex::deviceInquiry());
}

static juce::String noLink() { return juce::String::fromUTF8 ("Brak połączenia z syntezatorem (sprawdź ustawienia MIDI)."); }

void XDProcessor::requestCurrentProgram (Done done)
{
    if (! canTalkToSynth()) { if (done) done (false, noLink()); return; }
    makeActive();
    startPending (Pending::CurrentToPanel, 0, 0, std::move (done));
    send (sysex::request (settings->channel, sysex::CurrentProgramRequest));
}

void XDProcessor::requestSlotToPanel (int slot, Done done)
{
    if (! canTalkToSynth()) { if (done) done (false, noLink()); return; }
    makeActive();
    startPending (Pending::SlotToPanel, (int) BankId::Synth, slot, std::move (done));
    send (sysex::programRequest (settings->channel, slot));
}

void XDProcessor::requestBufferToBank (BankId b, int slot, Done done)
{
    if (! canTalkToSynth()) { if (done) done (false, noLink()); return; }
    makeActive();
    startPending (Pending::BufferToBank, (int) b, slot, std::move (done));
    send (sysex::request (settings->channel, sysex::CurrentProgramRequest));
}

void XDProcessor::requestGlobals (Done done)
{
    if (! canTalkToSynth()) { if (done) done (false, noLink()); return; }
    makeActive();
    startPending (Pending::Globals, 0, 0, std::move (done));
    send (sysex::request (settings->channel, sysex::GlobalRequest));
}

bool XDProcessor::sendGlobals()
{
    if (globals.empty() || ! canTalkToSynth()) return false;
    sendSysex (sysex::globalDump (settings->channel, globals));
    return true;
}

int XDProcessor::getGlobal (const GlobalField& f) const
{
    return f.offset < (int) globals.size() ? globals[(size_t) f.offset] : 0;
}

void XDProcessor::setGlobal (const GlobalField& f, int v)
{
    if (f.offset < (int) globals.size())
    {
        globals[(size_t) f.offset] = (uint8_t) juce::jlimit (f.minV, f.maxV, v);
        notify (cGlobals);
    }
}

void XDProcessor::loadUserNames (Done done)
{
    if (! canTalkToSynth()) { if (done) done (false, noLink()); return; }
    if (isTransferring()) { if (done) done (false, juce::String::fromUTF8 ("Trwa inny transfer.")); return; }
    makeActive();
    job = {};
    job.type = Job::UserNames;
    for (int s = 0; s < 16; ++s) job.slots.push_back (400 + s);
    for (int s = 0; s < 16; ++s) job.slots.push_back (100 + s);
    for (int s = 0; s < 8; ++s) job.slots.push_back (200 + s);
    for (int s = 0; s < 8; ++s) job.slots.push_back (300 + s);
    job.done = std::move (done);
    job.nextAt = (juce::uint32) msNow();
}

void XDProcessor::resetUserNames()
{
    settings->userNames.reset();
    settings->changed();
}

// ======================================================================= banks
IoResult XDProcessor::loadBankFile (BankId b, const juce::File& f)
{
    auto r = files::loadBank (f, banks[(size_t) b], settings->channel);
    if (r.ok) settings->lastBankDir = f.getParentDirectory().getFullPathName();
    notify (cBanks | cSelection);
    return r;
}

IoResult XDProcessor::saveBankFile (BankId b, const juce::File& f)
{
    auto& bank = banks[(size_t) b];
    auto r = files::saveBank (f, bank, settings->channel);
    if (r.ok)
    {
        if (f.hasFileExtension (".syx")) bank.file = f;
        settings->lastBankDir = f.getParentDirectory().getFullPathName();
    }
    notify (cBanks);
    return r;
}

void XDProcessor::bankModified (BankId b, const juce::Array<int>& slots, bool sendToSynth)
{
    auto& bank = banks[(size_t) b];
    bank.loaded = true;
    bank.dirty = true;
    if (settings->bankAutoSave && bank.file != juce::File() && bank.file.hasFileExtension (".syx"))
        files::saveBank (bank.file, bank, settings->channel);

    if (b == BankId::Synth && sendToSynth && canTalkToSynth() && ! slots.isEmpty())
    {
        if (job.type == Job::None)
        {
            job = {};
            job.type = Job::SendSlots;
            job.bank = (int) BankId::Synth;
            for (auto s : slots) job.slots.push_back (s);
            job.nextAt = (juce::uint32) msNow();
            job.done = [this] (bool, juce::String) { lcdMessage (juce::String::fromUTF8 ("Zapisano w syntezatorze.")); };
        }
        else if (job.type == Job::SendSlots && job.bank == (int) BankId::Synth)
        {
            for (auto s : slots) job.slots.push_back (s);
        }
        else lcdMessage (juce::String::fromUTF8 ("Trwa transfer - zmiana nie została wysłana do syntezatora."));
    }
    notify (cBanks);
}

void XDProcessor::initBank (BankId b)
{
    banks[(size_t) b].initAll();
    banks[(size_t) b].dirty = true;
    notify (cBanks);
}

void XDProcessor::clearBank (BankId b)
{
    banks[(size_t) b].clear();
    if (libSrcBank == (int) b) { libSrcBank = -1; libSrcSlots.clear(); }
    if (libDstBank == (int) b) { libDstBank = -1; libDstSlot = -1; morphTargetChanged(); }
    notify (cBanks | cSelection);
}

void XDProcessor::startReceiveBank (Done done)
{
    if (! canTalkToSynth()) { if (done) done (false, noLink()); return; }
    if (isTransferring()) { if (done) done (false, juce::String::fromUTF8 ("Trwa inny transfer.")); return; }
    makeActive();
    job = {};
    job.type = Job::ReceiveBank;
    job.bank = (int) BankId::Synth;
    for (int i = 0; i < numSlots; ++i) job.slots.push_back (i);
    job.received.resize ((size_t) numSlots);
    job.receivedOk.assign ((size_t) numSlots, false);
    job.done = std::move (done);
    job.nextAt = (juce::uint32) msNow();
    lcd.progress = true;
}

void XDProcessor::startSendBank (BankId b, Done done)
{
    if (! canTalkToSynth()) { if (done) done (false, noLink()); return; }
    if (isTransferring()) { if (done) done (false, juce::String::fromUTF8 ("Trwa inny transfer.")); return; }
    makeActive();
    job = {};
    job.type = Job::SendSlots;
    job.bank = (int) b;
    for (int i = 0; i < numSlots; ++i) job.slots.push_back (i);
    job.done = [this, b, done] (bool ok, juce::String msg)
    {
        if (ok && b != BankId::Synth)
        {
            auto& xdBank = banks[(size_t) BankId::Synth];
            xdBank.progs = banks[(size_t) b].progs;
            xdBank.info = banks[(size_t) b].info;
            xdBank.loaded = true;
            xdBank.dirty = true;
            notify (cBanks);
        }
        if (done) done (ok, msg);
    };
    job.nextAt = (juce::uint32) msNow();
    lcd.progress = true;
}

void XDProcessor::cancelTransfer()
{
    if (job.type != Job::None) finishJob (false, juce::String::fromUTF8 ("Transfer przerwany."));
}

void XDProcessor::finishJob (bool ok, const juce::String& msg)
{
    auto done = std::move (job.done);
    const bool wasUserNames = job.type == Job::UserNames;
    job = {};
    lcd.progress = false;
    lcd.message.clear();
    if (wasUserNames) settings->changed();
    notify (cLcd | cBanks | cStatus);
    if (done) done (ok, msg);
}

void XDProcessor::jobTick (juce::uint32 now)
{
    if (job.type == Job::None) return;
    const auto total = job.slots.size();

    auto progress = [&] (const char* what)
    {
        const int pct = total > 0 ? (int) (100 * job.pos / total) : 100;
        lcd.message = juce::String::fromUTF8 (what) + " " + juce::String (pct) + "%  (" + juce::String ((int) job.pos) + "/" + juce::String ((int) total) + ")";
        lcd.progress = true;
        notify (cLcd);
    };

    if (job.type == Job::SendSlots)
    {
        if (now < job.nextAt) return;
        if (job.pos >= total) { finishJob (true, juce::String ((int) total) + juce::String::fromUTF8 (" programów wysłanych.")); return; }
        const int slot = job.slots[job.pos];
        sendSysex (sysex::programDump (settings->channel, slot, banks[(size_t) job.bank].progs[(size_t) slot]));
        ++job.pos;
        job.nextAt = now + (juce::uint32) settings->transferMs;
        if (total > 1) progress ("Wysyłanie");
        return;
    }

    const bool userNames = job.type == Job::UserNames;
    if (job.waiting)
    {
        if (now < job.deadline) return;
        if (userNames)
        {
            const int e = job.slots[job.pos];
            const int kind = e / 100 == 4 ? 0 : e / 100 - 1;
            auto& arr = settings->userNames.kind (kind);
            if (juce::isPositiveAndBelow (e % 100, arr.size())) arr.set (e % 100, "EMPTY" + juce::String (e % 100 + 1));
            ++job.pos;
        }
        else if (job.retries < 2) ++job.retries;
        else { ++job.errors; ++job.pos; job.retries = 0; }
        job.waiting = false;
    }
    if (now < job.nextAt) return;

    if (job.pos >= total)
    {
        if (userNames) { finishJob (true, juce::String::fromUTF8 ("Nazwy modułów użytkownika wczytane.")); return; }
        auto& b = banks[(size_t) BankId::Synth];
        const auto file = b.file;
        b.clear();
        for (size_t i = 0; i < total; ++i)
            if (job.receivedOk[i]) b.progs[i] = job.received[i];
        b.loaded = true;
        b.dirty = true;
        b.file = file;
        const int errors = job.errors;
        finishJob (errors == 0, errors == 0 ? juce::String::fromUTF8 ("Odebrano 500 programów.")
                                            : juce::String::fromUTF8 ("Odebrano z błędami: ") + juce::String (errors) + juce::String::fromUTF8 (" programów nie dotarło."));
        return;
    }

    const int e = job.slots[job.pos];
    if (userNames)
        send (sysex::userSlotStatusRequest (settings->channel, e / 100, e % 100));
    else
        send (sysex::programRequest (settings->channel, e));
    job.waiting = true;
    job.deadline = now + (juce::uint32) (userNames ? 700 : juce::jmax (1500, settings->transferMs * 4));
    progress (userNames ? "Nazwy USER" : "Odbieranie");
}

// ======================================================================= selection / morph
void XDProcessor::setSlotSel (int s)
{
    slotSel = juce::jlimit (0, numSlots, s);
    if (directMode && slotSel > 0 && canTalkToSynth())
    {
        const int slot = slotSel - 1;
        sendCC (0, 0);
        sendCC (32, slot / 100);
        send (juce::MidiMessage::programChange (settings->channel + 1, slot % 100));
    }
    notify (cSelection | cLcd);
}

void XDProcessor::setDirectMode (bool on)
{
    directMode = on && canTalkToSynth();
    notify (cSelection | cLcd);
}

bool XDProcessor::hasMorphTarget() const
{
    return juce::isPositiveAndBelow (libDstBank, numBanks) && juce::isPositiveAndBelow (libDstSlot, numSlots)
        && banks[(size_t) libDstBank].loaded;
}

Program XDProcessor::morphTarget() const
{
    return hasMorphTarget() ? banks[(size_t) libDstBank].progs[(size_t) libDstSlot] : current;
}

void XDProcessor::morphTargetChanged()
{
    morphFromValid = false;
    morphAmount = 0.0;
    if (morphParam->get() > 0.0f) morphParam->setValueNotifyingHost (0.0f);
    notify (cMorph);
}

void XDProcessor::setMorph (double amount, bool fromHost, bool ignoreLocks)
{
    amount = juce::jlimit (0.0, 1.0, amount);
    if (! fromHost && std::abs (morphParam->get() - amount) > 1.0e-4)
        morphParam->setValueNotifyingHost ((float) amount);
    morphAmount = amount;
    if (! hasMorphTarget()) { notify (cMorph); return; }
    if (! morphFromValid) { morphFrom = current; morphFromValid = true; }

    std::array<bool, numSections> locks {};
    if (! ignoreLocks) locks = morphLocks;
    auto target = morph (morphFrom, morphTarget(), amount, locks);
    const int n = ParamTable::get().size();
    for (int pi = 0; pi < n; ++pi)
    {
        if (locks[(size_t) ParamTable::get()[pi].section]) continue; // locked: leave the current value alone
        const int v = target.get (pi);
        if (v == current.get (pi)) continue;
        current.set (pi, v);
        sendParam (pi);
        pushToHost (pi);
    }
    notify (cParam | cMorph);
}

// ======================================================================= timer / misc
void XDProcessor::lcdMessage (const juce::String& s, int ms)
{
    lcd.message = s;
    lcd.messageUntil = (juce::uint32) msNow() + (juce::uint32) ms;
    notify (cLcd);
}

void XDProcessor::timerCallback()
{
    const auto now = (juce::uint32) msNow();
    if (dumpPendingAt != 0 && now >= dumpPendingAt)
    {
        dumpPendingAt = 0;
        sendCurrentToSynth();
    }
    if (pending.type != Pending::None && now > pending.deadline)
        finishPending (false, juce::String::fromUTF8 ("Brak odpowiedzi z syntezatora."));
    jobTick (now);

    if (recallSendPending && now >= recallSendAt)
    {
        recallSendPending = false;
        if (canTalkToSynth() && sendCurrentToSynth())
            lcdMessage (juce::String::fromUTF8 ("Przywrócono patch z projektu: ") + current.getName(), 4000);
    }
    if (lcd.message.isNotEmpty() && ! lcd.progress && now > lcd.messageUntil)
    {
        lcd.message.clear();
        notify (cLcd);
    }
    if (seenMidiState != midi->stateCounter)
    {
        seenMidiState = midi->stateCounter;
        notify (cStatus);
    }
}

void XDProcessor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    applyMidiSettings();
    notify (cStatus | cParam | cBanks);
}

juce::String XDProcessor::buildInfoReport (ReportKind k) const
{
    const auto& names = settings->userNames;
    switch (k)
    {
        case ReportKind::CurrentValues: return buildReport (current, curInfo, "Current", nullptr, nullptr, {}, false, names);
        case ReportKind::SavedValues: return buildReport (saved, savedInfo, "Saved", nullptr, nullptr, {}, false, names);
        case ReportKind::DiffWithInit: { Program init; return buildReport (current, curInfo, "Current", &init, nullptr, "Init", true, names); }
        case ReportKind::FullComparison: return buildReport (current, curInfo, "Current", &saved, &savedInfo, "Saved", false, names);
        case ReportKind::DifferencesOnly: return buildReport (current, curInfo, "Current", &saved, &savedInfo, "Saved", true, names);
        case ReportKind::LibraryFullComparison:
        case ReportKind::LibraryDifferences:
        {
            const int s = libSrcFirst();
            if (! juce::isPositiveAndBelow (libSrcBank, numBanks) || s < 0 || ! juce::isPositiveAndBelow (libDstBank, numBanks) || libDstSlot < 0)
                return juce::String::fromUTF8 ("Wybierz program źródłowy (lewy klik) i docelowy (prawy klik) w zakładce Librarian.");
            auto title = [] (int b, int slot) { return bankTitle ((BankId) b) + " " + juce::String (slot + 1).paddedLeft ('0', 3); };
            auto& A = banks[(size_t) libSrcBank];
            auto& B = banks[(size_t) libDstBank];
            return buildReport (A.progs[(size_t) s], A.info[(size_t) s], title (libSrcBank, s),
                                &B.progs[(size_t) libDstSlot], &B.info[(size_t) libDstSlot], title (libDstBank, libDstSlot),
                                k == ReportKind::LibraryDifferences, names);
        }
    }
    return {};
}

// ======================================================================= state (DAW project)
void XDProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::ValueTree t ("XDEditorState");
    t.setProperty ("version", 1, nullptr);
    t.setProperty ("current", current.toBase64(), nullptr);
    t.setProperty ("saved", saved.toBase64(), nullptr);
    auto ci = curInfo.toTree(); ci.setProperty ("which", "current", nullptr); t.appendChild (ci, nullptr);
    auto si = savedInfo.toTree(); si.setProperty ("which", "saved", nullptr); t.appendChild (si, nullptr);
    for (int b = 0; b < numBanks; ++b)
        t.setProperty ("bank" + juce::String (b), banks[(size_t) b].file.getFullPathName(), nullptr);
    t.setProperty ("sourceBank", sourceBank, nullptr);
    t.setProperty ("slotSel", slotSel, nullptr);
    t.setProperty ("lcdOff", lcdOff, nullptr);
    t.setProperty ("uiTab", uiTab, nullptr);
    juce::String locks;
    for (auto l : morphLocks) locks << (l ? "1" : "0");
    t.setProperty ("locks", locks, nullptr);
    if (auto xml = t.createXml()) copyXmlToBinary (*xml, dest);
}

void XDProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr) return;
    auto t = juce::ValueTree::fromXml (*xml);
    if (! t.hasType ("XDEditorState")) return;

    Program c, s;
    if (! c.fromBase64 (t.getProperty ("current").toString())) return;
    if (! s.fromBase64 (t.getProperty ("saved").toString())) s = c;
    AddInfo ci, si;
    for (auto child : t)
    {
        if (child.getProperty ("which").toString() == "current") ci = AddInfo::fromTree (child);
        else if (child.getProperty ("which").toString() == "saved") si = AddInfo::fromTree (child);
    }
    for (int b = 0; b < numBanks; ++b)
    {
        juce::File f (t.getProperty ("bank" + juce::String (b)).toString());
        auto& bank = banks[(size_t) b];
        if (f.existsAsFile() && bank.file != f)
        {
            if (files::loadBank (f, bank, settings->channel).ok) bank.dirty = false;
        }
    }
    sourceBank = juce::jlimit (0, numBanks - 1, (int) t.getProperty ("sourceBank", 0));
    slotSel = juce::jlimit (0, numSlots, (int) t.getProperty ("slotSel", 0));
    lcdOff = t.getProperty ("lcdOff", false);
    uiTab = t.getProperty ("uiTab", 0);
    auto locks = t.getProperty ("locks").toString();
    for (int i = 0; i < numSections && i < locks.length(); ++i) morphLocks[(size_t) i] = locks[i] == '1';
    directMode = false;

    current = c; curInfo = ci;
    saved = s; savedInfo = si;
    morphAmount = 0.0;
    morphFromValid = false;
    pushAllToHost();
    notify (cProgram | cParam | cLcd | cInfo | cBanks | cSelection | cMorph);

    if (settings->sendOnRecall)
    {
        recallSendPending = true;
        recallSendAt = (juce::uint32) msNow() + 700;
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new XDProcessor(); }
