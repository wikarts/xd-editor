#include "Librarian.h"

using namespace xd;

namespace lib
{
static juce::String u (const char* s) { return juce::String::fromUTF8 (s); }

juce::StringArray programActionNames()
{
    return { u ("Load source to panel (and XD buffer)"), u ("Load Init program to panel"),
             u ("Load program file into destination"), u ("Init destination"),
             u ("Save source to file"), u ("Save XD buffer to destination"),
             u ("Move source → destination"), u ("Copy source → destination"),
             u ("Copy multiple (as-is)"), u ("Copy multiple (collated)"),
             u ("Swap source ↔ destination"), u ("Full comparison"), u ("Differences only") };
}

juce::StringArray bankActionNames()
{
    return { u ("Load from disk (.syx / .mnlgxdlib)"), u ("Save to disk (.syx)"),
             u ("Save as Korg library (.mnlgxdlib)"), u ("Empty bank (500 x Init)"),
             u ("Receive from Minilogue XD"), u ("Send bank to Minilogue XD"),
             u ("Clear / close bank"), u ("Export program list to text file") };
}

juce::Array<int> multiCopyTargets (const juce::Array<int>& src, int dstSlot, bool collated)
{
    juce::Array<int> out;
    if (src.isEmpty() || dstSlot < 0) return out;
    auto sorted = src;
    sorted.sort();
    for (int i = 0; i < sorted.size(); ++i)
    {
        const int d = collated ? dstSlot + i : dstSlot + (sorted[i] - sorted[0]);
        if (d < numSlots) out.add (d);
    }
    return out;
}

juce::String copy (XDProcessor& p, int sb, const juce::Array<int>& srcSlots, int db, int ds, bool collated)
{
    auto sorted = srcSlots;
    sorted.sort();
    auto targets = multiCopyTargets (sorted, ds, collated);
    // copy via a snapshot so overlapping ranges in the same bank behave
    std::vector<std::pair<Program, AddInfo>> snap;
    for (auto s : sorted) snap.emplace_back (p.bank (sb).progs[(size_t) s], p.bank (sb).info[(size_t) s]);
    auto& dst = p.bank (db);
    dst.highlighted.clear();
    for (int i = 0; i < targets.size(); ++i)
    {
        dst.progs[(size_t) targets[i]] = snap[(size_t) i].first;
        dst.info[(size_t) targets[i]] = snap[(size_t) i].second;
        dst.highlighted.insert (targets[i]);
    }
    p.bankModified ((BankId) db, targets);
    juce::String msg = u ("Copied ") + juce::String (targets.size()) + u (" program(s).");
    if (targets.size() < sorted.size())
        msg << u ("\nSkipped ") << (sorted.size() - targets.size()) << u (" - no room after slot 500.");
    return msg;
}

juce::String move (XDProcessor& p, int sb, int ss, int db, int ds)
{
    if (sb == db && ss == ds) return u ("Source and destination are the same.");
    auto prog = p.bank (sb).progs[(size_t) ss];
    auto info = p.bank (sb).info[(size_t) ss];
    p.bank (db).progs[(size_t) ds] = prog;
    p.bank (db).info[(size_t) ds] = info;
    p.bank (sb).progs[(size_t) ss] = Program();
    p.bank (sb).info[(size_t) ss] = {};
    if (sb == db) p.bankModified ((BankId) db, { ss, ds });
    else { p.bankModified ((BankId) sb, { ss }); p.bankModified ((BankId) db, { ds }); }
    return u ("Moved: ") + prog.getName();
}

juce::String swap (XDProcessor& p, int sb, int ss, int db, int ds)
{
    std::swap (p.bank (sb).progs[(size_t) ss], p.bank (db).progs[(size_t) ds]);
    std::swap (p.bank (sb).info[(size_t) ss], p.bank (db).info[(size_t) ds]);
    if (sb == db) p.bankModified ((BankId) db, { ss, ds });
    else { p.bankModified ((BankId) sb, { ss }); p.bankModified ((BankId) db, { ds }); }
    return u ("Programs swapped.");
}

juce::String initSlot (XDProcessor& p, int b, int s)
{
    p.bank (b).progs[(size_t) s] = Program();
    p.bank (b).info[(size_t) s] = {};
    p.bankModified ((BankId) b, { s });
    return u ("Init program inserted.");
}

juce::String putProgram (XDProcessor& p, int b, int s, const Program& prog, const AddInfo& info)
{
    p.bank (b).progs[(size_t) s] = prog;
    p.bank (b).info[(size_t) s] = info;
    p.bankModified ((BankId) b, { s });
    return u ("Saved: ") + prog.getName();
}
} // namespace lib
