# Prompt de départ — nouvelle discussion NOVA (à coller avec l'archive `nova-ui-series_2026-10-16.zip`)

Je développe une série de plugins audio CLAP « NOVA » (DPF + NanoVG). Je suis professeur de batterie ; je parle français.
Je joins l'archive `nova-ui-series_2026-10-16.zip` : 7 maquettes HTML (convolution, shaper de transitoires, compresseur, égaliseur, Hammond/Leslie, chorus, delay), les modules partagés (thèmes, potards, contraste, barre de défilement, redimensionnement, aide « ? »), et dans `docs/` la description des paramètres et du traitement de chaque plugin (`NOVA_<Plugin>_params_*.md`) ainsi que `NOVA_Common_modules_v0.1.md` (vu-mètres `meter_*`, sidechain `sc_*`).

**Méthode** : UI descriptive d'abord, puis code ailleurs. Les fichiers de paramètres sont la référence (ids stables, plages, défauts, échelles, automation, dépendances, textes d'aide). La maquette en dérive. Quand on tranche un point, on met à jour le fichier de paramètres **et** la maquette, puis on réarchive.

**Ce que je veux dans cette discussion** : passer en revue avec toi les points à définir ci-dessous, un plugin à la fois (commence par les points transversaux), me proposer une recommandation argumentée pour chacun, puis répercuter mes décisions dans les fichiers. Questions courtes, une à la fois si possible.

## Points transversaux (à traiter d'abord)
1. **Transport DSP → UI** des enveloppes, historiques, spectres (shaper, compresseur, égaliseur) : sous CLAP, `updateState` est un stub ; les paramètres de sortie ne conviennent pas à des tableaux. Choisir : tampon partagé (UI et DSP dans le même binaire) ou séries sous-échantillonnées exposées par le DSP.
2. **Module vu-mètres `meter_*`** (19 paramètres) : ajouter côté DSP `meter_peak_decay`, `meter_reset` et les 4 crêtes max, ou les faire côté UI ? Profils RMS : fenêtre HANN / attaque / relâchement côté DSP ou affichage seulement ? Garder la barre **LRA** du compresseur (le DSP n'en a pas) et `meter_lufs_standard` ? IN avant ou après le gain d'entrée ?
3. **Module sidechain `sc_*`** : fusionner les descriptions du shaper (§4) et du compresseur (§2.4) dans `NOVA_Common_modules`, arrêter les ids.
4. **Exposition à l'automation** : 36 paramètres de jeux par canal (compresseur), 112 + 14 (delay), 219 (égaliseur) ; tout exposer ou masquer par défaut ? Idem jeux 1 et 2 du shaper.
5. **Bypass** : l'icône d'en-tête n'est pas câblée dans plusieurs maquettes ; confirmer `*_bypass` pour chaque plugin.
6. **Latence reportée** et filtres d'oversampling (phase linéaire ou minimale) : compresseur, égaliseur.
7. **Textes d'aide nouveaux** proposés (égaliseur, Hammond…) à reporter dans les fichiers `*_help_*.md`, puis réinjecter avec `build_help.py`.

## Compresseur
Formes EXP / LOG / LINEAR (aide ≠ maquette) · définition d'ATTACK / RELEASE (63 % ou 90 %) et échelles (log ?) · Feedback : ratio effectif ≤ 2:1, griser lookahead / External ? · `comp_stereo_feed` (MIX proposé) · source MIDI (vélocité → clé) · sens du switch M/S en External · EQ de détection (pentes continues vs pas de 6 dB/oct, Q, place du gain de clé) · plage de THRESHOLD (−60…0 vs +12 de la maquette) · latence et `comp_report_latency` · fenêtre RMS · défauts d'usine (le preset de la maquette n'est pas neutre).

## Égaliseur
Nombre de bandes (16 proposé, ou 8) · assignation des canaux (4 jeux L/R/M/S de la maquette vs champ `eq_bN_channel` de l'aide) · oversampling 8x · fonctions absentes (gain d'entrée/sortie, auto-gain, solo de bande, phase linéaire, A/B/C/D) · pentes (continues vs Butterworth par pas de 6 dB/oct) · formule du gain des bandes dynamiques · échelles Q / attack / release · latence · défauts et contraintes de glisser (HP ≤ 4 kHz, LP ≥ 100 Hz) · analyseur (48 bandes → 96–128 ?).

## Hammond / Leslie
Pages LESLIE et SETTINGS **vides** dans la maquette : à concevoir (vitesses, accélérations, micros, croisement, key click, précision, oversampling) · routage MIDI (sens des splits, interaction avec les canaux) · table de messages du CTRL CH · contenu réel d'un preset (la maquette ne stocke que les tirettes) · `chainBanks`, `leakage`, 3ᵉ valeur du pédalier : supprimer ou câbler · percussion, loi des tirettes, vibrato/chorus (rôle de VIB DEPTH), drive, ordre de la chaîne (Leslie avant ou après la réverbération), réverbération, VOLUME (limiteur ?) · automation · sorties MIDI et lampe de la Leslie · claviers de l'interface qui jouent ou non · affichage « − » à corriger pour TRANSP et SPLIT.

## Shaper de transitoires
Formule du mode auto de SHAPE SMOOTHING · réglages de détecteurs en Settings · abandon de PEAK SAFE · exposition à l'automation des jeux 1 et 2.

## Chorus
Valeurs des presets Juno-60 à caler à l'écoute et par mesure · formule de DEPTH relatif au retard et plage de DELAY · feedback après filtres et BBD · bruit et saturation du caractère BBD.

## Delay
Décaleur de hauteur (deux grains croisés, coût de 8 lignes × 2 canaux, qualité High) · compensation de son retard propre · cascade : temps cumulés, plafond (8 s ?) · modulation : excursion ±8 ms et phases initiales · automation des 126 paramètres · R OFFSET : retard réel du droit borné à `max(0, T + OFFSET)` (décidé : pas de look-ahead).

## Convolution
Aucun point ouvert côté paramètres ; vérifier que `LowLatConvParams.h` reste cohérent avec le module `meter_*` une fois les points 2 et 3 tranchés.

## Livrables attendus à la fin
Fichiers de paramètres mis à jour, maquettes corrigées si besoin, `NOVA_Common_modules` complété, archive datée régénérée.
