#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "core/Bank.h"
#include "core/Reports.h"
#include "core/SysEx.h"
#include "MidiEngine.h"
#include "Settings.h"

class XDProcessor : public juce::AudioProcessor,
                    private juce::AudioProcessorParameter::Listener,
                    private juce::AsyncUpdater,
                    private juce::Timer,
                    private juce::ChangeListener,
                    private SharedMidi::Client
{
public:
    XDProcessor();
    ~XDProcessor() override;

    // ================= AudioProcessor =================
    void prepareToPlay (double sampleRate, int) override { currentSampleRate = sampleRate; }
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "XD Editor"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return current.getName(); }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ================= model access =================
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void xdChanged (int what) = 0;
    };
    enum Change { cProgram = 1, cParam = 2, cBanks = 4, cLcd = 8, cStatus = 16, cGlobals = 32, cSelection = 64, cMorph = 128, cInfo = 256, cReport = 512 };
    void addXDListener (Listener* l) { listeners.add (l); }
    void removeXDListener (Listener* l) { listeners.remove (l); }

    Settings& getSettings() { return *settings; }
    SharedMidi& getMidi() { return *midi; }
    void applyMidiSettings();
    void makeActive();

    const xd::Program& getCurrent() const { return current; }
    const xd::Program& getSaved() const { return saved; }
    xd::AddInfo& currentInfo() { return curInfo; }
    int getValue (int pi) const { return current.get (pi); }
    void setValueFromUI (int pi, int value);
    void beginGesture (int pi);
    void endGesture (int pi);
    void showParamInfo (int pi);

    /** Replaces the whole program (load, init, receive...). */
    void loadProgram (const xd::Program&, const xd::AddInfo&, bool sendToSynth, bool markSaved = true);
    void renameCurrent (const juce::String&);
    void infoEdited() { notify (cInfo); }
    void markCurrentSaved() { saved = current; savedInfo = curInfo; notify (cProgram | cLcd | cInfo); }
    void uiNotify (int what) { notify (what); }
    bool sendCurrentToSynth();
    bool canTalkToSynth() const;

    // ---- synth communication ----
    using Done = std::function<void (bool ok, juce::String message)>;
    void deviceInquiry();
    bool isConnected() const { return midi->connected; }
    juce::String getOsVersion() const { return midi->osVersion; }
    void requestCurrentProgram (Done);
    void requestSlotToPanel (int slot, Done);
    void requestBufferToBank (xd::BankId, int slot, Done);
    void requestGlobals (Done);
    bool sendGlobals();
    void loadUserNames (Done);
    void resetUserNames();

    // ---- globals ----
    bool hasGlobals() const { return ! globals.empty(); }
    int getGlobal (const xd::GlobalField&) const;
    void setGlobal (const xd::GlobalField&, int);

    // ---- banks / librarian ----
    xd::Bank& bank (xd::BankId b) { return banks[(size_t) b]; }
    xd::Bank& bank (int b) { return banks[(size_t) b]; }
    xd::IoResult loadBankFile (xd::BankId, const juce::File&);
    xd::IoResult saveBankFile (xd::BankId, const juce::File&);
    void bankModified (xd::BankId, const juce::Array<int>& slots, bool sendToSynth = true);
    void initBank (xd::BankId);
    void clearBank (xd::BankId);
    void startReceiveBank (Done);
    void startSendBank (xd::BankId, Done);
    void cancelTransfer();
    bool isTransferring() const { return job.type != Job::None; }

    // main panel selector
    int sourceBank = 0;   // BankId
    int slotSel = 0;      // 0 = file / buffer, 1..500 = slot
    bool directMode = false;
    bool lcdOff = false;
    void setSlotSel (int s);
    void setDirectMode (bool);

    // librarian selection
    int libSrcBank = -1, libDstBank = -1, libDstSlot = -1;
    juce::Array<int> libSrcSlots;
    int libSrcFirst() const { return libSrcSlots.isEmpty() ? -1 : libSrcSlots.getFirst(); }

    // ---- morph ----
    std::array<bool, xd::numSections> morphLocks {};
    double getMorph() const { return morphAmount; }
    void setMorph (double amount01, bool fromHost = false, bool ignoreLocks = false);
    bool hasMorphTarget() const;
    xd::Program morphTarget() const;
    void morphTargetChanged();

    // ---- LCD ----
    struct Lcd
    {
        int param = -1;
        juce::String message;
        juce::uint32 messageUntil = 0;
        bool progress = false;
    };
    const Lcd& getLcd() const { return lcd; }
    void lcdMessage (const juce::String&, int ms = 3000);

    juce::String buildInfoReport (xd::ReportKind) const;
    juce::String lastReport;
    int uiTab = 0;
    bool welcomeShown = false;

private:
    // parameters
    std::vector<juce::AudioParameterInt*> params;
    juce::AudioParameterFloat* morphParam = nullptr;
    std::unique_ptr<std::atomic<bool>[]> dirty;
    std::atomic<bool> morphDirty { false };
    bool updatingHost = false;
    void parameterValueChanged (int index, float) override;
    void parameterGestureChanged (int, bool) override {}
    void handleAsyncUpdate() override;
    void pushToHost (int pi);
    void pushAllToHost();

    // sending
    void sendParam (int pi);
    void sendCC (int cc, int v);
    void sendNRPN (int lsb, int value, int mode /*7,10,8*/);
    void send (const juce::MidiMessage&);
    void sendSysex (const std::vector<uint8_t>&);
    void requestDump() { dumpPendingAt = juce::Time::getMillisecondCounter() + 250; }
    juce::uint32 dumpPendingAt = 0;

    // receiving
    void deviceMidiReceived (const juce::MidiMessage&) override;
    void queueInbound (const juce::MidiMessage&);
    void handleInbound (const juce::MidiMessage&);
    void handleSysex (const juce::MidiMessage&);
    void applyIncomingParam (int pi, int value);
    CcParser parser;
    juce::CriticalSection inboundLock;
    std::vector<juce::MidiMessage> inbound;
    juce::SpinLock hostOutLock;
    juce::MidiBuffer hostOut;

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void notify (int what);

    // pending single requests
    struct Pending
    {
        enum Type { None, CurrentToPanel, SlotToPanel, BufferToBank, Globals, Inquiry } type = None;
        int bank = 0, slot = 0;
        juce::uint32 deadline = 0;
        Done done;
    } pending;
    void startPending (Pending::Type, int bank, int slot, Done, int timeoutMs = 2500);
    void finishPending (bool ok, const juce::String& msg);

    struct Job
    {
        enum Type { None, ReceiveBank, SendSlots, UserNames } type = None;
        int bank = 2;
        std::vector<int> slots;      // for SendSlots / ReceiveBank: slot numbers; for UserNames: module*100+slot
        size_t pos = 0;
        bool waiting = false;
        int retries = 0, errors = 0;
        juce::uint32 nextAt = 0, deadline = 0;
        Done done;
        std::vector<xd::Program> received;
        std::vector<bool> receivedOk;
    } job;
    void jobTick (juce::uint32 now);
    void finishJob (bool ok, const juce::String& msg);

    juce::SharedResourcePointer<Settings> settings;
    juce::SharedResourcePointer<SharedMidi> midi;
    juce::ListenerList<Listener> listeners;

    xd::Program current, saved;
    xd::AddInfo curInfo, savedInfo;
    std::array<xd::Bank, xd::numBanks> banks;
    std::vector<uint8_t> globals;
    double morphAmount = 0.0;
    xd::Program morphFrom;
    bool morphFromValid = false;

    int seenMidiState = -1;
    bool recallSendPending = false;
    juce::uint32 recallSendAt = 0;
    Lcd lcd;
    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (XDProcessor)
};
