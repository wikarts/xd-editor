#include "Reports.h"

namespace xd
{
juce::StringArray reportKindNames()
{
    return { juce::String::fromUTF8 ("Bieżące wartości"), juce::String::fromUTF8 ("Zapisane wartości"),
             juce::String::fromUTF8 ("Różnice względem Init"), juce::String::fromUTF8 ("Pełne porównanie (bieżące / zapisane)"),
             juce::String::fromUTF8 ("Tylko różnice (bieżące / zapisane)"),
             juce::String::fromUTF8 ("Pełne porównanie (biblioteka: źródło / cel)"),
             juce::String::fromUTF8 ("Różnice (biblioteka: źródło / cel)") };
}

juce::String buildReport (const Program& a, const AddInfo& ia, const juce::String& titleA,
                          const Program* b, const AddInfo* ib, const juce::String& titleB,
                          bool onlyDiff, const UserNames& names)
{
    juce::String s;
    auto header = [&] (const Program& p, const AddInfo& in, const juce::String& t)
    {
        s << t << ": " << p.getName();
        if (in.category.isNotEmpty()) s << "  [" << in.category << "]";
        if (in.author.isNotEmpty()) s << "  by " << in.author;
        if (in.date.isNotEmpty()) s << "  (" << in.date.substring (0, 10) << ")";
        s << "\n";
        if (in.description.isNotEmpty()) s << "   " << in.description << "\n";
    };
    header (a, ia, titleA);
    if (b != nullptr) header (*b, ib != nullptr ? *ib : AddInfo(), titleB);
    s << "\n";

    const int w1 = 26, w2 = 26;
    s << juce::String ("Parameter").paddedRight (' ', w1) << titleA.paddedRight (' ', w2);
    if (b != nullptr) s << titleB;
    s << "\n" << juce::String::repeatedString ("-", w1 + w2 + (b ? 26 : 0)) << "\n";

    Section last = Section::NumSections;
    int count = 0;
    for (auto& d : ParamTable::get().all())
    {
        const int va = a.get (d);
        const int vb = b != nullptr ? b->get (d) : va;
        if (onlyDiff && va == vb) continue;
        if (d.section != last)
        {
            s << "[" << sectionName (d.section) << "]\n";
            last = d.section;
        }
        s << ("  " + d.name).paddedRight (' ', w1)
          << formatInContext (d, a, &names).paddedRight (' ', w2);
        if (b != nullptr) s << formatInContext (d, *b, &names) << (va != vb ? "   *" : "");
        s << "\n";
        ++count;
    }
    if (b != nullptr && a.getName() != b->getName())
        s << juce::String ("  Name").paddedRight (' ', w1) << a.getName().paddedRight (' ', w2) << b->getName() << "   *\n";
    if (onlyDiff && count == 0) s << juce::String::fromUTF8 ("(brak różnic w parametrach)\n");
    return s;
}

Program morph (const Program& from, const Program& to, double t, const std::array<bool, numSections>& locked)
{
    Program out = from;
    t = juce::jlimit (0.0, 1.0, t);
    for (auto& d : ParamTable::get().all())
    {
        if (locked[(size_t) d.section] || d.section == Section::Sequencer) continue;
        const int a = from.get (d), b = to.get (d);
        if (a == b) continue;
        if (d.isContinuous()) out.set (d, juce::roundToInt (a + (b - a) * t));
        else out.set (d, t < 0.5 ? a : b);
    }
    return out;
}

const std::vector<GlobalField>& globalFields()
{
    static std::vector<GlobalField> f = {
        { "Master Tune", 4, 0, 100, {}, 50 },
        { "Transpose", 5, 0, 24, {}, 12 },
        { "Metronome", 6, 0, 1, { "Off", "On" }, 0 },
        { "Damper Polarity", 7, 0, 1, { "(-)", "(+)" }, 0 },
        { "Local SW", 8, 0, 1, { "Off", "On" }, 0 },
        { "Velocity Curve", 9, 0, 8, { "Type 1", "Type 2", "Type 3", "Type 4", "Type 5", "Type 6", "Type 7", "Type 8", "Const 127" }, 0 },
        { "Knob Mode", 10, 0, 2, { "Jump", "Catch", "Scale" }, 0 },
        { "Sync In Unit", 11, 0, 1, { "1/16", "1/8" }, 0 },
        { "Sync Out Unit", 12, 0, 1, { "1/16", "1/8" }, 0 },
        { "Sync In Polarity", 13, 0, 1, { "Rise", "Fall" }, 0 },
        { "Sync Out Polarity", 14, 0, 1, { "Rise", "Fall" }, 0 },
        { "MIDI Route", 15, 0, 1, { "USB + MIDI", "USB Only" }, 0 },
        { "MIDI Channel", 16, 0, 15, {}, -1 },
        { "Clock Source", 17, 0, 2, { "Auto (USB)", "Auto (MIDI)", "Internal" }, 0 },
        { "En Rx Transport", 18, 0, 1, { "Off", "On" }, 0 },
        { "MIDI Rx Prog Chg", 19, 0, 1, { "Off", "On" }, 0 },
        { "MIDI Rx CC", 20, 0, 1, { "Off", "On" }, 0 },
        { "MIDI Rx Pitch Bend", 21, 0, 1, { "Off", "On" }, 0 },
        { "MIDI Tx Prog Chg", 22, 0, 1, { "Off", "On" }, 0 },
        { "MIDI Tx CC", 23, 0, 1, { "Off", "On" }, 0 },
        { "MIDI Tx Pitch Bend", 24, 0, 1, { "Off", "On" }, 0 },
        { "Parameter Display", 25, 0, 2, { "Normal", "All", "Normal" }, 0 },
        { "Brightness", 26, 0, 9, {}, -1 },
        { "Auto Power Off", 27, 0, 1, { "Off", "On" }, 0 },
        { "Poly Chain", 60, 0, 2, { "Off", "Master", "Slave" }, 0 },
        { "Oscilloscope", 61, 0, 2, { "On", "Off", "On" }, 0 },
        { "Shift Function", 62, 0, 1, { "Favorite", "Active Step" }, 0 },
    };
    return f;
}

juce::String formatGlobal (const GlobalField& g, int v)
{
    if (! g.labels.isEmpty()) return juce::isPositiveAndBelow (v, g.labels.size()) ? g.labels[v] : juce::String (v);
    if (g.dispOffset == -1) return juce::String (v + 1);
    if (g.dispOffset > 0) { auto p = v - g.dispOffset; return (p > 0 ? "+" : "") + juce::String (p); }
    return juce::String (v);
}

} // namespace xd
