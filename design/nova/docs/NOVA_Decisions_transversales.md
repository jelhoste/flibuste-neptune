# NOVA — décisions transversales (relecture du 2026-10-18)

Prises avec l'utilisateur dans la discussion de relecture. Elles valent pour tous les plugins de la série ; chaque fichier de paramètres les applique.

## 0. Cadre technique
**JUCE + cœur partagé** (et non DPF + NanoVG) : le traitement vit dans le cœur portable de Neptune (`core/`), sert d'insert dans Neptune et de plugin CLAP indépendant (comme Moussaillon et Orgamon) ; l'interface JUCE suit la maquette HTML (référence visuelle et comportementale). Les maquettes et les fichiers de paramètres sont versionnés dans `design/nova/`.

## 1. Transport DSP → interface
Tampon partagé sans verrou (valeurs atomiques / tampons circulaires) lu directement par l'interface à 30 Hz. Aucun paramètre de sortie, aucun `updateState`. S'applique aux enveloppes, historiques, spectres, mesures.

## 2. Vu-mètres `meter_*`
Les mesures ne sont plus des paramètres d'hôte. Seuls `meter_rms_ms` et `meter_peak_decay` sont des réglages enregistrés. Crêtes max / retombée / remise à zéro : interface. RMS rectangulaire côté DSP ; HANN retiré ; attaque et relâchement = affichage. LRA gardée (EBU R128, libebur128), `meter_lufs_standard` retiré. IN après le gain d'entrée. Détail : `NOVA_Common_modules_v0.1.md` §1.

## 3. Sidechain `sc_*`
Fusionné dans `NOVA_Common_modules_v0.1.md` §2 : `sc_source`, `sc_enable`, `sc_integration` (optionnel), EQ de détection à 5 zones, ids réservés `sc_ext_strip` / `sc_ext_tap` pour la source interne de Neptune (routage plus tard). External = entrée sidechain de l'hôte dans un plugin indépendant.

## 4. Exposition à l'automation
Plugin indépendant : tout ce qui est « auto : oui » est exposé à l'hôte (y compris les jeux par canal du compresseur, du delay, les 219 de l'égaliseur). Insert dans Neptune : pas de paramètres d'hôte supplémentaires ; les paramètres d'insert sont des cibles de la couche de contrôle (macros, CC, NRPN, OSC `/neptune/track/N/insert/K/...`, clic droit « Assigner à une macro »).

## 5. Bypass
`<plugin>_bypass` (booléen, automatable) pour chaque plugin, icône d'en-tête câblée, fondu enchaîné de 10 ms, latence déclarée inchangée au basculement. Dans Neptune : le bypass d'insert existant.

## 6. Latence et oversampling
Demi-bandes FIR à phase linéaire, courtes (environ 1 ms à 4x), latence déclarée à l'hôte et modifiée seulement au changement d'oversampling (redémarrage côté hôte, à dire dans l'aide). À « None » : latence nulle. Égaliseur : biquads à phase minimale par défaut (phase linéaire et 8x = options à ajouter plus tard). Neptune n'a pas de compensation de latence entre pistes : un insert avec oversampling décale sa piste (documenté).

## 7. Textes d'aide
À reporter plugin par plugin dans les fichiers `*_help_*.md`, puis réinjecter avec `build_help.py`, au moment de traiter chaque plugin.
