#include "McpServer.h"
#include "PluginProcessor.h"

McpServer::McpServer() : juce::Thread ("XD Editor MCP") {}

McpServer::~McpServer() { stop(); }

void McpServer::add (XDProcessor* p)
{
    procs.addIfNotAlreadyThere (p);
    applySettings();
}

void McpServer::remove (XDProcessor* p) { procs.removeFirstMatchingValue (p); }

XDProcessor* McpServer::target() const
{
    for (auto* p : procs)
        if (p->isActive()) return p;
    return procs.getFirst();
}

void McpServer::applySettings()
{
    const int wanted = settings->mcpEnabled ? settings->mcpPort : 0;
    if (wanted == port && (listener != nullptr || failed || wanted == 0)) return;
    stop();
    port = wanted;
    failed = false;
    if (port == 0) return;
    listener = std::make_unique<juce::StreamingSocket>();
    if (! listener->createListener (port, "127.0.0.1"))
    {
        listener.reset();
        failed = true;
        return;
    }
    startThread();
}

void McpServer::stop()
{
    if (listener != nullptr) listener->close();
    stopThread (4000);
    listener.reset();
}

juce::String McpServer::status() const
{
    if (port == 0) return juce::String::fromUTF8 ("MCP server off.");
    if (failed) return "Cannot open port " + juce::String (port) + " (already in use?).";
    return "MCP: http://127.0.0.1:" + juce::String (port) + "/mcp";
}

void McpServer::run()
{
    while (! threadShouldExit())
    {
        std::unique_ptr<juce::StreamingSocket> client (listener->waitForNextConnection());
        if (client == nullptr) break;
        serve (*client);
    }
}

static bool isLocalOrigin (const juce::String& origin)
{
    const auto host = juce::URL (origin).getDomain();
    return host == "localhost" || host == "127.0.0.1" || host == "[::1]";
}

static void respond (juce::StreamingSocket& s, int code, const juce::String& reason, const juce::String& body = {}, const juce::String& extra = {})
{
    juce::String r;
    r << "HTTP/1.1 " << code << " " << reason << "\r\n"
      << "Content-Type: application/json\r\n"
      << "Content-Length: " << (int) body.getNumBytesAsUTF8() << "\r\n"
      << extra
      << "Connection: close\r\n\r\n"
      << body;
    s.write (r.toRawUTF8(), (int) r.getNumBytesAsUTF8());
}

void McpServer::serve (juce::StreamingSocket& s)
{
    constexpr int maxHeader = 64 * 1024, maxBody = 4 * 1024 * 1024;
    juce::MemoryOutputStream data;
    char buf[8192];
    int headerEnd = -1;
    while (headerEnd < 0)
    {
        if (data.getDataSize() > (size_t) maxHeader || s.waitUntilReady (true, 5000) != 1) return;
        const int n = s.read (buf, (int) sizeof (buf), false);
        if (n <= 0) return;
        data.write (buf, (size_t) n);
        headerEnd = data.toString().indexOf ("\r\n\r\n");
    }
    const auto raw = data.getMemoryBlock();
    const auto head = juce::String::fromUTF8 ((const char*) raw.getData(), headerEnd);
    auto lines = juce::StringArray::fromLines (head);
    const auto requestLine = juce::StringArray::fromTokens (lines[0], " ", {});
    const auto method = requestLine[0];
    const auto path = requestLine[1].upToFirstOccurrenceOf ("?", false, false);
    juce::StringPairArray headers;
    for (int i = 1; i < lines.size(); ++i)
        headers.set (lines[i].upToFirstOccurrenceOf (":", false, false).trim().toLowerCase(),
                     lines[i].fromFirstOccurrenceOf (":", false, false).trim());

    const auto origin = headers["origin"];
    if (origin.isNotEmpty() && ! isLocalOrigin (origin)) { respond (s, 403, "Forbidden"); return; }
    if (path != "/mcp") { respond (s, 404, "Not Found"); return; }
    if (method != "POST") { respond (s, 405, "Method Not Allowed", {}, "Allow: POST\r\n"); return; }

    const int length = headers["content-length"].getIntValue();
    if (length < 0 || length > maxBody) { respond (s, 413, "Payload Too Large"); return; }
    juce::MemoryBlock body ((const char*) raw.getData() + headerEnd + 4, raw.getSize() - (size_t) headerEnd - 4);
    while ((int) body.getSize() < length)
    {
        if (s.waitUntilReady (true, 5000) != 1) return;
        const int n = s.read (buf, juce::jmin ((int) sizeof (buf), length - (int) body.getSize()), false);
        if (n <= 0) return;
        body.append (buf, (size_t) n);
    }

    juce::var message;
    if (juce::JSON::parse (body.toString(), message).failed() || ! message.isObject())
    {
        respond (s, 400, "Bad Request", R"({"jsonrpc":"2.0","id":null,"error":{"code":-32700,"message":"Parse error"}})");
        return;
    }
    const auto out = dispatch (message);
    if (out.isEmpty()) respond (s, 202, "Accepted");
    else respond (s, 200, "OK", out);
}

juce::String McpServer::dispatch (const juce::var& message)
{
    struct State
    {
        juce::WaitableEvent ready;
        juce::CriticalSection lock;
        juce::String out;
        bool answered = false;
    };
    auto state = std::make_shared<State>();
    juce::WeakReference<McpServer> self (this);
    juce::MessageManager::callAsync ([self, state, message]
    {
        auto* p = self != nullptr ? self->target() : nullptr;
        if (p == nullptr) { state->ready.signal(); return; }
        mcp::handle (*p, message, [state] (juce::var r)
        {
            const juce::ScopedLock sl (state->lock);
            state->answered = true;
            if (! r.isVoid()) state->out = juce::JSON::toString (r, true);
            state->ready.signal();
        });
    });
    for (int waited = 0; ! state->ready.wait (100); waited += 100)
        if (threadShouldExit() || waited > 30000) break;

    const juce::ScopedLock sl (state->lock);
    if (state->answered || ! message.hasProperty ("id")) return state->out;
    auto* error = new juce::DynamicObject();
    error->setProperty ("code", -32603);
    error->setProperty ("message", "XD Editor did not answer in time.");
    auto* r = new juce::DynamicObject();
    r->setProperty ("jsonrpc", "2.0");
    r->setProperty ("id", message["id"]);
    r->setProperty ("error", juce::var (error));
    return juce::JSON::toString (juce::var (r), true);
}
