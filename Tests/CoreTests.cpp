#include "core/Codec.h"
#include "core/Bank.h"
#include "core/SysEx.h"
#include "core/Reports.h"
#include <iostream>

using namespace xd;

static int failures = 0;
#define CHECK(cond) do { if (! (cond)) { ++failures; std::cerr << "FAIL " << __LINE__ << ": " #cond "\n"; } } while (0)

static int inspect (const char* path)
{
    Bank b;
    auto r = files::loadBank (juce::File (juce::String::fromUTF8 (path)), b, 0);
    std::cout << (r.ok ? "OK: " : "FAIL: ") << r.message << "\n";
    if (! r.ok) return 1;
    int valid = 0, init = 0, withInfo = 0;
    for (int i = 0; i < numSlots; ++i)
    {
        auto& p = b.progs[(size_t) i];
        if (p.isValid()) ++valid;
        if (p.isInitProgram()) ++init;
        if (! b.info[(size_t) i].isEmpty()) ++withInfo;
    }
    std::cout << "valid=" << valid << " init=" << init << " withInfo=" << withInfo << "\n";
    auto& t = ParamTable::get();
    for (int i = 0; i < numSlots; ++i)
    {
        auto& p = b.progs[(size_t) i];
        if (p.isInitProgram() || i > 60) continue;
        std::cout << juce::String (i + 1).paddedLeft ('0', 3) << " " << p.getName().paddedRight (' ', 12)
                  << " mode=" << formatValue (*t.byKey ("voice_mode"), p.get (*t.byKey ("voice_mode")))
                  << " depth=" << formatInContext (*t.byKey ("voice_depth"), p, nullptr)
                  << " multi=" << formatValue (*t.byKey ("multi_type"), p.get (*t.byKey ("multi_type")))
                  << " fx=" << formatValue (*t.byKey ("modfx_type"), p.get (*t.byKey ("modfx_type")))
                  << " arp=" << p.get (*t.byKey ("arp_rate"))
                  << " mt=" << p.get (*t.byKey ("micro_tuning"))
                  << " lvl=" << p.get (*t.byKey ("program_level"))
                  << " seq=" << (std::memcmp (p.data() + 156, "PRED", 4) == 0 ? "PRED" : "????")
                  << " m1=" << motionParamName (p.motionParam (0)) << "\n";
    }
    // value range audit: count parameters outside the documented range
    int outOfRange = 0;
    for (int i = 0; i < numSlots; ++i)
        for (auto& d : t.all())
        {
            const auto& raw = b.progs[(size_t) i];
            int v = d.storage == Storage::U16 ? (raw.data()[d.offset] | (raw.data()[d.offset + 1] << 8))
                  : d.storage == Storage::S8 ? (int) (int8_t) raw.data()[d.offset] : raw.data()[d.offset];
            if (v < d.minV || v > d.maxV) { if (outOfRange++ < 15) std::cout << "out of range: slot " << i + 1 << " " << d.key << "=" << v << "\n"; }
        }
    std::cout << "outOfRange=" << outOfRange << "\n";
    return 0;
}

int main (int argc, char** argv)
{
    if (argc > 1) return inspect (argv[1]);

    // 7-bit codec
    {
        std::vector<uint8_t> raw (1024);
        for (size_t i = 0; i < raw.size(); ++i) raw[i] = (uint8_t) (i * 37 + 11);
        auto p = pack7 (raw.data(), raw.size());
        CHECK (p.size() == 1171);
        CHECK (p.size() == packedSize (1024));
        for (auto b : p) CHECK (b < 0x80);
        auto u = unpack7 (p.data(), p.size());
        CHECK (u == raw);
    }

    // parameter table sanity: in-range defaults, no overlapping storage
    {
        std::vector<int> owner (1024, -1);
        for (auto& d : ParamTable::get().all())
        {
            CHECK (d.defV >= d.minV && d.defV <= d.maxV);
            const int n = d.storage == Storage::U16 ? 2 : 1;
            for (int k = 0; k < n; ++k)
            {
                if (owner[(size_t) (d.offset + k)] != -1)
                    std::cerr << "overlap " << d.key << " with " << ParamTable::get()[owner[(size_t) (d.offset + k)]].key << "\n";
                CHECK (owner[(size_t) (d.offset + k)] == -1);
                owner[(size_t) (d.offset + k)] = d.index;
            }
            CHECK (ParamTable::get().indexOf (d.key) == d.index);
        }
        CHECK (ParamTable::get().paramForCC (43) == ParamTable::get().indexOf ("cutoff"));
        CHECK (ParamTable::get().paramForNRPN (0x12) == ParamTable::get().indexOf ("select_vpm"));
    }

    // program basics
    Program p;
    CHECK (p.isValid());
    CHECK (p.getName() == "Init Program");
    CHECK (p.isInitProgram());
    p.setName ("Bass Wobble X1234");
    CHECK (p.getName() == "Bass Wobble");
    p.setName ("Wobble");
    CHECK (p.getName() == "Wobble");
    for (auto& d : ParamTable::get().all())
    {
        p.set (d, d.maxV); CHECK (p.get (d) == d.maxV);
        p.set (d, d.minV); CHECK (p.get (d) == d.minV);
        p.set (d, d.maxV + 50); CHECK (p.get (d) == d.maxV);
    }
    p.set (ParamTable::get().indexOf ("cutoff"), 777);
    CHECK (p.data()[60] == (777 & 0xFF) && p.data()[61] == (777 >> 8));
    p.set (ParamTable::get().indexOf ("seq_swing"), 115);
    CHECK (formatValue (*ParamTable::get().byKey ("seq_swing"), 115) == "+40");

    // sysex
    {
        auto dump = sysex::programDump (0, 321, p);
        CHECK ((int) dump.size() == sysex::programDumpSize);
        auto parsed = sysex::parse (dump.data() + 1, (int) dump.size() - 2);
        CHECK (parsed.valid && parsed.func == sysex::ProgramDump && parsed.slot == 321);
        Program q;
        CHECK (q.loadFrom (parsed.payload.data(), parsed.payload.size()));
        CHECK (q == p);
        auto cur = sysex::currentProgramDump (3, p);
        CHECK ((int) cur.size() == sysex::currentProgramDumpSize);
        auto pc = sysex::parse (cur.data(), (int) cur.size());
        CHECK (pc.valid && pc.channel == 3 && pc.func == sysex::CurrentProgramDump);
        auto req = sysex::programRequest (0, 499);
        CHECK (req.getSysExDataSize() == 8 && req.getSysExData()[6] == (499 & 0x7F) && req.getSysExData()[7] == (499 >> 7));
        const uint8_t idr[] = { 0x7E, 0x00, 0x06, 0x02, 0x42, 0x51, 0x01, 0x00, 0x00, 0x05, 0x00, 0x02, 0x00 };
        auto id = sysex::parseIdentity (idr, sizeof (idr));
        CHECK (id.valid && id.major == 2 && id.minor == 5);
    }

    // files
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("xdtests");
    dir.deleteRecursively();
    dir.createDirectory();
    {
        AddInfo ai { "Wiktor", "Fat bass", "Bass", "2026-10-14T12:00:00" };
        auto f1 = dir.getChildFile ("a.syx");
        CHECK (files::saveProgramSyx (f1, p, ai, 0).ok);
        Program r; AddInfo ar;
        CHECK (files::loadProgram (f1, r, ar).ok);
        CHECK (r == p && ar == ai);

        auto f2 = dir.getChildFile ("a.mnlgxdprog");
        CHECK (files::saveProgramXdProg (f2, p, ai).ok);
        CHECK (files::loadProgram (f2, r, ar).ok);
        CHECK (r == p && ar.author == ai.author && ar.description == ai.description && ar.category == "Bass");

        Bank b; b.initAll();
        b.progs[7] = p; b.info[7] = ai;
        b.progs[499].setName ("Last");
        auto f3 = dir.getChildFile ("bank.syx");
        CHECK (files::saveBank (f3, b, 0).ok);
        CHECK (f3.getSize() == 590500);
        Bank c;
        auto res = files::loadBank (f3, c, 0);
        CHECK (res.ok);
        CHECK (c.file == f3);                // already clean
        CHECK (c.progs[7] == p && c.info[7] == ai && c.progs[499].getName() == "Last");

        auto f4 = dir.getChildFile ("bank.mnlgxdlib");
        CHECK (files::saveBank (f4, b, 0).ok);
        Bank d;
        CHECK (files::loadBank (f4, d, 0).ok);
        CHECK (d.progs[7] == p && d.info[7].author == "Wiktor" && d.progs[499].getName() == "Last");

        // messy dump: one global dump in front -> clean dump created
        juce::MemoryOutputStream os;
        auto g = sysex::globalDump (0, std::vector<uint8_t> (92, 1));
        os.write (g.data(), g.size());
        juce::MemoryBlock fb; f3.loadFileAsData (fb);
        os.write (fb.getData(), fb.getSize());
        auto f5 = dir.getChildFile ("messy.syx");
        f5.replaceWithData (os.getData(), os.getDataSize());
        Bank e;
        auto r5 = files::loadBank (f5, e, 0);
        CHECK (r5.ok && e.file.getFileName() == "messy_CleanDump.syx" && e.file.getSize() == 590500);
        CHECK (e.progs[7] == p);
        CHECK (files::exportBankList (dir.getChildFile ("list.txt"), e, "Disk 1").ok);
    }

    // morph
    {
        Program a, b;
        auto ci = ParamTable::get().indexOf ("cutoff");
        auto wi = ParamTable::get().indexOf ("vco1_wave");
        auto ri = ParamTable::get().indexOf ("resonance");
        a.set (ci, 0); b.set (ci, 1000);
        a.set (wi, 0); b.set (wi, 2);
        a.set (ri, 0); b.set (ri, 1000);
        std::array<bool, numSections> locked {};
        locked[(size_t) Section::Filter] = false;
        auto m = morph (a, b, 0.25, locked);
        CHECK (m.get (ci) == 250 && m.get (wi) == 0);
        m = morph (a, b, 0.75, locked);
        CHECK (m.get (wi) == 2);
        locked[(size_t) Section::Filter] = true;
        m = morph (a, b, 0.75, locked);
        CHECK (m.get (ci) == 0 && m.get (ri) == 0);
        UserNames n;
        auto rep = buildReport (a, {}, "A", &b, nullptr, "B", true, n);
        CHECK (rep.contains ("Cutoff") && ! rep.contains ("Amp Attack"));
    }

    // formatting
    {
        auto& t = ParamTable::get();
        CHECK (formatValue (*t.byKey ("vco1_pitch"), 512) == "0 cent");
        CHECK (formatValue (*t.byKey ("vco1_pitch"), 1023) == "+1200 cent");
        CHECK (formatValue (*t.byKey ("program_level"), 102) == "0.0 dB");
        CHECK (formatValue (*t.byKey ("modfx_type"), 3) == "PHASER");
        UserNames n; n.reverb.set (0, "Shimmer");
        CHECK (formatValue (*t.byKey ("reverb_type"), 10, &n) == "Shimmer");
    }

    dir.deleteRecursively();
    std::cout << (failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED") << " (" << failures << " failures)\n";
    return failures == 0 ? 0 : 1;
}
