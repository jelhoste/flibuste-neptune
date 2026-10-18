# ZL Audio (ZLCompressor, ZLEqualizer) : algorithmes portés

Ce dossier ne contient AUCUN fichier de ZL : il documente ce qui a été porté dans le cœur de Neptune.

- Auteur : zsliu98 (ZL Audio). Dépôts : https://github.com/ZL-Audio/ZLCompressor et https://github.com/ZL-Audio/ZLEqualizer.
- Licence d'origine : **GNU Affero General Public License v3** (les logos « ZL Audio » et « ZL Compressor » en sont exclus et ne sont pas repris). Neptune est
  aussi AGPLv3 : les deux licences sont compatibles. Les fichiers portés conservent la mention du copyright d'origine dans leur en-tête.
- État de la copie consultée : dépôt cloné le 2026-10-18 (branche `main`), compresseur version 0.6.x.

## Ce qui a été porté (réécrit, pas copié)

| Source ZL | Fichier chez nous | Écarts |
|---|---|---|
| `source/dsp/compressor/computer/*` | `core/include/neptune/core/DynamicsDsp.h` (`GainComputer`) | une seule classe pour compression / « shape » / expansion / gonflement, double précision, sans atomiques ; mêmes formules |
| `follower/ps_follower.hpp` | `DynamicsDsp.h` (`Follower`) | mêmes formules (attaque, relâchement, pump / punch, smooth) ; états lus à l'exécution plutôt qu'en gabarits |
| `tracker/rms_tracker.hpp` | `DynamicsDsp.h` (`RmsTracker`) | idem |
| `clipper/tanh_clipper.hpp` | `DynamicsDsp.h` (`TanhClipper`) | idem |
| `container/circular_minmax_buffer.hpp` (maintien) | `DynamicsDsp.h` (`MinHold`) | file monotone, sans allocation |
| `styles/clean, classic, optical, vocal` | `core/src/CompressorModule.cpp` (`styleProcess`, `rmsProcess`) | mêmes quatre styles ; la branche RMS est le style Clean |
| `zlp/compress_controller.cpp` (chaîne de traitement) | `CompressorModule::process` | même ordre : clé -> filtres -> lookahead -> M/S -> suréchantillonnage -> réduction, maintien, liaison, plage, wet, échange -> écrêteur -> makeup -> delta |

## Ce qui est différent de ZL (volontairement)

- Pas de Highway (SIMD manuel) : le compilateur vectorise ; le code compile avec MSVC, GCC et AppleClang.
- Suréchantillonnage : demi-bandes FIR à phase linéaire de Neptune (2x et 4x, `Oversampler.h`), pas les demi-bandes IIR de ZL (jusqu'à 64x).
- Filtres de la clé : biquads RBJ et cascades de Butterworth (`Biquad.h`) à la place des structures TDF / SVF de ZL ; l'inclinaison (Tilt) est approchée
  par deux étagères ; l'ordre des étagères, des cloches, du coupe-bande et du passe-bande est toujours de 2.
- Pas d'analyseur FFT ni de correspondance de sonie (LUFS matcher) dans le module : l'affichage est séparé.
- Le bouton « Compressor ON » de ZL est le bypass commun des modules (`stock::bypass`) ; « Wet » (part de la réduction) est conservé en plus du mix
  audio commun.
- La plage de « Pump » est bipolaire (négatif = punch), l'attaque et le relâchement vont de 0 à 1000 ms et 0 à 5000 ms.

## Références citées par ZL

D. Giannoulis, M. Massberg, J. D. Reiss, *Digital dynamic range compressor design - A tutorial and analysis*, JAES (2012) ; M. Vicanek, *Matched
One-Pole Digital Shelving Filters* (2019) et *Matched Second Order Digital Filters* (2016) ; N. Redmon, *Cascading filters* (2016) ; C. Moler,
*Makima Piecewise Cubic Interpolation* (2019).

# ZLEqualizer (https://github.com/ZL-Audio/ZLEqualizer, AGPLv3, zsliu98) : égaliseur porté

| Source ZL | Fichier chez nous | Écarts |
|---|---|---|
| `dsp/filter/filter_design/filter_design.hpp` | `core/src/EqualizerModule.cpp` (`designFilter`) | mêmes types, mêmes ordres (1, 2, 4, 6, 8, 12, 16), même étalement des Q pour les pentes raides, mêmes paires d'étagères pour les cloches d'ordre > 2 ; sections RBJ / premier ordre (`Biquad.h`) au lieu des structures TDF, SVF, parallèle d'Ivantsov |
| `dsp/filter/gain_compensation/gain_compensation.hpp` | `EqualizerModule.cpp` (`gainCompensationDb`) | mêmes formules et coefficients |
| `dsp/filter/dynamic_filter/dynamic_side_handler.hpp`, `dynamic_base.hpp` | `EqualizerModule::processBand` | même rapport niveau -> glissement entre le gain et le gain cible (carré de la rampe seuil - genou -> seuil + genou), suiveur attaque / relâchement / lissage, fenêtre RMS ; les coefficients sont recalculés par blocs de 16 échantillons (ZL les met à jour à chaque échantillon avec un cache trigonométrique) |
| `zlp/zlp_definitions.hpp` (paramètres) | idem (`bandParam`) | 16 bandes (ZL : 24), sans « Flat Tilt », sans structure de filtre (toujours phase minimale), sans phase linéaire (FIR) ni analyseur, sans `Static Gain`, `Dynamic Learn`, `Dynamic Relative` ni `Side Swap` |
- Inclinaison (Tilt) : positif relève les aigus et abaisse les graves de moitié du gain de chaque côté (choix de Neptune, ZL ne précise pas le sens dans le code lu).
- Filtre de détection d'une bande : BP, LP, HP ou AL (aucun), avec le même ordre et les mêmes pentes ; « Side Link » le cale sur la fréquence et le Q de la bande.
