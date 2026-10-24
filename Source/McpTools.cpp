#include "McpServer.h"
#include "PluginProcessor.h"
#include "Librarian.h"

using namespace xd;

namespace mcp
{
namespace
{
struct Fail { juce::String message; };
using Result = std::function<void (juce::var payload, juce::String error)>;
using Handler = std::function<void (XDProcessor&, const juce::var& args, Result)>;
using SyncHandler = std::function<juce::var (XDProcessor&, const juce::var& args)>;

struct Tool
{
    juce::String name, description;
    juce::var schema;
    Handler run;
};

juce::String u (const char* s) { return juce::String::fromUTF8 (s); }

juce::var obj (std::initializer_list<std::pair<const char*, juce::var>> props)
{
    auto* o = new juce::DynamicObject();
    for (auto& [k, v] : props) o->setProperty (k, v);
    return juce::var (o);
}

void set (juce::var& o, const juce::Identifier& k, const juce::var& v) { o.getDynamicObject()->setProperty (k, v); }

juce::var strings (const juce::StringArray& a)
{
    juce::Array<juce::var> out;
    for (auto& x : a) out.add (x);
    return out;
}

juce::var message (const juce::String& m) { return obj ({ { "message", m } }); }

juce::var type (const char* t, const juce::String& description = {})
{
    auto v = obj ({ { "type", t } });
    if (description.isNotEmpty()) set (v, "description", description);
    return v;
}

juce::var bankProp (const juce::String& description = "disk1, disk2 or synth (the librarian copy of the synth's memory).")
{
    auto v = type ("string", description);
    set (v, "enum", juce::Array<juce::var> ({ "disk1", "disk2", "synth" }));
    return v;
}

juce::var slotProp (const juce::String& description = "Program slot, 1..500.")
{
    auto v = type ("integer", description);
    set (v, "minimum", 1);
    set (v, "maximum", numSlots);
    return v;
}

juce::var slotRef (const juce::String& description)
{
    return obj ({ { "type", "object" }, { "description", description },
                  { "properties", obj ({ { "bank", bankProp() }, { "slot", slotProp() } }) },
                  { "required", juce::Array<juce::var> ({ "bank", "slot" }) } });
}

juce::var schema (std::initializer_list<std::pair<const char*, juce::var>> props = {}, juce::StringArray required = {})
{
    auto s = obj ({ { "type", "object" }, { "properties", obj (props) } });
    if (! required.isEmpty())
    {
        juce::Array<juce::var> r;
        for (auto& k : required) r.add (k);
        set (s, "required", r);
    }
    return s;
}

juce::var need (const juce::var& args, const char* name)
{
    if (! args.hasProperty (name)) throw Fail { u ("Missing argument: ") + name };
    return args[name];
}

bool isNumber (const juce::var& v) { return v.isInt() || v.isInt64() || v.isDouble(); }

int intArg (const juce::var& args, const char* name, int lo, int hi)
{
    const auto v = need (args, name);
    if (! isNumber (v)) throw Fail { juce::String (name) + u (" must be an integer.") };
    const int i = (int) v;
    if (i < lo || i > hi) throw Fail { juce::String (name) + u (" must be between ") + juce::String (lo) + u (" and ") + juce::String (hi) + "." };
    return i;
}

juce::String strArg (const juce::var& args, const char* name)
{
    const auto v = need (args, name);
    if (! v.isString()) throw Fail { juce::String (name) + u (" must be a string.") };
    return v.toString();
}

const char* bankKey (int b) { return b == 0 ? "disk1" : b == 1 ? "disk2" : "synth"; }

int bankArg (const juce::var& args, const char* name)
{
    const auto s = strArg (args, name).toLowerCase();
    for (int b = 0; b < numBanks; ++b)
        if (s == bankKey (b)) return b;
    throw Fail { juce::String (name) + u (" must be disk1, disk2 or synth.") };
}

int slotArg (const juce::var& args, const char* name) { return intArg (args, name, 1, numSlots) - 1; }

Bank& loadedBank (XDProcessor& p, int b)
{
    if (! p.bank (b).loaded) throw Fail { bankTitle ((BankId) b) + u (" is not loaded: load a bank file, init it or receive it from the synth first.") };
    return p.bank (b);
}

juce::File fileArg (const juce::var& args, const char* name)
{
    const auto s = strArg (args, name);
    if (! juce::File::isAbsolutePath (s)) throw Fail { juce::String (name) + u (" must be an absolute path.") };
    return juce::File (s);
}

int sectionArg (const juce::String& name)
{
    juce::StringArray all;
    for (int s = 0; s < numSections; ++s)
    {
        if (sectionName ((Section) s).equalsIgnoreCase (name.trim())) return s;
        all.add (sectionName ((Section) s));
    }
    throw Fail { u ("Unknown section \"") + name + u ("\". Valid: ") + all.joinIntoString (", ") };
}

int valueFrom (const juce::var& v, int lo, int hi, const juce::String& what, std::function<juce::String (int)> display)
{
    int value = 0;
    if (v.isString())
    {
        const auto s = v.toString().trim();
        int match = lo - 1;
        for (int x = lo; x <= hi && match < lo; ++x)
            if (display (x).trim().equalsIgnoreCase (s)) match = x;
        if (match >= lo) return match;
        if (s.isEmpty() || ! s.containsOnly ("-0123456789"))
            throw Fail { what + u (": no value is displayed as \"") + s + u ("\". Use list_params / get_globals to see valid values.") };
        value = s.getIntValue();
    }
    else if (isNumber (v)) value = (int) v;
    else throw Fail { what + u (": the value must be an integer or a displayed value.") };
    if (value < lo || value > hi) throw Fail { what + ": " + juce::String (value) + u (" is outside ") + juce::String (lo) + ".." + juce::String (hi) + "." };
    return value;
}

juce::var paramValue (XDProcessor& p, const ParamDef& d)
{
    return obj ({ { "value", p.getCurrent().get (d) },
                  { "display", formatInContext (d, p.getCurrent(), &p.getSettings().userNames) } });
}

juce::var bankInfo (XDProcessor& p, int b)
{
    auto& bank = p.bank (b);
    return obj ({ { "bank", bankKey (b) }, { "loaded", bank.loaded }, { "modified", bank.dirty },
                  { "file", bank.file.getFullPathName() } });
}

juce::var status (XDProcessor& p)
{
    auto& m = p.getMidi();
    auto& s = p.getSettings();
    juce::Array<juce::var> banks;
    for (int b = 0; b < numBanks; ++b) banks.add (bankInfo (p, b));
    static const char* routes[] = { "device", "daw", "both" };
    return obj ({ { "connected", p.isConnected() }, { "os_version", p.getOsVersion() },
                  { "midi_in", m.hasInput() ? m.inputName() : juce::String() },
                  { "midi_out", m.hasOutput() ? m.outputName() : juce::String() },
                  { "midi_channel", s.channel + 1 }, { "output_route", routes[juce::jlimit (0, 2, s.outputRoute)] },
                  { "transferring", p.isTransferring() }, { "lcd", p.getLcd().message },
                  { "last_transfer_result", p.lastTransferResult },
                  { "program", p.getCurrent().getName() }, { "program_modified", p.getCurrent() != p.getSaved() },
                  { "banks", banks } });
}

juce::var morphState (XDProcessor& p)
{
    auto locks = obj ({});
    for (int s = 0; s < (int) Section::Sequencer; ++s) set (locks, sectionName ((Section) s), p.morphLocks[(size_t) s]);
    juce::var target;
    if (p.hasMorphTarget())
        target = obj ({ { "bank", bankKey (p.libDstBank) }, { "slot", p.libDstSlot + 1 },
                        { "name", p.bank (p.libDstBank).progs[(size_t) p.libDstSlot].getName() } });
    return obj ({ { "amount", juce::roundToInt (p.getMorph() * 100.0) }, { "target", target }, { "locks", locks } });
}

juce::var sequence (XDProcessor& p)
{
    auto& t = ParamTable::get();
    const auto& prog = p.getCurrent();
    auto settingsObj = obj ({});
    for (auto& d : t.all())
        if (d.section == Section::Sequencer) set (settingsObj, d.key, paramValue (p, d));

    auto gateText = [] (int g) { return g >= 73 ? juce::String ("TIE") : juce::String (g * 100 / 72) + "%"; };
    juce::Array<juce::var> steps;
    for (int s = 0; s < 16; ++s)
    {
        const auto st = prog.step (s);
        juce::Array<juce::var> notes;
        for (int n = 0; n < 8; ++n)
            if (st.velocities[n] > 0)
                notes.add (obj ({ { "note", juce::MidiMessage::getMidiNoteName (st.notes[n], true, true, 4) },
                                  { "midi_note", st.notes[n] }, { "velocity", st.velocities[n] },
                                  { "gate", gateText (st.gates[n]) }, { "trigger", st.triggers[n] } }));
        steps.add (obj ({ { "step", s + 1 }, { "on", prog.stepOn (s) }, { "active", prog.stepActive (s) },
                          { "motion_on", prog.stepMotionOn (s) }, { "notes", notes } }));
    }

    juce::Array<juce::var> lanes;
    for (int lane = 0; lane < 4; ++lane)
    {
        const int pid = prog.motionParam (lane);
        juce::Array<juce::var> values;
        for (int s = 0; s < 16; ++s)
        {
            const auto st = prog.step (s);
            juce::Array<juce::var> v;
            for (int k = 0; k < 5; ++k) v.add (st.motion[lane][k]);
            values.add (obj ({ { "step", s + 1 }, { "on", prog.motionStepOn (lane, s) }, { "values", v } }));
        }
        lanes.add (obj ({ { "lane", lane + 1 }, { "parameter", pid == 0 ? juce::String() : motionParamName (pid) },
                          { "on", prog.motionSlotOn (lane) }, { "smooth", prog.motionSlotSmooth (lane) },
                          { "steps", pid == 0 ? juce::var() : juce::var (values) } }));
    }
    return obj ({ { "settings", settingsObj }, { "steps", steps }, { "motion", lanes } });
}

const GlobalField& globalField (const juce::String& name)
{
    for (auto& f : globalFields())
        if (name.trim().equalsIgnoreCase (f.name)) return f;
    throw Fail { u ("Unknown global parameter \"") + name + u ("\". Use get_globals to see the names.") };
}

void requireGlobals (XDProcessor& p)
{
    if (! p.hasGlobals()) throw Fail { u ("The global parameters have not been received yet: call request_globals first.") };
}

juce::var globalsView (XDProcessor& p)
{
    auto out = obj ({});
    for (auto& f : globalFields())
    {
        const int v = p.getGlobal (f);
        juce::Array<juce::var> choices;
        for (int x = f.minV; x <= f.maxV; ++x) choices.add (formatGlobal (f, x));
        set (out, f.name, obj ({ { "value", v }, { "display", formatGlobal (f, v) }, { "choices", choices } }));
    }
    return out;
}

Handler sync (SyncHandler h)
{
    return [h] (XDProcessor& p, const juce::var& args, Result done) { done (h (p, args), {}); };
}

void requireLink (XDProcessor& p)
{
    if (! p.canTalkToSynth()) throw Fail { u ("No connection to the synth (check the MIDI settings).") };
}

void answer (Result done, bool ok, const juce::String& text)
{
    if (ok) done (message (text), {});
    else done ({}, text);
}

void startTransfer (XDProcessor& p, std::function<void (XDProcessor::Done)> start, Result done, const juce::String& started)
{
    if (p.isTransferring()) throw Fail { u ("Another transfer is in progress.") };
    start ([&p] (bool, juce::String m) { p.lastTransferResult = m; p.lcdMessage (m, 5000); });
    if (! p.isTransferring()) throw Fail { p.lastTransferResult.isNotEmpty() ? p.lastTransferResult : u ("The transfer could not start.") };
    p.lastTransferResult.clear();
    done (message (started + u (" Follow progress with get_status (transferring, lcd, last_transfer_result).")), {});
}

juce::String loadToPanel (XDProcessor& p, const Program& prog, const AddInfo& info)
{
    const bool send = p.canTalkToSynth();
    p.loadProgram (prog, info, send);
    return u ("Loaded \"") + prog.getName() + (send ? u ("\" and sent to the synth's edit buffer.") : u ("\" into the panel (no connection to the synth, NOT sent)."));
}

juce::File programFile (const juce::var& args)
{
    auto f = fileArg (args, "path");
    if (! f.hasFileExtension (".syx;.mnlgxdprog")) throw Fail { u ("path must end with .syx or .mnlgxdprog.") };
    return f;
}

IoResult saveProgram (XDProcessor& p, const juce::File& f, const Program& prog, const AddInfo& info)
{
    return f.hasFileExtension (".mnlgxdprog") ? files::saveProgramXdProg (f, prog, info)
                                              : files::saveProgramSyx (f, prog, info, p.getSettings().channel);
}

juce::var slotPairSchema()
{
    return schema ({ { "src_bank", bankProp() }, { "src_slot", slotProp() }, { "dst_bank", bankProp() }, { "dst_slot", slotProp() } },
                   { "src_bank", "src_slot", "dst_bank", "dst_slot" });
}

Handler slotPairAction (juce::String (*action) (XDProcessor&, int, int, int, int))
{
    return sync ([action] (XDProcessor& p, const juce::var& args)
    {
        const int sb = bankArg (args, "src_bank"), ss = slotArg (args, "src_slot"), db = bankArg (args, "dst_bank"), ds = slotArg (args, "dst_slot");
        loadedBank (p, sb);
        loadedBank (p, db);
        return message (action (p, sb, ss, db, ds));
    });
}

juce::var ioResult (const IoResult& r, const juce::String& okText)
{
    if (! r.ok) throw Fail { r.message };
    return message (okText);
}

const std::vector<Tool>& tools()
{
    static const std::vector<Tool> list = {
        { "get_status", u ("Connection to the minilogue xd, MIDI ports, transfer progress, current program name and the state of the three librarian banks."),
          schema(), sync ([] (XDProcessor& p, const juce::var&) { return status (p); }) },

        { "device_inquiry", u ("Sends a MIDI Device Inquiry to (re)detect the synth. The reply arrives asynchronously: call get_status about 2 seconds later."),
          schema(), sync ([] (XDProcessor& p, const juce::var&)
          {
              p.deviceInquiry();
              return message (u ("Device Inquiry sent. Call get_status in about 2 seconds."));
          }) },

        { "list_params", u ("Lists the program parameters: key, name, section, raw range, default, whether it is sent in real time (CC/NRPN) or via a program dump, labels and description."),
          schema ({ { "section", type ("string", u ("Only this section (e.g. \"Filter\", \"VCO 1\", \"Sequencer\").")) } }),
          sync ([] (XDProcessor& p, const juce::var& args)
          {
              const int only = args.hasProperty ("section") ? sectionArg (strArg (args, "section")) : -1;
              juce::Array<juce::var> out;
              for (auto& d : ParamTable::get().all())
              {
                  if (only >= 0 && (int) d.section != only) continue;
                  auto e = obj ({ { "key", d.key }, { "name", d.name }, { "section", sectionName (d.section) },
                                  { "min", d.minV }, { "max", d.maxV }, { "default", d.defV }, { "realtime", d.isRealtime() } });
                  auto labels = labelsFor (d, &p.getSettings().userNames);
                  if (! labels.isEmpty()) set (e, "labels", strings (labels));
                  if (d.desc.isNotEmpty()) set (e, "description", d.desc);
                  out.add (e);
              }
              return juce::var (out);
          }) },

        { "get_program", u ("The program in the panel: name, author, description, category, whether it differs from the saved version, and every parameter as {value, display}."),
          schema ({ { "section", type ("string", u ("Only this section's parameters.")) } }),
          sync ([] (XDProcessor& p, const juce::var& args)
          {
              const int only = args.hasProperty ("section") ? sectionArg (strArg (args, "section")) : -1;
              auto params = obj ({});
              for (auto& d : ParamTable::get().all())
                  if (only < 0 || (int) d.section == only) set (params, d.key, paramValue (p, d));
              auto& info = p.currentInfo();
              return obj ({ { "name", p.getCurrent().getName() }, { "author", info.author }, { "description", info.description },
                            { "category", info.category }, { "saved_date", info.date }, { "modified", p.getCurrent() != p.getSaved() },
                            { "params", params } });
          }) },

        { "set_params", u ("Changes program parameters in the panel and sends them to the synth (CC/NRPN, or a program dump for parameters without CC). "
                           "Values: an integer is a raw value within the parameter's range; a string matches the displayed value or label (e.g. \"SAW\", \"+12 cent\"). "
                           "All values are validated before anything changes."),
          schema ({ { "values", type ("object", u ("Map of parameter key to value, e.g. {\"cutoff\": 700, \"vco1_wave\": \"SAW\"}.")) } }, { "values" }),
          sync ([] (XDProcessor& p, const juce::var& args)
          {
              auto* values = need (args, "values").getDynamicObject();
              if (values == nullptr) throw Fail { u ("values must be an object.") };
              auto& t = ParamTable::get();
              std::vector<std::pair<int, int>> changes;
              for (auto& prop : values->getProperties())
              {
                  const auto* d = t.byKey (prop.name.toString());
                  if (d == nullptr) throw Fail { u ("Unknown parameter: ") + prop.name.toString() + u (". Use list_params.") };
                  if (! d->isRealtime() && p.getSettings().nonCcMode != 0)
                      throw Fail { d->key + u (": parameters without CC are disabled in Settings (Green params).") };
                  changes.emplace_back (d->index, valueFrom (prop.value, d->minV, d->maxV, d->key, [&p, d] (int x)
                  {
                      return formatInContext (*d, p.getCurrent(), &p.getSettings().userNames, x);
                  }));
              }
              auto out = obj ({});
              for (auto [pi, v] : changes)
              {
                  p.beginGesture (pi);
                  p.setValueFromUI (pi, v);
                  p.endGesture (pi);
              }
              for (auto [pi, v] : changes) set (out, t[pi].key, paramValue (p, t[pi]));
              return out;
          }) },

        { "rename_program", u ("Renames the program in the panel (max. 12 ASCII characters) and sends the name to the synth."),
          schema ({ { "name", type ("string") } }, { "name" }),
          sync ([] (XDProcessor& p, const juce::var& args)
          {
              const auto name = strArg (args, "name");
              if (name.length() > 12) throw Fail { u ("The name is limited to 12 characters.") };
              for (auto c : name)
                  if (c < 32 || c > 126) throw Fail { u ("The name must use printable ASCII characters only.") };
              p.renameCurrent (name);
              return message (u ("Renamed to \"") + p.getCurrent().getName() + "\".");
          }) },

        { "set_program_info", u ("Edits the librarian information of the program in the panel: author (max. 15 characters), description, category."),
          schema ({ { "author", type ("string") }, { "description", type ("string") },
                    { "category", type ("string", u ("One of: ") + categories().joinIntoString (", ") + u (", or empty to clear.")) } }),
          sync ([] (XDProcessor& p, const juce::var& args)
          {
              auto info = p.currentInfo();
              if (args.hasProperty ("author"))
              {
                  info.author = strArg (args, "author").trim();
                  if (info.author.length() > 15) throw Fail { u ("The author is limited to 15 characters.") };
              }
              if (args.hasProperty ("description")) info.description = strArg (args, "description");
              if (args.hasProperty ("category"))
              {
                  const auto c = strArg (args, "category").trim();
                  const int i = categories().indexOf (c, true);
                  if (c.isNotEmpty() && i < 0) throw Fail { u ("Unknown category. Valid: ") + categories().joinIntoString (", ") };
                  info.category = i < 0 ? juce::String() : categories()[i];
              }
              p.currentInfo() = info;
              p.infoEdited();
              return message (u ("Program information updated."));
          }) },

        { "init_program", u ("Replaces the panel with the Init program (keeps the author) and sends it to the synth. Unsaved changes are lost."),
          schema(), sync ([] (XDProcessor& p, const juce::var&)
          {
              AddInfo info;
              info.author = p.currentInfo().author;
              return message (loadToPanel (p, Program(), info));
          }) },

        { "send_program", u ("Sends the program in the panel to the synth's edit buffer (full program dump)."),
          schema(), sync ([] (XDProcessor& p, const juce::var&)
          {
              if (! p.sendCurrentToSynth()) throw Fail { u ("No connection to the synth.") };
              return message (u ("Program sent to the edit buffer."));
          }) },

        { "receive_program", u ("Receives a program from the synth into the panel: the edit buffer, or a memory slot when slot is given. Unsaved changes in the panel are replaced."),
          schema ({ { "slot", slotProp (u ("Synth memory slot 1..500. Omit for the edit buffer.")) } }),
          [] (XDProcessor& p, const juce::var& args, Result done)
          {
              const int slot = args.hasProperty ("slot") ? slotArg (args, "slot") : -1;
              requireLink (p);
              auto reply = [done] (bool ok, juce::String m) { answer (done, ok, ok ? u ("Received: ") + m : m); };
              if (slot < 0) p.requestCurrentProgram (reply);
              else p.requestSlotToPanel (slot, reply);
          } },

        { "get_sequence", u ("The current program's step sequencer (read-only, as in the editor): sequencer settings, the 16 steps with their notes, and the 4 motion lanes."),
          schema(), sync ([] (XDProcessor& p, const juce::var&) { return sequence (p); }) },

        { "get_globals", u ("The synth's global parameters as last received, with their allowed values. Call request_globals first."),
          schema(), sync ([] (XDProcessor& p, const juce::var&)
          {
              requireGlobals (p);
              return globalsView (p);
          }) },

        { "set_globals", u ("Edits global parameters in the editor (not sent until send_globals). Values: raw integer or displayed value (e.g. {\"Knob Mode\": \"Catch\"})."),
          schema ({ { "values", type ("object", u ("Map of global parameter name to value.")) } }, { "values" }),
          sync ([] (XDProcessor& p, const juce::var& args)
          {
              requireGlobals (p);
              auto* values = need (args, "values").getDynamicObject();
              if (values == nullptr) throw Fail { u ("values must be an object.") };
              std::vector<std::pair<const GlobalField*, int>> changes;
              for (auto& prop : values->getProperties())
              {
                  auto& f = globalField (prop.name.toString());
                  changes.emplace_back (&f, valueFrom (prop.value, f.minV, f.maxV, f.name, [&f] (int x) { return formatGlobal (f, x); }));
              }
              for (auto [f, v] : changes) p.setGlobal (*f, v);
              return message (juce::String ((int) changes.size()) + u (" global parameter(s) changed. Call send_globals to write them to the synth."));
          }) },

        { "request_globals", u ("Receives the global parameters from the synth."),
          schema(), [] (XDProcessor& p, const juce::var&, Result done)
          {
              requireLink (p);
              p.requestGlobals ([done] (bool ok, juce::String m) { answer (done, ok, ok ? u ("Global parameters received.") : m); });
          } },

        { "send_globals", u ("Sends the edited global parameters to the synth."),
          schema(), sync ([] (XDProcessor& p, const juce::var&)
          {
              requireGlobals (p);
              if (! p.sendGlobals()) throw Fail { u ("No connection to the synth.") };
              return message (u ("Global parameters sent."));
          }) },

        { "get_morph", u ("Morph state: amount (0..100), target program, and section locks."),
          schema(), sync ([] (XDProcessor& p, const juce::var&) { return morphState (p); }) },

        { "set_morph", u ("Morphs the panel from the current program towards a target program in a bank. "
                          "Setting a target resets the morph start to the current program. Locked sections are not changed unless ignore_locks is true."),
          schema ({ { "target", slotRef (u ("Morph target (the librarian destination).")) },
                    { "locks", type ("object", u ("Map of section name to true (locked) / false, e.g. {\"Filter\": true}.")) },
                    { "amount", obj ({ { "type", "number" }, { "minimum", 0 }, { "maximum", 100 }, { "description", u ("Morph amount in percent.") } }) },
                    { "ignore_locks", type ("boolean", u ("With amount 0 or 100: morph every section, like the Morph 0% / 100% buttons.")) } }),
          sync ([] (XDProcessor& p, const juce::var& args)
          {
              int tb = -1, ts = -1;
              if (args.hasProperty ("target"))
              {
                  const auto target = args["target"];
                  tb = bankArg (target, "bank");
                  ts = slotArg (target, "slot");
                  loadedBank (p, tb);
              }
              std::vector<std::pair<int, bool>> locks;
              if (args.hasProperty ("locks"))
              {
                  auto* l = args["locks"].getDynamicObject();
                  if (l == nullptr) throw Fail { u ("locks must be an object.") };
                  for (auto& prop : l->getProperties())
                  {
                      const int s = sectionArg (prop.name.toString());
                      if (s >= (int) Section::Sequencer) throw Fail { sectionName ((Section) s) + u (" has no morph lock.") };
                      locks.emplace_back (s, (bool) prop.value);
                  }
              }
              double amount = -1.0;
              if (args.hasProperty ("amount"))
              {
                  if (! isNumber (args["amount"])) throw Fail { u ("amount must be a number.") };
                  amount = juce::jlimit (0.0, 100.0, (double) args["amount"]);
              }
              const bool ignoreLocks = args["ignore_locks"];
              if (ignoreLocks && amount != 0.0 && amount != 100.0) throw Fail { u ("ignore_locks only applies to amount 0 or 100, like the Morph 0% / 100% buttons.") };
              if (amount >= 0.0 && tb < 0 && ! p.hasMorphTarget()) throw Fail { u ("No morph target: set target first.") };

              if (tb >= 0) p.setLibDestination (tb, ts);
              for (auto [s, on] : locks) p.morphLocks[(size_t) s] = on;
              if (! locks.empty()) p.uiNotify (XDProcessor::cMorph);
              if (amount >= 0.0) p.setMorph (amount / 100.0, false, ignoreLocks);
              return morphState (p);
          }) },

        { "list_bank", u ("Lists the programs of a librarian bank (slot, name, category, author)."),
          schema ({ { "bank", bankProp() } }, { "bank" }),
          sync ([] (XDProcessor& p, const juce::var& args)
          {
              const int b = bankArg (args, "bank");
              auto out = bankInfo (p, b);
              auto& bank = p.bank (b);
              if (! bank.loaded) return out;
              juce::Array<juce::var> progs;
              for (int s = 0; s < numSlots; ++s)
              {
                  auto e = obj ({ { "slot", s + 1 }, { "name", bank.progs[(size_t) s].getName() } });
                  auto& info = bank.info[(size_t) s];
                  if (info.category.isNotEmpty()) set (e, "category", info.category);
                  if (info.author.isNotEmpty()) set (e, "author", info.author);
                  progs.add (e);
              }
              set (out, "programs", progs);
              return out;
          }) },

        { "load_slot", u ("Loads a program from a librarian bank into the panel and the synth's edit buffer. Unsaved changes in the panel are replaced."),
          schema ({ { "bank", bankProp() }, { "slot", slotProp() } }, { "bank", "slot" }),
          sync ([] (XDProcessor& p, const juce::var& args)
          {
              const int b = bankArg (args, "bank"), s = slotArg (args, "slot");
              auto& bank = loadedBank (p, b);
              return message (loadToPanel (p, bank.progs[(size_t) s], bank.info[(size_t) s]));
          }) },

        { "store_to_slot", u ("Saves the program in the panel into a bank slot (replacing it). In the synth bank this also writes the synth's memory."),
          schema ({ { "bank", bankProp() }, { "slot", slotProp() } }, { "bank", "slot" }),
          sync ([] (XDProcessor& p, const juce::var& args)
          {
              const int b = bankArg (args, "bank"), s = slotArg (args, "slot");
              loadedBank (p, b);
              p.currentInfo().date = juce::Time::getCurrentTime().toISO8601 (true);
              auto m = lib::putProgram (p, b, s, p.getCurrent(), p.currentInfo());
              p.markCurrentSaved();
              return message (m + u (" -> ") + slotTitle (b, s));
          }) },

        { "receive_buffer_to_slot", u ("Receives the synth's edit buffer and stores it in a bank slot."),
          schema ({ { "bank", bankProp() }, { "slot", slotProp() } }, { "bank", "slot" }),
          [] (XDProcessor& p, const juce::var& args, Result done)
          {
              const int b = bankArg (args, "bank"), s = slotArg (args, "slot");
              loadedBank (p, b);
              requireLink (p);
              p.requestBufferToBank ((BankId) b, s, [done] (bool ok, juce::String m) { answer (done, ok, ok ? u ("Saved: ") + m : m); });
          } },

        { "copy", u ("Copies one or more programs between bank slots. Several source slots are copied either as-is (same spacing) or collated (one after another)."),
          schema ({ { "src_bank", bankProp() },
                    { "src_slots", obj ({ { "type", "array" }, { "items", slotProp() }, { "minItems", 1 } }) },
                    { "dst_bank", bankProp() }, { "dst_slot", slotProp (u ("First destination slot.")) },
                    { "collated", type ("boolean", u ("Default true.")) } },
                  { "src_bank", "src_slots", "dst_bank", "dst_slot" }),
          sync ([] (XDProcessor& p, const juce::var& args)
          {
              const int sb = bankArg (args, "src_bank"), db = bankArg (args, "dst_bank"), ds = slotArg (args, "dst_slot");
              loadedBank (p, sb);
              loadedBank (p, db);
              const auto* src = need (args, "src_slots").getArray();
              if (src == nullptr || src->isEmpty()) throw Fail { u ("src_slots must be a non-empty array.") };
              juce::Array<int> slots;
              for (auto& v : *src)
              {
                  if (! isNumber (v) || (int) v < 1 || (int) v > numSlots) throw Fail { u ("src_slots must contain slots 1..500.") };
                  slots.addIfNotAlreadyThere ((int) v - 1);
              }
              const bool collated = args.hasProperty ("collated") ? (bool) args["collated"] : true;
              return message (lib::copy (p, sb, slots, db, ds, collated));
          }) },

        { "move", u ("Moves a program to another slot; the source becomes the Init program."), slotPairSchema(), slotPairAction (lib::move) },

        { "swap", u ("Swaps two programs."), slotPairSchema(), slotPairAction (lib::swap) },

        { "init_slot", u ("Puts the Init program into a bank slot."),
          schema ({ { "bank", bankProp() }, { "slot", slotProp() } }, { "bank", "slot" }),
          sync ([] (XDProcessor& p, const juce::var& args)
          {
              const int b = bankArg (args, "bank"), s = slotArg (args, "slot");
              loadedBank (p, b);
              return message (lib::initSlot (p, b, s));
          }) },

        { "init_bank", u ("Creates an empty bank (500 x Init). For the synth bank nothing is sent to the synth."),
          schema ({ { "bank", bankProp() } }, { "bank" }),
          sync ([] (XDProcessor& p, const juce::var& args)
          {
              const int b = bankArg (args, "bank");
              p.initBank ((BankId) b);
              return message (bankTitle ((BankId) b) + u (": 500 Init programs."));
          }) },

        { "clear_bank", u ("Closes a bank (unsaved changes are lost)."),
          schema ({ { "bank", bankProp() } }, { "bank" }),
          sync ([] (XDProcessor& p, const juce::var& args)
          {
              const int b = bankArg (args, "bank");
              p.clearBank ((BankId) b);
              return message (bankTitle ((BankId) b) + u (" closed."));
          }) },

        { "receive_bank", u ("Receives all 500 programs from the synth into the synth bank (takes about 500 x the transfer speed). Runs in the background."),
          schema(), [] (XDProcessor& p, const juce::var&, Result done)
          {
              startTransfer (p, [&p] (XDProcessor::Done d) { p.startReceiveBank (d); }, done, u ("Receiving 500 programs."));
          } },

        { "send_bank", u ("Sends a whole bank to the synth: ALL 500 programs in the synth's memory are overwritten. Runs in the background."),
          schema ({ { "bank", bankProp() } }, { "bank" }),
          [] (XDProcessor& p, const juce::var& args, Result done)
          {
              const int b = bankArg (args, "bank");
              loadedBank (p, b);
              startTransfer (p, [&p, b] (XDProcessor::Done d) { p.startSendBank ((BankId) b, d); }, done, u ("Sending 500 programs."));
          } },

        { "cancel_transfer", u ("Stops a running bank transfer or USER name loading."),
          schema(), sync ([] (XDProcessor& p, const juce::var&)
          {
              if (! p.isTransferring()) throw Fail { u ("No transfer in progress.") };
              p.cancelTransfer();
              return message (u ("Transfer stopped."));
          }) },

        { "load_program_file", u ("Loads a program file (.syx, .mnlgxdprog, .prog_bin, first program of a .mnlgxdlib) into the panel (and the synth's edit buffer), or into a bank slot when bank and slot are given."),
          schema ({ { "path", type ("string", u ("Absolute path.")) }, { "bank", bankProp() }, { "slot", slotProp() } }, { "path" }),
          sync ([] (XDProcessor& p, const juce::var& args)
          {
              const auto f = fileArg (args, "path");
              const bool toSlot = args.hasProperty ("bank");
              const int b = toSlot ? bankArg (args, "bank") : -1, s = toSlot ? slotArg (args, "slot") : -1;
              if (toSlot) loadedBank (p, b);
              if (toSlot && f.hasFileExtension (".mnlgxdlib")) throw Fail { u ("A .mnlgxdlib can only be loaded into the panel or as a bank (load_bank_file).") };
              Program prog;
              AddInfo info;
              auto r = files::loadProgram (f, prog, info);
              if (! r.ok) throw Fail { r.message };
              p.getSettings().lastProgramDir = f.getParentDirectory().getFullPathName();
              p.getSettings().save();
              if (toSlot) return message (lib::putProgram (p, b, s, prog, info) + u (" -> ") + slotTitle (b, s));
              return message (loadToPanel (p, prog, info));
          }) },

        { "save_program_file", u ("Saves the panel's program (or a bank slot when bank and slot are given) to a .syx or .mnlgxdprog file. "
                                   "For the panel, a .mnlgxdprog copy is also written next to the .syx when the save mode in Settings is \"Both\"."),
          schema ({ { "path", type ("string", u ("Absolute path ending in .syx or .mnlgxdprog.")) }, { "bank", bankProp() }, { "slot", slotProp() } }, { "path" }),
          sync ([] (XDProcessor& p, const juce::var& args)
          {
              const auto f = programFile (args);
              if (args.hasProperty ("bank"))
              {
                  const int b = bankArg (args, "bank"), s = slotArg (args, "slot");
                  auto& bank = loadedBank (p, b);
                  return ioResult (saveProgram (p, f, bank.progs[(size_t) s], bank.info[(size_t) s]), u ("Saved: ") + f.getFullPathName());
              }
              auto& st = p.getSettings();
              p.currentInfo().date = juce::Time::getCurrentTime().toISO8601 (true);
              auto r = saveProgram (p, f, p.getCurrent(), p.currentInfo());
              if (r.ok && f.hasFileExtension (".syx") && st.saveMode == 2)
                  r = files::saveProgramXdProg (f.withFileExtension (".mnlgxdprog"), p.getCurrent(), p.currentInfo());
              auto out = ioResult (r, u ("Saved: ") + f.getFullPathName());
              st.lastProgramDir = f.getParentDirectory().getFullPathName();
              st.lastSysexName = f.getFileName();
              st.save();
              p.markCurrentSaved();
              return out;
          }) },

        { "load_bank_file", u ("Loads a bank file (.syx or .mnlgxdlib) into a bank. Loading into the synth bank also sends all 500 programs to the synth, overwriting its memory."),
          schema ({ { "bank", bankProp() }, { "path", type ("string", u ("Absolute path.")) } }, { "bank", "path" }),
          [] (XDProcessor& p, const juce::var& args, Result done)
          {
              const int b = bankArg (args, "bank");
              auto r = p.loadBankFile ((BankId) b, fileArg (args, "path"));
              if (! r.ok) throw Fail { r.message };
              if (b == (int) BankId::Synth && p.canTalkToSynth())
                  startTransfer (p, [&p] (XDProcessor::Done d) { p.startSendBank (BankId::Synth, d); }, done, r.message + u (" Sending 500 programs to the synth."));
              else done (message (r.message), {});
          } },

        { "save_bank_file", u ("Saves a bank to a .syx or .mnlgxdlib (Korg Librarian) file."),
          schema ({ { "bank", bankProp() }, { "path", type ("string", u ("Absolute path ending in .syx or .mnlgxdlib.")) } }, { "bank", "path" }),
          sync ([] (XDProcessor& p, const juce::var& args)
          {
              const int b = bankArg (args, "bank");
              const auto f = fileArg (args, "path");
              if (! f.hasFileExtension (".syx;.mnlgxdlib")) throw Fail { u ("path must end with .syx or .mnlgxdlib.") };
              loadedBank (p, b);
              return ioResult (p.saveBankFile ((BankId) b, f), u ("Bank saved: ") + f.getFullPathName());
          }) },

        { "export_bank_list", u ("Exports a bank's program list to a text file."),
          schema ({ { "bank", bankProp() }, { "path", type ("string", u ("Absolute path of the .txt file.")) } }, { "bank", "path" }),
          sync ([] (XDProcessor& p, const juce::var& args)
          {
              const int b = bankArg (args, "bank");
              const auto f = fileArg (args, "path").withFileExtension (".txt");
              return ioResult (files::exportBankList (f, loadedBank (p, b), bankTitle ((BankId) b)), u ("List saved: ") + f.getFullPathName());
          }) },

        { "get_report", u ("Program information as text. Kinds: current, saved, diff_init (current vs Init), compare_saved, diff_saved (current vs saved), "
                           "compare_slots, diff_slots (two bank programs given as a and b)."),
          schema ({ { "kind", obj ({ { "type", "string" }, { "enum", juce::Array<juce::var> ({ "current", "saved", "diff_init", "compare_saved", "diff_saved", "compare_slots", "diff_slots" }) } }) },
                    { "a", slotRef (u ("First program, for compare_slots / diff_slots.")) },
                    { "b", slotRef (u ("Second program, for compare_slots / diff_slots.")) } },
                  { "kind" }),
          sync ([] (XDProcessor& p, const juce::var& args)
          {
              const auto kind = strArg (args, "kind");
              const juce::StringArray simple { "current", "saved", "diff_init", "compare_saved", "diff_saved" };
              if (simple.contains (kind)) return obj ({ { "report", p.buildInfoReport ((ReportKind) simple.indexOf (kind)) } });
              if (kind != "compare_slots" && kind != "diff_slots") throw Fail { u ("Unknown kind.") };
              const auto a = need (args, "a"), b = need (args, "b");
              const int ab = bankArg (a, "bank"), as = slotArg (a, "slot"), bb = bankArg (b, "bank"), bs = slotArg (b, "slot");
              auto& A = loadedBank (p, ab);
              auto& B = loadedBank (p, bb);
              return obj ({ { "report", buildReport (A.progs[(size_t) as], A.info[(size_t) as], slotTitle (ab, as),
                                                     &B.progs[(size_t) bs], &B.info[(size_t) bs], slotTitle (bb, bs),
                                                     kind == "diff_slots", p.getSettings().userNames) } });
          }) },

        { "load_user_names", u ("Reads the names of the USER oscillators and effects from the synth so they appear in parameter values. Runs in the background."),
          schema(), [] (XDProcessor& p, const juce::var&, Result done)
          {
              startTransfer (p, [&p] (XDProcessor::Done d) { p.loadUserNames (d); }, done, u ("Loading USER module names."));
          } },

        { "reset_user_names", u ("Resets the USER module names to USER1, USER2..."),
          schema(), sync ([] (XDProcessor& p, const juce::var&)
          {
              p.resetUserNames();
              return message (u ("USER names reset."));
          }) },
    };
    return list;
}

const char* instructions =
    "Controls XD Editor, the editor and librarian for the Korg minilogue xd. The panel holds the current program; "
    "set_params edits it and sends each change to the synth in real time. Banks: disk1, disk2 (files) and synth "
    "(the librarian copy of the synth's memory: writing to it also writes the synth). Slots are numbered 1..500. "
    "Parameter values are raw integers within the ranges from list_params, or strings matching the displayed value.";

juce::var rpcResult (const juce::var& id, const juce::var& result) { return obj ({ { "jsonrpc", "2.0" }, { "id", id }, { "result", result } }); }

}

juce::var error (const juce::var& id, int code, const juce::String& m)
{
    return obj ({ { "jsonrpc", "2.0" }, { "id", id }, { "error", obj ({ { "code", code }, { "message", m } }) } });
}

void handle (XDProcessor& p, const juce::var& msg, Reply reply)
{
    if (! msg.hasProperty ("id")) { reply ({}); return; }
    const auto id = msg["id"];
    const auto method = msg["method"].toString();
    const auto params = msg["params"];

    if (method == "initialize")
    {
        const juce::StringArray known { "2024-11-05", "2025-03-26", "2025-06-18", "2025-11-25" };
        const auto requested = params["protocolVersion"].toString();
        reply (rpcResult (id, obj ({ { "protocolVersion", known.contains (requested) ? requested : known[known.size() - 1] },
                                     { "capabilities", obj ({ { "tools", obj ({}) } }) },
                                     { "serverInfo", obj ({ { "name", "xd-editor" }, { "version", "1.0.0" } }) },
                                     { "instructions", u (instructions) } })));
        return;
    }
    if (method == "ping") { reply (rpcResult (id, obj ({}))); return; }
    if (method == "tools/list")
    {
        juce::Array<juce::var> list;
        for (auto& t : tools()) list.add (obj ({ { "name", t.name }, { "description", t.description }, { "inputSchema", t.schema } }));
        reply (rpcResult (id, obj ({ { "tools", list } })));
        return;
    }
    if (method == "tools/call")
    {
        const auto name = params["name"].toString();
        for (auto& t : tools())
        {
            if (t.name != name) continue;
            auto finish = [reply, id] (juce::var payload, juce::String error)
            {
                const bool isError = error.isNotEmpty();
                const auto text = isError ? error : juce::JSON::toString (payload, true);
                reply (rpcResult (id, obj ({ { "content", juce::Array<juce::var> ({ obj ({ { "type", "text" }, { "text", text } }) }) },
                                             { "isError", isError } })));
            };
            try { t.run (p, params["arguments"], finish); }
            catch (const Fail& f) { finish ({}, f.message); }
            return;
        }
        reply (error (id, -32602, "Unknown tool: " + name));
        return;
    }
    reply (error (id, -32601, "Method not found: " + method));
}
}
