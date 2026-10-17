#pragma once
// OSC (docs/couche-controle.md, étape 3) : codec des paquets, table figée des adresses des paramètres et répartiteur. Cœur portable,
// sans JUCE ni réseau : le serveur UDP de l'adaptateur lui passe les octets reçus et renvoie les réponses qu'il produit.
//
// Adresses (la forme hiérarchique est la principale ; l'identifiant reste toujours valable) :
//   /neptune/track/3/organ/reverb          valeur normalisée 0..1 (un entier ou un flottant ; T/F pour les bascules)
//   /neptune/track/3/organ/reverb/real     valeur en unités réelles (dB, Hz, %, pas...)
//   /neptune/id/9033  et  /neptune/id/9033/real
//   /neptune/list [préfixe]                catalogue, renvoyé à l'expéditeur
//   /neptune/sync [port]                   l'expéditeur devient le destinataire du retour d'information (port donné, sinon celui d'où il écrit) ;
//                                          tout l'état lui est renvoyé, puis chaque changement (valeurs normalisées 0..1, mêmes adresses)
// Les numéros de pistes, bus, pistes parallèles, inserts, envois et emplacements sont comptés à partir de 1 (comme à l'écran).
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "neptune/core/Control.h"
#include "neptune/core/Parameter.h"

namespace neptune::core::osc
{
constexpr std::string_view kPrefix = "/neptune";

// ---- codec ----
struct Arg
{
    char type = 'f';       // 'i' 'h' (entiers), 'f' 'd' (flottants), 's' (texte), 'T' 'F' 'N' 'I' (sans donnée)
    double number = 0.0;   // i, h, f, d ; T = 1, F = 0
    std::string text;      // s
};
struct Message
{
    std::string address;
    std::vector<Arg> args;
};

// Décode un paquet (message ou paquet groupé, imbriqué sur 4 niveaux au plus ; les étiquettes de temps sont ignorées : exécution
// immédiate). Ajoute les messages à `out`, au plus `maxMessages`. Retourne false si le paquet est malformé (les messages déjà lus restent).
bool parsePacket (const std::uint8_t* data, std::size_t size, std::vector<Message>& out, std::size_t maxMessages = 1024);
std::vector<std::uint8_t> encode (const Message&);
std::vector<std::uint8_t> encodeBundle (const std::vector<std::vector<std::uint8_t>>& messages);   // étiquette « immédiat »

// ---- table des adresses ----
// Table figée par un test : jamais déduite des noms affichés, jamais traduite, jamais renommée sans alias.
const std::string& addressFor (ParamId);                            // « /neptune/... » ; vide si le paramètre n'a pas d'adresse
bool paramForAddress (std::string_view address, ParamId& out);      // adresse hiérarchique, sans suffixe « /real »
const std::vector<ParamId>& catalogIds();                           // tous les identifiants adressables, dans l'ordre

// ---- répartiteur ----
// Retour d'information : un message « adresse, flottant normalisé » par paramètre de `indices` (indices dans `set`) qui a une adresse, regroupés en
// paquets groupés de moins de 1,1 Ko (un datagramme chacun).
std::vector<std::vector<std::uint8_t>> buildFeedback (const ParameterSet& set, const std::vector<std::size_t>& indices);

// Plafond de messages traités par fenêtre de temps (un envoi trop rapide ne sature rien) : `now` en millisecondes, croissant.
class RateLimiter
{
public:
    RateLimiter (int perWindow = 4000, int windowMs = 100) : perWindow_ (perWindow), windowMs_ (windowMs) {}
    bool allow (std::int64_t nowMs) noexcept;
    std::uint64_t dropped() const noexcept { return dropped_; }
private:
    int perWindow_, windowMs_;
    std::int64_t windowStart_ = 0;
    int used_ = 0;
    std::uint64_t dropped_ = 0;
};

class Dispatcher
{
public:
    Dispatcher (control::ControlEngine&, ParameterSet&);

    // Traite un paquet reçu. Retourne le nombre de messages appliqués ; les réponses (catalogue) sont ajoutées à `replies`
    // (un datagramme chacune, à renvoyer à l'expéditeur). `limiter` facultatif.
    int handlePacket (const std::uint8_t* data, std::size_t size, std::vector<std::vector<std::uint8_t>>& replies, RateLimiter* limiter = nullptr, std::int64_t nowMs = 0);
    int handleMessage (const Message&, std::vector<std::vector<std::uint8_t>>& replies);
    // Vrai (une seule fois) si un « /neptune/sync » a été reçu depuis le dernier appel ; `port` : celui donné en argument, 0 si absent.
    bool takeSync (int& port) noexcept;

private:
    void catalogue (std::string_view prefix, std::vector<std::vector<std::uint8_t>>& replies);
    control::ControlEngine& control_;
    ParameterSet& set_;
    bool syncRequested_ = false;
    int syncPort_ = 0;
};
} // namespace neptune::core::osc
