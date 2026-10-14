#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <deque>

/** MIDI devices opened once and shared by every instance in the process (a device can usually
    only be opened once on Windows). Incoming messages go to the "active" instance only - the one
    the user last worked with - so twisting a knob on the synth edits the patch you are looking at. */
class SharedMidi : private juce::MidiInputCallback
{
public:
    struct Client
    {
        virtual ~Client() = default;
        /** Called on the MIDI thread. */
        virtual void deviceMidiReceived (const juce::MidiMessage&) = 0;
    };

    SharedMidi() = default;
    ~SharedMidi() override;

    void addClient (Client*);
    void removeClient (Client*);
    void setActive (Client*);
    bool isActive (const Client* c) const { return active.load() == c; }

    /** Opens the devices (message thread). Empty id = none. */
    void open (const juce::String& inId, const juce::String& outId);
    bool hasOutput() const;
    bool hasInput() const;
    juce::String inputName() const;
    juce::String outputName() const;

    bool sendNow (const juce::MidiMessage&);

    // connection state shared by all instances (message thread only)
    bool connected = false;
    juce::String osVersion;
    int stateCounter = 0;
    /** Audio-thread safe: forwards a block of messages to the output (never blocks). */
    void sendBlockFromAudioThread (const juce::MidiBuffer&, double sampleRate);

private:
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;

    juce::CriticalSection clientLock;
    juce::Array<Client*> clients;
    std::atomic<Client*> active { nullptr };

    mutable juce::SpinLock outLock;
    std::unique_ptr<juce::MidiOutput> out;
    std::unique_ptr<juce::MidiInput> in;
    juce::String inId, outId;
};

/** Parses CC / NRPN streams following the minilogue xd conventions (CC63 carries the low bits). */
class CcParser
{
public:
    struct Event
    {
        enum Type { None, Param, Morph, ContextCC, ProgramChange, NameChar } type = None;
        int param = -1, value = 0, cc = 0;
    };
    Event handle (int cc, int value);
    Event programChange (int program);
    int bankMsb = 0, bankLsb = 0;

private:
    int pendingLsb = -1;
    int nrpnMsb = -1, nrpnLsb = -1;
};
