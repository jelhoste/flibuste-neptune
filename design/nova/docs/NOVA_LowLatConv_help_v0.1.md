# NOVA LowLatConv — textes d'aide « ? » (v0.1)

Format identique à `NOVA_TransientShaper_params_v0.2.md` : un bloc `####` par contrôle, `label`, `help_short` (infobulle), `help_long` (panneau), `meta`.
L'identifiant est celui du contrôle de l'UI ; à rapprocher du paramètre DSP indiqué.

#### conv_input_mode
- label : INPUT
- param : pInputMode
- help_short : Comment les canaux de l'IR sont utilisés.
- help_long : Stereo garde les deux canaux de la réponse impulsionnelle tels quels. Sum les additionne en un seul canal, Left et Right ne gardent qu'un côté. Utile pour une IR mono ou pour neutraliser un côté d'une IR stéréo.
- meta : Choix : Stereo, Sum, Left, Right · Non automatisable

#### conv_stretch
- label : STRETCH
- param : pStretch
- help_short : Allonge ou raccourcit l'IR dans le temps.
- help_long : Rééchantillonne la réponse impulsionnelle : au-dessus de 100 %, la réverbération dure plus longtemps (et devient plus grave) ; en dessous, elle est plus courte (et plus aiguë). À 100 %, l'IR est inchangée.
- meta : 50…200 % · Défaut 100 · Non automatisable

#### conv_auto_trim
- label : AUTO TRIM
- param : pAutoTrim
- help_short : Retire le silence au début de l'IR.
- help_long : Supprime automatiquement le vide avant le premier son de la réponse impulsionnelle. Sans cela, ce silence ajoute un retard à toute la réverbération. BEGIN reste utilisable en plus.
- meta : Interrupteur · Non automatisable

#### conv_begin
- label : BEGIN
- param : pBegin
- help_short : Début de la portion d'IR utilisée.
- help_long : Coupe le début de la réponse impulsionnelle. Se règle aussi en déplaçant la poignée « B » sur la vue WAVE. Appliqué au relâchement.
- meta : 0…500 ms · Défaut 0 · Non automatisable

#### conv_end
- label : END
- param : pEnd
- help_short : Fin de la portion d'IR utilisée.
- help_long : Coupe la queue de la réponse impulsionnelle pour raccourcir la réverbération. OFF (0) signifie qu'aucune coupe n'est faite. Se règle aussi avec la poignée « E » sur la vue WAVE. Appliqué au relâchement.
- meta : OFF…10 s · Défaut OFF · Non automatisable

#### conv_auto_len
- label : AUTO LEN
- param : pAutoLen
- help_short : Coupe automatiquement la queue inaudible.
- help_long : Détecte la fin utile de l'IR (là où elle devient inaudible) et supprime ce qui suit. Réduit la charge processeur sans changer le son perceptible. END reste utilisable en plus.
- meta : Interrupteur · Non automatisable

#### conv_attack
- label : IR ATTACK
- param : pAttack
- help_short : Adoucit le début de l'IR (fondu d'entrée).
- help_long : Applique un fondu d'entrée sur le début de la réponse impulsionnelle. Une valeur plus longue adoucit l'attaque de la réverbération : utile pour éviter un claquement initial ou pour obtenir un effet « gonflé ».
- meta : 0…500 ms · Défaut 0 · Non automatisable

#### conv_decay
- label : IR DECAY
- param : pDecay
- help_short : Raccourcit la queue de l'IR par une pente de décroissance.
- help_long : Applique une décroissance supplémentaire sur la fin de la réponse impulsionnelle : plus la valeur est élevée, plus la réverbération s'éteint vite. À 0, l'IR n'est pas modifiée. Contrairement à END, la coupe est progressive et ne produit pas de saut.
- meta : 0…60 dB · Défaut 0 · Non automatisable

#### conv_norm
- label : NORM
- param : pNorm
- help_short : Normalise le niveau de l'IR.
- help_long : OFF laisse l'IR telle quelle. PEAK ajuste le niveau pour que la crête de l'IR atteigne un niveau fixe. ENERGY ajuste selon l'énergie totale : le volume perçu reste cohérent d'une IR à l'autre, même si leurs longueurs diffèrent beaucoup.
- meta : Choix : OFF, PEAK, ENERGY · Non automatisable

#### conv_pre_delay
- label : PRE-DELAY
- param : pPreDelay
- help_short : Retard avant le début de la réverbération.
- help_long : Retarde uniquement le signal wet (la réverbération), pas le signal direct. Quelques dizaines de millisecondes séparent le son sec de sa réverbération et gardent la clarté. Ce retard n'est pas de la latence : le plugin reste à latence nulle.
- meta : 0…2000 ms (réglage non linéaire) · Défaut 0 · Automatisable

#### conv_eq_wet
- label : EQ de sortie
- param : pages 2
- help_short : Cet égaliseur ne traite que le signal wet.
- help_long : L'égaliseur de cette page agit uniquement sur la réverbération, pas sur le signal sec (DRY). Il permet par exemple de retirer les graves d'une salle ou d'adoucir les aigus sans toucher au son direct.
- meta : Wet uniquement
