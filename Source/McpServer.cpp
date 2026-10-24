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
    if (port == 0) return "MCP server off.";
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

bool McpServer::readSome (juce::StreamingSocket& s, juce::MemoryBlock& data, int maxBytes)
{
    for (int waited = 0; waited < 5000 && ! threadShouldExit(); waited += 100)
    {
        const int ready = s.waitUntilReady (true, 100);
        if (ready < 0) return false;
        if (ready == 0) continue;
        char buf[8192];
        const int n = s.read (buf, juce::jmin ((int) sizeof (buf), maxBytes), false);
        if (n <= 0) return false;
        data.append (buf, (size_t) n);
        return true;
    }
    return false;
}

void McpServer::serve (juce::StreamingSocket& s)
{
    constexpr int maxHeader = 64 * 1024, maxBody = 4 * 1024 * 1024;
    const char separator[] = "\r\n\r\n";
    juce::MemoryBlock data;
    const char* headerEnd = nullptr;
    while (headerEnd == nullptr)
    {
        if (data.getSize() > (size_t) maxHeader || ! readSome (s, data, maxHeader)) return;
        auto* begin = (const char*) data.getData();
        auto* end = begin + data.getSize();
        auto* found = std::search (begin, end, separator, separator + 4);
        if (found != end) headerEnd = found;
    }
    const auto headerSize = (size_t) (headerEnd - (const char*) data.getData());
    auto lines = juce::StringArray::fromLines (juce::String::fromUTF8 ((const char*) data.getData(), (int) headerSize));
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
    juce::MemoryBlock body ((const char*) data.getData() + headerSize + 4, data.getSize() - headerSize - 4);
    while ((int) body.getSize() < length)
        if (! readSome (s, body, length - (int) body.getSize())) return;

    juce::var message;
    if (juce::JSON::parse (body.toString(), message).failed() || ! message.isObject())
    {
        respond (s, 400, "Bad Request", juce::JSON::toString (mcp::error ({}, -32700, "Parse error"), true));
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
    auto reply = [state] (juce::var r)
    {
        const juce::ScopedLock sl (state->lock);
        state->answered = true;
        if (! r.isVoid()) state->out = juce::JSON::toString (r, true);
        state->ready.signal();
    };
    juce::WeakReference<McpServer> self (this);
    juce::MessageManager::callAsync ([self, message, reply]
    {
        if (auto* p = self != nullptr ? self->target() : nullptr) mcp::handle (*p, message, reply);
        else if (message.hasProperty ("id")) reply (mcp::error (message["id"], -32603, "No XD Editor instance is open."));
        else reply ({});
    });
    for (int waited = 0; ! state->ready.wait (100); waited += 100)
        if (threadShouldExit() || waited > 30000) break;

    const juce::ScopedLock sl (state->lock);
    if (state->answered || ! message.hasProperty ("id")) return state->out;
    return juce::JSON::toString (mcp::error (message["id"], -32603, "XD Editor did not answer in time."), true);
}
