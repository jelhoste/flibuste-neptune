# Neptune

**Neptune** est un sampler au format **CLAP**, conçu pour être puissant (mapping à des milliers de Zones, éditeur de sample, synthés intégrés, effets, modulation) et à **basse latence**. Il fait partie de la **Flibuste Suite**, par *le Tournedos Rossini*.

> Statut : **pré-alpha**. Pour l'instant : conception détaillée, maquette visuelle, ossature du projet et CI. Aucun son n'est encore produit.

*English: Neptune is a CLAP sampler (low-latency, large-scale mapping, built-in editor/synths/effects), part of the Flibuste Suite. Pre-alpha: design docs, UI mockup and build scaffolding only.*

## Contenu du dépôt

| Dossier | Rôle |
|---|---|
| `docs/` | Brief, roadmap par paliers, spécifications détaillées des modules |
| `design/` | Maquette visuelle HTML (`neptune_ui.html`) et briques de style partagées |
| `core/` | Cœur du moteur, C++ portable, **sans dépendance à JUCE** |
| `adapter-juce/` | Fine couche JUCE / CLAP autour du cœur |
| `tests/` | Tests unitaires du cœur (Catch2) |
| `cmake/` | Dépendances externes, versions figées |

## Compiler

Prérequis : CMake ≥ 3.21 et un compilateur C++17. Sous Linux, les bibliothèques de développement de JUCE (la liste exacte est dans `.github/workflows/build.yml`).

```bash
# Cœur + tests uniquement (sans JUCE)
cmake -S . -B build -DNEPTUNE_BUILD_PLUGIN=OFF
cmake --build build && ctest --test-dir build

# Plugin CLAP complet
cmake -S . -B build
cmake --build build --target Neptune_CLAP
```

Les binaires Windows, macOS et Linux sont produits par GitHub Actions à chaque push (artefacts du workflow `build`).

## Licence

[GNU AGPLv3](LICENSE). Le projet est open source et gratuit ; JUCE est utilisé sous la même licence.
