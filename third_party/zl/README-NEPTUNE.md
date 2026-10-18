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
