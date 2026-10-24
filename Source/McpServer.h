#pragma once
#include <juce_events/juce_events.h>
#include "Settings.h"

class XDProcessor;

namespace mcp
{
using Reply = std::function<void (juce::var response)>;

/** Handles one JSON-RPC message on the message thread. A void response means a notification. */
void handle (XDProcessor&, const juce::var& message, Reply);
} // namespace mcp

/** MCP server (Streamable HTTP on 127.0.0.1) shared by every instance; it drives the active instance. */
class McpServer : private juce::Thread
{
public:
    McpServer();
    ~McpServer() override;

    void add (XDProcessor*);
    void remove (XDProcessor*);
    void applySettings();
    juce::String status() const;

private:
    void run() override;
    void stop();
    void serve (juce::StreamingSocket&);
    juce::String dispatch (const juce::var& message);
    XDProcessor* target() const;

    juce::SharedResourcePointer<Settings> settings;
    juce::Array<XDProcessor*> procs;
    std::unique_ptr<juce::StreamingSocket> listener;
    int port = 0;
    bool failed = false;

    JUCE_DECLARE_WEAK_REFERENCEABLE (McpServer)
    JUCE_DECLARE_NON_COPYABLE (McpServer)
};
