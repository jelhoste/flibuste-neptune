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
    {
        const juce::ScopedLock lock (destinationLock_);
        dynamic_ = {};   // un destinataire appris ne survit pas à l'arrêt du serveur
    }
    if (socket_ == nullptr) return;
    signalThreadShouldExit();
    socket_->shutdown();               // réveille la lecture bloquée
    stopThread (2000);
    socket_.reset();
    port_.store (0);
}

void OscServer::setManualDestination (const juce::String& host, int port)
{
    const juce::ScopedLock lock (destinationLock_);
    manual_ = { host, port };
}

void OscServer::stopFeedback()
{
    const juce::ScopedLock lock (destinationLock_);
    manual_ = {};
    dynamic_ = {};
}

bool OscServer::hasDestination() const
{
    const juce::ScopedLock lock (destinationLock_);
    return (dynamic_.port > 0 && dynamic_.host.isNotEmpty()) || (manual_.port > 0 && manual_.host.isNotEmpty());
}

juce::String OscServer::destinationText() const
{
    const juce::ScopedLock lock (destinationLock_);
    const auto& d = dynamic_.port > 0 && dynamic_.host.isNotEmpty() ? dynamic_ : manual_;
    return d.port > 0 && d.host.isNotEmpty() ? d.host + ":" + juce::String (d.port) : juce::String();
}

void OscServer::send (const std::vector<std::vector<std::uint8_t>>& datagrams)
{
    Destination d;
    {
        const juce::ScopedLock lock (destinationLock_);
        d = dynamic_.port > 0 && dynamic_.host.isNotEmpty() ? dynamic_ : manual_;
    }
    if (d.port <= 0 || d.host.isEmpty()) return;
    for (const auto& datagram : datagrams)
        sender_.write (d.host, d.port, datagram.data(), static_cast<int> (datagram.size()));
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
        int syncPort = 0;
        if (dispatcher_.takeSync (syncPort) && senderPort > 0)
        {
            {
                const juce::ScopedLock lock (destinationLock_);
                dynamic_ = { senderIp, syncPort > 0 ? syncPort : senderPort };
            }
            fullSend_.store (true);   // le thread de message envoie tout l'état
        }
    }
}
