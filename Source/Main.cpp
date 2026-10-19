#include <JuceHeader.h>
#include "LocalServer.h"
#include "PluginHost.h"

using namespace juce;

namespace
{
String hu (const char* utf8) { return String (CharPointer_UTF8 (utf8)); }
constexpr int preferredPort = 47613;   // állandó port: így a felület helyi tárolója (MIDI-hozzárendelések) megmarad

std::optional<LocalServer::HttpResource> serveResource (const String& path)
{
    if (path == "/" || path == "/index.html")
    {
        LocalServer::HttpResource r;
        r.data.append (BinaryData::index_html, (size_t) BinaryData::index_htmlSize);
        r.mimeType = "text/html; charset=utf-8";
        return r;
    }
    if (path == "/favicon.ico" || path == "/icon.png")
    {
        LocalServer::HttpResource r;
        r.data.append (BinaryData::icon_png, (size_t) BinaryData::icon_pngSize);
        r.mimeType = "image/png";
        return r;
    }
    return std::nullopt;
}
} // namespace

#if ! HZ_HEADLESS
//==============================================================================
class MainWindow final : public DocumentWindow
{
public:
    MainWindow (const String& url, const File& dataDir)
        : DocumentWindow (hu ("Hangszőnyeg"), Colours::white, DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);

        auto options = WebBrowserComponent::Options {}
                           .withBackend (WebBrowserComponent::Options::Backend::webview2)
                           .withWinWebView2Options (WebBrowserComponent::Options::WinWebView2 {}
                                                        .withUserDataFolder (dataDir.getChildFile ("WebView2"))
                                                        .withStatusBarDisabled()
                                                        .withBackgroundColour (Colours::white))
                           .withKeepPageLoadedWhenBrowserIsHidden();

        browser = std::make_unique<WebBrowserComponent> (options);
        browser->setSize (1440, 900);
        setContentNonOwned (browser.get(), true);
        setResizable (true, false);
        setResizeLimits (900, 600, 10000, 10000);

        const auto area = Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea;
        centreWithSize (jmin (1500, area.getWidth() - 40), jmin (960, area.getHeight() - 40));
        setVisible (true);
        browser->goToURL (url);
    }

    ~MainWindow() override { clearContentComponent(); }

    void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }

    void setKiosk (bool on)
    {
        auto& d = Desktop::getInstance();
        if (on && d.getKioskModeComponent() != this)
            d.setKioskModeComponent (this, false);
        else if (! on && d.getKioskModeComponent() == this)
            d.setKioskModeComponent (nullptr);
    }

private:
    std::unique_ptr<WebBrowserComponent> browser;
};
#endif

//==============================================================================
class HangszonyegApp final : public JUCEApplication
{
public:
    const String getApplicationName() override { return hu ("Hangszőnyeg"); }
    const String getApplicationVersion() override { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const String& commandLine) override
    {
        PluginHost::AppHooks hooks;
#if ! HZ_HEADLESS
        hooks.setFullscreen = [this] (bool on) { if (window != nullptr) window->setKiosk (on); };
        hooks.getMainComponent = [this]() -> Component* { return window.get(); };
#endif
        host = std::make_unique<PluginHost> (hooks);

        auto token = Uuid().toString().removeCharacters ("-");
        int port = preferredPort;
#if HZ_HEADLESS
        // próbafuttatás: a jelszó és a port a parancssorból is megadható
        auto args = StringArray::fromTokens (commandLine, true);
        if (args.indexOf ("--token") >= 0) token = args[args.indexOf ("--token") + 1];
        if (args.indexOf ("--port") >= 0) port = args[args.indexOf ("--port") + 1].getIntValue();
#else
        ignoreUnused (commandLine);
#endif
        server = std::make_unique<LocalServer> (*host, serveResource, token);
        host->attachServer (server.get());
        port = server->start (port);
        if (port == 0)
        {
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, hu ("Hangszőnyeg"),
                                              hu ("Nem sikerült helyi portot nyitni a felületnek. Lehet, hogy egy másik példány már fut."));
            quit();
            return;
        }

        const auto url = "http://127.0.0.1:" + String (port) + "/?token=" + token;
#if HZ_HEADLESS
        std::cout << "HZ_READY " << port << " " << token << std::endl;
        ignoreUnused (url);
#else
        window = std::make_unique<MainWindow> (url, host->getDataDir());
#endif
    }

    void shutdown() override
    {
        if (host != nullptr)
            host->closeAllEditors();
        if (server != nullptr)
            server->stop();
        if (host != nullptr)
            host->attachServer (nullptr);
#if ! HZ_HEADLESS
        window = nullptr;
#endif
        host = nullptr;
        server = nullptr;
    }

    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted (const String&) override
    {
#if ! HZ_HEADLESS
        if (window != nullptr)
            window->toFront (true);
#endif
    }

private:
    std::unique_ptr<PluginHost> host;
    std::unique_ptr<LocalServer> server;
#if ! HZ_HEADLESS
    std::unique_ptr<MainWindow> window;
#endif
};

START_JUCE_APPLICATION (HangszonyegApp)
