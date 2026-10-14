#include "Librarian.h"

using namespace xd;

namespace lib
{
static juce::String u (const char* s) { return juce::String::fromUTF8 (s); }

juce::StringArray programActionNames()
{
    return { u ("Wczytaj źródło do panelu (i bufora XD)"), u ("Wczytaj program Init do panelu"),
             u ("Wczytaj plik programu do celu"), u ("Wstaw Init do celu"),
             u ("Zapisz źródło do pliku"), u ("Zapisz bufor XD do celu"),
             u ("Przenieś źródło → cel"), u ("Kopiuj źródło → cel"),
             u ("Kopiuj wiele (jak jest)"), u ("Kopiuj wiele (ciągiem)"),
             u ("Zamień źródło ↔ cel"), u ("Pełne porównanie"), u ("Tylko różnice") };
}

juce::StringArray bankActionNames()
{
    return { u ("Wczytaj z dysku (.syx / .mnlgxdlib)"), u ("Zapisz na dysk (.syx)"),
             u ("Zapisz jako bibliotekę Korg (.mnlgxdlib)"), u ("Pusty bank (500 x Init)"),
             u ("Odbierz z Minilogue XD"), u ("Wyślij bank do Minilogue XD"),
             u ("Wyczyść / zamknij bank"), u ("Eksportuj listę do pliku tekstowego") };
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
    juce::String msg = u ("Skopiowano ") + juce::String (targets.size()) + u (" program(ów).");
    if (targets.size() < sorted.size())
        msg << u ("\nPominięto ") << (sorted.size() - targets.size()) << u (" - brak miejsca za slotem 500.");
    return msg;
}

juce::String move (XDProcessor& p, int sb, int ss, int db, int ds)
{
    if (sb == db && ss == ds) return u ("Źródło i cel są takie same.");
    auto prog = p.bank (sb).progs[(size_t) ss];
    auto info = p.bank (sb).info[(size_t) ss];
    p.bank (db).progs[(size_t) ds] = prog;
    p.bank (db).info[(size_t) ds] = info;
    p.bank (sb).progs[(size_t) ss] = Program();
    p.bank (sb).info[(size_t) ss] = {};
    if (sb == db) p.bankModified ((BankId) db, { ss, ds });
    else { p.bankModified ((BankId) sb, { ss }); p.bankModified ((BankId) db, { ds }); }
    return u ("Przeniesiono: ") + prog.getName();
}

juce::String swap (XDProcessor& p, int sb, int ss, int db, int ds)
{
    std::swap (p.bank (sb).progs[(size_t) ss], p.bank (db).progs[(size_t) ds]);
    std::swap (p.bank (sb).info[(size_t) ss], p.bank (db).info[(size_t) ds]);
    if (sb == db) p.bankModified ((BankId) db, { ss, ds });
    else { p.bankModified ((BankId) sb, { ss }); p.bankModified ((BankId) db, { ds }); }
    return u ("Zamieniono programy.");
}

juce::String initSlot (XDProcessor& p, int b, int s)
{
    p.bank (b).progs[(size_t) s] = Program();
    p.bank (b).info[(size_t) s] = {};
    p.bankModified ((BankId) b, { s });
    return u ("Wstawiono program Init.");
}

juce::String putProgram (XDProcessor& p, int b, int s, const Program& prog, const AddInfo& info)
{
    p.bank (b).progs[(size_t) s] = prog;
    p.bank (b).info[(size_t) s] = info;
    p.bankModified ((BankId) b, { s });
    return u ("Zapisano: ") + prog.getName();
}
} // namespace lib
