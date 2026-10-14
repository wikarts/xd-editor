#include "Tabs.h"

using namespace xd;
static juce::String u (const char* s) { return juce::String::fromUTF8 (s); }

SequencerTab::SequencerTab (XDProcessor& p) : proc (p) { proc.addXDListener (this); }
SequencerTab::~SequencerTab() { proc.removeXDListener (this); }

void SequencerTab::paint (juce::Graphics& g)
{
    auto& p = proc.getCurrent();
    auto& t = ParamTable::get();
    auto fmt = [&] (const char* key) { auto& d = *t.byKey (key); return formatValue (d, p.get (d)); };

    auto box = [&] (juce::Rectangle<int> r, const juce::String& title)
    {
        g.setColour (col::panel); g.fillRoundedRectangle (r.toFloat(), 6.0f);
        g.setColour (col::panelEdge); g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 6.0f, 1.0f);
        g.setColour (col::dim); g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (title.toUpperCase(), r.reduced (10, 4).removeFromTop (14), juce::Justification::centredLeft);
    };

    // header
    g.setColour (col::text);
    g.setFont (juce::FontOptions (13.0f));
    juce::String h;
    h << "BPM " << fmt ("seq_bpm") << "    Steps " << fmt ("seq_length") << "    Resolution " << fmt ("seq_resolution")
      << "    Swing " << fmt ("seq_swing") << "%    Default gate " << fmt ("seq_gate")
      << "    Arp " << fmt ("arp_rate") << " / gate " << fmt ("arp_gate");
    g.drawText (h, 16, 4, 1240, 22, juce::Justification::centredLeft);
    g.setColour (col::dim);
    g.setFont (juce::FontOptions (11.5f));
    g.drawText (u ("Podgląd (tylko odczyt) danych sekwencera bieżącego programu."), 16, 4, 1240, 22, juce::Justification::centredRight);

    // ---------------- note sequencer
    auto noteArea = juce::Rectangle<int> (8, 30, 1264, 300);
    box (noteArea, "Note sequencer");
    const int colW = 78, x0 = 18, y0 = 52;
    const int length = p.get (t.indexOf ("seq_length"));
    for (int s = 0; s < 16; ++s)
    {
        auto st = p.step (s);
        const int x = x0 + s * colW;
        const bool inLen = s < length;
        const bool on = p.stepOn (s);
        const bool active = p.stepActive (s);
        juce::Rectangle<int> head (x, y0, colW - 4, 18);
        g.setColour (! inLen ? juce::Colour (0xff1a1b1e) : on ? col::accent.withAlpha (active ? 0.8f : 0.35f) : juce::Colour (0xff2c2f34));
        g.fillRoundedRectangle (head.toFloat(), 3.0f);
        g.setColour (on && inLen ? juce::Colours::black : col::dim);
        g.setFont (XDLook::mono (12.0f).boldened());
        g.drawText (juce::String (s + 1), head, juce::Justification::centred);

        int line = 0;
        g.setFont (XDLook::mono (12.0f));
        for (int n = 0; n < 8; ++n)
        {
            if (st.velocities[n] == 0) continue;
            g.setColour (inLen ? col::lcdText : col::dim);
            juce::String txt = juce::MidiMessage::getMidiNoteName (st.notes[n], true, true, 4);
            txt << " " << st.velocities[n];
            g.drawText (txt, x + 2, y0 + 22 + line * 15, colW - 6, 14, juce::Justification::centredLeft);
            ++line;
        }
        // velocity bar (first note)
        const int barTop = y0 + 150, barH = 100;
        g.setColour (juce::Colour (0xff15171a));
        g.fillRect (x + 6, barTop, colW - 16, barH);
        if (st.velocities[0] > 0)
        {
            const int hgt = st.velocities[0] * barH / 127;
            g.setColour (col::accent2.withAlpha (inLen ? 0.85f : 0.3f));
            g.fillRect (x + 6, barTop + barH - hgt, colW - 16, hgt);
            g.setColour (col::dim);
            g.setFont (juce::FontOptions (10.5f));
            const int gate = st.gates[0];
            g.drawText (gate >= 73 ? juce::String ("TIE") : juce::String (gate * 100 / 72) + "%", x, barTop + barH + 2, colW - 4, 14, juce::Justification::centred);
        }
    }
    g.setColour (col::dim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (u ("Słupki: dynamika pierwszej nuty kroku, pod spodem długość (gate)."), 18, 314, 600, 14, juce::Justification::centredLeft);

    // ---------------- motion sequencer
    auto motionArea = juce::Rectangle<int> (8, 338, 1264, 306);
    box (motionArea, "Motion sequencer");
    for (int lane = 0; lane < 4; ++lane)
    {
        const int y = 360 + lane * 70;
        const int pid = p.motionParam (lane);
        g.setColour (pid == 0 ? col::dim : col::text);
        g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        g.drawText ("Lane " + juce::String (lane + 1), 18, y, 120, 16, juce::Justification::centredLeft);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (motionParamName (pid), 18, y + 16, 120, 16, juce::Justification::centredLeft);
        g.setColour (col::dim);
        g.drawText (juce::String (p.motionSlotOn (lane) ? "ON" : "OFF") + (p.motionSlotSmooth (lane) ? "  smooth" : ""), 18, y + 32, 120, 16, juce::Justification::centredLeft);

        const int gx = 140, gw = (1260 - gx) / 16;
        for (int s = 0; s < 16; ++s)
        {
            auto st = p.step (s);
            juce::Rectangle<int> cell (gx + s * gw, y, gw - 4, 60);
            g.setColour (juce::Colour (0xff15171a));
            g.fillRect (cell);
            if (pid == 0 || ! p.motionStepOn (lane, s)) continue;
            const int bw = (cell.getWidth() - 2) / 5;
            for (int k = 0; k < 5; ++k)
            {
                const int v = st.motion[lane][k];
                const int hgt = v * 58 / 1023;
                g.setColour (col::green.withAlpha (0.8f));
                g.fillRect (cell.getX() + 1 + k * bw, cell.getBottom() - 1 - hgt, bw - 1, hgt);
            }
        }
    }
}
