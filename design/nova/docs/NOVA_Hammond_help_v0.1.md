# NOVA Hammond — textes d'aide « ? » (v0.1)

#### ham_drawbars
- label : DRAWBARS
- help_short : Les tirettes règlent le mélange des harmoniques.
- help_long : Chaque tirette ajoute une hauteur, nommée par sa longueur de tuyau en pieds (16', 8', 4'…). 8' est la note jouée ; 16' est une octave plus grave ; 4' une octave plus aiguë ; les autres (5⅓', 2⅔', 2', 1⅗', 1⅓', 1') ajoutent des harmoniques. Plus la tirette est sortie (0 à 8), plus la hauteur est présente.
- meta : 9 tirettes, 0…8 par manuel

#### ham_perc_harmonic
- label : PERCUSSION HARMONIC
- help_short : Harmonique ajoutée au début de la note.
- help_long : La percussion ajoute un claquement bref qui s'éteint, sur une harmonique : 2nd (octave au-dessus) ou 3rd (octave + quinte au-dessus). La 3rd est plus brillante et plus pointue.
- meta : Choix : 2nd, 3rd

#### ham_perc_decay
- label : PERCUSSION DECAY
- help_short : Durée du claquement percussif.
- help_long : Fast : le claquement disparaît très vite, attaque sèche et rythmique. Slow : il dure davantage et sonne comme un son de cloche.
- meta : Choix : Fast, Slow

#### ham_perc_volume
- label : PERCUSSION VOLUME
- help_short : Niveau du claquement percussif.
- help_long : Soft : percussion plus discrète. Normal : percussion bien présente. Elle s'active avec le sélecteur OFF / ON de PERCUSSION.
- meta : Choix : Soft, Normal
#### ham_chorale
- label : CHORALE / CHOR · TREM · BRK
- help_short : Réglages de la cabine Leslie.
- help_long : CHOR (Chorale) : rotation lente des rotors, effet d'ensemble doux. TREM (Tremolo) : rotation rapide, vibrato intense. BRK (Break) : la Leslie s'arrête de tourner ; le son devient fixe, comme un haut-parleur ordinaire. Le passage d'une vitesse à l'autre est progressif, comme l'inertie des rotors.
- meta : Choix : Chor, Trem, Brk

#### ham_split
- label : SPLIT UPPER / SPLIT LOWER / TRANSP
- help_short : Partage du clavier et transposition par manuel.
- help_long : SPLIT UPPER et SPLIT LOWER sont des numéros de note MIDI (0 à 127, « − » = désactivé) qui délimitent la zone de chaque manuel. TRANSP décale chaque manuel (Upper, Lower, Pedal) de −24 à +24 demi-tons, par exemple pour jouer une ligne de basse à la main gauche.
- meta : Split : 0…127 ou off · Transposition : ±24 demi-tons
#### ham_matrix
- label : GRILLE / COLONNES / LIGNES
- help_short : Taille de la matrice de presets.
- help_long : La matrice affiche les presets sous forme de dalles. COLONNES et LIGNES règlent combien de dalles sont visibles : plus il y en a, plus chacune est petite. Cela ne change que l'affichage, pas le son. Un appui long sur les flèches les déverrouille.
- meta : Affichage uniquement
#### ham_bank_preset
- label : BANK / PRESET
- help_short : Banques et presets de sons.
- help_long : Un preset mémorise tous les réglages (tirettes, percussion, Leslie…). Les presets sont rangés en banques : les flèches de BANK changent de banque, PRESET charge un son de la banque courante. Charger un preset remplace les réglages en cours.
- meta : Sélection par banque puis preset
