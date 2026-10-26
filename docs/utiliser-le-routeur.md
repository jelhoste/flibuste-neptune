# Utiliser le routeur de notes (onglet ROUTEUR)

Le routeur est la couche entre ton clavier MIDI et les pistes de Neptune. Il sert aux **accouplements** (un clavier joue aussi un autre manuel), aux **octaves** (grave / aiguë) et aux **divisions de clavier**. Principe : un clavier de l'orgue = une piste = un canal MIDI.

## Une règle, colonne par colonne
| Colonne | Rôle |
|---|---|
| Actif | met la règle en service ou la met de côté sans la perdre |
| Nom | libre (« Récit → Grand-orgue ») |
| Du canal | le canal MIDI dont on prend les notes (Tous = n'importe lequel) |
| Touche bas / haut | l'étendue des touches concernées (clic : fenêtre avec boutons -12, -1, +1, +12 et champ texte) |
| Vers canal | le canal MIDI qui reçoit la note, donc la piste qui l'écoute |
| Transposer | décalage en demi-tons (12 = une octave plus haut, -12 = plus bas) |
| Mode | **Ajoute** ou **À la place** (voir ci-dessous) |
| Condition | quand la règle s'applique (voir plus bas) |

## Les deux modes
- **Ajoute (accouplement)** : la note jouée sur le canal source sonne **comme d'habitude**, et elle est **aussi** donnée au canal de destination. C'est le comportement d'un accouplement d'orgue. Exemple : « du canal 2 vers canal 1, Ajoute » : quand tu joues le clavier 2, la piste du clavier 2 sonne et la piste du clavier 1 sonne aussi.
- **À la place (division)** : la note d'origine **ne joue plus** ; seule la note routée sonne. Exemple : un clavier maître sur le canal 1, avec deux règles « À la place » : touches 0 à 59 vers le canal 2 et touches 60 à 127 vers le canal 3. Le clavier est coupé en deux manuels et la piste du canal 1 ne joue rien. Si plusieurs règles « À la place » se recouvrent, l'origine est supprimée dès qu'une seule s'applique.

Octave aiguë du même clavier : règle « du canal 1 vers canal 1, Ajoute, Transposer +12 ». Octave grave : -12. Unisson hors : règle avec 0 et le canal d'un autre manuel.

Seules les **notes** sont routées. Les CC (pédale, expression), le pitch bend et l'aftertouch restent sur leur canal d'origine. Un Note Off défait exactement ce que le Note On a fait, même si tu as repoussé un accouplement entre-temps.

## Les conditions (le bouton de la dernière colonne)
Une règle **sans condition** (« Toujours ») est toujours appliquée. Avec une condition, elle ne s'applique que si la condition est **vraie**.

Une condition est une liste de **1 à 4 éléments** (des paramètres à deux états : un tirant, un piston, un mute, un interrupteur d'orgue...) combinés par un **opérateur** :
- **ET** : tous les éléments sont allumés ;
- **OU** : au moins un est allumé ;
- **OU exclusif** : exactement un est allumé ;
- **NON ET** : pas tous allumés (au moins un éteint) ;
- **NON OU** : aucun n'est allumé.

Un élément est « allumé » quand la valeur du paramètre atteint la moitié de sa course (un interrupteur : 1). **Inverser** un élément le fait compter comme allumé quand il est éteint. Le menu du bouton permet : « Toujours », « Combiner les conditions par... » (l'opérateur), « Ajouter une condition » (un tirant du routeur, ou « Autre paramètre... »), « Retirer : ... », « Inverser : ... ». Les conditions sont **à plat** (un seul niveau d'opérateur).

Exemple « Récit à Grand-orgue seulement si le tirant 1 est tiré et que le tirant 2 ne l'est pas » : éléments `Router Switch 1` et `Router Switch 2` inversé, opérateur ET.

## Créer un tirant d'accouplement
Un tirant est un des **32 paramètres « Router Switch 1 » à « Router Switch 32 »** (interrupteurs). Ils existent déjà : il n'y a rien à créer. Tu en choisis un dans la condition d'une règle (« Ajouter une condition » > « Router Switch n »), puis tu lui donnes une commande :

1. **Dans la Façade** (la plus courante) : onglet FAÇADE, mode Modification de la mise en page, **+ Bouton** (ou **+ Voyant** pour un témoin). Sélectionne le bouton, **Lier...**, efface le filtre ou choisis la portée « Global », tape « Router », double-clique **Router Switch 1**. Le texte du bouton se change avec **Texte...** (par exemple « Récit → GO »). En mode Jeu, chaque clic tire ou repousse le tirant. Un **voyant** lié au même paramètre s'allume quand l'accouplement est tiré.
2. **Par un CC** (pédale, bouton de ta console) : menu Contrôle > Gérer les assignations... > Nouvelle assignation, bouge le contrôleur, puis choisis la cible `Router Switch n`.
3. **Par un piston** : un bouton **Action** (piston) dont la capture contient les `Router Switch` fait tirer ou repousser les accouplements d'un coup avec les jeux : c'est la combinaison. Voir « + SET et PISTONS » dans l'aide de la Façade.
4. Par l'automation de l'hôte, une macro, ou l'adresse OSC `/neptune/router/n`.

## Exemple complet : un orgue à deux claviers
1. Deux pistes Sampler (ou Orgue) : piste 1 = Grand-orgue sur le canal 1, piste 2 = Récit sur le canal 2. Chaque jeu est un Groupe de la piste, commandé par un modulateur (le tirant du jeu).
2. ROUTEUR > **+ Règle** : Nom « Récit → GO », Du canal 2, Vers canal 1, Transposer 0, Mode Ajoute, Condition : `Router Switch 1`.
3. Façade : **+ Bouton** « Récit → GO » lié à `Router Switch 1`.
4. Joue le clavier 2 : bouton éteint, seul le Récit sonne ; bouton allumé, le Grand-orgue sonne aussi (avec ses jeux tirés).
