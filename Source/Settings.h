#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include "core/Params.h"

/** Settings shared by all instances (standalone and every plugin instance), persisted on disk. */
class Settings : public juce::ChangeBroadcaster
{
public:
    Settings();
    ~Settings() override;

    // panel behaviour (names follow the original panel's switches)
    bool bankAutoSave = true;
    bool actionsConfirm = true;
    int saveMode = 2;            // 0 = sysex, 1 = mnlgxdprog, 2 = both
    bool saveOkConfirm = true;
    bool loadConfirm = true;
    bool loadOkConfirm = true;
    bool receiveConfirm = true;
    bool receiveOkConfirm = true;
    bool sendConfirm = true;     // small send buttons
    bool sendOkConfirm = true;
    bool welcome = true;
    int transferMs = 200;
    int zoomPct = 100;
    int nonCcMode = 0;           // 0 visible+enabled, 1 disabled, 2 hidden
    bool autoSendDump = true;    // send a full dump (debounced) after changing a parameter without CC/NRPN
    bool sendOnRecall = true;    // send the stored patch to the synth when a DAW project is opened
    bool sendOnEditorOpen = false;

    // MIDI
    juce::String midiInId, midiOutId;
    int channel = 0;             // 0..15
    int outputRoute = 0;         // 0 device, 1 plugin host, 2 both
    bool hostThru = true;        // forward notes coming from the host to the device output
    bool hostInputAsSynth = true;// parse CC/NRPN/SysEx arriving from the host as synth feedback

    xd::UserNames userNames;
    juce::String lastProgramDir, lastBankDir, lastSysexName, lastLibName;

    void save();
    void changed() { save(); sendChangeMessage(); }

private:
    std::unique_ptr<juce::PropertiesFile> props;
    void load();
};
