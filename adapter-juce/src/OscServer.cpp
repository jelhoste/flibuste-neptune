#include "OscServer.h"

OscServer::OscServer (neptune::core::control::ControlEngine& control, neptune::core::ParameterSet& set) : juce::Thread ("Neptune OSC"), dispatcher_ (control, set) {}

OscServer::~OscServer() { stop(); }

int OscServer::start (int basePort, bool allInterfaces)
{
    stop();
    // Pas de partage de port (setEnablePortReuse n'est jamais appelé) : une seconde instance échoue sur le port pris et passe au suivant.
    for (int offset = 0; offset < 100 && basePort + offset <= 65535; ++offset)
    {
        auto candidate = std::make_unique<juce::DatagramSocket> (false);
        if (candidate->bindToPort (basePort + offset, allInterfaces ? juce::String() : juce::String ("127.0.0.1")))
        {
            socket_ = std::move (candidate);
            port_.store (basePort + offset);
            startThread (juce::Thread::Priority::normal);
            return basePort + offset;
        }
    }
    return 0;
}

void OscServer::stop()
{
    if (socket_ == nullptr) return;
    signalThreadShouldExit();
    socket_->shutdown();               // réveille la lecture bloquée
    stopThread (2000);
    socket_.reset();
    port_.store (0);
}

void OscServer::run()
{
    std::vector<std::uint8_t> buffer (65536);
    std::vector<std::vector<std::uint8_t>> replies;
    neptune::core::osc::RateLimiter limiter;
    while (! threadShouldExit())
    {
        const int ready = socket_->waitUntilReady (true, 200);
        if (threadShouldExit() || ready < 0) break;
        if (ready == 0) continue;
        juce::String senderIp;
        int senderPort = 0;
        const int n = socket_->read (buffer.data(), static_cast<int> (buffer.size()), false, senderIp, senderPort);
        if (n <= 0) continue;
        replies.clear();
        dispatcher_.handlePacket (buffer.data(), static_cast<std::size_t> (n), replies, &limiter, static_cast<std::int64_t> (juce::Time::getMillisecondCounterHiRes()));
        if (senderPort > 0)
            for (const auto& r : replies)
                socket_->write (senderIp, senderPort, r.data(), static_cast<int> (r.size()));
    }
}
