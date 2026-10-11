# Test de performance du callback audio

Outil : `tests/perf_callback.cpp` → exécutable `neptune_perf_callback` (cible CMake, **hors ctest**). En CI : étape informative, jamais bloquante (`continue-on-error`), qui archive `perf-<OS>.json` dans l'artefact `Perf-<OS>` ; les runners étant partagés, ces temps ne servent qu'à repérer une dérive grossière.

## Ce qui est mesuré

`Engine::process()` du cœur seul (pas l'adaptateur JUCE : il ne se compile pas en local). 128 canaux (64 sorties stéréo), 48 kHz. Dix scénarios : blocs de 32, 64, 128, 256, 512 échantillons × `idle` (aucune note, aucun paramètre) et `loaded` (64 notes tenues, 64 `setParameter` avant chaque bloc, sur les identifiants réels du pool sonde). Par scénario : médiane, min et max sur 21 répétitions (temps par bloc) et part du budget temps réel (durée du bloc).

Limite actuelle : le moteur ne produit que du silence, donc les chiffres mesurent surtout l'écriture de 128 tampons. Le tableau devient significatif au fur et à mesure que le moteur grossit ; les scénarios sont à enrichir en même temps (voix, pistes, inserts).

## Utilisation (avant/après une refactorisation, sur la même machine)

```bash
cmake -S . -B build -G Ninja -DNEPTUNE_BUILD_PLUGIN=OFF && cmake --build build
./build/tests/neptune_perf_callback --save avant.json      # sur le code d'avant
./build/tests/neptune_perf_callback --compare avant.json   # sur le code d'après (code de sortie 1 si régression)
```

`--tolerance N` règle le seuil en % sur la médiane (défaut 25). Les fichiers de référence ne sont **pas** versionnés : les temps dépendent de la machine. Constaté sur la machine de développement (Linux, 4 cœurs) : deux exécutions successives diffèrent de 2 % au plus.
