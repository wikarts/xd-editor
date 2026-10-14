#include "Bank.h"
#include "SysEx.h"

namespace xd
{
juce::String bankTitle (BankId b)
{
    switch (b)
    {
        case BankId::Disk1: return "Disk 1";
        case BankId::Disk2: return "Disk 2";
        case BankId::Synth: return "Minilogue XD";
    }
    return {};
}

void Bank::clear()
{
    for (auto& p : progs) p.initialise();
    for (auto& i : info) i = {};
    file = juce::File();
    loaded = false;
    dirty = false;
    highlighted.clear();
}

void Bank::initAll()
{
    clear();
    loaded = true;
}

namespace files
{
static juce::String nowIso() { return juce::Time::getCurrentTime().toISO8601 (true); }

juce::File addInfoFileFor (const juce::File& f)
{
    return f.getSiblingFile (f.getFileNameWithoutExtension() + "_AddInfo.xml");
}

static void writeSingleAddInfo (const juce::File& f, const AddInfo& a)
{
    juce::ValueTree t ("XDAddInfo");
    auto e = a.toTree();
    e.setProperty ("slot", 0, nullptr);
    t.appendChild (e, nullptr);
    if (auto xml = t.createXml())
        xml->writeTo (addInfoFileFor (f));
}

static AddInfo readSingleAddInfo (const juce::File& f)
{
    auto xf = addInfoFileFor (f);
    if (auto xml = juce::XmlDocument::parse (xf))
    {
        auto t = juce::ValueTree::fromXml (*xml);
        if (t.getNumChildren() > 0) return AddInfo::fromTree (t.getChild (0));
    }
    return {};
}

void saveAddInfoBank (const juce::File& bankFile, const Bank& b)
{
    juce::ValueTree t ("XDAddInfo");
    for (int i = 0; i < numSlots; ++i)
    {
        if (b.info[(size_t) i].isEmpty()) continue;
        auto e = b.info[(size_t) i].toTree();
        e.setProperty ("slot", i, nullptr);
        t.appendChild (e, nullptr);
    }
    if (auto xml = t.createXml())
        xml->writeTo (addInfoFileFor (bankFile));
}

void loadAddInfoBank (const juce::File& bankFile, Bank& b)
{
    if (auto xml = juce::XmlDocument::parse (addInfoFileFor (bankFile)))
    {
        auto t = juce::ValueTree::fromXml (*xml);
        for (auto c : t)
        {
            int slot = c.getProperty ("slot", -1);
            if (juce::isPositiveAndBelow (slot, numSlots))
                b.info[(size_t) slot] = AddInfo::fromTree (c);
        }
    }
}

// ------------------------------------------------------------------------------------------
static juce::String xmlText (const juce::XmlElement* root, const char* tag)
{
    if (root == nullptr) return {};
    if (auto* e = root->getChildByName (tag)) return e->getAllSubText().trim();
    return {};
}

static bool readZipEntry (juce::ZipFile& zip, const juce::String& name, juce::MemoryBlock& out)
{
    auto idx = zip.getIndexOfFileName (name, true);
    if (idx < 0) return false;
    std::unique_ptr<juce::InputStream> s (zip.createStreamForEntry (idx));
    if (s == nullptr) return false;
    out.reset();
    s->readIntoMemoryBlock (out);
    return true;
}

static void readProgInfo (juce::ZipFile& zip, const juce::String& infoName, AddInfo& a)
{
    juce::MemoryBlock mb;
    if (! readZipEntry (zip, infoName, mb)) return;
    auto xml = juce::XmlDocument::parse (mb.toString());
    a.author = xmlText (xml.get(), "Programmer");
    a.description = xmlText (xml.get(), "Comment");
    a.category = xmlText (xml.get(), "Category");
    a.date = xmlText (xml.get(), "Date");
}

static juce::String progInfoXml (const AddInfo& a)
{
    juce::XmlElement root ("xd_ProgramInformation");
    root.createNewChildElement ("Programmer")->addTextElement (a.author);
    root.createNewChildElement ("Comment")->addTextElement (a.description);
    if (a.category.isNotEmpty()) root.createNewChildElement ("Category")->addTextElement (a.category);
    if (a.date.isNotEmpty()) root.createNewChildElement ("Date")->addTextElement (a.date);
    return root.toString();
}

static juce::String fileInfoXml (const juce::Array<int>& slots, bool favourites)
{
    juce::XmlElement root ("KorgMSLibrarian_Data");
    root.createNewChildElement ("Product")->addTextElement ("minilogue xd");
    auto* c = root.createNewChildElement ("Contents");
    c->setAttribute ("NumProgramData", slots.size());
    c->setAttribute ("NumPresetInformation", 0);
    c->setAttribute ("NumTuneScaleData", 0);
    c->setAttribute ("NumTuneOctData", 0);
    c->setAttribute ("NumFavoriteData", favourites ? 1 : 0);
    if (favourites)
        c->createNewChildElement ("FavoriteData")->createNewChildElement ("File")->addTextElement ("FavoriteData.fav_data");
    for (auto i : slots)
    {
        auto* p = c->createNewChildElement ("ProgramData");
        auto n = juce::String::formatted ("Prog_%03d", i);
        p->createNewChildElement ("Information")->addTextElement (n + ".prog_info");
        p->createNewChildElement ("ProgramBinary")->addTextElement (n + ".prog_bin");
    }
    return root.toString();
}

static void addEntry (juce::ZipFile::Builder& zb, const juce::String& name, const void* data, size_t size)
{
    zb.addEntry (new juce::MemoryInputStream (data, size, true), 9, name, juce::Time::getCurrentTime());
}

static void addEntry (juce::ZipFile::Builder& zb, const juce::String& name, const juce::String& text)
{
    auto utf8 = text.toStdString();
    addEntry (zb, name, utf8.data(), utf8.size());
}

static IoResult writeZip (juce::ZipFile::Builder& zb, const juce::File& f)
{
    juce::TemporaryFile tmp (f);
    {
        juce::FileOutputStream os (tmp.getFile());
        if (! os.openedOk()) return IoResult::fail ("Nie można zapisać pliku: " + f.getFullPathName());
        if (! zb.writeToStream (os, nullptr)) return IoResult::fail ("Błąd zapisu archiwum.");
    }
    if (! tmp.overwriteTargetFileWithTemporary()) return IoResult::fail ("Nie można zapisać pliku: " + f.getFullPathName());
    return IoResult::success();
}

// ------------------------------------------------------------------------------------------
IoResult loadProgram (const juce::File& f, Program& p, AddInfo& a)
{
    if (! f.existsAsFile()) return IoResult::fail ("Plik nie istnieje.");
    const auto ext = f.getFileExtension().toLowerCase();
    a = {};

    if (ext == ".mnlgxdprog" || ext == ".mnlgxdlib")
    {
        juce::ZipFile zip (f);
        juce::MemoryBlock mb;
        juce::String binName;
        for (int i = 0; i < zip.getNumEntries(); ++i)
            if (zip.getEntry (i)->filename.endsWithIgnoreCase (".prog_bin")) { binName = zip.getEntry (i)->filename; break; }
        if (binName.isEmpty() || ! readZipEntry (zip, binName, mb))
            return IoResult::fail ("Plik nie zawiera programu (.prog_bin).");
        if (! p.loadFrom ((const uint8_t*) mb.getData(), mb.getSize()))
            return IoResult::fail ("Nieprawidłowe dane programu.");
        readProgInfo (zip, binName.upToLastOccurrenceOf (".", false, false) + ".prog_info", a);
        return IoResult::success();
    }

    juce::MemoryBlock data;
    if (! f.loadFileAsData (data)) return IoResult::fail ("Nie można odczytać pliku.");

    if (ext == ".prog_bin" || (data.getSize() >= (size_t) programSize && std::memcmp (data.getData(), "PROG", 4) == 0))
    {
        if (! p.loadFrom ((const uint8_t*) data.getData(), data.getSize()))
            return IoResult::fail ("Nieprawidłowy plik .prog_bin.");
        a = readSingleAddInfo (f);
        return IoResult::success();
    }

    for (auto& m : sysex::splitMessages (data))
    {
        auto parsed = sysex::parse (m.data(), (int) m.size());
        if (parsed.valid && (parsed.func == sysex::CurrentProgramDump || parsed.func == sysex::ProgramDump)
            && p.loadFrom (parsed.payload.data(), parsed.payload.size()))
        {
            a = readSingleAddInfo (f);
            return IoResult::success();
        }
    }
    return IoResult::fail ("Plik nie zawiera programu Minilogue XD.");
}

IoResult saveProgramSyx (const juce::File& f, const Program& p, const AddInfo& a, int channel)
{
    auto bytes = sysex::currentProgramDump (channel, p);
    if (! f.replaceWithData (bytes.data(), bytes.size()))
        return IoResult::fail ("Nie można zapisać: " + f.getFullPathName());
    writeSingleAddInfo (f, a);
    return IoResult::success();
}

IoResult saveProgramXdProg (const juce::File& f, const Program& p, const AddInfo& a)
{
    juce::ZipFile::Builder zb;
    addEntry (zb, "Prog_000.prog_bin", p.data(), (size_t) programSize);
    auto info = a;
    if (info.date.isEmpty()) info.date = nowIso();
    addEntry (zb, "Prog_000.prog_info", progInfoXml (info));
    addEntry (zb, "FileInformation.xml", fileInfoXml ({ 0 }, false));
    return writeZip (zb, f);
}

// ------------------------------------------------------------------------------------------
IoResult loadBank (const juce::File& f, Bank& b, int channel)
{
    if (! f.existsAsFile()) return IoResult::fail ("Plik nie istnieje: " + f.getFullPathName());
    const auto ext = f.getFileExtension().toLowerCase();
    Bank nb;
    nb.clear();

    if (ext == ".mnlgxdlib" || ext == ".mnlgxdprog")
    {
        juce::ZipFile zip (f);
        int count = 0;
        for (int i = 0; i < zip.getNumEntries(); ++i)
        {
            auto name = zip.getEntry (i)->filename;
            if (! name.endsWithIgnoreCase (".prog_bin")) continue;
            auto slot = name.fromFirstOccurrenceOf ("Prog_", false, true).upToFirstOccurrenceOf (".", false, false).getIntValue();
            if (! juce::isPositiveAndBelow (slot, numSlots)) continue;
            juce::MemoryBlock mb;
            if (readZipEntry (zip, name, mb) && nb.progs[(size_t) slot].loadFrom ((const uint8_t*) mb.getData(), mb.getSize()))
            {
                readProgInfo (zip, name.upToLastOccurrenceOf (".", false, false) + ".prog_info", nb.info[(size_t) slot]);
                ++count;
            }
        }
        if (count == 0) return IoResult::fail ("Brak programów w pliku biblioteki.");
        nb.loaded = true;
        nb.file = f;
        b = nb;
        return IoResult::success (juce::String (count) + " programów wczytanych.");
    }

    juce::MemoryBlock data;
    if (! f.loadFileAsData (data)) return IoResult::fail ("Nie można odczytać pliku.");
    int count = 0, other = 0, sequential = 0;
    for (auto& m : sysex::splitMessages (data))
    {
        auto p = sysex::parse (m.data(), (int) m.size());
        if (! p.valid || (p.func != sysex::ProgramDump && p.func != sysex::CurrentProgramDump)) { ++other; continue; }
        int slot = p.func == sysex::ProgramDump ? p.slot : sequential;
        ++sequential;
        if (! juce::isPositiveAndBelow (slot, numSlots)) { ++other; continue; }
        if (nb.progs[(size_t) slot].loadFrom (p.payload.data(), p.payload.size())) ++count;
        else ++other;
    }
    if (count == 0) return IoResult::fail ("Plik nie zawiera programów Minilogue XD.");

    nb.loaded = true;
    nb.file = f;
    loadAddInfoBank (f, nb);
    juce::String msg = juce::String (count) + " programów wczytanych.";

    const bool clean = count == numSlots && other == 0 && data.getSize() == (size_t) (numSlots * sysex::programDumpSize);
    if (! clean)
    {
        auto cleanFile = f.getFileNameWithoutExtension().endsWith ("_CleanDump") ? f
                       : f.getSiblingFile (f.getFileNameWithoutExtension() + "_CleanDump.syx");
        nb.file = cleanFile;
        auto r = saveBank (cleanFile, nb, channel);
        if (r.ok) msg << "\nUtworzono oczyszczony plik: " << cleanFile.getFileName();
        if (other > 0) msg << "\nPominięto " << other << " innych komunikatów (globalne/skale itp.).";
    }
    b = nb;
    return IoResult::success (msg);
}

IoResult saveBank (const juce::File& f, Bank& b, int channel)
{
    const auto ext = f.getFileExtension().toLowerCase();
    if (ext == ".mnlgxdlib")
    {
        juce::ZipFile::Builder zb;
        juce::Array<int> slots;
        for (int i = 0; i < numSlots; ++i)
        {
            auto n = juce::String::formatted ("Prog_%03d", i);
            addEntry (zb, n + ".prog_bin", b.progs[(size_t) i].data(), (size_t) programSize);
            addEntry (zb, n + ".prog_info", progInfoXml (b.info[(size_t) i]));
            slots.add (i);
        }
        addEntry (zb, "FileInformation.xml", fileInfoXml (slots, false));
        auto r = writeZip (zb, f);
        if (r.ok) b.dirty = false;
        return r;
    }

    juce::MemoryOutputStream os;
    for (int i = 0; i < numSlots; ++i)
    {
        auto m = sysex::programDump (channel, i, b.progs[(size_t) i]);
        os.write (m.data(), m.size());
    }
    if (! f.replaceWithData (os.getData(), os.getDataSize()))
        return IoResult::fail ("Nie można zapisać: " + f.getFullPathName());
    saveAddInfoBank (f, b);
    b.dirty = false;
    return IoResult::success();
}

IoResult exportBankList (const juce::File& f, const Bank& b, const juce::String& title)
{
    juce::String s;
    s << title << "  -  " << b.file.getFileName() << "\n\n";
    for (int i = 0; i < numSlots; ++i)
    {
        auto& in = b.info[(size_t) i];
        s << juce::String (i + 1).paddedLeft ('0', 3) << "  " << b.progs[(size_t) i].getName().paddedRight (' ', 12);
        if (in.category.isNotEmpty()) s << "  [" << in.category << "]";
        if (in.author.isNotEmpty()) s << "  by " << in.author;
        if (in.description.isNotEmpty()) s << "  - " << in.description.replaceCharacters ("\r\n", "  ");
        s << "\n";
    }
    return f.replaceWithText (s) ? IoResult::success() : IoResult::fail ("Nie można zapisać pliku.");
}

} // namespace files
} // namespace xd
