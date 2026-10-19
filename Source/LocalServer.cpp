#include "LocalServer.h"
#include "Sha1.h"

using namespace juce;

//==============================================================================
LocalServer::LocalServer (Listener& l, HttpProvider h, String tokenIn)
    : Thread ("hz-server"), listener (l), http (std::move (h)), token (std::move (tokenIn))
{
}

LocalServer::~LocalServer() { stop(); }

int LocalServer::start (int preferredPort, int attempts)
{
    for (int p = preferredPort; p < preferredPort + attempts; ++p)
    {
        auto s = std::make_unique<StreamingSocket>();
        if (s->createListener (p, "127.0.0.1"))
        {
            listenerSocket = std::move (s);
            port = p;
            startThread();
            return port;
        }
    }
    return 0;
}

void LocalServer::stop()
{
    signalThreadShouldExit();
    if (listenerSocket != nullptr)
        listenerSocket->close();
    stopThread (2000);

    Array<Connection*> all;
    {
        const ScopedLock sl (connLock);
        for (auto* c : connections)
        {
            c->signalThreadShouldExit();
            c->closeSocket();
            all.add (c);
        }
    }
    // a zárolást elengedve várjuk meg a szálakat, hogy egy épp üzenetet küldő kapcsolat ne akadjon el
    for (auto* c : all)
        c->stopThread (2000);
    const ScopedLock sl (connLock);
    connections.clear();
}

void LocalServer::run()
{
    while (! threadShouldExit())
    {
        std::unique_ptr<StreamingSocket> client (listenerSocket->waitForNextConnection());
        if (client == nullptr)
        {
            if (threadShouldExit())
                break;
            Thread::sleep (20);
            continue;
        }

        reap();
        const ScopedLock sl (connLock);
        auto* c = connections.add (new Connection (*this, std::move (client), nextId++));
        c->startThread();
    }
}

void LocalServer::reap()
{
    const ScopedLock sl (connLock);
    for (int i = connections.size(); --i >= 0;)
        if (! connections[i]->isThreadRunning())
            connections.remove (i);
}

void LocalServer::broadcastText (const String& kind, const String& text)
{
    const ScopedLock sl (connLock);
    for (auto* c : connections)
        if (c->isWebSocket() && c->getKind() == kind)
            c->sendText (text);
}

//==============================================================================
LocalServer::Connection::Connection (LocalServer& o, std::unique_ptr<StreamingSocket> s, int idIn)
    : Thread ("hz-conn"), owner (o), socket (std::move (s)), id (idIn)
{
}

LocalServer::Connection::~Connection()
{
    signalThreadShouldExit();
    closeSocket();
    stopThread (2000);
}

void LocalServer::Connection::closeSocket()
{
    const ScopedLock sl (sendLock);
    if (socket != nullptr)
        socket->close();
}

bool LocalServer::Connection::readExact (void* dest, size_t n)
{
    auto* d = static_cast<char*> (dest);
    size_t got = 0;
    while (got < n)
    {
        if (threadShouldExit())
            return false;
        const int ready = socket->waitUntilReady (true, 250);
        if (ready < 0)
            return false;
        if (ready == 0)
            continue;
        const int r = socket->read (d + got, (int) (n - got), false);
        if (r <= 0)
            return false;
        got += (size_t) r;
    }
    return true;
}

bool LocalServer::Connection::readHeaders (String& out)
{
    std::string buf;
    char ch;
    while (buf.size() < 32768)
    {
        if (! readExact (&ch, 1))
            return false;
        buf.push_back (ch);
        if (buf.size() >= 4 && buf.compare (buf.size() - 4, 4, "\r\n\r\n") == 0)
        {
            out = String::fromUTF8 (buf.data(), (int) buf.size());
            return true;
        }
    }
    return false;
}

static StringPairArray parseQuery (const String& q)
{
    StringPairArray r;
    for (auto& part : StringArray::fromTokens (q, "&", ""))
    {
        const auto k = part.upToFirstOccurrenceOf ("=", false, false);
        const auto v = part.fromFirstOccurrenceOf ("=", false, false);
        r.set (URL::removeEscapeChars (k), URL::removeEscapeChars (v.replaceCharacter ('+', ' ')));
    }
    return r;
}

void LocalServer::Connection::run()
{
    String req;
    if (! readHeaders (req))
        return;

    auto lines = StringArray::fromLines (req);
    const auto first = lines[0];
    const auto method = first.upToFirstOccurrenceOf (" ", false, false);
    const auto target = first.fromFirstOccurrenceOf (" ", false, false).upToLastOccurrenceOf (" ", false, false);
    const auto path = target.upToFirstOccurrenceOf ("?", false, false);
    const auto query = parseQuery (target.fromFirstOccurrenceOf ("?", false, false));

    StringPairArray headers (false);
    for (int i = 1; i < lines.size(); ++i)
        if (lines[i].containsChar (':'))
            headers.set (lines[i].upToFirstOccurrenceOf (":", false, false).trim().toLowerCase(),
                         lines[i].fromFirstOccurrenceOf (":", false, false).trim());

    const bool wantsUpgrade = headers["upgrade"].equalsIgnoreCase ("websocket");

    if (! wantsUpgrade)
    {
        if (method == "GET" || method == "HEAD")
            serveHttp (path);
        closeSocket();
        return;
    }

    // a WebSocketet csak a saját oldalunk nyithatja meg: az indításkor kapott jelszó nélkül elutasítjuk
    if (query["token"] != owner.token)
    {
        const char* resp = "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        socket->write (resp, (int) strlen (resp));
        closeSocket();
        return;
    }

    kind = query["kind"].isNotEmpty() ? query["kind"] : String ("ctl");
    if (! handshake (headers))
    {
        closeSocket();
        return;
    }

    upgraded = true;
    owner.listener.wsOpened (*this);

    MemoryBlock message;
    uint8_t messageOpcode = 0;

    while (! threadShouldExit())
    {
        uint8_t h[2];
        if (! readExact (h, 2))
            break;
        const bool fin = (h[0] & 0x80) != 0;
        const uint8_t opcode = h[0] & 0x0f;
        const bool masked = (h[1] & 0x80) != 0;
        uint64_t len = h[1] & 0x7f;
        if (len == 126)
        {
            uint8_t e[2];
            if (! readExact (e, 2)) break;
            len = (uint64_t (e[0]) << 8) | e[1];
        }
        else if (len == 127)
        {
            uint8_t e[8];
            if (! readExact (e, 8)) break;
            len = 0;
            for (int i = 0; i < 8; ++i)
                len = (len << 8) | e[i];
        }
        if (len > (uint64_t) 64 * 1024 * 1024)
            break;

        uint8_t mask[4] = { 0, 0, 0, 0 };
        if (masked && ! readExact (mask, 4))
            break;

        MemoryBlock payload ((size_t) len);
        if (len > 0 && ! readExact (payload.getData(), (size_t) len))
            break;
        if (masked)
        {
            auto* p = static_cast<uint8_t*> (payload.getData());
            for (size_t i = 0; i < (size_t) len; ++i)
                p[i] ^= mask[i & 3];
        }

        if (opcode == 0x8)   // bezárás
        {
            sendFrame (0x8, payload.getData(), jmin ((size_t) len, (size_t) 2));
            break;
        }
        if (opcode == 0x9)   // ping
        {
            sendFrame (0xA, payload.getData(), (size_t) len);
            continue;
        }
        if (opcode == 0xA)
            continue;

        if (opcode == 0x1 || opcode == 0x2)
        {
            message = payload;
            messageOpcode = opcode;
        }
        else if (opcode == 0x0)
        {
            message.append (payload.getData(), payload.getSize());
        }

        if (! fin)
            continue;

        if (messageOpcode == 0x1)
            owner.listener.wsText (*this, String::fromUTF8 (static_cast<const char*> (message.getData()), (int) message.getSize()));
        else if (messageOpcode == 0x2)
            owner.listener.wsBinary (*this, static_cast<const uint8_t*> (message.getData()), message.getSize());

        message.reset();
        messageOpcode = 0;
    }

    upgraded = false;
    owner.listener.wsClosed (*this);
    closeSocket();
}

bool LocalServer::Connection::handshake (const StringPairArray& headers)
{
    const auto key = headers["sec-websocket-key"].trim();
    if (key.isEmpty())
        return false;

    const std::string src = (key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").toStdString();
    uint8_t digest[20];
    hz::sha1 (src.data(), src.size(), digest);
    const auto accept = Base64::toBase64 (digest, 20);

    const auto resp = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: "
                      + accept + "\r\n\r\n";
    const ScopedLock sl (sendLock);
    return socket->write (resp.toRawUTF8(), (int) resp.getNumBytesAsUTF8()) > 0;
}

bool LocalServer::Connection::sendFrame (uint8_t opcode, const void* data, size_t size)
{
    uint8_t h[10];
    size_t hl = 2;
    h[0] = (uint8_t) (0x80 | opcode);
    if (size < 126)
        h[1] = (uint8_t) size;
    else if (size < 65536)
    {
        h[1] = 126;
        h[2] = (uint8_t) (size >> 8);
        h[3] = (uint8_t) (size & 0xff);
        hl = 4;
    }
    else
    {
        h[1] = 127;
        for (int i = 0; i < 8; ++i)
            h[2 + i] = (uint8_t) ((uint64_t (size) >> (56 - 8 * i)) & 0xff);
        hl = 10;
    }

    const ScopedLock sl (sendLock);
    if (socket == nullptr || ! socket->isConnected())
        return false;
    if (socket->write (h, (int) hl) != (int) hl)
        return false;
    size_t sent = 0;
    while (sent < size)
    {
        const int w = socket->write (static_cast<const char*> (data) + sent, (int) jmin<size_t> (size - sent, 1 << 20));
        if (w <= 0)
            return false;
        sent += (size_t) w;
    }
    return true;
}

bool LocalServer::Connection::sendText (const String& text)
{
    if (! upgraded)
        return false;
    return sendFrame (0x1, text.toRawUTF8(), text.getNumBytesAsUTF8());
}

bool LocalServer::Connection::sendBinary (const void* data, size_t size)
{
    if (! upgraded)
        return false;
    return sendFrame (0x2, data, size);
}

void LocalServer::Connection::serveHttp (const String& path)
{
    std::optional<HttpResource> res;
    if (owner.http)
        res = owner.http (path);

    String head;
    if (! res.has_value())
    {
        head = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        socket->write (head.toRawUTF8(), (int) head.getNumBytesAsUTF8());
        return;
    }

    head << "HTTP/1.1 200 OK\r\nContent-Type: " << res->mimeType
         << "\r\nContent-Length: " << String ((int64) res->data.getSize())
         << "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
    socket->write (head.toRawUTF8(), (int) head.getNumBytesAsUTF8());

    size_t sent = 0;
    while (sent < res->data.getSize())
    {
        const int w = socket->write (static_cast<const char*> (res->data.getData()) + sent,
                                     (int) jmin<size_t> (res->data.getSize() - sent, 65536));
        if (w <= 0)
            break;
        sent += (size_t) w;
    }
}
