#pragma once
#include "PluginProcessor.h"

namespace lib
{
enum class ProgramAction
{
    LoadToPanel, InitToPanel, LoadFileToDest, InitDest, SaveSrcToFile, BufferToDest,
    Move, Copy, CopyMultiAsIs, CopyMultiCollated, Swap, CompareFull, CompareDiff
};
juce::StringArray programActionNames();

enum class BankAction { LoadFromDisk, SaveToDisk, SaveAsKorgLib, InitBank, ReceiveFromSynth, SendToSynth, Clear, ExportList };
juce::StringArray bankActionNames();

/** Slots that a multi copy would overwrite (for the preview highlighting). */
juce::Array<int> multiCopyTargets (const juce::Array<int>& src, int dstSlot, bool collated);

/** Pure bank operations; they call proc.bankModified() so files and the synth are updated. */
juce::String copy (XDProcessor&, int srcBank, const juce::Array<int>& srcSlots, int dstBank, int dstSlot, bool collated);
juce::String move (XDProcessor&, int srcBank, int srcSlot, int dstBank, int dstSlot);
juce::String swap (XDProcessor&, int srcBank, int srcSlot, int dstBank, int dstSlot);
juce::String initSlot (XDProcessor&, int bank, int slot);
juce::String putProgram (XDProcessor&, int bank, int slot, const xd::Program&, const xd::AddInfo&);
} // namespace lib
