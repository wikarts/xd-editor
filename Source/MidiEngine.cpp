#include "MidiEngine.h"
#include "core/Params.h"

SharedMidi::~SharedMidi()
{
    if (in != nullptr) in->stop();
}

void SharedMidi::addClient (Client* c)
{
    const juce::ScopedLock sl (clientLock);
    clients.addIfNotAlreadyThere (c);
    if (active.load() == nullptr) active = c;
}

void SharedMidi::removeClient (Client* c)
{
    const juce::ScopedLock sl (clientLock);
    clients.removeFirstMatchingValue (c);
    if (active.load() == c) active = clients.isEmpty() ? nullptr : clients.getFirst();
}

void SharedMidi::setActive (Client* c)
{
    const juce::ScopedLock sl (clientLock);
    if (clients.contains (c)) active = c;
}

void SharedMidi::open (const juce::String& newIn, const juce::String& newOut)
{
    if (newIn != inId || (in == nullptr && newIn.isNotEmpty()))
    {
        if (in != nullptr) { in->stop(); in.reset(); }
        inId = newIn;
        if (inId.isNotEmpty())
        {
            in = juce::MidiInput::openDevice (inId, this);
            if (in != nullptr) in->start();
        }
    }
    if (newOut != outId || (out == nullptr && newOut.isNotEmpty()))
    {
        std::unique_ptr<juce::MidiOutput> o;
        if (newOut.isNotEmpty())
        {
            o = juce::MidiOutput::openDevice (newOut);
            if (o != nullptr) o->startBackgroundThread();
        }
        const juce::SpinLock::ScopedLockType sl (outLock);
        out = std::move (o);
        outId = newOut;
    }
}

bool SharedMidi::hasOutput() const { const juce::SpinLock::ScopedLockType sl (outLock); return out != nullptr; }
bool SharedMidi::hasInput() const { return in != nullptr; }
juce::String SharedMidi::inputName() const { return in != nullptr ? in->getName() : juce::String(); }
juce::String SharedMidi::outputName() const { const juce::SpinLock::ScopedLockType sl (outLock); return out != nullptr ? out->getName() : juce::String(); }

bool SharedMidi::sendNow (const juce::MidiMessage& m)
{
    const juce::SpinLock::ScopedLockType sl (outLock);
    if (out == nullptr) return false;
    out->sendMessageNow (m);
    return true;
}

void SharedMidi::sendBlockFromAudioThread (const juce::MidiBuffer& b, double sampleRate)
{
    const juce::SpinLock::ScopedTryLockType sl (outLock);
    if (! sl.isLocked() || out == nullptr || b.isEmpty()) return;
    out->sendBlockOfMessages (b, juce::Time::getMillisecondCounterHiRes() + 1.0, sampleRate);
}

void SharedMidi::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    const juce::ScopedLock sl (clientLock);
    if (auto* c = active.load()) c->deviceMidiReceived (m);
}

// ------------------------------------------------------------------------------------------
CcParser::Event CcParser::programChange (int program)
{
    Event e;
    e.type = Event::ProgramChange;
    e.value = (bankLsb % 5) * 100 + program;
    return e;
}

CcParser::Event CcParser::handle (int cc, int v)
{
    using namespace xd;
    auto& table = ParamTable::get();
    Event e;
    switch (cc)
    {
        case 0: bankMsb = v; return e;
        case 32: bankLsb = v; return e;
        case 63: pendingLsb = v; return e;
        case 99: nrpnMsb = v; nrpnLsb = -1; return e;
        case 98: nrpnLsb = v; return e;
        case 100: e.type = Event::Morph; e.value = v; return e;
        case 6:
        {
            if (nrpnMsb != 0 || nrpnLsb < 0) { pendingLsb = -1; return e; }
            const int lsb = pendingLsb < 0 ? 0 : (pendingLsb & 7);
            pendingLsb = -1;
            if (nrpnLsb >= 0x01 && nrpnLsb <= 0x0C) { e.type = Event::NameChar; e.param = nrpnLsb - 1; e.value = v; return e; }
            const int pi = table.paramForNRPN (nrpnLsb);
            if (pi < 0) return e;
            auto& d = table[pi];
            int value = v;
            if (d.tx == Tx::NRPN10) value = (v << 3) | lsb;
            else if (d.tx == Tx::NRPN8) value = ((v & 0x1F) << 3) | lsb;
            else if (d.key == "program_level") value = v + 12;
            else if (d.key == "micro_tuning" && v >= 112) value = v + 16;
            else if (d.key == "voice_mode" && v == 0) value = 1; // ARP LATCH is stored as ARP
            e.type = Event::Param; e.param = pi; e.value = value;
            return e;
        }
        case 54: case 96: case 103: case 104:
        {
            // context dependent: resolved by the processor using the current multi / mod fx type
            const int lsb = pendingLsb < 0 ? 0 : (pendingLsb & 7);
            pendingLsb = -1;
            e.type = Event::ContextCC; e.cc = cc;
            e.value = (cc == 54 || cc == 104) ? ((v << 3) | lsb) : v;
            return e;
        }
        default: break;
    }

    const int pi = table.paramForCC (cc);
    if (pi < 0) { pendingLsb = -1; return e; }
    auto& d = table[pi];
    int value = v;
    switch (d.tx)
    {
        case Tx::CC10: value = (v << 3) | (pendingLsb < 0 ? 0 : (pendingLsb & 7)); break;
        case Tx::CCStep: value = juce::jmin (d.steps - 1, v * d.steps / 128) + d.valueBase; break;
        case Tx::CCBool: value = v >= 64 ? 1 : 0; break;
        case Tx::None: case Tx::CC7: case Tx::CCCtx: case Tx::NRPN7: case Tx::NRPN10: case Tx::NRPN8:
        default: break;
    }
    pendingLsb = -1;
    e.type = Event::Param; e.param = pi; e.value = value;
    return e;
}
