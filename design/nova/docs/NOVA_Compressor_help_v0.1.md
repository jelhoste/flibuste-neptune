# NOVA Compressor — textes d'aide « ? » (v0.1)

Note DSP à confirmer : sens exact de EXP / LOG pour les deux formes (l'UI affiche « EXP x % » pour les valeurs négatives et « LOG x % » pour les positives).

Seuls les contrôles qui méritent une explication ont un « ? » (pas THRESHOLD, RATIO, ATTACK, RELEASE, MAKEUP, MIX).

#### comp_feedback_mode
- label : FEEDFORWARD / FEEDBACK
- help_short : Où la détection prend le signal.
- help_long : Feedforward mesure le signal avant la compression : réaction précise, son plus moderne. Feedback le mesure après : réaction plus douce et plus musicale, caractère « vintage » ; le ratio effectif y est un peu moins strict.
- meta : Choix : Feedforward, Feedback

#### comp_attack_shape
- label : ATTACK SHAPE
- help_short : Courbe de la prise de compression (LINEAR / EXP / LOG).
- help_long : Choisit la trajectoire du gain pendant le temps d'attaque. Au centre (LINEAR), le gain descend à vitesse constante en dB. D'un côté (EXP), la prise est progressive puis s'accélère : plus discrète sur le début du son. De l'autre (LOG), elle est vive d'emblée puis ralentit : plus nette, elle freine tout de suite l'attaque.
- meta : Curseur −100…+100 % · LINEAR au centre
#### comp_release_shape
- label : RELEASE SHAPE
- help_short : Courbe du retour à 0 dB (LINEAR / EXP / LOG).
- help_long : Choisit la trajectoire du gain pendant le relâchement. Au centre (LINEAR), il remonte à vitesse constante en dB. EXP et LOG changent la rapidité du début et de la fin de la remontée : une remontée plus rapide au départ rend le compresseur plus discret sur les notes courtes, une remontée plus lente en fin de course la rend plus douce et musicale.
- meta : Curseur −100…+100 % · LINEAR au centre
#### comp_channel_mode
- label : STEREO / DUAL MONO / M-S
- help_short : Comment les canaux sont compressés.
- help_long : Stereo : les deux canaux reçoivent la même réduction de gain, l'image reste stable. Dual Mono : chaque canal est compressé séparément, l'image peut se déplacer. M-S : le centre (mid) et les côtés (side) sont compressés séparément, pratique pour contrôler la largeur.
- meta : Choix : Stereo, Dual Mono, M-S

#### comp_lookahead
- label : LOOKAHEAD
- help_short : Anticipe les attaques ; ajoute de la latence.
- help_long : Retarde le signal pour que le compresseur voie arriver les pics et réagisse avant eux. Plus précis sur les attaques rapides, mais ajoute une latence, reportée à l'hôte.
- meta : Latence = valeur réglée

#### comp_oversampling
- label : OVERSAMPLING
- help_short : Traitement à fréquence plus élevée.
- help_long : Le signal est traité à une fréquence 2× ou 4× supérieure pour limiter les artefacts de repliement des attaques rapides. Plus la valeur est élevée, plus la charge processeur et la latence augmentent.
- meta : Choix : None, 2x, 4x

#### comp_sidechain_source
- label : SIDECHAIN SOURCE
- help_short : Ce que le détecteur écoute.
- help_long : Internal : le signal du plugin lui-même. External : l'entrée sidechain de l'hôte (par exemple la grosse caisse pour pomper une basse). Mix : mélange des deux. LISTEN fait entendre le signal de détection pour vérifier ce qui déclenche la compression.
- meta : Choix : Internal, External, Mix

#### comp_detect
- label : DETECT / RMS WINDOW
- help_short : Mesure de niveau du détecteur.
- help_long : Peak réagit aux crêtes instantanées : contrôle serré des pics. RMS suit l'énergie moyenne : plus proche de la perception, plus lisse. La fenêtre RMS règle la durée de cette moyenne : courte = plus réactif, longue = plus doux.
- meta : Choix : Peak, RMS

#### comp_spectrum_point
- label : Spectrum analyzer — measurement point
- help_short : Où le spectre affiché est mesuré.
- help_long : PRE-EQ mesure le signal avant l'égaliseur de sidechain, POST-EQ après. Pour vérifier l'effet de l'égaliseur sur ce que le détecteur écoute, comparer les deux.
- meta : Choix : PRE-EQ, POST-EQ · Affichage uniquement
