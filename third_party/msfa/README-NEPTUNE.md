# msfa (music-synthesizer-for-android) — moteur FM de type DX7

Source : https://github.com/google/music-synthesizer-for-android (dossier `app/src/main/jni`), commit
`f67d41d313b7dc85f6fb99e79e515cc9d208cfff`. Licence : **Apache 2.0** (`LICENSE-APACHE-2.0.txt`), compatible avec l'AGPLv3 de Neptune.
Ce n'est PAS du code de Dexed (GPLv3) : on n'utilise que le moteur d'origine de Google.

Fichiers utilisés : `aligned_buf.h controllers.h dx7note.* env.* exp2.* fm_core.* fm_op_kernel.* freqlut.* lfo.* patch.* pitchenv.* sin.* synth.h`.
Non utilisés (non copiés) : `synth_unit`, `ringbuffer`, `wavout`, `resofilter`, `sawtooth`, `fir`, `main`, `android_glue`, applications.

## Modifications faites par Neptune (marquées « Neptune » dans les fichiers)

- `env.h/.cc` : `Env::init_sr()` : la durée des segments est ramenée à la fréquence d'échantillonnage réelle (msfa suppose 44,1 kHz).
- `controllers.h` : `pitchbend_range_`, `wheel_pitch_depth_`, `wheel_amp_depth_` fournis par la couche de voix.
- `dx7note.h/.cc` : modulation d'amplitude (AMS / AMD, absente de msfa), profondeur de vibrato de la molette, étendue de pitch bend
  réglable (msfa : 3 demi-tons fixes), masque d'opérateurs actifs (octet 155 du patch), décalages d'unisson (`set_detune`) et de
  glissando (`set_glide`, opérateurs en mode rapport seulement), remise à zéro du tampon de feedback à l'initialisation.

Le tout est compilé en un seul fichier (`core/src/FmMsfa.cpp`, « unity build ») dans l'espace de noms `msfa` : aucun symbole global
de msfa (`N`, `min`, `max`, `Env`...) ne fuit dans le reste du projet.
- `dx7note.*` : `set_level_offset(op, ...)` et `set_feedback(...)` (macros en direct).
- `synth.h` : les modèles `min` / `max` de msfa sont remplacés par ceux de la bibliothèque standard (ambiguïté avec `using namespace std`).
- Les en-têtes sans garde d'inclusion reçoivent `#pragma once` (compilation en un seul fichier).
- `synth.h` : la barrière mémoire (`SynthMemoryBarrier`, inutilisée) est retirée (`#warning` refusé par MSVC).
