#include "Program.h"

namespace xd
{
juce::StringArray categories()
{
    return juce::StringArray { "", "Bass", "Lead", "Pad", "Keys", "Pluck", "Brass", "Strings", "Bell", "FX",
                               "Seq/Arp", "Drum/Perc", "Poly", "Sync", "Other" };
}

juce::ValueTree AddInfo::toTree() const
{
    juce::ValueTree t ("AddInfo");
    t.setProperty ("author", author, nullptr);
    t.setProperty ("description", description, nullptr);
    t.setProperty ("category", category, nullptr);
    t.setProperty ("date", date, nullptr);
    return t;
}

AddInfo AddInfo::fromTree (const juce::ValueTree& t)
{
    AddInfo a;
    a.author = t.getProperty ("author").toString();
    a.description = t.getProperty ("description").toString();
    a.category = t.getProperty ("category").toString();
    a.date = t.getProperty ("date").toString();
    return a;
}

void Program::initialise()
{
    bytes.fill (0);
    std::memcpy (bytes.data(), "PROG", 4);
    setName ("Init Program");
    for (auto& d : ParamTable::get().all())
        set (d, d.defV);
    std::memcpy (bytes.data() + 156, "PRED", 4);
    std::memcpy (bytes.data() + 160, "SQ", 2);
    bytes[162] = bytes[163] = 0xFF;   // all steps active
    for (int s = 0; s < 16; ++s)
    {
        auto* e = bytes.data() + 190 + s * 52;
        for (int n = 0; n < 8; ++n) e[n] = 60;
        for (int n = 0; n < 8; ++n) e[16 + n] = 54;
    }
}

bool Program::isInitProgram() const
{
    return getName().removeCharacters (" ").equalsIgnoreCase ("InitProgram");
}

int Program::get (const ParamDef& d) const
{
    const auto o = (size_t) d.offset;
    switch (d.storage)
    {
        case Storage::U16: return (int) bytes[o] | ((int) bytes[o + 1] << 8);
        case Storage::S8: return (int) (int8_t) bytes[o];
        case Storage::U8:
        default: return bytes[o];
    }
}

void Program::set (const ParamDef& d, int v)
{
    v = juce::jlimit (d.minV, d.maxV, v);
    const auto o = (size_t) d.offset;
    switch (d.storage)
    {
        case Storage::U16: bytes[o] = (uint8_t) (v & 0xFF); bytes[o + 1] = (uint8_t) ((v >> 8) & 0xFF); break;
        case Storage::S8: bytes[o] = (uint8_t) (int8_t) v; break;
        case Storage::U8:
        default: bytes[o] = (uint8_t) v; break;
    }
}

juce::String Program::getName() const
{
    char buf[13] {};
    for (int i = 0; i < 12; ++i)
    {
        auto c = (char) bytes[(size_t) (4 + i)];
        buf[i] = (c >= 32 && c < 127) ? c : (c == 0 ? 0 : '?');
        if (c == 0) break;
    }
    return juce::String (buf).trimEnd();
}

void Program::setName (const juce::String& name)
{
    auto s = name.substring (0, 12);
    for (int i = 0; i < 12; ++i)
    {
        auto c = i < s.length() ? (juce::juce_wchar) s[i] : 0;
        if (c != 0 && (c < 32 || c > 126)) c = '?';
        bytes[(size_t) (4 + i)] = (uint8_t) c;
    }
}

int Program::userParamType (int p) const
{
    if (p < 4) return (bytes[149] >> (p * 2)) & 3;
    return (bytes[148] >> ((p - 4) * 2)) & 3;
}

bool Program::loadFrom (const uint8_t* src, size_t n)
{
    if (n < (size_t) programSize) return false;
    std::memcpy (bytes.data(), src, programSize);
    return isValid();
}

bool Program::isValid() const { return std::memcmp (bytes.data(), "PROG", 4) == 0; }

juce::String Program::toBase64() const
{
    juce::MemoryBlock mb (bytes.data(), bytes.size());
    return mb.toBase64Encoding();
}

bool Program::fromBase64 (const juce::String& s)
{
    juce::MemoryBlock mb;
    if (! mb.fromBase64Encoding (s) || mb.getSize() < (size_t) programSize) return false;
    Program p;
    if (! p.loadFrom ((const uint8_t*) mb.getData(), mb.getSize())) return false;
    *this = p;
    return true;
}

Program::Step Program::step (int i) const
{
    Step s;
    const auto* e = bytes.data() + 190 + i * 52;
    for (int n = 0; n < 8; ++n)
    {
        s.notes[n] = e[n] & 0x7F;
        s.velocities[n] = e[8 + n] & 0x7F;
        s.gates[n] = e[16 + n] & 0x7F;
        s.triggers[n] = (e[16 + n] & 0x80) != 0;
    }
    for (int m = 0; m < 4; ++m)
    {
        const auto* ms = e + 24 + m * 7;
        for (int k = 0; k < 5; ++k)
        {
            const int lo = k < 4 ? (ms[5] >> (k * 2)) & 3 : ms[6] & 3;
            s.motion[m][k] = (ms[k] << 2) | lo;
        }
    }
    return s;
}

static bool bit16 (const uint8_t* b, int i) { return (b[i / 8] >> (i % 8)) & 1; }

bool Program::stepOn (int i) const { return bit16 (bytes.data() + 170, i); }
bool Program::stepActive (int i) const { return bit16 (bytes.data() + 162, i); }
bool Program::stepMotionOn (int i) const { return bit16 (bytes.data() + 172, i); }
int Program::motionParam (int slot) const { return bytes[(size_t) (175 + slot * 2)]; }
bool Program::motionSlotOn (int slot) const { return (bytes[(size_t) (174 + slot * 2)] & 1) != 0; }
bool Program::motionSlotSmooth (int slot) const { return (bytes[(size_t) (174 + slot * 2)] & 2) != 0; }
bool Program::motionStepOn (int slot, int step) const { return bit16 (bytes.data() + 182 + slot * 2, step); }

juce::String motionParamName (int id)
{
    switch (id) // MIDI Implementation note S2-1
    {
        case 0: return "None";
        case 15: return "Portamento";
        case 16: return "Voice Mode Depth";
        case 17: return "Voice Mode Type";
        case 18: return "VCO1 Wave";
        case 19: return "VCO1 Octave";
        case 20: return "VCO1 Pitch";
        case 21: return "VCO1 Shape";
        case 22: return "VCO2 Wave";
        case 23: return "VCO2 Octave";
        case 24: return "VCO2 Pitch";
        case 25: return "VCO2 Shape";
        case 26: return "Sync";
        case 27: return "Ring";
        case 28: return "Cross Mod";
        case 29: return "Multi Type";
        case 30: return "Noise Type";
        case 31: return "VPM Type";
        case 33: return "Noise Shape";
        case 34: return "VPM Shape";
        case 35: return "User Shape";
        case 36: return "Noise Shift Shape";
        case 37: return "VPM Shift Shape";
        case 38: return "User Shift Shape";
        case 39: return "VCO1 Level";
        case 40: return "VCO2 Level";
        case 41: return "Multi Level";
        case 42: return "Cutoff";
        case 43: return "Resonance";
        case 45: return "Key Track";
        case 46: return "Amp Attack";
        case 47: return "Amp Decay";
        case 48: return "Amp Sustain";
        case 49: return "Amp Release";
        case 50: return "EG Attack";
        case 51: return "EG Decay";
        case 52: return "EG Int";
        case 53: return "EG Target";
        case 54: return "LFO Wave";
        case 55: return "LFO Mode";
        case 56: return "LFO Rate";
        case 57: return "LFO Int";
        case 58: return "LFO Target";
        case 59: return "Mod FX On/Off";
        case 66: return "Mod FX Time";
        case 67: return "Mod FX Depth";
        case 68: return "Delay On/Off";
        case 70: return "Delay Time";
        case 71: return "Delay Depth";
        case 72: return "Reverb On/Off";
        case 74: return "Reverb Time";
        case 75: return "Reverb Depth";
        case 126: return "Pitch Bend";
        case 129: return "Gate Time";
        default: return "Param #" + juce::String (id);
    }
}

juce::String formatInContext (const ParamDef& d, const Program& p, const UserNames* names, int valueOverride)
{
    const int v = valueOverride != -100000 ? valueOverride : p.get (d);
    auto& t = ParamTable::get();
    if (d.disp == Disp::UserParam)
        return formatValue (d, v, names, p.userParamType (d.offset - 142));
    if (d.disp == Disp::VoiceDepth)
    {
        const int mode = p.get (t.indexOf ("voice_mode"));
        switch (mode) // note P2
        {
            case 4: return v < 256 ? juce::String ("Poly") : "Duo " + juce::String (v - 256);
            case 3: return juce::String (v * 50.0 / 1023.0, 1) + " cent";
            case 2:
            {
                static const char* c[] = { "5th", "sus2", "m", "Maj", "sus4", "m7", "7", "7sus4", "Maj7", "aug", "dim", "m7b5", "mMaj7", "Maj7b5" };
                static const int lim[] = { 73, 146, 219, 292, 365, 438, 511, 585, 658, 731, 804, 877, 950, 1023 };
                for (int i = 0; i < 14; ++i) if (v <= lim[i]) return c[i];
                return c[13];
            }
            case 1:
            {
                static const char* a[] = { "Manual 1", "Manual 2", "Rise 1", "Rise 2", "Fall 1", "Fall 2", "Rise Fall 1",
                                           "Rise Fall 2", "Poly 1", "Poly 2", "Random 1", "Random 2", "Random 3" };
                return a[juce::jlimit (0, 12, v / 78)];
            }
            default: return juce::String (v);
        }
    }
    if (d.key == "lfo_rate" && p.get (t.indexOf ("lfo_mode")) == 2) // note P11, BPM sync
    {
        static const char* n[] = { "4", "2", "1", "3/4", "1/2", "3/8", "1/3", "1/4", "3/16", "1/6", "1/8", "1/12", "1/16", "1/24", "1/32", "1/36" };
        return n[juce::jlimit (0, 15, v / 64)];
    }
    return formatValue (d, v, names);
}

} // namespace xd
