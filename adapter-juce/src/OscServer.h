#pragma once
// Serveur OSC (UDP) : un thread réseau qui lit les datagrammes et les passe au répartiteur du cœur (neptune/core/Osc.h). Il ne touche
// jamais le thread audio : il écrit dans ParameterSet (valeurs atomiques) et signale les changements au relais du thread de message.
// Désactivé par défaut ; écoute sur localhost sauf demande contraire. Un port par instance : le premier libre à partir du port de base.
#include <juce_core/juce_core.h>

#include <atomic>
#include <memory>

#include "neptune/core/Control.h"
#include "neptune/core/Osc.h"

class OscServer final : private juce::Thread
{
public:
    OscServer (neptune::core::control::ControlEngine&, neptune::core::ParameterSet&);
    ~OscServer() override;

    // Démarre (ou redémarre) l'écoute. Retourne le port pris, ou 0 si aucun des 100 ports à partir de `basePort` n'est libre.
    int start (int basePort, bool allInterfaces);
    void stop();
    int port() const noexcept { return port_.load(); }

    // ---- retour d'information (thread de message) ----
    // Destinataire réglé à la main (« hôte:port », vide : aucun) ; celui d'un « /neptune/sync » reçu le remplace jusqu'à l'arrêt.
    void setManualDestination (const juce::String& host, int port);
    void stopFeedback();                                  // efface les deux destinataires
    bool hasDestination() const;
    juce::String destinationText() const;                 // « hôte:port » ou vide
    void send (const std::vector<std::vector<std::uint8_t>>& datagrams);
    bool takeFullSendRequest() noexcept { return fullSend_.exchange (false); }   // un « /neptune/sync » demande l'état complet

private:
    void run() override;

    neptune::core::osc::Dispatcher dispatcher_;
    std::unique_ptr<juce::DatagramSocket> socket_;
    std::atomic<int> port_ { 0 };
    struct Destination { juce::String host; int port = 0; };
    mutable juce::CriticalSection destinationLock_;
    Destination manual_, dynamic_;
    juce::DatagramSocket sender_ { false };
    std::atomic<bool> fullSend_ { false };
};
