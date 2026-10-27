# Étude : importer des orgues GrandOrgue dans Neptune

Rédigée le 2026-10-27. Sources lues (lecture seule) : dépôts `GrandOrgue/grandorgue` (GPL v2 ou ultérieure) et `GrandOrgue/GoOdf` (GPL v3), et le **petit orgue de démonstration** de GrandOrgue (`packages/demo-4232...orgue`, 21 Mo, publié sous GPL v2 et CC BY-SA 2.5), téléchargé et examiné dans `~/Bureau/FlibusteSuite/ref-grandorgue/demo-orgue/` (hors du dépôt, rien exécuté). Rien n'est encore codé pour l'import.

## 1. Le format

- **ODF** (`.organ`) : fichier texte de type INI (fins de ligne Windows). Sections `[Organ]`, `[ManualNNN]` (`Manual000` = pédalier), `[StopNNN]`, `[RankNNN]`, `[CouplerNNN]`, `[TremulantNNN]`, `[SwitchNNN]`, `[EnclosureNNN]`, `[WindchestGroupNNN]`, `[GeneralNNN]`, `[PanelNNN]`... Les jeux modernes sont commandés par des interrupteurs (`Function=And`, `SwitchCount`, `SwitchNNN`) : les tirants à l'écran sont des interrupteurs.
- **Rangs** : `PipeNNN=chemin\echantillon.wav` (séparateur `\`), `PipeNNNHarmonicNumber`, `PipeNNNPitchTuning` (centièmes), `AmplitudeLevel`, `FirstMidiNoteNumber`, `WindchestGroup`. Un même fichier peut servir à plusieurs tuyaux avec des accords différents (le démonstrateur le fait).
- **Paquet `.orgue`** : archive zip (celle du démonstrateur est « stockée », sans compression) contenant `organindex.ini`, le `.organ`, les dossiers d'échantillons et `Images/`.
- **Fichiers `.cmb`** : combinaisons de l'utilisateur (GoOdf a un analyseur, `CmbParser`).

## 2. Ce que contient le démonstrateur (mesuré)

- 3 claviers (pédalier + 2 manuels), 47 jeux, 29 interrupteurs (tirants), 6 rangs, 5 accouplements, 4 sommiers, 2 boîtes expressives (`AmpMinimumLevel`), 1 tremblant (`Period`, `AmpModDepth`, `StartRate`, `StopRate`), 6 généraux, 2 panneaux.
- **100 fichiers d'échantillons, tous au format WavPack** (signature `wvpk`) **même s'ils s'appellent `.wav`** : 16 bits, stéréo, 44,1 kHz (84 fichiers), 22,05 kHz (14), 16 kHz (2) ; durée de 73 657 à 488 354 échantillons.
- **Chaque fichier contient un bloc de boucle (`smpl`) et des repères (`cue`)** : la boucle tient la note, le repère de relâchement (le plus grand `cue`, d'après `GOWave.cpp`) marque où commence la queue enregistrée du tuyau. Le fichier ODF ne dit rien de plus (aucune clef `Loop`, `Attack` ou `Release`).

## 3. Correspondance avec Neptune

| GrandOrgue | Neptune | État |
|---|---|---|
| Jeu | Groupe + son interrupteur (modulateur) | existe |
| Tuyau | Zone (fichier, touche, `rootNote`, `tuneCents`, `gainDb`) | existe |
| Boucle `smpl` | Boucle de zone (+ fondu) | existe (import `smpl` des WAV) |
| Clavier | Piste + canal MIDI | existe |
| Accouplement normal | Règle du routeur | existe |
| Accouplement basse / mélodie | — | **à ajouter** (la note la plus grave / la plus aiguë) |
| Généraux / divisionnels | Combinaisons | existe |
| Interrupteur | Modulateur | existe |
| Tremblant (oscillateur) | LFO de la matrice | existe (à brancher) |
| Boîte expressive | Contrôle de volume (CC) | à relier |
| Queue de relâchement enregistrée (`cue`) | — | **à ajouter** : au Note Off, passer (fondu) au repère et jouer jusqu'à la fin |
| Relâchement selon la durée d'appui (`MaxKeyPressTime`) | — | **à ajouter** : durée d'appui mini / maxi sur les zones de relâchement |
| Panneaux (images, positions) | Façade | existe (reconstruction à écrire) |
| **Lecture du WavPack** | libsndfile (WAV, AIFF, FLAC), Opus | **à ajouter** |
| Paquets `.orgue` (zip) | `juce::ZipFile` (côté adaptateur : presets, packs de style) | existe ; à extraire sur disque une fois |

## 4. Décisions et avis

- **WavPack natif, plutôt qu'un outil de conversion.** Bibliothèque libwavpack (licence BSD, C, petite ; `libwavpack-dev` 5.6 existe sous Ubuntu mais il faut la figer et la tirer par CMake pour Windows et macOS). Un convertisseur obligerait à avoir un décodeur quand même (wvunpack / ffmpeg), doublerait l'espace disque, et couperait le lien avec les fichiers d'origine. Première version : décodage en mémoire (comme l'Opus) ; le streaming disque viendra pour les gros orgues. Les boucles et repères se lisent dans l'enveloppe RIFF que WavPack garde en métadonnées.
- **Paquets zip** : extraire une fois dans un dossier de la bibliothèque d'orgues (les zones référencent des fichiers sur disque). Zip64 (paquets de plus de 4 Go) : à vérifier avec `juce::ZipFile`.
- **Pas de Lua.** Raisons : exécuter un script dans le fil audio risque des coupures (allocations, ramasse-miettes) ; l'exécuter ailleurs ajoute de la latence aux notes ; il faut une interface (éditeur, erreurs), une sécurité (presets partagés), une documentation et un support durable ; et la feuille de route (palier 10) a déjà tranché pour des règles à menus, sans vrai langage. **Le relâchement selon la durée d'appui se fait nativement** (condition sur la zone), plus simple et plus fiable. À revoir seulement si un vrai besoin apparaît.
- **Échelle** : viser des orgues petits à moyens (le démonstrateur d'abord). Les très gros orgues restent le domaine de GrandOrgue.
- **Licence** : on lit le format, on écrit notre importeur nous-mêmes ; les échantillons restent les fichiers de l'utilisateur (on ne les redistribue pas). Le démonstrateur est libre (GPL v2+ / CC BY-SA 2.5).

## 5. Plan

1. Relâchement par durée d'appui sur les zones de relâchement, et queue de relâchement dans le même fichier (repère `cue`). Tests dans le cœur.
2. Lecture du WavPack (couche d'accès aux échantillons, métadonnées `smpl` / `cue`), puis paquets `.orgue`.
3. Accouplements basse et mélodie dans le routeur.
4. Lecteur ODF dans le cœur (INI + sections ci-dessus), avec un abstrait « fournisseur de fichiers » pour les tests, puis construction : claviers, jeux en groupes, tuyaux en zones, accouplements, combinaisons, interrupteurs.
5. Façade depuis les panneaux ; tremblant, boîtes expressives.
6. Essai : porter le démonstrateur (3 claviers, 47 jeux). Mesurer la charge et la mémoire ; l'utilisateur fournira de plus gros orgues ensuite.

À vérifier à l'usage : si Neptune charge une seule fois un fichier référencé par plusieurs zones (le démonstrateur en réutilise beaucoup) ; combien de voix simultanées tiennent avec 40 jeux tirés et 10 doigts.
