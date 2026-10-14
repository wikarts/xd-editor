#pragma once
#include "../PluginEditor.h"
#include "../Librarian.h"

// ------------------------------------------------------------------------- Main parameters
class MainTab : public juce::Component, public XDProcessor::Listener
{
public:
    explicit MainTab (XDEditor&);
    ~MainTab() override;
    void resized() override;
    void xdChanged (int what) override;

private:
    XDProcessor& proc;
    juce::OwnedArray<PanelBox> sections;
    juce::OwnedArray<ParamControl> controls;
    juce::OwnedArray<juce::TextButton> voiceButtons;
    std::unique_ptr<EnvelopeGraph> ampGraph, egGraph;
    std::map<juce::String, ParamControl*> byKey;
    PanelBox* vpmSection = nullptr;

    template <typename T> T* add (const juce::String& key, const juce::String& title = {});
    PanelBox* section (const juce::String& name, juce::Rectangle<int> r);
    void place (const juce::String& key, juce::Rectangle<int> r);
    void updateContext();
};

// ------------------------------------------------------------------------- Program & Global
class ProgramTab : public juce::Component, public XDProcessor::Listener
{
public:
    explicit ProgramTab (XDEditor&);
    ~ProgramTab() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    void xdChanged (int what) override;

private:
    XDEditor& ed;
    XDProcessor& proc;
    juce::OwnedArray<ParamControl> rows;
    juce::OwnedArray<PanelBox> sections;
    juce::TextButton send1 { "Send" }, send2 { "Send" };
    juce::TextEditor description;
    juce::TextButton authorButton { "Author" };
    juce::ComboBox category;
    juce::Label dateLabel;
    // globals
    juce::TextButton requestGlobals { "Request" }, sendGlobals { "Send" };
    juce::OwnedArray<juce::ComboBox> globalBoxes;
    juce::OwnedArray<juce::Label> globalLabels;
    void refreshInfo();
    void refreshGlobals();
};

// ------------------------------------------------------------------------- Librarian & morph
class BankList;
class LibrarianTab : public juce::Component, public XDProcessor::Listener
{
public:
    explicit LibrarianTab (XDEditor&);
    ~LibrarianTab() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    void xdChanged (int what) override;

    void selectSource (int bank, const juce::Array<int>& slots);
    void selectDestination (int bank, int slot);
    juce::Array<int> previewTargets (int bank) const;
    int selectedBank = 0;
    void bankHeaderClicked (int bank);

private:
    XDEditor& ed;
    XDProcessor& proc;
    juce::OwnedArray<BankList> lists;
    juce::TextButton headers[3];
    juce::ComboBox bankAction, programAction;
    juce::TextButton bankProceed { "Proceed" }, programProceed { "Proceed" }, cancelTransfer { "Stop" };
    juce::Label selectionInfo;
    // morph
    FreeKnob morphKnob { "Morph", 0, 100, 1 };
    juce::OwnedArray<juce::TextButton> lockButtons;
    juce::TextButton lockAll { "Lock all" }, unlockAll { "Unlock all" }, morph0 { "Morph 0%" }, morph100 { "Morph 100%" };
    juce::Label morphInfo;
    bool updatingMorph = false;

    void runBankAction();
    void runProgramAction();
    void refreshAll();
    bool needsSource (lib::ProgramAction) const;
    bool needsDest (lib::ProgramAction) const;
};

// ------------------------------------------------------------------------- Sequencer (read only)
class SequencerTab : public juce::Component, public XDProcessor::Listener
{
public:
    explicit SequencerTab (XDProcessor&);
    ~SequencerTab() override;
    void paint (juce::Graphics&) override;
    void xdChanged (int what) override { if (what & (XDProcessor::cProgram | XDProcessor::cParam)) repaint(); }

private:
    XDProcessor& proc;
};

// ------------------------------------------------------------------------- Settings & info
class SettingsTab : public juce::Component, public XDProcessor::Listener
{
public:
    explicit SettingsTab (XDEditor&);
    ~SettingsTab() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    void xdChanged (int what) override;

private:
    XDEditor& ed;
    XDProcessor& proc;
    juce::OwnedArray<juce::ToggleButton> toggles;
    juce::ComboBox saveMode, nonCcMode, midiIn, midiOut, channel, route;
    juce::Slider transfer;
    juce::TextButton zoomIn { "+" }, zoomOut { "-" }, inquiry { "Device Inquiry" };
    juce::TextButton loadNames { "Load USER names" }, resetNames { "Reset USER names" }, refreshDevices { "Refresh" };
    juce::Label zoomLabel, statusLabel;
    juce::TextEditor about;
    juce::ComboBox reportKind;
    juce::TextButton display { "Display" }, exportButton { "Export" };
    juce::TextEditor report;
    juce::StringArray inIds, outIds;
    void refresh();
    void fillDevices();
};
