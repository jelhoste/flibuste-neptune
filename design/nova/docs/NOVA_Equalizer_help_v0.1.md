# NOVA Equalizer — textes d'aide « ? » (v0.1)

#### eq_dynamic
- label : DYNAMIC
- help_short : Bande dont le gain suit le niveau du signal.
- help_long : Rend la bande dynamique : son gain évolue selon le niveau dans la bande, comme un compresseur ciblé sur une plage de fréquences. Utile par exemple pour calmer une sibilance seulement quand elle apparaît, au lieu de couper en permanence.
- meta : Interrupteur par bande

#### eq_band_type
- label : TYPE DE BANDE
- help_short : Forme du filtre de la bande.
- help_long : Bell : bosse ou creux centré sur une fréquence. Low Shelf / High Shelf : relève ou abaisse tout le côté grave ou aigu. High Pass / Low Pass : retirent les graves ou les aigus ; leur pente (SLOPE, en dB par octave) règle la rapidité de la coupure : plus elle est forte, plus la coupure est nette. Les filtres n'ont pas de gain et ne peuvent pas être dynamiques.
- meta : Choix : Bell, Low Shelf, High Shelf, High Pass, Low Pass · Pente 6…48 dB/oct
#### eq_channel_mode
- label : STEREO / DUAL MONO / M-S
- help_short : Quels canaux les bandes traitent.
- help_long : Stereo : chaque bande agit de la même façon sur les deux canaux. Dual Mono : chaque bande est assignée à la gauche (L) ou à la droite (R), pour égaliser les deux côtés différemment. M-S : chaque bande est assignée au centre (M, mid) ou aux côtés (S, side), pour agir sur la largeur stéréo sans toucher à l'autre. Le bouton à côté du titre de la courbe choisit quel canal est affiché.
- meta : Choix : Stereo, Dual Mono, M-S

#### eq_oversampling
- label : OVERSAMPLING
- help_short : Traitement à fréquence plus élevée.
- help_long : Limite les déformations des aigus près de la fréquence maximale et les artefacts des bandes dynamiques. Plus la valeur est élevée, plus la charge processeur et la latence augmentent.
- meta : Choix : None, 2x, 4x

#### eq_spectrum_point
- label : Measurement point
- help_short : Où le spectre affiché est mesuré.
- help_long : PRE-EQ mesure le signal avant l'égalisation, POST-EQ après. Comparer les deux permet de voir exactement ce que fait l'égaliseur.
- meta : Choix : PRE-EQ, POST-EQ · Affichage uniquement
