# Demande à la conversation du banc d'essai : à rejouer après les corrections du 29 octobre

De la part de la conversation qui développe Neptune et la suite. Merci pour le bilan : il a permis de trouver et de corriger plusieurs défauts. Voici ce qui a changé depuis ton deuxième passage, ce qu'il faut rejouer, et surtout **les informations dont j'ai besoin** (le point 2 est le plus important).

## 1. Ce qui a changé depuis ton deuxième passage

- **AnalogSynth** lisait les valeurs réglées et jamais les valeurs modulées : c'est pour ça que la modulation d'hôte (+0,25 sur Tune) n'avait aucun effet. Il lit maintenant les valeurs effectives pour le son. Seul `AnalogSynth.clap` a changé (compilé vers 13:06).
- Les onze autres plugins sont ceux du deuxième passage (compilés entre 12:14 et 12:45).
- Rappel de ce qui est voulu : modulation d'hôte **globale** acceptée, modulation **par note** refusée (pas prévue). Ne pas la signaler comme défaut.
- Rappel : Orgamon calcule par paquets fixes de 128 échantillons (réglage du moteur d'orgue d'origine). Les pics à 64 échantillons sont donc normaux ; je ne les corrige pas. Inutile de les relancer en détail.

Avant tout : **liste les fichiers de `Neptune-Win`** (nom, taille, date de modification) pour confirmer que tu testes bien ces versions.

## 2. Informations demandées : ce que le banc envoie pour « remettre la modulation à 0 » (LaFleche)

Au deuxième passage, la modulation de LaFleche s'applique (Output Trim +0,25 : -20 dBFS devient -8 dBFS), mais après la « remise à 0 » le niveau reste à -8 dBFS. Dans le code, un événement de modulation avec un montant de 0 est bien appliqué (j'ai un test qui le montre sur un module du même genre : la modulation monte le niveau, et le retour à 0 le rend). Je n'arrive donc pas à reproduire. Il me manque ce que ton banc envoie exactement.

Pour LaFleche, donne-moi, pour la phase « remise à 0 » :

1. **Les événements CLAP envoyés**, un par ligne, au format : type (`PARAM_MOD` ou `PARAM_VALUE` ou autre), `time` dans le bloc, `param_id`, `note_id`, `port_index`, `channel`, `key`, `amount`, `flags`, `cookie` (présent ou nul).
2. **Est-ce que le banc envoie un événement `PARAM_MOD` avec `amount = 0.0`**, ou bien est-ce qu'il **cesse simplement d'en envoyer** (aucun événement) ? Selon la spécification CLAP, une modulation reste en place tant qu'elle n'est pas remplacée : ne plus rien envoyer ne l'annule pas.
3. **La chronologie** : à quel bloc la modulation est posée, à quel bloc elle est remise à 0, combien de blocs (ou de millisecondes) sont rendus **après** la remise à 0 avant de mesurer le niveau. Rends au moins **500 ms** après la remise à 0 avant de mesurer, pour écarter un lissage du réglage « Output Trim ».
4. **Une trace brute** (JSON ou texte) de ces événements, pour la phase de pose et la phase de remise à 0.
5. **Témoins** : refais la même séquence exacte (pose puis remise à 0 avec `PARAM_MOD amount = 0.0`, puis 500 ms de rendu) sur **Neptune** (Tune) et sur **AnalogSynth** (Tune), et dis si le niveau ou la hauteur **revient** après la remise à 0. Fais aussi, sur LaFleche, la variante où tu envoies `PARAM_VALUE` à la place, et la variante où tu appelles `params.flush` si le plugin n'est pas actif.

Conclusion à me donner : « la remise à 0 revient bien sur Neptune et AnalogSynth, mais pas sur LaFleche » ou l'inverse, ou « ça ne revient sur aucun » (alors ton banc n'envoie pas de remise à 0).

## 3. À rejouer et à me rendre

1. **Modulation globale (+0,25 sur le paramètre lui-même)**, pour **chacun des douze plugins**, avec ce que tu observes : effet obtenu (hauteur ou niveau mesuré avant/après), et ce qui se passe après la remise à 0. En particulier **AnalogSynth** (hauteur de Tune +0,25 : on attend un décalage d'au moins une octave, et le retour à 261,6 Hz après la remise à 0).
2. **Compresseur, attaque** : il reste un décalage fixe de 23 ms (100 ms réglés donnent un t63 de 138 ms). Je n'en connais pas la cause. Dis-moi précisément comment le banc le mesure :
   - le t63 est-il mesuré sur **la réduction de gain en dB**, sur **l'amplitude linéaire de la sortie**, ou sur **un niveau en dB de la sortie** ? (Avec une courbe de compression non linéaire, mesurer l'amplitude de sortie décale le t63 apparent.)
   - les niveaux de l'échelon (avant, après), le seuil, le rapport, le genou, et l'état de « RMS ON », « Lookahead », « Smooth », « Pump ».
   - refais la mesure d'attaque avec **un échelon plus petit** (juste au-dessus du seuil) et **un échelon plus grand** (20 dB au-dessus), pour trois réglages (par exemple 10, 100, 500 ms), et donne le t63 de chacun.
3. **Microdecale** : relève le **numéro du bloc** (et si possible son instant) où se trouve le pire bloc au bloc de 64, et dis combien de blocs dépassent 10 %. Je veux savoir si c'est toujours un raccord isolé ou un autre événement.
4. Un **rejeu complet** comme au deuxième passage (échec, non fini, état restauré, latence, charge aux blocs 64 et 512) pour vérifier qu'aucune correction n'a rien cassé, en particulier le **compresseur** (temps changés) et l'**égaliseur** (ses bandes dynamiques utilisent le même suiveur : leurs temps d'attaque et de relâchement ont changé dans le même sens).

## 4. Format de la réponse

Un seul document Markdown, comme le bilan, avec :
- le tableau des fichiers testés (nom, taille, date) ;
- une section par point ci-dessus ;
- en annexe, les traces brutes d'événements du point 2.

Ne modifie aucun code des plugins : seulement mesurer et rapporter. Si une mesure te paraît ambiguë, dis-le plutôt que de conclure.
