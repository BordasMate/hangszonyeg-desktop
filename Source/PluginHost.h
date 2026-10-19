#pragma once
#include <JuceHeader.h>
#include "LocalServer.h"
#include <array>
#include <functional>

// VST3-gazda a Hangszőnyeghez.
// Négy csatornalánc (A, B, C, Mester). A webes hangmotor a csatorna hangját 512 mintás blokkokban
// küldi át a hangcsatornán; a gazda végigviszi a láncon, és ugyanazzal a fejléccel visszaküldi.
class PluginHost : public LocalServer::Listener,
                   private juce::Timer
{
public:
    static constexpr int numChains = 4;
    static constexpr uint32_t audioMagic = 0x31415A48; // "HZA1"

    struct AppHooks
    {
        std::function<void (bool)> setFullscreen;
        std::function<juce::Component*()> getMainComponent;
    };

    explicit PluginHost (AppHooks hooks);
    ~PluginHost() override;

    void attachServer (LocalServer* s) { server = s; }
    juce::File getDataDir() const { return dataDir; }

    // LocalServer::Listener
    void wsOpened (LocalServer::Connection&) override;
    void wsText (LocalServer::Connection&, const juce::String&) override;
    void wsBinary (LocalServer::Connection&, const uint8_t* data, size_t size) override;

    void closeAllEditors();

private:
    struct PlayHead final : public juce::AudioPlayHead
    {
        std::atomic<double> bpm { 120.0 };
        std::atomic<bool> playing { false };
        double sampleRate = 48000.0;
        juce::int64 samples = 0;
        juce::Optional<PositionInfo> getPosition() const override;
    };

    class EditorWindow;

    struct Slot
    {
        int id = 0;
        juce::PluginDescription desc;
        std::unique_ptr<juce::AudioPluginInstance> inst;
        bool bypass = false;
        std::unique_ptr<EditorWindow> editor;
    };

    struct Chain
    {
        std::vector<std::unique_ptr<Slot>> slots;
        PlayHead playHead;
        int latency = 0;
    };

    // vezérlés (az üzenetszálon)
    void handleControl (const juce::var& msg);
    void send (const juce::var& obj);
    void sendError (const juce::var& req, const juce::String& text);
    void sendPluginList();
    void sendChains();
    void loadPlugin (int ch, const juce::String& uid, const juce::String& stateB64, bool bypass, const juce::var& req);
    void removeSlot (int ch, int id);
    void moveSlot (int ch, int id, int dir);
    void setBypass (int ch, int id, bool on);
    void openEditor (int ch, int id);
    void closeEditor (Slot&);
    void sendState (const juce::var& req);
    void clearAll();
    void retire (std::unique_ptr<Slot>);
    void prepareAll();
    void preparePlugin (juce::AudioPluginInstance&, PlayHead&);
    Slot* findSlot (int ch, int id);

    // keresés
    void startScan (bool rescanAll);
    void timerCallback() override;
    void saveKnownList();
    void loadSettings();
    void saveSettings();
    juce::FileSearchPath searchPath() const;
    void chooseFolder();

    // hang (a hangcsatorna szálán)
    void processChain (int ch, float* L, float* R, int n);

    static int chainIndex (const juce::String& s);
    static juce::String chainName (int i);

    AppHooks hooks;
    LocalServer* server = nullptr;
    juce::File dataDir;
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList known;
    juce::StringArray extraFolders;

    std::array<Chain, numChains> chains;
    juce::CriticalSection chainLock;
    double sampleRate = 48000.0;
    int blockSize = 512;
    int nextSlotId = 1;
    juce::AudioBuffer<float> work;
    juce::MidiBuffer midi;
    std::vector<uint8_t> reply;

    std::unique_ptr<juce::PluginDirectoryScanner> scanner;
    juce::AudioPluginFormat* scanFormat = nullptr;
    int scanCount = 0;
    std::array<int, numChains> lastLatency { { -1, -1, -1, -1 } };
    int latencyPoll = 0;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginHost)
};
