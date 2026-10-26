# À valider dans Reaper — travaux du 2026-10-23 au 25

Liste à cocher, dans l'ordre. Pour chaque point : quoi faire, ce que vous devez voir. Un point qui ne se passe pas comme décrit est un défaut à signaler. Les anciens points « à valider » des paliers précédents restent dans `docs/passage-de-relais.md` (chaque section porte sa date).

## Matrice de modulation (onglet MOD)
- [ ] **Légendes** : au-dessus des LFO (forme, vitesse en Hz, phase en %) et des routes (source, n°, canal, quantité, plage, destination).
- [ ] **Plage de la source** : le bouton à deux états « 0..1 / -1..+1 » ; grisé et fixé à -1..+1 pour un LFO ou le pitch bend ; une quantité négative inverse l'effet.
- [ ] **Enveloppes** : boutons ENV 1 à 4, éditeur graphique (délai, attaque, maintien, decay, sustain, relâchement, courbes), étiquette « ENV n » en haut à droite ; l'enveloppe suit les notes de la piste.
- [ ] **Anneau de modulation** sur les potards de Neptune et de la Façade : il bouge avec le LFO ou l'enveloppe, la valeur réglée ne bouge pas, et l'automation de l'hôte ne voit pas la modulation.
- [ ] **Anneau dans les éditeurs d'effets** (insert en NOVA : compresseur, égaliseur, délai, chœur...) : une route vers un paramètre d'insert fait bouger l'anneau du potard. *(non vérifié visuellement)*
- [ ] **Destinations** : volume/pan/envois de la bande ; réglages du Sampler ; paramètres continus du synthé, du FM et de l'orgue ; paramètres des inserts (la recherche de l'onglet MOD les propose).
- [ ] **Filtres des listes** : menu de portée (piste, parallèle, bus, Master, global) et pastilles de familles, dans l'onglet MOD et dans le sélecteur de la Façade.
- [ ] **Constat à trancher** : moduler `Track N Filter n Env Attack` ne bouge rien à l'écran de la page Filtres (elle dessine l'enveloppe de la description, pas le décalage) et n'agit qu'au Note On suivant. Fantôme de l'enveloppe modulée voulu ? *(décision à prendre)*
- [ ] **Charge CPU** : avec une matrice et beaucoup de voix, pas de craquement (le bloc est découpé en morceaux de 64 échantillons ; mesure : environ 13 % de plus sur 32 voix).
- [ ] **FM extrême** : « FM Op N Level » à -99 sur les six opérateurs (ou modulé) ne produit plus de crête énorme.

## Façade
- [ ] **Échelle et courbe libre** d'un lien (bouton « Échelle... » : échelle du paramètre / linéaire / logarithmique / exponentielle + courbe, avec OK / Annuler) : la position du potard suit la courbe choisie, l'anneau de modulation aussi ; seuls les paramètres continus sont concernés.
- [ ] **Vumètre multi-sources** : piste, bus, piste parallèle ou Master au choix ; **réduction de gain** d'un compresseur inséré.
- [ ] **Façade dans un preset** (validé par vous le 2026-10-25) : enregistrer, recharger sur une autre piste, annuler d'un coup.
- [ ] **Banque `.flibank`** : exporter une banque dont un preset a une Façade avec images, l'importer sur une autre installation (ou après avoir vidé `facade-assets/`) : les images reviennent.
- [ ] **`.flipreset` isolé** : la Façade se charge, images manquantes à relocaliser (« Relocaliser... »).

## Export de piste `.neptunetrack`
- [ ] **Exporter** depuis le bouton de type d'une piste (Sampler avec samples, insert de convolution avec IR, modulation, Façade) : le message annonce le nombre de fichiers, images et packs embarqués.
- [ ] **Importer** : une nouvelle piste apparaît, sélectionnée, avec son instrument, ses inserts, sa modulation (routes vers sa propre piste) et sa Façade (liens rebranchés), son canal MIDI à elle.
- [ ] **Sur une autre machine** (ou après déplacement des samples d'origine) : les samples viennent de `imported-tracks/`.
- [ ] **16 pistes prises** : message « Les 16 pistes sont prises ».
- [ ] **Annuler** après l'import : l'historique est vidé (comportement voulu).

## Preset global (bouton « Preset global », rangée du bas)
- [ ] **Placement** : le bouton à gauche de Annuler ne chevauche pas le texte du profil de performance ; sinon dites où le mettre.
- [ ] **Enregistrer** : nom demandé, confirmation avant d'écraser, message avec le nombre d'images et de packs embarqués.
- [ ] **Charger** dans un autre projet Neptune : confirmation, puis tout est remplacé (pistes, instruments, inserts, modulation, Façades avec leurs images, bus, Master, valeurs) ; Annuler est vidé ; les assignations CC / OSC et le verrou de profil de l'instance ne changent pas.
- [ ] **Samples** : ils se rechargent depuis leurs chemins d'origine (non copiés dans le fichier).
- [ ] **Supprimer** : le fichier va à la corbeille du système. **Ouvrir le dossier** : ouvre `GlobalPresets/`.
- [ ] **Sauvegarde du projet Reaper** après un chargement : le projet se rouvre avec le même état.

## Modulation des paramètres par pas
- [ ] **Tirettes de l'orgue** : une route LFO (quantité 0,5, centrée) vers `Track N Upper 8'` : la registration se balade autour de la valeur réglée, sans clic gênant ; la tirette affichée garde sa valeur réglée.
- [ ] **Interrupteur** : une route CC vers `Track N Mute` ou `Percussion` : l'interrupteur bascule quand le CC dépasse la moitié ; la valeur réglée ne change pas.
- [ ] **Choix** : une route vers `Leslie Speed` ou `Vibrato` : les positions défilent avec la source.
- [ ] **FM / Sampler** : une route vers `FM LFO Speed` ou `Tune (semitones)` (LFO carré : arpège de hauteurs).
- [ ] **Liste de l'onglet MOD** : les paramètres par pas apparaissent maintenant ; `Track N Type` et `FM Algorithm` restent absents.

## Groupes dans la liste des zones (page Sampler)
- [ ] **Colonne Groupe** dans la liste des zones (avant Déclenchement), vide pour les zones sans Groupe.
- [ ] **Nouveau Groupe** : sélectionner des zones, clic droit -> Groupe -> « Nouveau Groupe avec la sélection... », taper « Principal 8' » : la colonne se remplit ; la matrice propose une vue pour ce Groupe ; Annuler défait d'un coup.
- [ ] **Affecter** : clic droit sur d'autres zones -> Groupe -> choisir le Groupe existant (coché pour celui de la zone) ; « Aucun Groupe » le retire.
- [ ] **Renommer** : le nom change partout (colonne, matrice, cases de chargement), les modulateurs et emplacements du Groupe suivent ; un nom déjà pris est refusé avec un message.
- [ ] **Supprimer** : les zones restent, sans Groupe ; les réglages propres du Groupe disparaissent.
- [ ] **Jeu d'orgue** : un Groupe commandé par un modulateur (menu Modulateurs du même clic droit) ; couper le modulateur (CC / macro / bouton de Façade) rend les zones du Groupe muettes.

## Groupes dans la matrice
- [ ] **Rangée des Groupes** au-dessus de la matrice : menu « Groupe : », « + Groupe », « Renommer », « Supprimer » ; ne chevauche pas la barre d'outils ; la matrice garde une hauteur correcte.
- [ ] **+ Groupe** : nom demandé, le Groupe devient celui affiché, vide ; il reste après enregistrement et rechargement du projet.
- [ ] **Quatre déclenchements** : Attaque / Premier / Legato / Relâchement pour chaque Groupe ; déposer des zones de la liste ou de la réserve les met dans ce Groupe et ce déclenchement.
- [ ] **Renommer / Supprimer** depuis la rangée ; Annuler défait chaque geste.

## Routeur de notes (onglet ROUTEUR)
- [ ] **Onglet** : « ROUTEUR » à gauche de MOD dans la barre du haut, sans chevaucher les boutons voisins (sinon dites ce qu'il faut raccourcir).
- [ ] **Accouplement** : deux pistes Sampler ou Orgue (clavier 1 sur le canal 1, clavier 2 sur le canal 2) ; règle « du canal 2 vers canal 1 », condition = un tirant. Tirant éteint : le clavier 2 ne joue que sa piste. Tirant allumé (bouton de la Façade lié à `Router Switch 1`, ou CC assigné) : il joue aussi la piste 1.
- [ ] **Note tenue pendant qu'on repousse le tirant** : rien ne reste bloqué quand on relâche la touche.
- [ ] **Octave** : règle du canal 1 vers le canal 1, transposition +12, étendue de touches limitée : l'octave aiguë sonne en plus.
- [ ] **Division** : un clavier maître sur le canal 1, deux règles « À la place » (touches 0-59 vers le canal 2, 60-127 vers le canal 3) : la note d'origine ne joue plus.
- [ ] **Conditions** : ET / OU / OU exclusif / NON ET / NON OU avec deux tirants ; « Inverser » ; « Autre paramètre... » (par exemple un modulateur ou un mute).
- [ ] **Annuler** défait une modification de règle ; le routeur est enregistré avec le projet et avec un preset global.

## Retours d'essai du routeur (2026-10-26)
- [ ] **Barres** : MIDI Send et All Notes Off ne se chevauchent plus ; la barre du bas montre latence, profil, verrou, voix, texte du profil, Preset global, Annuler, Rétablir, chacun à la largeur de son texte.
- [ ] **Fenêtres à pas** du routeur : clic sur « touche bas », « touche haut » ou « transposer » ouvre la fenêtre (−12, −1, champ, +1, +12, OK / Annuler, Entrée, Échap).
- [ ] **Pastille Routeur** dans « Choisir le paramètre à lier » (Façade) : montre les Router Switch et les Router Rule n Transpose sans changer de portée.
- [ ] **Transposition pilotée** : un curseur ou un menu de la Façade lié à `Router Rule 1 Transpose` change la transposition de la règle 1 en direct.
- [ ] **Clavier de la Façade** sur le canal 2 avec une règle « du canal 2 vers canal 1 » + tirant : la piste du clavier sonne, et la piste 1 aussi quand le tirant est tiré ; le relâchement ne laisse rien bloqué.

## Façade en console d'orgue
- [ ] **Voyants** : un voyant « notes jouées sur le canal 2 » dans la Façade de la piste 1 s'allume quand on joue le canal 2 (même si seule la piste 2 le joue) ; les touches d'un clavier de la Façade sur le canal 2 s'allument aussi.
- [ ] **Piston** : « Capturer... » > « Tous les réglages de toutes les pistes » coche les réglages des pistes actives et les accouplements ; le piston les restitue d'un coup.
- [ ] **Console** : bouton « Console » > « Verrouiller sur la Façade de la piste N » : l'interface se réduit à la Façade (+ Annuler / Rétablir, Tout arrêter, Déverrouiller) ; fermer puis rouvrir l'éditeur, ou rouvrir le projet : on retombe sur la Façade, verrouillé ; « Déverrouiller » rend tout ; « Désactiver le mode console » supprime l'ouverture automatique.

## Pages de Façade et Façade globale
- [ ] **Rangée du haut** de l'onglet FAÇADE : « Piste | Globale », l'onglet « 1 » de la page ; en mode Édition « + Page » et « Page... ».
- [ ] **Pages d'une Façade de piste** : « + Page » crée une page vide et s'y place ; deux pages différentes (widgets, fond) par piste ; « Page... » > Renommer (le nom s'affiche dans l'onglet), Déplacer, Supprimer (jamais la dernière) ; Annuler défait chaque geste.
- [ ] **Anciens projets** : une Façade existante est la page « 1 » de sa piste, rien n'est perdu ; enregistrer et rouvrir le projet garde toutes les pages.
- [ ] **Façade globale** : « Globale » montre une Façade indépendante de la piste sélectionnée (la changer de piste ne change pas la page) ; ses widgets lient n'importe quel paramètre ; un clavier posé dessus (canal 1, 2, 3...) joue les pistes qui écoutent ce canal et suit le routeur ; un voyant « notes du canal » s'allume.
- [ ] **Fichiers** : « Enregistrer » de la Façade (.neptunefacade) emporte toutes les pages ; recharger sur une autre piste les remet ; le preset global et l'export de piste emportent aussi les pages.
- [ ] **Mode console** : « Verrouiller sur la Façade globale » : à la réouverture on tombe sur la Façade globale, sans le choix Piste | Globale, mais avec les onglets de pages.
