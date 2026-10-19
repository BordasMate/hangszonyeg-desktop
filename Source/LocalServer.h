#pragma once
#include <JuceHeader.h>
#include <functional>
#include <optional>

// Kis helyi kiszolgáló a 127.0.0.1 címen:
//  - HTTP GET: kiszolgálja a Hangszőnyeg felületét (index.html), így az oldal saját, állandó eredetű
//    (http://127.0.0.1:port), biztonságos környezetnek számít, és a helyi tárolója megmarad indítások között;
//  - WebSocket: vezérlőcsatorna (JSON szöveg) és hangcsatorna (bináris blokkok) a natív plugin-gazdához.
class LocalServer : private juce::Thread
{
public:
    class Connection;

    struct Listener
    {
        virtual ~Listener() = default;
        virtual void wsOpened (Connection&) {}
        virtual void wsClosed (Connection&) {}
        virtual void wsText (Connection&, const juce::String&) {}
        virtual void wsBinary (Connection&, const uint8_t* data, size_t size) { juce::ignoreUnused (data, size); }
    };

    struct HttpResource
    {
        juce::MemoryBlock data;
        juce::String mimeType;
    };
    using HttpProvider = std::function<std::optional<HttpResource> (const juce::String& path)>;

    class Connection : public juce::Thread
    {
    public:
        Connection (LocalServer& o, std::unique_ptr<juce::StreamingSocket> s, int idIn);
        ~Connection() override;

        bool sendText (const juce::String& text);
        bool sendBinary (const void* data, size_t size);
        void closeSocket();

        const juce::String& getKind() const noexcept { return kind; }
        int getId() const noexcept { return id; }
        bool isWebSocket() const noexcept { return upgraded.load(); }

    private:
        void run() override;
        bool readExact (void* dest, size_t n);
        bool readHeaders (juce::String& out);
        bool sendFrame (uint8_t opcode, const void* data, size_t size);
        void serveHttp (const juce::String& path);
        bool handshake (const juce::StringPairArray& headers);

        LocalServer& owner;
        std::unique_ptr<juce::StreamingSocket> socket;
        juce::CriticalSection sendLock;
        juce::String kind;
        int id;
        std::atomic<bool> upgraded { false };
    };

    LocalServer (Listener&, HttpProvider, juce::String tokenIn);
    ~LocalServer() override;

    // Az első szabad portot adja vissza a megadottól kezdve (0, ha nem sikerült).
    int start (int preferredPort, int attempts = 20);
    void stop();

    int getPort() const noexcept { return port; }
    const juce::String& getToken() const noexcept { return token; }

    void broadcastText (const juce::String& kind, const juce::String& text);

private:
    void run() override;
    void reap();

    Listener& listener;
    HttpProvider http;
    juce::String token;
    int port = 0;
    std::unique_ptr<juce::StreamingSocket> listenerSocket;
    juce::CriticalSection connLock;
    juce::OwnedArray<Connection> connections;
    int nextId = 1;

    friend class Connection;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LocalServer)
};
