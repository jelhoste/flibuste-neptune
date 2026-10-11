#pragma once
// Observations de ce que l'hôte nous envoie réellement (palier 0, point 4) : transport, notes CLAP, expressions de note,
// modulation de paramètres. Écrit par le thread audio, lu par la fenêtre de sonde. Aucune logique métier.
#include <array>
#include <atomic>
#include <cstdint>

struct HostProbe
{
    // Transport / tempo (rempli dans processBlock via AudioPlayHead)
    std::atomic<bool> hasTransport { false };
    std::atomic<bool> playing { false };
    std::atomic<double> bpm { 0.0 };
    std::atomic<double> ppq { 0.0 };
    std::atomic<int> timeSigNum { 0 };
    std::atomic<int> timeSigDen { 0 };

    // Blocs audio
    std::atomic<int> blocks { 0 };
    std::atomic<int> lastBlockSize { 0 };
    std::atomic<int> outputChannels { 0 };

    // Notes : par la voie MIDI de JUCE, et par les événements CLAP (avec note_id)
    std::atomic<int> midiNoteOns { 0 };
    std::atomic<int> clapNoteOns { 0 };
    std::atomic<int> clapNoteOffs { 0 };
    std::atomic<int> lastNoteId { -2 }; // -2 : aucune note CLAP reçue ; -1 : l'hôte n'a pas fourni d'identifiant
    std::atomic<int> lastKey { -1 };
    std::atomic<int> noteEndsSent { 0 };

    // Expressions de note CLAP : indice = clap_note_expression (0 volume, 1 pan, 2 tuning, 3 vibrato, 4 expression, 5 brightness, 6 pressure)
    static constexpr int numExpressions = 7;
    std::array<std::atomic<int>, numExpressions> expressionCount {};
    std::array<std::atomic<double>, numExpressions> expressionLast {};

    // Modulation de paramètres (CLAP_EVENT_PARAM_MOD)
    std::atomic<int> monoModCount { 0 };
    std::atomic<int> polyModCount { 0 };
    std::atomic<double> lastModAmount { 0.0 };
    std::atomic<int> lastPolyNoteId { -2 };
};
