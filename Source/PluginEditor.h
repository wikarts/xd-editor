#pragma once
#include "PluginProcessor.h"
#include "gui/Widgets.h"

class MainTab;
class ProgramTab;
class LibrarianTab;
class SequencerTab;
class SettingsTab;

class XDEditor : public juce::AudioProcessorEditor, public XDProcessor::Listener
{
public:
    static constexpr int baseW = 1280, baseH = 820;

    explicit XDEditor (XDProcessor&);
    ~XDEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void xdChanged (int what) override;
    void mouseDown (const juce::MouseEvent&) override { proc.makeActive(); }

    // ---- dialogs (all asynchronous, safe inside plugins) ----
    void confirm (bool ask, const juce::String& title, const juce::String& msg, std::function<void()> then);
    void inform (bool show, const juce::String& title, const juce::String& msg, bool error = false);
    void askText (const juce::String& title, const juce::String& msg, const juce::String& initial, int maxLen,
                  std::function<void (juce::String)> then);
    void chooseFile (bool save, const juce::String& title, const juce::String& patterns, const juce::File& initial,
                     std::function<void (juce::File)> then);

    // ---- main panel operations ----
    void doLoad();
    void doSave();
    void doReceive();
    void doSend (bool mainButton);
    void doRename();
    void doInit();
    void doAuthor();
    void loadProgramFile (const juce::File&, std::function<void (bool)> after = nullptr);
    void setZoom (int pct);
    void showTab (int i);

    XDProcessor& proc;

private:
    XDLook look;
    juce::Component content;
    juce::TooltipWindow tooltips { this, 600 };
    std::unique_ptr<juce::FileChooser> chooser;

    // top area
    juce::TextButton tabButtons[5];
    juce::TextButton lcdOffButton { "LCD OFF" };
    LcdDisplay lcd;
    juce::TextButton directButton { "DIRECT" };
    juce::TextButton sourceButtons[3];
    FreeKnob bankKnob { "Bank", 0, 4, 1 }, progKnob { "Program", 0, 100, 1 };
    juce::TextButton loadButton { "LOAD" }, saveButton { "SAVE" }, receiveButton { "RECEIVE" },
                     sendButton { "SEND" }, renameButton { "RENAME" }, initButton { "INIT" };

    std::unique_ptr<MainTab> mainTab;
    std::unique_ptr<ProgramTab> programTab;
    std::unique_ptr<LibrarianTab> librarianTab;
    std::unique_ptr<SequencerTab> sequencerTab;
    std::unique_ptr<SettingsTab> settingsTab;
    juce::Component* tabs[5] {};

    void refreshSelectors();
    bool updatingSelectors = false;
    bool resizingFromZoom = false;
    void selectorsChanged();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (XDEditor)
};
