#include "Params.h"

namespace xd
{
juce::String sectionName (Section s)
{
    static const char* n[] = { "VCO 1", "VCO 2", "Multi Engine", "Mixer", "Modulation", "Filter",
                               "Amp EG", "EG", "LFO", "Voice Mode", "Mod FX", "Delay", "Reverb",
                               "Program", "Controllers", "Sequencer" };
    return n[(int) s];
}

static juce::StringArray sa (std::initializer_list<const char*> l)
{
    juce::StringArray a;
    for (auto* s : l) a.add (s);
    return a;
}

static juce::StringArray numbered (const juce::String& prefix, int n)
{
    juce::StringArray a;
    for (int i = 1; i <= n; ++i) a.add (prefix + juce::String (i));
    return a;
}

static const juce::StringArray& modTargets()
{
    // MIDI Implementation note P19
    static juce::StringArray a = sa ({ "Gate Time", "Portamento", "V.M Depth", "VCO1 Pitch", "VCO1 Shape", "VCO2 Pitch",
                                       "VCO2 Shape", "Cross Mod", "Multi Shape", "VCO1 Level", "VCO2 Level", "Multi Level",
                                       "Cutoff", "Resonance", "A.EG Attack", "A.EG Decay", "A.EG Sustain", "A.EG Release",
                                       "EG Attack", "EG Decay", "EG Int", "LFO Rate", "LFO Int", "Mod FX Speed",
                                       "Mod FX Depth", "Reverb Time", "Reverb Depth", "Delay Time", "Delay Depth" });
    return a;
}

ParamTable::ParamTable()
{
    ccMap.fill (-1);
    nrpnMap.fill (-1);

    auto add = [this] (const char* key, const char* name, Section sec, int off, Storage st, int mn, int mx, int def,
                       Tx tx, int num, const char* desc) -> ParamDef&
    {
        ParamDef d;
        d.index = (int) defs.size();
        d.key = key; d.name = name; d.section = sec; d.offset = off; d.storage = st;
        d.minV = mn; d.maxV = mx; d.defV = def; d.tx = tx; d.number = num; d.desc = juce::String::fromUTF8 (desc);
        defs.push_back (d);
        return defs.back();
    };
    auto u10 = [&] (const char* key, const char* name, Section sec, int off, int def, Tx tx, int num, const char* desc) -> ParamDef&
    { return add (key, name, sec, off, Storage::U16, 0, 1023, def, tx, num, desc); };
    auto lst = [&] (const char* key, const char* name, Section sec, int off, juce::StringArray labels, int def,
                    Tx tx, int num, const char* desc, int base = 0) -> ParamDef&
    {
        auto& d = add (key, name, sec, off, Storage::U8, base, base + labels.size() - 1, def, tx, num, desc);
        d.labels = labels; d.disp = Disp::Labels; d.steps = labels.size(); d.valueBase = base;
        return d;
    };
    const auto onOff = sa ({ "Off", "On" });
    const auto octaves = sa ({ "16'", "8'", "4'", "2'" });
    const auto waves = sa ({ "SQR", "TRI", "SAW" });
    const auto pct3 = sa ({ "0%", "50%", "100%" });

    // ---------------- VCO 1 ----------------
    lst ("vco1_wave", "VCO1 Wave", Section::VCO1, 22, waves, 2, Tx::CCStep, 50, "Kształt fali VCO 1: kwadrat, trójkąt, piła.");
    lst ("vco1_octave", "VCO1 Octave", Section::VCO1, 23, octaves, 1, Tx::CCStep, 48, "Oktawa VCO 1 (stopa organowa).");
    u10 ("vco1_pitch", "VCO1 Pitch", Section::VCO1, 24, 512, Tx::CC10, 34, "Strojenie VCO 1 w zakresie ±1200 centów.").disp = Disp::PitchCents;
    u10 ("vco1_shape", "VCO1 Shape", Section::VCO1, 26, 0, Tx::CC10, 36, "Modyfikacja kształtu fali VCO 1 (szerokość impulsu / fold / harmoniczne).");
    // ---------------- VCO 2 ----------------
    lst ("vco2_wave", "VCO2 Wave", Section::VCO2, 28, waves, 2, Tx::CCStep, 51, "Kształt fali VCO 2.");
    lst ("vco2_octave", "VCO2 Octave", Section::VCO2, 29, octaves, 1, Tx::CCStep, 49, "Oktawa VCO 2.");
    u10 ("vco2_pitch", "VCO2 Pitch", Section::VCO2, 30, 512, Tx::CC10, 35, "Strojenie VCO 2 w zakresie ±1200 centów.").disp = Disp::PitchCents;
    u10 ("vco2_shape", "VCO2 Shape", Section::VCO2, 32, 0, Tx::CC10, 37, "Modyfikacja kształtu fali VCO 2.");
    // ---------------- Modulation ----------------
    lst ("sync", "Sync", Section::Modulation, 34, onOff, 0, Tx::CCBool, 80, "Synchronizacja VCO 2 do VCO 1.");
    lst ("ring", "Ring", Section::Modulation, 35, onOff, 0, Tx::CCBool, 81, "Modulacja kołowa VCO 1 x VCO 2.");
    u10 ("cross_mod", "Cross Mod Depth", Section::Modulation, 36, 0, Tx::CC10, 41, "Głębokość modulacji częstotliwości VCO 2 przez VCO 1.");
    // ---------------- Multi engine ----------------
    lst ("multi_type", "Multi Type", Section::Multi, 38, sa ({ "NOISE", "VPM", "USER" }), 0, Tx::CCStep, 53, "Typ silnika Multi: szum, VPM lub oscylator użytkownika.");
    lst ("select_noise", "Noise Type", Section::Multi, 39, sa ({ "HIGH", "LOW", "PEAK", "DECIM" }), 0, Tx::NRPN7, 0x11, "Rodzaj szumu silnika Multi.");
    lst ("select_vpm", "VPM Type", Section::Multi, 40, sa ({ "Sin1", "Sin2", "Sin3", "Sin4", "Saw1", "Saw2", "Squ1", "Squ2", "Fat1", "Fat2", "Air1", "Air2", "Decay1", "Decay2", "Creep", "Throat" }), 0, Tx::NRPN7, 0x12, "Algorytm oscylatora VPM.");
    { auto& d = lst ("select_user", "User Osc", Section::Multi, 41, numbered ("USER", 16), 0, Tx::NRPN7, 0x13, "Wybrany oscylator użytkownika (logue SDK)."); d.userNamesKind = 0; }
    u10 ("shape_noise", "Noise Shape", Section::Multi, 42, 0, Tx::NRPN10, 0x14, "Shape dla szumu.");
    u10 ("shape_vpm", "VPM Shape", Section::Multi, 44, 0, Tx::NRPN10, 0x15, "Shape (głębokość modulacji) VPM.");
    u10 ("shape_user", "User Shape", Section::Multi, 46, 0, Tx::NRPN10, 0x16, "Shape oscylatora użytkownika.");
    u10 ("shift_shape_noise", "Noise Shift Shape", Section::Multi, 48, 0, Tx::NRPN10, 0x17, "Shift+Shape dla szumu.");
    u10 ("shift_shape_vpm", "VPM Shift Shape", Section::Multi, 50, 0, Tx::NRPN10, 0x18, "Shift+Shape (współczynnik) VPM.");
    u10 ("shift_shape_user", "User Shift Shape", Section::Multi, 52, 0, Tx::NRPN10, 0x19, "Shift+Shape oscylatora użytkownika.");
    lst ("multi_octave", "Multi Octave", Section::Multi, 130, octaves, 1, Tx::NRPN7, 0x3A, "Oktawa silnika Multi.");
    lst ("multi_routing", "Multi Routing", Section::Multi, 131, sa ({ "Pre VCF", "Post VCF" }), 0, Tx::NRPN7, 0x3B, "Wpięcie silnika Multi przed lub za filtrem.");
    {
        const char* vpmNames[] = { "VPM Feedback", "VPM Noise Depth", "VPM Shape Mod Int", "VPM Mod Attack", "VPM Mod Decay", "VPM Mod Key Track" };
        for (int i = 0; i < 6; ++i)
        {
            auto key = juce::String ("vpm_param") + juce::String (i + 1);
            auto& d = add ("", vpmNames[i], Section::Multi, 136 + i, Storage::U8, 0, 200, 100, Tx::NRPN8, 0x40 + i, "Parametr VPM (-100%..+100%).");
            d.key = key; d.disp = Disp::Bipolar100;
        }
        for (int i = 0; i < 6; ++i)
        {
            auto& d = add ("", "", Section::Multi, 142 + i, Storage::U8, 0, 200, 0, Tx::NRPN8, 0x48 + i, "Parametr oscylatora użytkownika (typ zależy od modułu).");
            d.key = "user_param" + juce::String (i + 1); d.name = "User Param " + juce::String (i + 1); d.disp = Disp::UserParam;
        }
    }
    // ---------------- Mixer ----------------
    u10 ("vco1_level", "VCO1 Level", Section::Mixer, 54, 1023, Tx::CC10, 39, "Poziom VCO 1 w mikserze.");
    u10 ("vco2_level", "VCO2 Level", Section::Mixer, 56, 1023, Tx::CC10, 40, "Poziom VCO 2 w mikserze.");
    u10 ("multi_level", "Multi Level", Section::Mixer, 58, 0, Tx::CC10, 33, "Poziom silnika Multi w mikserze.");
    // ---------------- Filter ----------------
    u10 ("cutoff", "Cutoff", Section::Filter, 60, 1023, Tx::CC10, 43, "Częstotliwość odcięcia filtra dolnoprzepustowego 2-bieguowego.");
    u10 ("resonance", "Resonance", Section::Filter, 62, 0, Tx::CC10, 44, "Rezonans filtra.");
    lst ("drive", "Drive", Section::Filter, 64, pct3, 0, Tx::CCStep, 84, "Przester filtra.");
    lst ("keytrack", "Key Track", Section::Filter, 65, pct3, 0, Tx::CCStep, 83, "Śledzenie klawiatury przez filtr.");
    add ("cutoff_velocity", "Cutoff Velocity", Section::Filter, 128, Storage::U8, 0, 127, 0, Tx::NRPN7, 0x38, "Wpływ dynamiki na częstotliwość odcięcia.");
    // ---------------- Amp EG ----------------
    u10 ("amp_attack", "Amp Attack", Section::AmpEG, 66, 0, Tx::CC10, 16, "Czas narastania obwiedni wzmacniacza.");
    u10 ("amp_decay", "Amp Decay", Section::AmpEG, 68, 512, Tx::CC10, 17, "Czas opadania obwiedni wzmacniacza.");
    u10 ("amp_sustain", "Amp Sustain", Section::AmpEG, 70, 1023, Tx::CC10, 18, "Poziom podtrzymania obwiedni wzmacniacza.");
    u10 ("amp_release", "Amp Release", Section::AmpEG, 72, 0, Tx::CC10, 19, "Czas wybrzmiewania obwiedni wzmacniacza.");
    add ("amp_velocity", "Amp Velocity", Section::AmpEG, 129, Storage::U8, 0, 127, 64, Tx::NRPN7, 0x39, "Wpływ dynamiki na głośność.");
    // ---------------- EG ----------------
    u10 ("eg_attack", "EG Attack", Section::EG, 74, 0, Tx::CC10, 20, "Czas narastania obwiedni modulacji (AD).");
    u10 ("eg_decay", "EG Decay", Section::EG, 76, 512, Tx::CC10, 21, "Czas opadania obwiedni modulacji (AD).");
    u10 ("eg_int", "EG Int", Section::EG, 78, 512, Tx::CC10, 22, "Intensywność obwiedni (-100%..+100%).").disp = Disp::EgInt;
    lst ("eg_target", "EG Target", Section::EG, 80, sa ({ "CUTOFF", "PITCH 2", "PITCH" }), 0, Tx::CCStep, 23, "Cel obwiedni modulacji.");
    lst ("eg_legato", "EG Legato", Section::EG, 132, onOff, 0, Tx::NRPN7, 0x3C, "Obwiednie nie są wyzwalane ponownie przy grze legato.");
    // ---------------- LFO ----------------
    lst ("lfo_wave", "LFO Wave", Section::LFO, 81, waves, 1, Tx::CCStep, 57, "Kształt fali LFO.");
    lst ("lfo_mode", "LFO Mode", Section::LFO, 82, sa ({ "1-SHOT", "NORMAL", "BPM" }), 1, Tx::CCStep, 58, "Tryb LFO.");
    u10 ("lfo_rate", "LFO Rate", Section::LFO, 83, 512, Tx::CC10, 24, "Szybkość LFO (w trybie BPM - wartość nutowa).");
    u10 ("lfo_int", "LFO Int", Section::LFO, 85, 512, Tx::CC10, 26, "Intensywność LFO (wartości ujemne przez Shift+Wave).").disp = Disp::LfoInt;
    lst ("lfo_target", "LFO Target", Section::LFO, 87, sa ({ "CUTOFF", "SHAPE", "PITCH" }), 2, Tx::CCStep, 56, "Cel modulacji LFO.");
    lst ("lfo_key_sync", "LFO Key Sync", Section::LFO, 125, onOff, 0, Tx::NRPN7, 0x34, "Restart LFO przy każdej nucie.");
    lst ("lfo_voice_sync", "LFO Voice Sync", Section::LFO, 126, onOff, 0, Tx::NRPN7, 0x35, "Wspólna faza LFO dla wszystkich głosów.");
    lst ("lfo_target_osc", "LFO Target Osc", Section::LFO, 127, sa ({ "ALL", "VCO1+2", "VCO2", "MULTI" }), 0, Tx::NRPN7, 0x36, "Oscylatory modulowane przez LFO.");
    // ---------------- Voice mode ----------------
    lst ("voice_mode", "Voice Mode", Section::VoiceMode, 21, sa ({ "ARP/LATCH", "CHORD", "UNISON", "POLY" }), 4, Tx::NRPN7, 0x10, "Tryb głosów: Poly / Unison / Chord / Arp-Latch.", 1);
    u10 ("voice_depth", "Voice Mode Depth", Section::VoiceMode, 19, 0, Tx::CC10, 27, "Głębokość trybu głosów: Duo / detune / akord / wzór arpeggio.").disp = Disp::VoiceDepth;
    add ("portamento", "Portamento", Section::VoiceMode, 17, Storage::U8, 0, 127, 0, Tx::CC7, 5, "Czas portamento.");
    lst ("portamento_mode", "Portamento Mode", Section::VoiceMode, 133, sa ({ "Auto", "On" }), 1, Tx::NRPN7, 0x3D, "Auto = tylko legato.");
    lst ("portamento_bpm", "Portamento BPM", Section::VoiceMode, 134, onOff, 0, Tx::NRPN7, 0x3E, "Synchronizacja portamento z tempem.");
    lst ("key_trig", "Key Trig", Section::VoiceMode, 18, onOff, 0, Tx::None, -1, "Wyzwalanie sekwencji klawiszem.");
    lst ("octave", "Keyboard Octave", Section::VoiceMode, 16, sa ({ "-2", "-1", "0", "+1", "+2" }), 2, Tx::None, -1, "Oktawa klawiatury zapisana w programie.");
    // ---------------- Mod FX ----------------
    lst ("modfx_on", "Mod FX On", Section::ModFX, 88, onOff, 0, Tx::CCBool, 92, "Włączenie efektu modulacyjnego.");
    lst ("modfx_type", "Mod FX Type", Section::ModFX, 89, sa ({ "CHORUS", "ENSEMBLE", "PHASER", "FLANGER", "USER" }), 1, Tx::CCStep, 88, "Rodzaj efektu modulacyjnego.", 1);
    lst ("modfx_chorus", "Chorus Type", Section::ModFX, 90, sa ({ "Stereo", "Light", "Deep", "Triphase", "Harmonic", "Mono", "Feedback", "Vibrato" }), 0, Tx::CCCtx, 96, "Odmiana chorusa.");
    lst ("modfx_ensemble", "Ensemble Type", Section::ModFX, 91, sa ({ "Stereo", "Light", "Mono" }), 0, Tx::CCCtx, 96, "Odmiana ensemble.");
    lst ("modfx_phaser", "Phaser Type", Section::ModFX, 92, sa ({ "Stereo", "Fast", "Orange", "Small", "Small Reso", "Black", "Formant", "Twinkle" }), 0, Tx::CCCtx, 96, "Odmiana phasera.");
    lst ("modfx_flanger", "Flanger Type", Section::ModFX, 93, sa ({ "Stereo", "Light", "Mono", "High Sweep", "Mid Sweep", "Pan Sweep", "Mono Sweep", "Triphase" }), 0, Tx::CCCtx, 96, "Odmiana flangera.");
    { auto& d = lst ("modfx_user", "Mod FX User", Section::ModFX, 94, numbered ("USER", 16), 0, Tx::CCCtx, 96, "Efekt modulacyjny użytkownika."); d.userNamesKind = 1; }
    u10 ("modfx_time", "Mod FX Time", Section::ModFX, 95, 512, Tx::CC10, 28, "Szybkość / czas efektu modulacyjnego.");
    u10 ("modfx_depth", "Mod FX Depth", Section::ModFX, 97, 512, Tx::CC10, 29, "Głębokość efektu modulacyjnego.");
    // ---------------- Delay ----------------
    lst ("delay_on", "Delay On", Section::Delay, 99, onOff, 0, Tx::CCBool, 93, "Włączenie delaya.");
    {
        auto labels = sa ({ "Stereo", "Mono", "Ping Pong", "Hipass", "Tape", "One Tap", "Stereo BPM", "Mono BPM", "Ping BPM", "Hipass BPM", "Tape BPM", "Doubling" });
        labels.addArray (numbered ("USER", 8));
        auto& d = lst ("delay_type", "Delay Type", Section::Delay, 100, labels, 0, Tx::CCStep, 89, "Rodzaj delaya.");
        d.userNamesKind = 2; d.userNamesFirst = 12;
    }
    u10 ("delay_time", "Delay Time", Section::Delay, 101, 512, Tx::CC10, 105, "Czas opóźnienia (w trybach BPM - wartość nutowa).");
    u10 ("delay_depth", "Delay Depth", Section::Delay, 103, 512, Tx::CC10, 106, "Sprzężenie zwrotne / głębokość delaya.");
    add ("delay_drywet", "Delay Dry/Wet", Section::Delay, 151, Storage::U16, 0, 1024, 512, Tx::CC10, 107, "Proporcja sygnału suchego i efektu (512 = zbalansowane).");
    // ---------------- Reverb ----------------
    lst ("reverb_on", "Reverb On", Section::Reverb, 105, onOff, 0, Tx::CCBool, 94, "Włączenie pogłosu.");
    {
        auto labels = sa ({ "Hall", "Smooth", "Arena", "Plate", "Room", "Early Ref", "Space", "Riser", "Submarine", "Horror" });
        labels.addArray (numbered ("USER", 8));
        auto& d = lst ("reverb_type", "Reverb Type", Section::Reverb, 106, labels, 0, Tx::CCStep, 90, "Rodzaj pogłosu.");
        d.userNamesKind = 3; d.userNamesFirst = 10;
    }
    u10 ("reverb_time", "Reverb Time", Section::Reverb, 107, 512, Tx::CC10, 108, "Czas pogłosu.");
    u10 ("reverb_depth", "Reverb Depth", Section::Reverb, 109, 512, Tx::CC10, 109, "Głębokość / gęstość pogłosu.");
    add ("reverb_drywet", "Reverb Dry/Wet", Section::Reverb, 153, Storage::U16, 0, 1024, 512, Tx::CC10, 110, "Proporcja sygnału suchego i pogłosu.");
    // ---------------- Program level ----------------
    add ("program_level", "Program Level", Section::Program, 135, Storage::U8, 12, 132, 102, Tx::NRPN7, 0x3F, "Głośność programu (-18 dB..+6 dB).").disp = Disp::LevelDb;
    { auto& d = add ("program_transpose", "Program Transpose", Section::Program, 150, Storage::U8, 1, 25, 13, Tx::NRPN7, 0x50, "Transpozycja programu w półtonach."); d.disp = Disp::Signed; d.dispOffset = 13; }
    { auto& d = add ("program_tuning", "Program Tuning", Section::Program, 124, Storage::U8, 0, 100, 50, Tx::NRPN7, 0x32, "Dostrojenie programu w centach (-50..+50)."); d.disp = Disp::Signed; d.dispOffset = 50; }
    { auto& d = add ("scale_key", "Scale Key", Section::Program, 123, Storage::U8, 0, 24, 12, Tx::NRPN7, 0x31, "Tonacja skali mikrotonalnej (-12..+12)."); d.disp = Disp::Signed; d.dispOffset = 12; }
    add ("micro_tuning", "Micro Tuning", Section::Program, 122, Storage::U8, 0, 139, 0, Tx::NRPN7, 0x30, "Skala mikrotonalna.").disp = Disp::MicroTuning;
    lst ("arp_gate", "Arp Gate Time", Section::Program, 1022, juce::StringArray(), 0, Tx::None, -1, "");
    defs.back().labels.clear(); defs.back().disp = Disp::Gate72; defs.back().minV = 0; defs.back().maxV = 127; defs.back().defV = 72; defs.back().steps = 0;
    defs.back().desc = juce::String::fromUTF8 ("Długość nut arpeggiatora (0..100%).");
    lst ("arp_rate", "Arp Rate", Section::Program, 1023, sa ({ "1/64", "1/48", "1/32", "1/24", "1/16", "1/16.", "1/12", "1/8", "1/8.", "1/6", "1/4" }), 4, Tx::None, -1, "Podział rytmiczny arpeggiatora.");
    // ---------------- Controllers ----------------
    { auto& d = add ("bend_plus", "Bend Range +", Section::Controllers, 111, Storage::U8, 0, 12, 2, Tx::NRPN7, 0x20, "Zakres pitch bend w górę (półtony)."); juce::ignoreUnused (d); }
    add ("bend_minus", "Bend Range -", Section::Controllers, 112, Storage::U8, 0, 12, 2, Tx::NRPN7, 0x21, "Zakres pitch bend w dół (półtony).");
    { auto& d = lst ("joy_assign_plus", "Joystick Y+ Assign", Section::Controllers, 113, modTargets(), 12, Tx::NRPN7, 0x22, "Cel joysticka Y+."); juce::ignoreUnused (d); }
    add ("joy_range_plus", "Joystick Y+ Range", Section::Controllers, 114, Storage::U8, 0, 200, 200, Tx::NRPN8, 0x23, "Zakres joysticka Y+ (-100%..+100%).").disp = Disp::Bipolar100;
    lst ("joy_assign_minus", "Joystick Y- Assign", Section::Controllers, 115, modTargets(), 12, Tx::NRPN7, 0x24, "Cel joysticka Y-.");
    add ("joy_range_minus", "Joystick Y- Range", Section::Controllers, 116, Storage::U8, 0, 200, 200, Tx::NRPN8, 0x25, "Zakres joysticka Y- (-100%..+100%).").disp = Disp::Bipolar100;
    lst ("cv_mode", "CV In Mode", Section::Controllers, 117, sa ({ "Modulation", "CV/Gate (+)", "CV/Gate (-)" }), 0, Tx::NRPN7, 0x28, "Tryb wejść CV.");
    lst ("cv1_assign", "CV In 1 Assign", Section::Controllers, 118, modTargets(), 0, Tx::NRPN7, 0x29, "Cel wejścia CV 1.");
    add ("cv1_range", "CV In 1 Range", Section::Controllers, 119, Storage::U8, 0, 200, 200, Tx::NRPN8, 0x2A, "Zakres CV 1.").disp = Disp::Bipolar100;
    lst ("cv2_assign", "CV In 2 Assign", Section::Controllers, 120, modTargets(), 0, Tx::NRPN7, 0x2B, "Cel wejścia CV 2.");
    add ("cv2_range", "CV In 2 Range", Section::Controllers, 121, Storage::U8, 0, 200, 200, Tx::NRPN8, 0x2C, "Zakres CV 2.").disp = Disp::Bipolar100;
    lst ("aftertouch_assign", "MIDI Aftertouch Assign", Section::Controllers, 155, modTargets(), 12, Tx::None, -1, "Cel modulacji MIDI aftertouch (brak NRPN - wysyłane zrzutem programu).");
    // ---------------- Sequencer (program dump only) ----------------
    add ("seq_bpm", "Seq BPM", Section::Sequencer, 164, Storage::U16, 100, 3000, 1200, Tx::None, -1, "Tempo sekwencera (10.0..300.0).").disp = Disp::Bpm;
    add ("seq_length", "Seq Step Length", Section::Sequencer, 166, Storage::U8, 1, 16, 16, Tx::None, -1, "Liczba kroków sekwencji.");
    lst ("seq_resolution", "Seq Resolution", Section::Sequencer, 167, sa ({ "1/16", "1/8", "1/4", "1/2", "1/1" }), 0, Tx::None, -1, "Rozdzielczość kroku.");
    { auto& d = add ("seq_swing", "Seq Swing", Section::Sequencer, 168, Storage::U8, 0, 150, 75, Tx::None, -1, "Swing (-75%..+75%)."); d.disp = Disp::Signed; d.dispOffset = 75; }
    add ("seq_gate", "Seq Default Gate", Section::Sequencer, 169, Storage::U8, 0, 72, 54, Tx::None, -1, "Domyślna długość nut sekwencji.").disp = Disp::Gate72;

    for (auto& d : defs)
    {
        if (d.tx == Tx::CC10 || d.tx == Tx::CC7 || d.tx == Tx::CCStep || d.tx == Tx::CCBool)
            ccMap[(size_t) d.number] = d.index;
        else if (d.tx == Tx::NRPN7 || d.tx == Tx::NRPN10 || d.tx == Tx::NRPN8)
            nrpnMap[(size_t) d.number] = d.index;
    }
}

const ParamTable& ParamTable::get()
{
    static ParamTable t;
    return t;
}

int ParamTable::indexOf (const juce::String& key) const
{
    for (auto& d : defs)
        if (d.key == key) return d.index;
    return -1;
}

const ParamDef* ParamTable::byKey (const juce::String& key) const
{
    auto i = indexOf (key);
    return i >= 0 ? &defs[(size_t) i] : nullptr;
}

// ------------------------------------------------------------------------------------------
void UserNames::reset()
{
    osc = numbered ("USER", 16);
    modfx = numbered ("USER", 16);
    delay = numbered ("USER", 8);
    reverb = numbered ("USER", 8);
}

juce::String UserNames::toString() const
{
    juce::StringArray rows;
    rows.add (osc.joinIntoString ("|"));
    rows.add (modfx.joinIntoString ("|"));
    rows.add (delay.joinIntoString ("|"));
    rows.add (reverb.joinIntoString ("|"));
    return rows.joinIntoString ("\n");
}

void UserNames::fromString (const juce::String& s)
{
    reset();
    auto rows = juce::StringArray::fromLines (s);
    for (int k = 0; k < 4 && k < rows.size(); ++k)
    {
        auto parts = juce::StringArray::fromTokens (rows[k], "|", "");
        auto& dst = kind (k);
        for (int i = 0; i < dst.size() && i < parts.size(); ++i)
            if (parts[i].isNotEmpty()) dst.set (i, parts[i]);
    }
}

// ------------------------------------------------------------------------------------------
static double pitchCents (int v)
{
    // Korg MIDI implementation note P5
    struct Seg { int x0, x1; double c0, c1; };
    static const Seg segs[] = { { 0, 4, -1200, -1200 }, { 4, 356, -1200, -256 }, { 356, 476, -256, -16 },
                                { 476, 492, -16, 0 }, { 492, 532, 0, 0 }, { 532, 548, 0, 16 },
                                { 548, 668, 16, 256 }, { 668, 1020, 256, 1200 }, { 1020, 1023, 1200, 1200 } };
    for (auto& s : segs)
        if (v >= s.x0 && v <= s.x1)
            return s.x1 == s.x0 ? s.c0 : s.c0 + (s.c1 - s.c0) * (v - s.x0) / double (s.x1 - s.x0);
    return 0;
}

static double egIntPercent (int v)
{
    if (v < 11) return -100;
    if (v < 492) return -((492.0 - v) * (492.0 - v) * 4641.0 * 100.0) / 1073741824.0;
    if (v <= 532) return 0;
    if (v <= 1013) return ((v - 532.0) * (v - 532.0) * 4641.0 * 100.0) / 1073741824.0;
    return 100;
}

static juce::String voiceDepthText (int v)
{
    // meaning depends on the voice mode (Poly: <256 poly / >=256 duo detune, Unison: detune, Chord: chord type)
    return juce::String (v);
}

juce::StringArray labelsFor (const ParamDef& d, const UserNames* names)
{
    auto labels = d.labels;
    if (names != nullptr && d.userNamesKind >= 0)
    {
        auto& n = names->kind (d.userNamesKind);
        for (int i = 0; i < n.size() && d.userNamesFirst + i < labels.size(); ++i)
            labels.set (d.userNamesFirst + i, n[i]);
    }
    return labels;
}

juce::String formatValue (const ParamDef& d, int v, const UserNames* names, int userParamType)
{
    switch (d.disp)
    {
        case Disp::Labels:
        {
            auto labels = labelsFor (d, names);
            auto i = v - d.valueBase;
            return juce::isPositiveAndBelow (i, labels.size()) ? labels[i] : juce::String (v);
        }
        case Disp::PitchCents: { auto c = pitchCents (v); return (c > 0 ? "+" : "") + juce::String (juce::roundToInt (c)) + " cent"; }
        case Disp::EgInt: { auto p = egIntPercent (v); return (p > 0 ? "+" : "") + juce::String (p, 1) + "%"; }
        case Disp::LfoInt: return v == 512 ? juce::String ("0") : (v > 512 ? "+" + juce::String (v - 512) : juce::String (v - 512));
        case Disp::Bipolar100: { auto p = v - 100; return (p > 0 ? "+" : "") + juce::String (p) + "%"; }
        case Disp::Signed: { auto p = v - d.dispOffset; return (p > 0 ? "+" : "") + juce::String (p); }
        case Disp::LevelDb: { auto db = (v - 102) * 0.2; return (db > 0 ? "+" : "") + juce::String (db, 1) + " dB"; }
        case Disp::Bpm: return juce::String (v / 10.0, 1) + " BPM";
        case Disp::Gate72: return v >= 73 ? juce::String ("TIE") : juce::String (juce::roundToInt (v * 100.0 / 72.0)) + "%";
        case Disp::VoiceDepth: return voiceDepthText (v);
        case Disp::MicroTuning:
        {
            static const char* n[] = { "Equal Temp", "Pure Major", "Pure Minor", "Pythagorean", "Werckmeister", "Kirnberger",
                                       "Slendro", "Pelog", "Ionian", "Dorian", "Aeolian", "Major Penta", "Minor Penta",
                                       "Reverse", "AFX001", "AFX002", "AFX003", "AFX004", "AFX005", "AFX006",
                                       "DC001", "DC002", "DC003" };
            if (v < (int) (sizeof (n) / sizeof (n[0]))) return n[v];
            if (v >= 128 && v <= 133) return "User Scale " + juce::String (v - 127);
            if (v >= 134 && v <= 139) return "User Octave " + juce::String (v - 133);
            return "#" + juce::String (v);
        }
        case Disp::UserParam:
            if (userParamType == 1) { auto p = v - 100; return (p > 0 ? "+" : "") + juce::String (p) + "%"; }
            if (userParamType == 2) return juce::String (v + 1);
            return juce::String (v) + "%";
        case Disp::Raw:
        default: return juce::String (v);
    }
}

} // namespace xd
