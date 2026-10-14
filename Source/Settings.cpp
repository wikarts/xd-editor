#include "Settings.h"

static juce::PropertiesFile::Options options()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "XD Editor";
    o.filenameSuffix = ".settings";
    o.folderName = "XD Editor";
    o.osxLibrarySubFolder = "Application Support";
    o.storageFormat = juce::PropertiesFile::storeAsXML;
    o.processLock = nullptr;
    return o;
}

Settings::Settings()
{
    props = std::make_unique<juce::PropertiesFile> (options());
    load();
}

Settings::~Settings() { save(); }

void Settings::load()
{
    auto& p = *props;
    bankAutoSave = p.getBoolValue ("bankAutoSave", bankAutoSave);
    actionsConfirm = p.getBoolValue ("actionsConfirm", actionsConfirm);
    saveMode = p.getIntValue ("saveMode", saveMode);
    saveOkConfirm = p.getBoolValue ("saveOkConfirm", saveOkConfirm);
    loadConfirm = p.getBoolValue ("loadConfirm", loadConfirm);
    loadOkConfirm = p.getBoolValue ("loadOkConfirm", loadOkConfirm);
    receiveConfirm = p.getBoolValue ("receiveConfirm", receiveConfirm);
    receiveOkConfirm = p.getBoolValue ("receiveOkConfirm", receiveOkConfirm);
    sendConfirm = p.getBoolValue ("sendConfirm", sendConfirm);
    sendOkConfirm = p.getBoolValue ("sendOkConfirm", sendOkConfirm);
    welcome = p.getBoolValue ("welcome", welcome);
    transferMs = juce::jlimit (20, 1000, p.getIntValue ("transferMs", transferMs));
    zoomPct = juce::jlimit (50, 200, p.getIntValue ("zoomPct", zoomPct));
    nonCcMode = juce::jlimit (0, 2, p.getIntValue ("nonCcMode", nonCcMode));
    autoSendDump = p.getBoolValue ("autoSendDump", autoSendDump);
    sendOnRecall = p.getBoolValue ("sendOnRecall", sendOnRecall);
    sendOnEditorOpen = p.getBoolValue ("sendOnEditorOpen", sendOnEditorOpen);
    midiInId = p.getValue ("midiIn");
    midiOutId = p.getValue ("midiOut");
    channel = juce::jlimit (0, 15, p.getIntValue ("channel", channel));
    outputRoute = juce::jlimit (0, 2, p.getIntValue ("outputRoute", outputRoute));
    hostThru = p.getBoolValue ("hostThru", hostThru);
    hostInputAsSynth = p.getBoolValue ("hostInputAsSynth", hostInputAsSynth);
    userNames.fromString (p.getValue ("userNames"));
    lastProgramDir = p.getValue ("lastProgramDir");
    lastBankDir = p.getValue ("lastBankDir");
    lastSysexName = p.getValue ("lastSysexName");
    lastLibName = p.getValue ("lastLibName");
}

void Settings::save()
{
    auto& p = *props;
    p.setValue ("bankAutoSave", bankAutoSave);
    p.setValue ("actionsConfirm", actionsConfirm);
    p.setValue ("saveMode", saveMode);
    p.setValue ("saveOkConfirm", saveOkConfirm);
    p.setValue ("loadConfirm", loadConfirm);
    p.setValue ("loadOkConfirm", loadOkConfirm);
    p.setValue ("receiveConfirm", receiveConfirm);
    p.setValue ("receiveOkConfirm", receiveOkConfirm);
    p.setValue ("sendConfirm", sendConfirm);
    p.setValue ("sendOkConfirm", sendOkConfirm);
    p.setValue ("welcome", welcome);
    p.setValue ("transferMs", transferMs);
    p.setValue ("zoomPct", zoomPct);
    p.setValue ("nonCcMode", nonCcMode);
    p.setValue ("autoSendDump", autoSendDump);
    p.setValue ("sendOnRecall", sendOnRecall);
    p.setValue ("sendOnEditorOpen", sendOnEditorOpen);
    p.setValue ("midiIn", midiInId);
    p.setValue ("midiOut", midiOutId);
    p.setValue ("channel", channel);
    p.setValue ("outputRoute", outputRoute);
    p.setValue ("hostThru", hostThru);
    p.setValue ("hostInputAsSynth", hostInputAsSynth);
    p.setValue ("userNames", userNames.toString());
    p.setValue ("lastProgramDir", lastProgramDir);
    p.setValue ("lastBankDir", lastBankDir);
    p.setValue ("lastSysexName", lastSysexName);
    p.setValue ("lastLibName", lastLibName);
    p.saveIfNeeded();
}
