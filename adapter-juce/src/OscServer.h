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

private:
    void run() override;

    neptune::core::osc::Dispatcher dispatcher_;
    std::unique_ptr<juce::DatagramSocket> socket_;
    std::atomic<int> port_ { 0 };
};
