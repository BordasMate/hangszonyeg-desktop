#include "PluginHost.h"

using namespace juce;

namespace
{
const char* const chainNames[] = { "A", "B", "C", "M" };

var obj (std::initializer_list<std::pair<const char*, var>> kv)
{
    auto* o = new DynamicObject();
    for (auto& p : kv)
        o->setProperty (p.first, p.second);
    return var (o);
}

String hu (const char* utf8) { return String (CharPointer_UTF8 (utf8)); }
} // namespace

//==============================================================================
Optional<AudioPlayHead::PositionInfo> PluginHost::PlayHead::getPosition() const
{
    PositionInfo info;
    const double b = bpm.load();
    const double secs = (double) samples / jmax (1.0, sampleRate);
    info.setBpm (b);
    info.setIsPlaying (playing.load());
    info.setTimeInSamples (samples);
    info.setTimeInSeconds (secs);
    info.setPpqPosition (secs * b / 60.0);
    info.setTimeSignature (TimeSignature { 4, 4 });
    return info;
}

//==============================================================================
class PluginHost::EditorWindow final : public DocumentWindow
{
public:
    EditorWindow (PluginHost& h, int slotIdIn, const String& title, AudioProcessorEditor* ed)
        : DocumentWindow (title, Colour (0xff202020), DocumentWindow::closeButton | DocumentWindow::minimiseButton),
          host (h), slotId (slotIdIn)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (ed, true);
        setResizable (ed->isResizable(), false);
        setAlwaysOnTop (true);
        if (auto* main = host.hooks.getMainComponent ? host.hooks.getMainComponent() : nullptr)
            centreAroundComponent (main, getWidth(), getHeight());
        else
            centreWithSize (getWidth(), getHeight());
        setVisible (true);
        toFront (true);
    }

    void closeButtonPressed() override
    {
        auto* h = &host;
        const int id = slotId;
        MessageManager::callAsync ([h, id]
        {
            for (int c = 0; c < numChains; ++c)
                if (auto* s = h->findSlot (c, id))
                {
                    h->closeEditor (*s);
                    h->sendChains();
                }
        });
    }

private:
    PluginHost& host;
    int slotId;
};

//==============================================================================
PluginHost::PluginHost (AppHooks h) : hooks (std::move (h))
{
    dataDir = File::getSpecialLocation (File::userApplicationDataDirectory).getChildFile ("Hangszonyeg");
    dataDir.createDirectory();

    formatManager.addDefaultFormats();

    for (int c = 0; c < numChains; ++c)
        chains[(size_t) c].playHead.sampleRate = sampleRate;

    loadSettings();
    if (auto xml = parseXML (dataDir.getChildFile ("plugins.xml")))
        known.recreateFromXml (*xml);

    startTimer (1000);
}

PluginHost::~PluginHost()
{
    stopTimer();
    closeAllEditors();
    const ScopedLock sl (chainLock);
    for (auto& c : chains)
        c.slots.clear();
}

int PluginHost::chainIndex (const String& s)
{
    for (int i = 0; i < numChains; ++i)
        if (s == chainNames[i])
            return i;
    return -1;
}

String PluginHost::chainName (int i) { return chainNames[jlimit (0, numChains - 1, i)]; }

PluginHost::Slot* PluginHost::findSlot (int ch, int id)
{
    if (ch < 0 || ch >= numChains)
        return nullptr;
    for (auto& s : chains[(size_t) ch].slots)
        if (s->id == id)
            return s.get();
    return nullptr;
}

//==============================================================================
void PluginHost::send (const var& o)
{
    if (server != nullptr)
        server->broadcastText ("ctl", JSON::toString (o, true));
}

void PluginHost::sendError (const var& req, const String& text)
{
    send (obj ({ { "t", "error" }, { "req", req }, { "msg", text } }));
}

void PluginHost::wsOpened (LocalServer::Connection& c)
{
    if (c.getKind() != "ctl")
        return;
    MessageManager::callAsync ([this]
    {
        StringArray formats;
        for (auto* f : formatManager.getFormats())
            formats.add (f->getName());
        send (obj ({ { "t", "info" }, { "version", ProjectInfo::versionString }, { "formats", formats.joinIntoString (", ") },
                     { "dataDir", dataDir.getFullPathName() } }));
        sendPluginList();
        sendChains();
        if (known.getNumTypes() == 0 && scanner == nullptr)
            startScan (false);
    });
}

void PluginHost::wsText (LocalServer::Connection& c, const String& text)
{
    if (c.getKind() != "ctl")
        return;
    auto msg = JSON::parse (text);
    if (! msg.isObject())
        return;
    MessageManager::callAsync ([this, msg] { handleControl (msg); });
}

void PluginHost::handleControl (const var& m)
{
    const String t = m["t"].toString();
    const var req = m["req"];
    const int ch = chainIndex (m["ch"].toString());

    if (t == "prepare")
    {
        const double sr = (double) m["sr"];
        const int bs = (int) m["block"];
        if (sr > 1000.0 && bs > 0 && (! approximatelyEqual (sr, sampleRate) || bs != blockSize))
        {
            sampleRate = sr;
            blockSize = bs;
            prepareAll();
        }
    }
    else if (t == "tempo")
    {
        for (auto& c : chains)
        {
            c.playHead.bpm = jlimit (10.0, 999.0, (double) m["bpm"]);
            c.playHead.playing = (bool) m["playing"];
        }
    }
    else if (t == "list")       sendPluginList();
    else if (t == "chains")     sendChains();
    else if (t == "scan")       startScan ((bool) m["full"]);
    else if (t == "chooseFolder") chooseFolder();
    else if (t == "load" && ch >= 0)
        loadPlugin (ch, m["uid"].toString(), m["state"].toString(), (bool) m["bypass"], req);
    else if (t == "remove" && ch >= 0) removeSlot (ch, (int) m["id"]);
    else if (t == "move" && ch >= 0)   moveSlot (ch, (int) m["id"], (int) m["dir"]);
    else if (t == "bypass" && ch >= 0) setBypass (ch, (int) m["id"], (bool) m["on"]);
    else if (t == "editor" && ch >= 0) openEditor (ch, (int) m["id"]);
    else if (t == "closeEditor" && ch >= 0)
    {
        if (auto* s = findSlot (ch, (int) m["id"]))
            closeEditor (*s);
        sendChains();
    }
    else if (t == "getState")   sendState (req);
    else if (t == "clear")      clearAll();
    else if (t == "fullscreen")
    {
        if (hooks.setFullscreen)
            hooks.setFullscreen ((bool) m["on"]);
    }
}

//==============================================================================
void PluginHost::sendPluginList()
{
    Array<var> list;
    for (auto& d : known.getTypes())
    {
        list.add (obj ({ { "uid", d.createIdentifierString() }, { "name", d.name }, { "vendor", d.manufacturerName },
                         { "category", d.category }, { "format", d.pluginFormatName }, { "ins", d.numInputChannels },
                         { "outs", d.numOutputChannels }, { "instrument", d.isInstrument }, { "file", d.fileOrIdentifier } }));
    }
    Array<var> folders;
    for (int i = 0; i < searchPath().getNumPaths(); ++i)
        folders.add (searchPath()[i].getFullPathName());
    send (obj ({ { "t", "plugins" }, { "list", list }, { "folders", folders }, { "scanning", scanner != nullptr } }));
}

void PluginHost::sendChains()
{
    auto* o = new DynamicObject();
    auto* lat = new DynamicObject();
    {
        const ScopedLock sl (chainLock);
        for (int c = 0; c < numChains; ++c)
        {
            Array<var> a;
            int latency = 0;
            for (auto& s : chains[(size_t) c].slots)
            {
                const int l = s->inst != nullptr ? s->inst->getLatencySamples() : 0;
                if (! s->bypass)
                    latency += l;
                a.add (obj ({ { "id", s->id }, { "uid", s->desc.createIdentifierString() }, { "name", s->desc.name },
                              { "vendor", s->desc.manufacturerName }, { "bypass", s->bypass }, { "latency", l },
                              { "editor", s->editor != nullptr } }));
            }
            chains[(size_t) c].latency = latency;
            lastLatency[(size_t) c] = latency;
            o->setProperty (chainNames[c], a);
            lat->setProperty (chainNames[c], latency);
        }
    }
    send (obj ({ { "t", "chains" }, { "chains", var (o) }, { "latency", var (lat) } }));
}

void PluginHost::preparePlugin (AudioPluginInstance& p, PlayHead& ph)
{
    p.setPlayHead (&ph);
    auto layout = p.getBusesLayout();
    if (layout.inputBuses.size() > 0)
        layout.inputBuses.getReference (0) = AudioChannelSet::stereo();
    if (layout.outputBuses.size() > 0)
        layout.outputBuses.getReference (0) = AudioChannelSet::stereo();
    if (p.checkBusesLayoutSupported (layout))
        p.setBusesLayout (layout);
    p.setNonRealtime (false);
    p.prepareToPlay (sampleRate, blockSize);
}

void PluginHost::prepareAll()
{
    const ScopedLock sl (chainLock);
    for (auto& c : chains)
    {
        c.playHead.sampleRate = sampleRate;
        for (auto& s : c.slots)
            if (s->inst != nullptr)
            {
                const ScopedLock cl (s->inst->getCallbackLock());
                s->inst->releaseResources();
                preparePlugin (*s->inst, c.playHead);
            }
    }
}

void PluginHost::loadPlugin (int ch, const String& uid, const String& stateB64, bool bypass, const var& req)
{
    auto d = known.getTypeForIdentifierString (uid);
    if (d == nullptr)
    {
        sendError (req, hu ("Ez a plugin nincs a gépen talált pluginok között. Futtass új keresést, vagy telepítsd újra."));
        return;
    }
    if (d->numInputChannels == 0 && d->isInstrument)
    {
        sendError (req, hu ("Ez hangszer-plugin, nincs hangbemenete, ezért csatornára effektként nem tehető."));
        return;
    }

    const auto desc = *d;
    formatManager.createPluginInstanceAsync (desc, sampleRate, blockSize,
        [this, ch, desc, stateB64, bypass, req] (std::unique_ptr<AudioPluginInstance> inst, const String& err)
        {
            if (inst == nullptr)
            {
                sendError (req, hu ("A plugin nem indult el: ") + err);
                return;
            }
            preparePlugin (*inst, chains[(size_t) ch].playHead);
            if (stateB64.isNotEmpty())
            {
                MemoryBlock mb;
                if (mb.fromBase64Encoding (stateB64))
                    inst->setStateInformation (mb.getData(), (int) mb.getSize());
            }
            auto slot = std::make_unique<Slot>();
            slot->id = nextSlotId++;
            slot->desc = desc;
            slot->inst = std::move (inst);
            slot->bypass = bypass;
            const int id = slot->id;
            {
                const ScopedLock sl (chainLock);
                chains[(size_t) ch].slots.push_back (std::move (slot));
            }
            send (obj ({ { "t", "loaded" }, { "req", req }, { "ch", chainName (ch) }, { "id", id }, { "name", desc.name } }));
            sendChains();
        });
}

void PluginHost::closeEditor (Slot& s)
{
    s.editor.reset();
}

void PluginHost::closeAllEditors()
{
    for (auto& c : chains)
        for (auto& s : c.slots)
            closeEditor (*s);
}

void PluginHost::removeSlot (int ch, int id)
{
    std::unique_ptr<Slot> gone;
    if (auto* s = findSlot (ch, id))
        closeEditor (*s);
    {
        const ScopedLock sl (chainLock);
        auto& v = chains[(size_t) ch].slots;
        for (auto it = v.begin(); it != v.end(); ++it)
            if ((*it)->id == id)
            {
                gone = std::move (*it);
                v.erase (it);
                break;
            }
    }
    sendChains();
    // a plugin példányát csak a nyitott felülete teljes lebontása után, egy későbbi üzenetkörben töröljük
    if (gone != nullptr)
        retire (std::move (gone));
}

void PluginHost::retire (std::unique_ptr<Slot> s)
{
    auto* raw = s.release();
    Timer::callAfterDelay (300, [raw]
    {
        std::unique_ptr<Slot> victim (raw);
        if (victim->inst != nullptr)
            victim->inst->releaseResources();
    });
}

void PluginHost::moveSlot (int ch, int id, int dir)
{
    {
        const ScopedLock sl (chainLock);
        auto& v = chains[(size_t) ch].slots;
        for (size_t i = 0; i < v.size(); ++i)
            if (v[i]->id == id)
            {
                const auto j = (int) i + (dir < 0 ? -1 : 1);
                if (j >= 0 && j < (int) v.size())
                    std::swap (v[i], v[(size_t) j]);
                break;
            }
    }
    sendChains();
}

void PluginHost::setBypass (int ch, int id, bool on)
{
    {
        const ScopedLock sl (chainLock);
        if (auto* s = findSlot (ch, id))
            s->bypass = on;
    }
    sendChains();
}

void PluginHost::openEditor (int ch, int id)
{
    auto* s = findSlot (ch, id);
    if (s == nullptr || s->inst == nullptr)
        return;
    if (s->editor != nullptr)
    {
        s->editor->toFront (true);
        return;
    }
    AudioProcessorEditor* ed = s->inst->hasEditor() ? s->inst->createEditorIfNeeded() : nullptr;
    if (ed == nullptr)
        ed = new GenericAudioProcessorEditor (*s->inst);
    const String title = (ch == 3 ? hu ("Mester") : chainName (ch)) + ": " + s->desc.name;
    s->editor = std::make_unique<EditorWindow> (*this, s->id, title, ed);
    sendChains();
}

void PluginHost::sendState (const var& req)
{
    auto* o = new DynamicObject();
    const ScopedLock sl (chainLock);
    for (int c = 0; c < numChains; ++c)
    {
        Array<var> a;
        for (auto& s : chains[(size_t) c].slots)
        {
            MemoryBlock mb;
            if (s->inst != nullptr)
                s->inst->getStateInformation (mb);
            a.add (obj ({ { "uid", s->desc.createIdentifierString() }, { "name", s->desc.name }, { "vendor", s->desc.manufacturerName },
                          { "bypass", s->bypass }, { "state", mb.toBase64Encoding() } }));
        }
        o->setProperty (chainNames[c], a);
    }
    send (obj ({ { "t", "state" }, { "req", req }, { "chains", var (o) } }));
}

void PluginHost::clearAll()
{
    closeAllEditors();
    std::array<std::vector<std::unique_ptr<Slot>>, numChains> gone;
    {
        const ScopedLock sl (chainLock);
        for (int c = 0; c < numChains; ++c)
            gone[(size_t) c] = std::move (chains[(size_t) c].slots);
    }
    sendChains();
    for (auto& v : gone)
        for (auto& s : v)
            retire (std::move (s));
}

//==============================================================================
FileSearchPath PluginHost::searchPath() const
{
    FileSearchPath p;
    for (auto* f : formatManager.getFormats())
        if (f->getName() == "VST3")
            p = f->getDefaultLocationsToSearch();
    for (auto& e : extraFolders)
        p.addIfNotAlreadyThere (File (e));
    p.removeNonExistentPaths();
    return p;
}

void PluginHost::loadSettings()
{
    auto j = JSON::parse (dataDir.getChildFile ("settings.json"));
    if (auto* a = j["folders"].getArray())
        for (auto& v : *a)
            extraFolders.addIfNotAlreadyThere (v.toString());
}

void PluginHost::saveSettings()
{
    Array<var> a;
    for (auto& f : extraFolders)
        a.add (f);
    dataDir.getChildFile ("settings.json").replaceWithText (JSON::toString (obj ({ { "folders", a } })));
}

void PluginHost::saveKnownList()
{
    if (auto xml = known.createXml())
        xml->writeTo (dataDir.getChildFile ("plugins.xml"));
}

void PluginHost::chooseFolder()
{
    chooser = std::make_unique<FileChooser> (hu ("Válaszd ki a VST3-pluginok mappáját"), File(), String());
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories,
        [this] (const FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (f.isDirectory())
            {
                extraFolders.addIfNotAlreadyThere (f.getFullPathName());
                saveSettings();
                startScan (false);
            }
        });
}

void PluginHost::startScan (bool rescanAll)
{
    if (scanner != nullptr)
        return;
    scanFormat = nullptr;
    for (auto* f : formatManager.getFormats())
        if (f->getName() == "VST3")
            scanFormat = f;
    if (scanFormat == nullptr)
    {
        send (obj ({ { "t", "scan" }, { "active", false }, { "done", true }, { "msg", hu ("Ebben a változatban nincs VST3-támogatás.") } }));
        return;
    }
    if (rescanAll)
        known.clear();
    scanner = std::make_unique<PluginDirectoryScanner> (known, *scanFormat, searchPath(), true,
                                                        dataDir.getChildFile ("scan-crash.txt"), false);
    scanCount = 0;
    send (obj ({ { "t", "scan" }, { "active", true }, { "progress", 0.0 } }));
    startTimer (5);
}

void PluginHost::timerCallback()
{
    if (scanner != nullptr)
    {
        // egyszerre egy fájl: közben az ablak és a hang is él
        String name;
        const bool more = scanner->scanNextFile (true, name);
        ++scanCount;
        send (obj ({ { "t", "scan" }, { "active", more }, { "file", name }, { "progress", (double) scanner->getProgress() } }));
        if (! more)
        {
            const auto failed = scanner->getFailedFiles();
            scanner.reset();
            saveKnownList();
            send (obj ({ { "t", "scan" }, { "active", false }, { "done", true }, { "failed", failed.joinIntoString ("\n") },
                         { "count", known.getNumTypes() } }));
            sendPluginList();
            startTimer (1000);
        }
        return;
    }

    // a pluginok késleltetése menet közben is változhat (például ha átkapcsolsz bennük valamit)
    bool changed = false;
    {
        const ScopedLock sl (chainLock);
        for (int c = 0; c < numChains; ++c)
        {
            int l = 0;
            for (auto& s : chains[(size_t) c].slots)
                if (! s->bypass && s->inst != nullptr)
                    l += s->inst->getLatencySamples();
            if (l != lastLatency[(size_t) c])
                changed = true;
        }
    }
    if (changed)
        sendChains();
}

//==============================================================================
void PluginHost::wsBinary (LocalServer::Connection& c, const uint8_t* data, size_t size)
{
    if (c.getKind() != "audio" || size < 16)
        return;
    uint32_t magic, frames;
    std::memcpy (&magic, data, 4);
    std::memcpy (&frames, data + 8, 4);
    const int ch = data[4];
    if (magic != audioMagic || ch < 0 || ch >= numChains || frames == 0 || frames > 8192 || size != 16 + (size_t) frames * 8)
        return;

    thread_local std::vector<uint8_t> buf;
    buf.assign (data, data + size);
    auto* L = reinterpret_cast<float*> (buf.data() + 16);
    auto* R = L + frames;
    processChain (ch, L, R, (int) frames);
    c.sendBinary (buf.data(), buf.size());
}

void PluginHost::processChain (int ch, float* L, float* R, int n)
{
    const ScopedLock sl (chainLock);
    auto& chain = chains[(size_t) ch];
    for (auto& s : chain.slots)
    {
        if (s->bypass || s->inst == nullptr)
            continue;
        auto& p = *s->inst;
        const ScopedLock cl (p.getCallbackLock());
        if (p.isSuspended())
            continue;
        const int ins = p.getTotalNumInputChannels(), outs = p.getTotalNumOutputChannels();
        const int nch = jmax (2, ins, outs);
        work.setSize (nch, n, false, false, true);
        work.clear();
        if (ins >= 2)
        {
            work.copyFrom (0, 0, L, n);
            work.copyFrom (1, 0, R, n);
        }
        else if (ins == 1)
        {
            work.copyFrom (0, 0, L, n, 0.5f);
            work.addFrom (0, 0, R, n, 0.5f);
        }
        midi.clear();
        p.processBlock (work, midi);
        if (outs >= 2)
        {
            FloatVectorOperations::copy (L, work.getReadPointer (0), n);
            FloatVectorOperations::copy (R, work.getReadPointer (1), n);
        }
        else if (outs == 1)
        {
            FloatVectorOperations::copy (L, work.getReadPointer (0), n);
            FloatVectorOperations::copy (R, work.getReadPointer (0), n);
        }
    }
    chain.playHead.samples += n;
}
