#pragma once
#include "Params.h"
#include <array>

namespace xd
{
constexpr int programSize = 1024;
constexpr int numSlots = 500;

/** Extra information that is not part of the synth data (kept by the librarian). */
struct AddInfo
{
    juce::String author, description, category;
    juce::String date; // ISO 8601
    bool isEmpty() const { return author.isEmpty() && description.isEmpty() && category.isEmpty() && date.isEmpty(); }
    juce::ValueTree toTree() const;
    static AddInfo fromTree (const juce::ValueTree&);
    bool operator== (const AddInfo& o) const { return author == o.author && description == o.description && category == o.category && date == o.date; }
};

juce::StringArray categories();

/** One Minilogue XD program: the raw 1024-byte block from the MIDI implementation (TABLE 2). */
class Program
{
public:
    Program() { initialise(); }
    static Program init() { return {}; }

    void initialise();
    bool isInitProgram() const;

    int get (const ParamDef& d) const;
    void set (const ParamDef& d, int value);
    int get (int paramIndex) const { return get (ParamTable::get()[paramIndex]); }
    void set (int paramIndex, int value) { set (ParamTable::get()[paramIndex], value); }

    juce::String getName() const;
    void setName (const juce::String&);

    int userParamType (int p /*0..5*/) const;

    const uint8_t* data() const { return bytes.data(); }
    uint8_t* data() { return bytes.data(); }
    bool loadFrom (const uint8_t* src, size_t n);
    bool isValid() const; // 'PROG' header

    bool operator== (const Program& o) const { return bytes == o.bytes; }
    bool operator!= (const Program& o) const { return bytes != o.bytes; }

    juce::String toBase64() const;
    bool fromBase64 (const juce::String&);

    // ---- sequencer read access ----
    struct Step
    {
        int notes[8] {}, velocities[8] {}, gates[8] {};
        bool triggers[8] {};
        int motion[4][5] {};
    };
    Step step (int i) const;
    bool stepOn (int i) const;
    bool stepActive (int i) const;
    bool stepMotionOn (int i) const;
    int motionParam (int slot) const;
    bool motionSlotOn (int slot) const;
    bool motionSlotSmooth (int slot) const;
    bool motionStepOn (int slot, int step) const;

private:
    std::array<uint8_t, programSize> bytes {};
};

juce::String motionParamName (int id);

/** Value text that depends on other parameters of the program (user param types, voice mode depth
    meaning per voice mode, LFO rate as a note value in BPM mode). */
juce::String formatInContext (const ParamDef&, const Program&, const UserNames* names, int valueOverride = -100000);

} // namespace xd
