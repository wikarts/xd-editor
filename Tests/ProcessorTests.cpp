// Integration tests for the processor: MIDI out/in through the "plugin host" route,
// DAW state save/restore (patch recall) and the bank receive transfer against a simulated synth.
#include "PluginProcessor.h"
#include "Librarian.h"
#include "core/Codec.h"
#include <iostream>

using namespace xd;
static int failures = 0;
#define CHECK(c) do { if (! (c)) { ++failures; std::cerr << "FAIL line " << __LINE__ << ": " #c "\n"; } } while (0)

static juce::MidiBuffer run (XDProcessor& p, const juce::MidiBuffer& in = {})
{
    juce::AudioBuffer<float> audio (2, 256);
    juce::MidiBuffer m (in);
    p.processBlock (audio, m);
    return m;
}

static void pump (int ms = 60) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

static std::vector<juce::MidiMessage> msgs (const juce::MidiBuffer& b)
{
    std::vector<juce::MidiMessage> v;
    for (auto m : b) v.push_back (m.getMessage());
    return v;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    {
        XDProcessor p;
        auto& s = p.getSettings();
        s.outputRoute = 1; // via plugin host -> messages appear in processBlock output
        s.midiInId = s.midiOutId = {};
        s.sendOnRecall = true;
        s.autoSendDump = true;
        s.channel = 0;
        s.changed();
        pump();
        run (p); // flush the device inquiry

        auto& t = ParamTable::get();
        // ---- audio passes through unchanged (effect insert)
        {
            juce::AudioBuffer<float> audio (2, 64);
            for (int i = 0; i < 64; ++i) { audio.setSample (0, i, 0.25f); audio.setSample (1, i, -0.5f); }
            juce::MidiBuffer m;
            p.processBlock (audio, m);
            CHECK (std::abs (audio.getSample (0, 10) - 0.25f) < 1.0e-6f && std::abs (audio.getSample (1, 63) + 0.5f) < 1.0e-6f);
        }
        // ---- 10-bit CC
        p.setValueFromUI (t.indexOf ("cutoff"), 777);
        auto out = msgs (run (p));
        CHECK (out.size() == 2);
        if (out.size() == 2)
        {
            CHECK (out[0].isControllerOfType (63) && out[0].getControllerValue() == (777 & 7));
            CHECK (out[1].isControllerOfType (43) && out[1].getControllerValue() == (777 >> 3));
        }
        // ---- stepped CC
        p.setValueFromUI (t.indexOf ("vco1_wave"), 1);
        out = msgs (run (p));
        CHECK (out.size() == 1 && out[0].isControllerOfType (50) && out[0].getControllerValue() == 64);
        // ---- NRPN 8-bit hi-res
        p.setValueFromUI (t.indexOf ("vpm_param2"), 150);
        out = msgs (run (p));
        CHECK (out.size() == 4);
        if (out.size() == 4)
        {
            CHECK (out[0].isControllerOfType (99) && out[0].getControllerValue() == 0);
            CHECK (out[1].isControllerOfType (98) && out[1].getControllerValue() == 0x41);
            CHECK (out[2].isControllerOfType (63) && out[2].getControllerValue() == (150 & 7));
            CHECK (out[3].isControllerOfType (6) && out[3].getControllerValue() == (150 >> 3));
        }
        // ---- host automation reflects UI changes
        CHECK (dynamic_cast<juce::AudioParameterInt*> (p.getParameters()[t.indexOf ("cutoff")])->get() == 777);

        // ---- voice mode via NRPN 0x10 (official MIDI implementation)
        p.setValueFromUI (t.indexOf ("voice_mode"), 2);
        out = msgs (run (p));
        CHECK (out.size() == 3 && out[1].getControllerValue() == 0x10 && out[2].isControllerOfType (6) && out[2].getControllerValue() == 2);
        // ---- program name via NRPN 0x01..0x0C
        p.renameCurrent ("AB");
        out = msgs (run (p));
        CHECK (out.size() == 36 && out[1].getControllerValue() == 0x01 && out[2].getControllerValue() == 'A'
               && out[34].getControllerValue() == 0x0C && out[35].getControllerValue() == 32);
        // ---- micro tuning user scale: stored 130 -> NRPN value 114
        p.setValueFromUI (t.indexOf ("micro_tuning"), 130);
        out = msgs (run (p));
        CHECK (out.size() == 3 && out[1].getControllerValue() == 0x30 && out[2].getControllerValue() == 114);

        // ---- parameter without CC -> debounced program dump
        p.setValueFromUI (t.indexOf ("key_trig"), 1);
        CHECK (run (p).isEmpty());
        pump (400);
        out = msgs (run (p));
        CHECK (out.size() == 1 && out[0].isSysEx() && out[0].getRawDataSize() == sysex::currentProgramDumpSize);

        // ---- host automation -> CC to the synth
        auto* res = dynamic_cast<juce::RangedAudioParameter*> (p.getParameters()[t.indexOf ("resonance")]);
        res->setValueNotifyingHost (res->convertTo0to1 (500));
        pump();
        out = msgs (run (p));
        CHECK (p.getValue (t.indexOf ("resonance")) == 500);
        CHECK (out.size() == 2 && out[1].isControllerOfType (44) && out[1].getControllerValue() == 500 >> 3);

        // ---- synth knob (CC63 + CC16) arrives from the host -> parameter updated, nothing echoed
        juce::MidiBuffer in;
        in.addEvent (juce::MidiMessage::controllerEvent (1, 63, 5), 0);
        in.addEvent (juce::MidiMessage::controllerEvent (1, 16, 100), 1);
        in.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 2);
        out = msgs (run (p, in));
        pump();
        CHECK (p.getValue (t.indexOf ("amp_attack")) == (100 << 3 | 5));
        CHECK (out.size() == 1 && out[0].isNoteOn()); // the note is passed thru, the CCs are not
        CHECK (run (p).isEmpty());

        // ---- NRPN from synth
        in.clear();
        in.addEvent (juce::MidiMessage::controllerEvent (1, 99, 0), 0);
        in.addEvent (juce::MidiMessage::controllerEvent (1, 98, 0x3F), 1);
        in.addEvent (juce::MidiMessage::controllerEvent (1, 6, 100), 2);
        run (p, in);
        pump();
        CHECK (p.getValue (t.indexOf ("program_level")) == 112);
        in.clear();
        in.addEvent (juce::MidiMessage::controllerEvent (1, 99, 0), 0);
        in.addEvent (juce::MidiMessage::controllerEvent (1, 98, 0x30), 1);
        in.addEvent (juce::MidiMessage::controllerEvent (1, 6, 113), 2);
        in.addEvent (juce::MidiMessage::controllerEvent (1, 99, 0), 3);
        in.addEvent (juce::MidiMessage::controllerEvent (1, 98, 0x10), 4);
        in.addEvent (juce::MidiMessage::controllerEvent (1, 6, 3), 5);
        run (p, in);
        pump();
        CHECK (p.getValue (t.indexOf ("micro_tuning")) == 129);
        CHECK (p.getValue (t.indexOf ("voice_mode")) == 3);

        // ---- out-of-range value from a real patch survives the host parameter round trip
        {
            Program odd;
            odd.data()[1022] = 77; // arp gate seen in a factory-style patch (range is documented as 0~72)
            odd.data()[168] = 115;
            odd.data()[135] = 5; // program level below the documented 12~132
            p.loadProgram (odd, {}, false);
            pump();
            CHECK (p.getCurrent().data()[1022] == 77 && p.getCurrent().data()[168] == 115 && p.getCurrent().data()[135] == 5);
            run (p);
        }

        // ---- context CC: multi shape follows multi type
        p.setValueFromUI (t.indexOf ("multi_type"), 1);
        run (p);
        in.clear();
        in.addEvent (juce::MidiMessage::controllerEvent (1, 63, 3), 0);
        in.addEvent (juce::MidiMessage::controllerEvent (1, 54, 64), 1);
        run (p, in);
        pump();
        CHECK (p.getValue (t.indexOf ("shape_vpm")) == (64 << 3 | 3));
        CHECK (p.getValue (t.indexOf ("shape_noise")) == 0);

        // ---- receive current program from synth
        Program fromSynth;
        fromSynth.setName ("FromSynth");
        fromSynth.set (t.indexOf ("cutoff"), 123);
        bool gotIt = false;
        p.requestCurrentProgram ([&] (bool ok, juce::String) { gotIt = ok; });
        out = msgs (run (p));
        CHECK (out.size() == 1 && out[0].isSysEx() && out[0].getSysExData()[5] == sysex::CurrentProgramRequest);
        in.clear();
        in.addEvent (sysex::toMessage (sysex::currentProgramDump (0, fromSynth)), 0);
        run (p, in);
        pump();
        CHECK (gotIt && p.getCurrent().getName() == "FromSynth" && p.getValue (t.indexOf ("cutoff")) == 123);
        CHECK (p.getCurrent() == p.getSaved());

        // ---- DAW state: save, change, restore -> patch recalled and sent to the synth
        p.setValueFromUI (t.indexOf ("cutoff"), 900);
        p.renameCurrent ("MySong");
        run (p);
        juce::MemoryBlock state;
        p.getStateInformation (state);

        XDProcessor q;
        q.getSettings().outputRoute = 1;
        pump();
        run (q);
        q.setStateInformation (state.getData(), (int) state.getSize());
        CHECK (q.getCurrent().getName() == "MySong" && q.getValue (t.indexOf ("cutoff")) == 900);
        CHECK (q.getSaved().getName() == "FromSynth"); // saved version kept separately
        CHECK (dynamic_cast<juce::AudioParameterInt*> (q.getParameters()[t.indexOf ("cutoff")])->get() == 900);
        run (q);
        pump (900);
        out = msgs (run (q));
        bool recallDump = false;
        for (auto& m : out)
            if (m.isSysEx() && m.getSysExData()[5] == sysex::CurrentProgramDump)
            {
                auto parsed = sysex::parse (m.getSysExData(), m.getSysExDataSize());
                Program r;
                recallDump = r.loadFrom (parsed.payload.data(), parsed.payload.size()) && r.getName() == "MySong";
            }
        CHECK (recallDump);

        // ---- bank receive against a simulated synth (fast transfer)
        q.getSettings().transferMs = 20;
        bool bankOk = false;
        q.startReceiveBank ([&] (bool ok, juce::String) { bankOk = ok; });
        for (int iter = 0; iter < 4000 && q.isTransferring(); ++iter)
        {
            pump (5);
            auto o = msgs (run (q));
            juce::MidiBuffer reply;
            for (auto& m : o)
                if (m.isSysEx() && m.getSysExData()[5] == sysex::ProgramRequest)
                {
                    const int slot = m.getSysExData()[6] | (m.getSysExData()[7] << 7);
                    Program sp;
                    sp.setName ("Synth " + juce::String (slot + 1));
                    reply.addEvent (sysex::toMessage (sysex::programDump (0, slot, sp)), 0);
                }
            if (! reply.isEmpty()) run (q, reply);
        }
        CHECK (bankOk);
        CHECK (q.bank (BankId::Synth).loaded && q.bank (BankId::Synth).progs[499].getName() == "Synth 500");

        // ---- librarian: copy into XD bank sends the program to the synth slot
        q.initBank (BankId::Disk1);
        q.bank (BankId::Disk1).progs[3].setName ("Lead");
        lib::copy (q, 0, { 3 }, (int) BankId::Synth, 10, true);
        CHECK (q.bank (BankId::Synth).progs[10].getName() == "Lead");
        bool wroteSlot = false;
        for (int iter = 0; iter < 50 && ! wroteSlot; ++iter)
        {
            pump (30);
            for (auto& m : msgs (run (q)))
                if (m.isSysEx() && m.getSysExData()[5] == sysex::ProgramDump && m.getSysExData()[6] == 10) wroteSlot = true;
        }
        CHECK (wroteSlot);

        // ---- morph
        q.libDstBank = (int) BankId::Disk1;
        q.libDstSlot = 3;
        q.bank (BankId::Disk1).progs[3].set (t.indexOf ("cutoff"), 100);
        q.morphTargetChanged();
        q.setMorph (0.5);
        CHECK (q.getValue (t.indexOf ("cutoff")) == 500);
        q.morphLocks[(size_t) Section::Filter] = true;
        q.setMorph (1.0);
        CHECK (q.getValue (t.indexOf ("cutoff")) == 500);
        run (q);
    }
    std::cout << (failures == 0 ? "ALL PROCESSOR TESTS PASSED" : "PROCESSOR TESTS FAILED") << " (" << failures << ")\n";
    return failures == 0 ? 0 : 1;
}
