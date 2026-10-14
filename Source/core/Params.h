#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include <vector>

namespace xd
{
/** How a parameter is transmitted in real time. */
enum class Tx
{
    None,     // only via full program dump ("light green" parameters)
    CC10,     // 10-bit: CC63 (bits 0-2) then CCnn (bits 3-9)
    CC7,      // plain 7-bit CC
    CCStep,   // discrete value spread over 0..127
    CCBool,   // 0 / 127
    CCCtx,    // context dependent CC (mod FX sub type) - see ParamStore
    NRPN7,    // CC99/98 + CC6
    NRPN10,   // CC99/98 + CC63 (bits 0-2) + CC6 (bits 3-9)
    NRPN8     // CC99/98 + CC63 (bits 0-2) + CC6 (bits 3-7), 0..200
};

enum class Storage { U8, U16, S8 };

enum class Disp
{
    Raw, Labels, PitchCents, EgInt, LfoInt, Bipolar100, Signed, LevelDb,
    Bpm, Gate72, UserParam, VoiceDepth, MicroTuning
};

enum class Section
{
    VCO1, VCO2, Multi, Mixer, Modulation, Filter, AmpEG, EG, LFO, VoiceMode,
    ModFX, Delay, Reverb, Program, Controllers, Sequencer, NumSections
};

constexpr int numSections = (int) Section::NumSections;
juce::String sectionName (Section s);

struct ParamDef
{
    int index = 0;                // position in table == plugin parameter index
    juce::String key;             // stable id (used for VST automation id)
    juce::String name;            // display name
    Section section = Section::Program;
    int offset = 0;
    Storage storage = Storage::U8;
    int minV = 0, maxV = 127, defV = 0;
    Tx tx = Tx::None;
    int number = -1;              // CC number or NRPN LSB address
    int steps = 0;                // for CCStep
    int valueBase = 0;            // stored = index + valueBase for CCStep (mod fx type is 1-based)
    Disp disp = Disp::Raw;
    juce::StringArray labels;
    int dispOffset = 0;           // for Disp::Signed
    juce::String desc;
    int userNamesKind = -1;       // 0 osc, 1 modfx, 2 delay, 3 reverb user names start index in labels
    int userNamesFirst = 0;

    bool isRealtime() const { return tx != Tx::None; }
    bool isContinuous() const { return disp != Disp::Labels && (maxV - minV) > 24; }
    int range() const { return maxV - minV; }
};

class ParamTable
{
public:
    static const ParamTable& get();

    const std::vector<ParamDef>& all() const { return defs; }
    int size() const { return (int) defs.size(); }
    const ParamDef& operator[] (int i) const { return defs[(size_t) i]; }
    int indexOf (const juce::String& key) const;
    const ParamDef* byKey (const juce::String& key) const;
    /** Parameter indices driven by a given incoming CC (excluding context ones). */
    int paramForCC (int cc) const { return cc >= 0 && cc < 128 ? ccMap[(size_t) cc] : -1; }
    int paramForNRPN (int lsb) const { return lsb >= 0 && lsb < 128 ? nrpnMap[(size_t) lsb] : -1; }

private:
    ParamTable();
    std::vector<ParamDef> defs;
    std::array<int, 128> ccMap {};
    std::array<int, 128> nrpnMap {};
};

/** User module names (oscillators, mod fx, delay, reverb) shown instead of USERn. */
struct UserNames
{
    juce::StringArray osc, modfx, delay, reverb;
    UserNames() { reset(); }
    void reset();
    juce::StringArray& kind (int k) { return k == 0 ? osc : k == 1 ? modfx : k == 2 ? delay : reverb; }
    const juce::StringArray& kind (int k) const { return k == 0 ? osc : k == 1 ? modfx : k == 2 ? delay : reverb; }
    juce::String toString() const;
    void fromString (const juce::String&);
};

/** Human readable value. */
juce::String formatValue (const ParamDef& d, int value, const UserNames* names = nullptr,
                          int userParamType = 0);
juce::StringArray labelsFor (const ParamDef& d, const UserNames* names);

} // namespace xd
