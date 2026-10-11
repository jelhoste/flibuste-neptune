# Spécifications détaillées des modules — Sampler CLAP

Document complémentaire au brief (`brief-projet-sampler-clap.md`) et à la roadmap (`roadmap-sampler-clap.md`). Détaille le fonctionnement précis de tous les modules du projet.

---

## 1. Unité de base : la Piste

### Types de génération (sélecteur en haut de piste)
- **Synthé soustractif**, **FM**, **Sampler**, **Entrée audio** (le module Wavetable envisagé initialement a été abandonné — trop coûteux à construire pour une priorité secondaire ; le module FM a en revanche été ajouté, voir section 17).
- Un seul type actif à la fois, sections masquables selon le type sélectionné.
- **Entrée audio** : reçoit un signal envoyé par une autre piste via le système d'envois. Contrairement à un futur bus (plus simple, à définir plus tard), une piste en Entrée audio garde toute la richesse d'une piste complète (inserts, envois propres, pan, pistes parallèles).
- **Changement de type de piste en cours de route** : nécessite une confirmation utilisateur ; la configuration existante est perdue, la piste est réinitialisée dans le nouveau type.

### Sélection MIDI (au niveau piste, pas dans les modules)
- Port/canal choisi directement sur la piste, à côté du sélecteur de type de génération.
- Raison du choix : cohérence lors d'un changement de type de piste (la source MIDI n'a pas besoin d'être réassignée), et même point d'entrée que le futur routeur de notes du mode orgue (palier 7).
- Bouton dédié ouvrant une modale de **filtre MIDI avancé** : canal (ou tous canaux), type de message (Note On/Off avec plage de notes + plage de vélocité ; Control Change avec plage de numéros CC + plage de valeurs ; Pitch Bend ; Aftertouch monophonique et polyphonique séparément ; Program Change). Plusieurs règles empilables, combinables via le système de logique booléenne (partagé avec le moteur de modulation, palier 5).
- Ce même mécanisme sera réutilisé pour les splits de clavier en mode orgue.

### Composants génériques transverses (à construire dès le palier 0)
- **Contrôle universel potard/modale** : réglage précis par drag vertical + modificateur (Shift ou clic droit maintenu) pour le fin, molette souris en support. Double-clic ouvre une modale avec champ texte + slider étendu, redimensionnable, taille mémorisée par type de terminal. Devient un fader rotatif si la piste n'a pas assez de hauteur (responsive). Réutilisé partout où pertinent (trim, mixeur du synthé, etc.). **Compatible souris/clavier et tactile** : taille minimale des zones tactiles, appui long = équivalent clic droit, double-tap = équivalent double-clic, navigation clavier complète, l'accessibilité lecteur d'écran est hors périmètre V1 (décision actée, voir le brief).
- **Presse-papier de module** : copier-coller entre modules compatibles du même type (une enveloppe vers une autre, un filtre vers un autre, un oscillateur d'une piste vers une autre). S'appuie sur la sérialisation JSON déjà prévue pour les presets — un copier-coller n'est qu'un export/import partiel du sous-arbre JSON du module.

### Section entrée traditionnelle
- Trim de volume : potard générique, plage ±12 dB.

### Section inserts
- **8 emplacements par piste** (pool fixe). Accueille les plugins stock du sampleur (et plus tard les plugins tiers hébergés, palier 9).

### Section envois
- **8 emplacements par piste** (pool fixe). Réglage rapide sans modale (contrôle compact) + clic ouvre une modale complète : point de captation dans la chaîne, volume d'envoi, panoramique, mute.
- **4 points de captation possibles**, dans l'ordre réel de traitement du signal (voir note importante ci-dessous) :
  1. Pré-inserts (signal brut)
  2. Post-inserts / pré-fader
  3. Post-fader / pré-pan
  4. Post-pan (signal final de la piste) — **valeur par défaut**

### Section pan
- Deux algorithmes disponibles, sélectionnables sur le contrôle : **balance** et **pan vrai**.
- **⚠ Ordre réel de traitement ≠ ordre visuel de l'interface.** Visuellement, le pan est affiché au-dessus du fader (ordre : trim → inserts → envois → pan → fader → nom). Mais dans la chaîne de signal réelle, **le fader intervient avant le pan** (trim → inserts → fader → pan → sortie). Ce choix ergonomique (affichage ≠ traitement) est assumé et documenté ici pour éviter toute confusion lors de l'implémentation DSP.

### Fader, nom, couleur
- Fader sous la section pan visuellement (rotatif si hauteur insuffisante). Nom de piste sous le fader, piste colorable.

### Mute / Solo / Piste parallèle (à droite du fader)
- **Solo additif** : plusieurs pistes solo cumulables simultanément.
- Modificateur (Shift) sur le solo → **solo exclusif ponctuel** : annule tous les autres mute/solo actifs et isole la piste cliquée.
- **Piste parallèle** : bouton ouvrant une piste dédiée juste à droite, visuellement distincte.
  - Reprend toute la structure d'une piste complète : choix du point de captation dans la piste maîtresse (mêmes 4 tap points que les envois), 8 inserts, 8 envois, pan, volume, mute, solo, et son propre bouton de création de piste parallèle.
  - Le rendu est sommé à la piste maîtresse (point de sommation configurable dans la chaîne).
  - **Structure en arbre** : un parent peut avoir plusieurs pistes filles, et chaque fille peut elle-même avoir des filles (cascade récursive).
  - **Règle de robustesse** : une piste parallèle ne peut capter le signal que dans la chaîne de son **parent direct**, jamais d'un grand-parent ou d'une piste latérale. Cette contrainte garantit une structure en arbre strict sans cycle possible — élimine tout risque de boucle de feedback par construction, sans détection de cycle nécessaire à l'exécution.
  - Pas de plafond dur sur la profondeur d'emboîtement — bornée naturellement par le pool de slots global.
  - **Repliement visuel automatique au-delà du niveau 1** (fonctionnel mais masqué par défaut, dépliable) pour préserver la lisibilité.

### Apparence de piste
- Deux modes : **Normal** et **Slim** (piste repliée, très fine), bouton dédié près du trim de gain.
- S'applique à tous les types de piste (génératrices, bus, master — à confirmer lors de leur définition).
- Prévoir un bouton global "tout replier / tout déplier" au niveau du mixeur, en plus du contrôle individuel par piste.

---

## 2. Module Synthé soustractif

### Principe général
Structure **fixe, non modulaire** (choix assumé pour la robustesse de l'automation CLAP, cohérent avec le pool de slots fixe de l'architecture globale).

### Oscillateurs
- **3 oscillateurs + 1 sous-oscillateur + 1 générateur de bruit**, avant un mixeur commun.
- Sélection de forme d'onde par menu déroulant ou dalle visuelle, avec explorateur filtrable par catégorie (numérique classique / style analogique / complexe).
- **Ondes de style analogique** : génération à bande limitée obligatoire (PolyBLEP ou équivalent) pour éviter l'aliasing — point d'implémentation DSP, transparent pour l'utilisateur.
- **Ondes inspirées de synthés classiques** (type Moog, Oberheim...) : nommage générique obligatoire dans l'UI et les métadonnées (pas de noms de marques déposées), ex. "Ladder Saw" plutôt qu'un nom de marque.
- **Unisson générique** par oscillateur : nombre de voix, détune, répartition stéréo, **+ courbe de répartition du détune** sélectionnable (linéaire, ou "supersaw classique" pondérée façon JP-8000). Ce système unique remplace un oscillateur "supersaw" dédié séparé — un seul moteur, applicable à n'importe quelle forme d'onde.

### Mixeur des 5 sources
- Potentiomètres ronds (façon gros potard de console), avec **niveau + mute individuel + pan individuel** par source.

### Filtres
- **2 filtres**, chacun avec plusieurs modèles/topologies sélectionnables (passe-bas/haut/bande/notch, pentes 12/24 dB, styles inspirés de topologies classiques avec nommage générique).
- **Routage configurable série ou parallèle** via sélecteur dédié. En mode parallèle, un potard de balance dose le mélange entre les deux sorties de filtre avant resommation dans la chaîne.
- **Chaque filtre a sa propre enveloppe indépendante** (forme + profondeur de modulation du cutoff séparées par filtre).

### Enveloppe de volume
- Même moteur d'enveloppe généralisé AHDSHR (potard + édition graphique + courbes modifiables) que les enveloppes de filtre — un seul moteur d'enveloppe pour tout le projet.

### Enveloppe de pitch (dédiée)
- **3ᵉ enveloppe dédiée**, au même niveau d'accessibilité que les 2 enveloppes de filtre et l'enveloppe de volume — même moteur AHDSHR généralisé réutilisé, pas de nouveau moteur.
- Permet les effets classiques type "pitch qui chute" sur un kick synthétique ou un léger bend d'attaque sur un pluck, sans avoir à configurer manuellement une route dans la matrice de modulation générale (qui reste disponible en complément pour des cas plus avancés).
- Réglage de profondeur/amount comme sur les enveloppes de filtre.

### Sortie
- Pas d'étage de sortie séparé : la piste elle-même sert de "master" du synthé (fader/pan/inserts/envois déjà définis au niveau piste).

---

## 3. Module Sampler

### Ouverture et disposition générale
- Plein écran, remplace la vue mixeur, redimensionnable comme le reste de l'interface.

### Zone clavier / mapping (inspirée de l'éditeur de mapping Kontakt)
- Axe horizontal = notes, axe vertical = vélocité.
- Ajout de samples par glisser-déposer, ou clic ouvrant une modale de sélection de fichier.
- **Comportement de dépôt automatique** : la position verticale de la souris au moment du dépôt détermine l'étendue de vélocité de la zone créée (bas = étroit, haut = toute la plage) ; la position horizontale détermine l'étendue de notes en cas de dépôt multiple.

### Placement automatique par nom de fichier
- Délimiteur configurable (par défaut `_`). Chaque segment du nom de fichier devient une **carte** affichée et réordonnable par glisser-déposer.
- Rôle assignable par menu déroulant sur chaque carte : Note racine / Note basse de plage / Note haute de plage / Vélocité basse / Vélocité haute / Round robin (index) / Groupe / Ignorer.
- Le gabarit (nombre de cartes + rôles assignés) est **sauvegardable comme template nommé et réutilisable**, car chaque bibliothèque de samples a sa propre convention de nommage.

### Zone d'édition de sample (sample sélectionné)
Liste des réglages disponibles :
- Note racine, plage de notes (basse/haute), plage de vélocité (basse/haute)
- Accordage fin (cents) et grossier (demi-tons)
- Points de lecture : début, fin, boucles multiples, crossfade de boucle
- Lecture inversée (reverse)
- **Délai de lecture** (en millisecondes, retarde le déclenchement du son après le Note On — distinct du délai pré-attack de l'enveloppe déjà prévu dans l'AHDSHR généralisée, qui ne retarde que le début de la phase d'attaque, pas le déclenchement du son lui-même)
- **Type de déclenchement** : Attaque (Note On, par défaut) / Relâchement (Note Off — utile pour bruits de mécanique, samples de fin de note réalistes) / Premier (ne se déclenche que si aucune autre note du Groupe n'est déjà tenue) / Legato (ne se déclenche que si la note chevauche une autre note déjà tenue — transitions liées réalistes sur cordes/cuivres)
- **Volume de relâchement lié au niveau audio réel de la note source** (option sur le déclenchement Relâchement) : interrupteur "Volume lié au niveau audio de la voix interrompue" + courbe de mise en correspondance (réutilise le composant de courbe du projet). Mesuré via un suiveur de niveau tournant en continu sur chaque voix active (Pic, RMS, ou **LUFS pondéré K — paramètre définissable, LUFS par défaut**, cohérent avec l'intention d'origine "ce que ressent l'oreille humaine" ; RMS reste disponible pour un calibrage plus proche de l'énergie technique brute, voir section 23 sur la distinction RMS vs LUFS), capturé à l'instant précis du Note Off. Mesure le signal audio réellement entendu (forme d'onde + enveloppe + modulations combinées), pas seulement la valeur d'enveloppe programmée. Analyse en parallèle du signal, aucune latence ajoutée — coût CPU limité au suivi de niveau par voix active. Correspondance 1:1 naturelle entre la voix éteinte et la voix de relâchement déclenchée (même événement Note Off). Valeur également exposée au moteur de règles (palier 10) pour des usages plus avancés.

- **Fenêtre d'analyse par Zone** (position de départ + durée) : réglable directement sur la forme d'onde déjà affichée dans l'éditeur de sample (glisser les marqueurs, comme les points de boucle). Par défaut calée sur les premières millisecondes du fichier (l'attaque), librement déplaçable si le moment perceptuellement pertinent se trouve ailleurs (ex. un pic de résonance plus tardif). Sert de base de mesure à la fois pour le calibrage manuel de référence et pour la compensation automatique au remplacement de fichier (ci-dessous) — évite de fausser la comparaison avec une moyenne sur tout le fichier, qui ne reflète pas forcément le moment réellement entendu à la transition.

- **Compensation automatique au remplacement de fichier** : quand l'utilisateur remplace le fichier audio d'une Zone existante (pas à l'import d'un nouveau sample), l'application mesure automatiquement le niveau du nouveau fichier sur la fenêtre d'analyse définie (même détecteur Pic/RMS), le compare au niveau déjà enregistré de l'ancien fichier sur cette même fenêtre au moment où l'utilisateur l'avait calibré à l'oreille, puis calcule et applique un **gain de compensation différentiel** pour préserver l'équilibre déjà réglé. Correction visible et modifiable dans la zone d'édition (pas une correction cachée), pas une normalisation vers une cible absolue arbitraire. **Option activable/désactivable** ; pour un tout nouvel import (pas de fichier de référence à comparer), le niveau brut reste inchangé par défaut.
- Volume et pan du sample individuel
- Appartenance à un groupe round-robin + mode (séquentiel / aléatoire / aléatoire sans répétition)
- Groupe de choke
- **Override ou héritage des réglages de Groupe/global, avec le même mécanisme de liaison fine par bloc que les Groupes** (filtre/enveloppe/effets indépendamment liés au Groupe parent ou personnalisés au niveau du sample) — permet par exemple à un charleston fermé et un charleston ouvert du même Groupe d'avoir chacun leur propre enveloppe de volume (relâchement court vs naturel) sans dupliquer tout le Groupe.
- **Suivi de vélocité sur le volume** (indépendant du choix de la couche) : détermine à quel point la vélocité affecte le volume à l'intérieur d'une même couche, pour des micro-nuances de dynamique.
- **Suivi du clavier sur le volume** (key tracking) : les notes aiguës/graves jouent naturellement plus fort ou plus doucement selon un réglage.
- **Condition MIDI supplémentaire** (native) : réutilise le même composant que le filtre MIDI de piste (canal + type de message + plage de valeurs) appliqué en condition de déclenchement au niveau de la zone/sample — ex. un sample qui ne joue que si la pédale de sustain (CC64) est dans une plage donnée. Le moteur de règles du palier 10 reste disponible pour les cas non couverts nativement.
- Sortie interne (si bus internes multiples au sein du sampleur)
- Métadonnées en lecture seule : nom de fichier, format, fréquence d'échantillonnage, bit-depth, durée
- **Détection automatique de la note du sample** (analyse à l'import, type autocorrélation/YIN), affichée en champ lecture seule à côté de la note racine assignée, pour faciliter le réglage du tuning. Non fiable sur samples percussifs/atonaux — affichage "indéterminé" prévu dans ce cas, limite connue et acceptée.

### Zone de réglages globaux (échelle de l'instrument)
- Courbe de crossfade par défaut entre couches de vélocité
- Mode round-robin par défaut
- Quantité de "humanize" (variation aléatoire légère de pitch/volume à chaque déclenchement)
- Limite de voix à l'échelle de l'instrument (en plus du réglage par piste déjà défini)
- **Tuning global** : cents (fin) + transposition par demi-ton sur ±24 demi-tons (±2 octaves). S'applique comme un **décalage de la note MIDI reçue avant lecture** (pas de pitch-shifting DSP a posteriori sur l'audio) — reste léger, cohérent avec la priorité basse latence.
- **Courbe de réponse de vélocité personnalisable** (échelle de l'instrument) : remappe la vélocité MIDI brute avant qu'elle ne serve à choisir la couche et à moduler le volume — réutilise l'éditeur de courbe déjà construit. Essentiel dès qu'un instrument a plusieurs couches de vélocité inégalement réparties, pour garder une réponse dynamique réaliste plutôt qu'une réponse linéaire.

### Groupes (menu déroulant "Groupe" en haut à gauche de la zone clavier)
- Terme **"Groupe"** utilisé délibérément plutôt que "layer", pour éviter la confusion avec les couches de vélocité.
- Chaque Groupe = ensemble de zones indépendant, avec son **propre filtre/enveloppe/effets**.
- **Mécanisme de liaison fine par bloc** : filtre, enveloppe et effets peuvent chacun être indépendamment liés à un réglage "par défaut" (mise à jour automatique si le défaut change) ou personnalisés (détachés) — réglable bloc par bloc, pas globalement par Groupe.
- **Maximum 32 Groupes par piste**.
- Création : bouton dédié + clic droit (les deux entrées disponibles, même action).
- Duplication : **par référence par défaut** (les Groupes dupliqués partagent les mêmes fichiers samples, économise la mémoire).
- **Keyswitches** : implémentés comme des switches génériques (voir Modulateurs ci-dessous) déclenchés par une règle du filtre MIDI de piste (note spécifique → active le switch au lieu de jouer un son). Aucun nouveau mécanisme requis — réutilise entièrement l'architecture existante.

### Modulateurs (ex. boutons-poussoirs de tirants d'orgue)
- Mode configurable par modulateur : **latch** (bascule, reste actif jusqu'au prochain appui) ou **momentané** (actif seulement pendant l'appui).
- Peuvent être **continus** (crossfade progressif entre états) ou **discrets** (on/off) selon les cas.
- Applicables aux **trois niveaux** : piste, Groupe, sample/zone individuel.
- Contrôlables en MIDI et OSC comme tout paramètre (couche Paramètre générique, feedback bidirectionnel inclus).
- **Usage alternatif pour le mode orgue (palier 7)** : un jeu d'orgue peut être représenté soit comme une **piste entière** (isolation complète, approche initiale du palier 7), soit comme un **Groupe togglé par un modulateur** au sein d'une seule piste (économise le budget de slots). Les deux approches coexistent, au choix selon le projet de l'utilisateur.

### Round-robin
- Compteur **indépendant par couche de vélocité** (pas partagé à l'échelle du Groupe).
- **Option d'emprunt aux couches de vélocité adjacentes** en cas de pénurie de samples de round-robin, avec correction de volume, limité aux couches **immédiatement adjacentes** (pas les couches éloignées) pour minimiser l'écart de timbre. Fonctionnalité optionnelle, activable/désactivable par instrument. Limite connue et acceptée : la correction de volume ne compense pas parfaitement le changement naturel de timbre entre vélocités sur un instrument acoustique réel — utile surtout pour éviter la répétition exacte d'un round-robin, pas pour un résultat parfaitement transparent.

### Chaîne de traitement (confirmée)
Chaque Groupe traite son propre signal (filtre/enveloppe/effets propres ou hérités du défaut) → les Groupes actifs sont sommés ensemble → ce mélange passe dans la chaîne de piste déjà définie (inserts → envois → fader → pan, dans l'ordre réel de traitement documenté plus haut).

---

## 4. Bus

### Structure
Reprend l'essentiel de la piste générique, sans ce qui n'a pas de sens pour un point d'agrégation :
- Pas de sélecteur de type de génération (pas de Synthé/Sampler/Entrée audio) — le signal d'un bus est la somme de tout ce qui y est routé.
- Inserts : même pool que les pistes (8).
- Pan : mêmes deux algorithmes (balance/pan vrai), même ordre réel de traitement (fader avant pan).
- Fader, mute/solo (même comportement additif + exclusif Shift), nom/couleur, mode Normal/Slim.
- **Envois propres** : disponibles, permet de chaîner un bus vers un autre bus (sous-groupes en cascade). Même mécanisme que sur les pistes.
- **Pas de piste parallèle sur un bus** (redondant avec l'ajout d'un insert, évite de complexifier l'arbre de routage sans bénéfice réel).
- **Maximum 16 bus** (pool fixe, même logique que les pistes).

### Arrivée du signal dans un bus
Deux mécanismes disponibles simultanément :
1. Via les envois des pistes (comme décrit dans la section Piste, avec point de captation/volume/pan propres à chaque envoi)
2. Via réassignation de la sortie principale d'une piste ou d'un autre bus directement vers ce bus (subgroup routing classique)

---

## 5. Master

### Structure
- **Instance unique**, toujours présente (pas de pool à gérer).
- Trim de gain d'entrée (séparé du fader), fader, pan final, inserts (même pool que les pistes).
- Pas de solo (n'a pas de sens sur le Master), mute disponible.
- **Métrage LUFS/RMS/peak** — particulièrement important ici pour la conformité loudness (diffusion/streaming).
- **Réduction mono** : bouton bascule — activé, les deux canaux du Master sont sommés en un signal mono (dupliqué sur L/R, avec correction de niveau pour éviter un doublement) ; désactivé, retour à l'état stéréo naturel.
- Pas de bloc limiteur dédié : le compresseur/limiteur stock déjà prévu peut être posé en dernier insert du Master si besoin.
- Pas de dithering (pertinent pour l'export offline, géré par Reaper lui-même, pas par notre plugin temps réel).

---

## 6. Routage de sortie audio (pistes, bus, Master)

### Templates de sorties (déclarées à l'insertion du plugin dans Reaper)
Étend la liste de templates déjà prévue dans le brief : stéréo simple / 8 / 16 / 32 / **jusqu'à 64 sorties stéréo**. Choisi une fois à l'insertion, stable ensuite (cohérent avec le modèle retenu pour l'automation CLAP). Architecture éprouvée : c'est le même modèle que le mode multi-sorties de Kontakt.

### Sélecteur de destination de sortie (par piste, par bus, et sur le Master lui-même)
Chaque piste, bus, et le Master ont un sélecteur : "Master" (comportement par défaut pour pistes/bus) ou "Sortie physique N" (le Master pointe par défaut vers "Sortie 1").

### Bouton "Envoi vers Master" (modèle additif, pas exclusif)
- Bouton dédié à droite du fader de chaque piste/bus (à côté de mute/solo/piste parallèle), **activé par défaut**.
- Comportement additif plutôt qu'exclusif : la sortie directe et l'envoi vers le Master sont indépendants et peuvent être actifs simultanément (signal dupliqué aux deux destinations), ou désactivés séparément pour isoler complètement un élément sur sa sortie directe.
- Implémentation : deux opérations indépendantes et synchrones par bloc de traitement (écriture conditionnelle vers la sortie directe + accumulation conditionnelle dans le Master), sans bufferisation supplémentaire — coût CPU négligeable, **aucune latence ajoutée**.

---

## 7. Plugins stock — fonctionnalités communes

Usage strictement interne (inserts du sampleur uniquement, pas de distribution comme plugins CLAP indépendants — décision prise pour rester concentré sur l'essentiel du projet).

### Communes à tous les plugins stock
- Copier-coller (réutilise le presse-papier de module générique, section 1)
- Bypass, Dry/Wet
- Entrée et sortie avec trimmer dédié (indépendant du volume interne du plugin)
- Interfaces visuelles agrandissables facilement (composant générique redimensionnable, cohérent avec le reste du projet)
- **Entrée de signal clé externe (sidechain)** pour les plugins qui en ont l'usage (ex. compresseur) : la source se choisit via le même sélecteur que les points de captation d'envoi déjà définis (n'importe quelle piste/bus interne comme source) — aucun nouveau mécanisme de routage nécessaire.

### Gestionnaire de presets (transverse à tout le plugin, pas seulement aux plugins stock)
- Import/export, explorateur, mots-clés, favoris (déjà prévu au cahier des charges initial)
- Recherche plein texte, tags/catégories
- **Comparaison A/B** : deux emplacements temporaires pour basculer instantanément entre deux réglages en cours d'ajustement
- Presets récemment utilisés, tri (alphabétique/date/note utilisateur)
- Métadonnées auteur, preset "par défaut" définissable pour les nouvelles instances

---

## 8. Plugin stock — Utilitaire

- Tranche de console minimale : volume, pan
- Mode stéréo ou double mono (tous les contrôles suivent ce mode)
- Inverseur de phase
- Délai d'ajustement en échantillons ou millisecondes (alignement temporel manuel, ex. entre deux micros)
- **Largeur stéréo** (mid-side, resserrer/élargir l'image stéréo)
- **Inversion/échange L-R**
- **Suppression de DC offset** (filtre retirant un décalage continu parasite)
- **Bouton somme mono** (vérification de compatibilité mono, même logique que sur le Master)

---

## 9. Plugin stock — Égaliseur

- Pool fixe de **32 bandes** par instance (UI n'affiche que les bandes actives). **4 bandes actives par défaut**, extensible au choix. Les bandes désactivées sont entièrement sautées dans la boucle de traitement audio (pas seulement coupées après calcul) — coût CPU nul pour les bandes inactives, indépendant de la taille du pool réservé.
- 100% paramétrique : gain, fréquence, facteur de qualité (Q) réglables par potard ou directement sur la courbe (points cliquables/déplaçables, tactile).
- **Preset "graphique 31 bandes"** : verrouille automatiquement fréquence et Q de 31 bandes sur les fréquences centrales standard ISO (tiers d'octave, 20 Hz–20 kHz), seul le gain reste réglable par bande — comportement d'un égaliseur graphique classique. La 32ᵉ bande reste libre en usage paramétrique normal.
- **Analyseur de spectre intégré**, activable/désactivable. Aucun impact sur la latence (analyse en parallèle du chemin de signal, comme le métrage), coût uniquement en CPU — désactivable notamment en profil basse latence.
- **Égaliseur dynamique par bande** : interrupteur individuel par bande (pas un mode global de l'instance). Détection interne au signal de la bande, ou pilotée par une **entrée de clé externe** unique à l'échelle de l'instance (réutilise le mécanisme de sidechain commun à tous les plugins stock, section 7) via un interrupteur par bande dynamique "signal interne" / "clé externe".
- Type de filtre par bande : cloche, plateau grave/aigu, passe-haut/passe-bas (pente réglable), notch.
- Bypass individuel par bande, mode "écoute solo" par bande (isoler temporairement une bande pour l'entendre seule).
- **Minimum-phase uniquement** (comportement standard type ReaEQ, zéro latence ajoutée) — le mode phase-linéaire a été volontairement écarté du scope pour rester simple et cohérent avec la priorité basse latence.
- Détection de la dynamique par bande liée au **profil de performance global** (Live/basse latence vs Studio/qualité max), comme le reste du projet.

---

## Mécanisme transverse — profil de performance global partagé entre instances

- Le switch Live/Studio déjà prévu comme profil de performance global peut être **propagé automatiquement à toutes les instances du plugin actuellement chargées dans le processus Reaper en cours** (portée définie ainsi plutôt que la notion ambiguë de "session").
- Implémentation envisagée : zone de mémoire partagée entre instances, lue par chaque instance, mise à jour quasi instantanément quand l'une d'elles change le réglage.
- **Interrupteur "verrouiller cette instance"** par plugin : permet à une instance précise de rester sur son propre réglage même si le switch global bascule le reste.
- **Oversampling 2x/4x** (pertinent notamment pour la distorsion/saturation et dans une moindre mesure la compression, réduit l'aliasing des traitements non-linéaires) suit la même logique : lié au profil de performance, désactivé en mode basse latence. Introduit une latence minime (filtres d'interpolation), plus faible qu'une convolution complète mais non strictement nulle. **4x retenu comme maximum pour la V1** — au-delà, le gain perceptible devient marginal face au doublement du coût CPU à chaque palier ; 8x/16x resteront une extension possible plus tard si un besoin précis se présente, notamment une fois la Distorsion/Saturation conçue.

---

## 10. Plugin stock — Compresseur / Limiteur / Expandeur / Gate

Structure en onglets pour basculer entre les modes.

### Compresseur (et Limiteur = Compresseur avec ratio → l'infini)
- Threshold, attaque (instantané à 60 ms, zone de précision plus fine de 0 à 20 ms via courbe de mise à l'échelle non-linéaire sur le contrôle générique), relâchement (instantané à 1,5 s, zone de précision plus fine 20-300 ms, même principe)
- **Knee** : deux paramètres indépendants au sein d'un même contrôle — **largeur en dB** (étendue de la zone de transition douce, centrée sur le threshold) et **forme concave/convexe** (courbure à l'intérieur de cette zone). Utilise le même type de composant de forme de courbe que l'attaque/relâchement, mais reste un réglage conceptuellement distinct : le knee agit sur la relation statique niveau d'entrée → réduction de gain, l'attaque/relâchement agit sur l'évolution dans le temps.
- Clé (sidechain) : entrée externe, ou détection sur le signal filtré par un **égaliseur 4 bandes interne** (réutilise le moteur d'égaliseur déjà conçu), le tout désactivable
- **Look-ahead** désactivable et réglable — améliore l'anticipation temporelle du détecteur (complémentaire de l'oversampling, ne le remplace pas : l'oversampling améliore la précision instantanée/inter-échantillon, le look-ahead donne une vraie avance temporelle en millisecondes). Auto-désactivé en profil basse latence, réactivé en profil Studio, comme tout réglage générateur de latence du projet.
- Topologie de détection : feedforward ou feedback
- Type de détection : pic, RMS, ou intégration (temps réglable)
- Makeup gain
- **Visualisation en courbe temporelle** de la compression (pas juste un bargraphe), désactivable — analyse en parallèle du signal, aucun impact latence, coût CPU négligeable
- **Limiteur** = même moteur avec ratio → l'infini, pas de plugin séparé. Détection **"true peak"** optionnelle : activer l'oversampling sur le chemin de détection **suffit à lui seul** à obtenir cette détection (capte les pics inter-échantillons qu'une détection au taux d'échantillonnage normal manquerait) — aucun système séparé nécessaire, l'oversampling est directement le mécanisme du true peak. Utile en usage mastering.
- Courbe d'attaque/relâchement modifiable, réutilise le moteur d'enveloppe généralisé du projet (même système que les enveloppes AHDSHR)

### Expandeur
- Mêmes réglages que le Compresseur, adaptés au fonctionnement propre de l'expansion (relation inverse seuil/ratio)
- **Option Ducker** (déclenchement par clé externe, réutilise le mécanisme sidechain commun)

### Gate — étage indépendant cascadable
- **Étage séparé et complet** (propre seuil/ratio/attaque/relâchement/courbe), pas une simple variante de l'Expandeur à ratio infini
- **Positionnable avant ou après l'étage Expandeur** dans la même instance de plugin — permet par exemple une expansion douce en cascade avec un vrai silence coupé net

---

## 11. Plugin stock — Chorus

Sélecteur de type d'algorithme en haut du plugin : **Moderne** ou **Vintage**.

### Mode Moderne (paramétrique)
- Taux de modulation (rate), profondeur (depth)
- Délai central (delay time de base avant modulation)
- Nombre de voix (2, 4, plus...) avec écartement de phase entre elles
- Largeur stéréo / spread
- Feedback/régénération
- Forme du LFO (sinus/triangle)
- Filtre sur le signal modulé (passe-haut léger, évite que les basses "flottent")
- Mix (dry/wet)

### Mode Vintage (émulation BBD à 2 boutons)
Architecture technique reproduite fidèlement (nommage générique, pas de marque déposée dans le produit) :
- Deux circuits BBD indépendants (un par canal stéréo), alimentés par le même signal d'entrée mono en interne, modulés par un LFO commun **déphasé à 180°** entre les deux canaux
- **Bouton I** : LFO triangle lent (~0,5 Hz)
- **Bouton II** : LFO triangle légèrement plus rapide (~0,8 Hz)
- **I + II activés simultanément** (pas un 3ᵉ bouton séparé) : LFO plus proche du sinus (filtré), vitesse ~1 Hz, intensité fortement réduite (~8% contre 100% pour I ou II seuls) — effet plus subtil, proche d'un léger vibrato/tremolo
- Signal sec sommé au signal traité sur chaque canal (mix dry/wet fixe par construction, fidèle au comportement du matériel d'origine)
- **Multiplicateur de taux** additionnel (ajuste la vitesse globale sans dénaturer le caractère du mode — flexibilité en plus de la fidélité)

**Limite connue et acceptée** : les artefacts propres au BBD analogique (bruit de fond léger, bande passante limitée, légère saturation) sont difficiles à égaler parfaitement par émulation numérique — une approche soignée s'en approche de très près sans garantir une identité parfaite à l'oreille d'un expert.

---

## 12. Plugin stock — Distorsion / Saturation

**Mono-bande pour la V1** (multibande écarté, cohérent avec l'esprit "efficace" du projet — pourra être reconsidéré plus tard si un besoin précis se présente).

### Sélecteur de type de circuit
Analogique modélisé : Tube (harmoniques paires dominantes, caractère "chaud"), Tape (saturation douce + compression naturelle liée), Transistor, Transformateur, Diode (plus agressif, caractère "synthé acide").
Digital/créatif : Rectifier (redressement, harmoniques paires marquées), Bitcrusher (réduction de résolution, lo-fi), Wavefolder (repliement d'onde), Waveshaper personnalisé.

### Contrôles communs à tous les types
- **Drive** (quantité de saturation)
- **Tonalité** : un filtre simple avant/après le circuit de saturation (pas un égaliseur complet)
- **Bias** (asymétrie de l'écrêtage — équilibre harmoniques paires/impaires, exposé explicitement plutôt que caché dans un mode fixe)
- **Mix** (dry/wet)
- **Gain de sortie**
- **Oversampling** (mécanisme déjà défini pour le projet, particulièrement pertinent ici pour limiter l'aliasing généré par la non-linéarité — jusqu'à 4x, lié au profil de performance)

### Distorsion pilotée par clé externe
Le paramètre Drive (et par extension tout autre paramètre du plugin) peut être une destination de modulation pilotée par le signal d'une autre piste/bus, via la combinaison déjà existante de la modulation inter-pistes par signal audio (palier 5) et de l'entrée de clé externe commune à tous les plugins stock (section 7) — aucun nouveau mécanisme nécessaire.

---

## 13. Plugin stock — IR Loader (convolution)

### Latence — trois modes disponibles, avec bouton d'information
Un bouton "i" ouvre une explication en langage clair des trois modes, avec une section dépliable pour une explication technique plus poussée (algorithmes, compromis CPU/latence) pour les utilisateurs qui le souhaitent.

1. **Zéro latence réelle** (convolution partitionnée non-uniforme, algorithme de Gardner — "Efficient Convolution without Input/Output Delay") : le premier segment est traité en convolution directe dans le domaine temporel (causal, instantané par nature), les segments suivants en FFT anticipée. Latence réellement nulle, pas une approximation. **Recommandé par défaut**, particulièrement efficace en CPU sur les IR courtes (ampli/cabinet) — c'est sur les IR longues (grande réverbe) que le coût CPU de ce mode devient le plus significatif.
2. **Traditionnel, latence déclarée à l'hôte** (convolution FFT par bloc uniforme, CPU le plus faible des trois modes). Latence = taille du bloc FFT, **déclarée correctement à Reaper** via l'extension de latence CLAP pour compensation automatique (PDC).
3. **Décorrélation non déclarée** (techniquement identique au mode 2, mais la latence n'est **pas** déclarée à l'hôte — pas de compensation automatique). Pensé pour un usage reverbe en envoi/retour, où un léger délai non corrigé sur la traîne seule est indissociable d'un réglage de pré-délai (le signal sec, non affecté, n'est jamais désynchronisé). La valeur du décalage réel est affichée à titre informatif. **Avertissement textuel affiché dans l'interface** si ce mode est sélectionné, déconseillant son usage en insert direct sur un signal principal (où l'alignement temporel avec les autres pistes compte).

### Reste de l'interface

**Affichage de l'IR** : graphe temporel avec référence de temps, recalculé pour refléter l'IR **telle qu'effectivement utilisée par le moteur de convolution** (pas la version brute chargée) — se met à jour à chaque changement de compression/extension temporelle, de gain, ou de toute autre transformation du pipeline ci-dessous.

**Égaliseur pré et post**, 3 bandes chacun, désactivables — réutilise une instance interne du moteur d'égaliseur déjà construit, limitée à 3 bandes actives.

**Pipeline de transformation de l'IR** (non-destructif : le fichier source n'est jamais modifié, tout est recalculé une seule fois à chaque changement de paramètre pertinent, hors temps réel — même logique que le `need_refft` du script JSFX étudié) :
- **Compression/extension temporelle** (rééchantillonnage du buffer). Change le caractère sonore de l'IR (effet créatif à part entière, pas neutre — comparable à un time-stretch audio classique), à documenter dans l'UI pour que l'utilisateur le sache.
- **Modification du gain, de la largeur stéréo et du panoramique** appliqués directement sur l'IR (avant convolution) — différent d'appliquer ces réglages après coup sur le signal de sortie, l'interaction avec l'EQ post et le reste de la chaîne change selon l'endroit où ça se passe.
- **Pré-délai** (silence ajouté avant le début de la traîne — réglage musical classique, aucun rapport avec la latence de traitement).
- **Anticipation/look-ahead** : fait entendre l'IR légèrement avant le signal sec traité. Techniquement, retarde l'ensemble de la sortie du plugin (signal sec + traité) de la durée choisie pour rester causal — une vraie latence ajoutée, désactivée automatiquement en mode basse latence (même principe que le look-ahead du Compresseur).
- **Troncation par l'avant** : coupe le début de l'IR, le reste recalé au début du buffer (technique classique pour isoler la traîne d'une reverb en retirant les premières réflexions).
- **Compression dynamique appliquée directement sur le buffer de l'IR** : fait passer le buffer une seule fois, hors temps réel, à travers le moteur Compresseur/Gate déjà construit, au moment du chargement. Permet de reproduire une "reverb gated" (technique classique des batteries années 80). **Bouton bypass dédié à cette étape** pour comparer instantanément avec/sans sur le graphe (pas d'étape "confirmer" nécessaire, le pipeline étant non-destructif).
- **Gate temporel avec mise en forme du volume** : fenêtre d'amplitude statique appliquée sur le buffer — point de départ de la coupure + forme de courbe de descente (linéaire/exponentielle/personnalisée), réutilise le composant de courbe déjà construit pour les enveloppes et le knee, appliqué ici une fois sur un buffer statique plutôt qu'en temps réel.

---

## 13.5 Éditeur de sample intégré (module Sampler)

Même philosophie non-destructive que l'éditeur d'IR (section 13) : opérations sur une **copie de travail**, fichier source jamais modifié sauf action explicite "Enregistrer sous" pour figer le résultat en nouveau fichier indépendant.

### Éditeur visuel de sample
Affichage de la forme d'onde avec superposition optionnelle des **empreintes de volume** (Pic/RMS/LUFS, section 23), sélectionnables/masquables individuellement. Manipulation directe à la souris/au doigt des marqueurs (début/fin de lecture, points de boucle — déjà définis comme paramètres descriptifs en section 3, cet éditeur en est simplement l'interface visuelle) et des fondus (voir ci-dessous). **Aimantation au zéro** (snap to zero-crossing) disponible comme option pour positionner précisément un marqueur ou un fondu là où la forme d'onde croise zéro, évite les clics audibles à la coupure ou à la boucle.

**Visualisation des empreintes** : dégradé continu de couleur (jusqu'au violet au maximum) appliqué sur la courbe d'empreinte affichée, lecture immédiate de la dynamique du sample. Navigation "aller au plus fort/plus faible" (extremum global) et "pic suivant/précédent" (parcours des maxima locaux un par un), lues directement dans le contour continu déjà calculé (section 23), aucun nouveau calcul nécessaire. **Filtre de plage de puissance** : illumine les zones dont le niveau se situe entre deux valeurs définies par l'utilisateur.

**Outils dérivés du filtre de plage** :
- **Détection automatique de silence** : seuil bas défini par l'utilisateur, l'application propose automatiquement les marqueurs de début/fin réels du son utile en ignorant le silence de part et d'autre.
- **Détection de transitoires pour découpage automatique** : repère plusieurs coups dans un long enregistrement et crée une Zone par coup détecté — belle synergie avec l'enregistrement audio direct (section 21) : enregistrer plusieurs prises à la suite, laisser l'application les découper automatiquement.

**Outils de boucle** :
- **Recherche automatique de point de boucle** : analyse la forme d'onde et propose plusieurs candidats de boucle sans coupure audible (basé sur la ressemblance de forme d'onde entre points candidats), plutôt que de chercher uniquement à l'oreille.
- **Vue superposée du point de boucle** : affichage du début et de la fin de la boucle superposés ou côte à côte, pour vérifier visuellement l'alignement avant de valider.
- **Écoute isolée de la boucle** : lecture en boucle uniquement de la petite région autour du point de boucle, sans rejouer tout le sample à chaque test.

**Confort d'usage** : info au survol de la souris (position temporelle + valeur en dB en direct), undo/redo désactivable dans l'éditeur pour les très gros fichiers (économie mémoire).

### Fondus (fade in/out) — paramètres descriptifs temps réel, pas une opération figée
Contrairement à une première approche envisagée (fondu baked dans le buffer), les fondus sont en réalité une **rampe de gain bon marché**, appliquée en direct par le moteur de lecture au début/à la fin de la lecture — même catégorie que les marqueurs de lecture, pas besoin de pré-calcul ni de figer quoi que ce soit dans un buffer. Conséquence pratique : un **petit fondu par défaut** (quelques millisecondes) est automatiquement actif tant que l'utilisateur n'a rien réglé, pour éviter les clics sans aucune configuration ni coût — ajustable ou désactivable à tout moment, gratuitement, puisque rien n'est jamais figé.

### Opérations disponibles (pipeline non-destructif figé — pré-calcul nécessaire, distinct des fondus ci-dessus)
- **Trim/rognage** : découpe réelle du buffer aux points choisis — distinct des points de lecture début/fin déjà existants (section 3), qui ne font que déplacer des marqueurs de lecture sans réduire le fichier. Utile pour réellement alléger le fichier.
- **Gain/Normalisation** : ajustement de niveau, cible en crête ou en RMS — complète le volume de lecture déjà existant (réglage temps réel, pas un changement permanent du fichier).
- **Suppression de DC offset** : réutilise l'algorithme déjà défini dans le plugin Utilitaire (section 8), appliqué ici comme opération ponctuelle sur le buffer plutôt qu'en traitement temps réel.
- **Time-stretch/compression temporelle** (section 21) : même pipeline non-destructif, applicable ici aussi.
- **Pitch-shift sans changement de durée** : même moteur que le time-stretch (un bon algorithme de ce type expose deux ratios indépendants, temps et hauteur — pas un second algorithme à construire), appliqué ici sur l'axe inverse. Hors temps réel, donc **sans la contrainte de basse latence** qui s'applique à l'usage en direct dans le Sampler — permet une fenêtre de traitement plus grande et une meilleure qualité que l'équivalent temps réel, puisque calculé une seule fois au moment de l'édition plutôt qu'à chaque échantillon joué.

Pas de "reverse" ici — existe déjà comme réglage de lecture non-destructif (section 3), pas dupliqué en version figée dans le buffer.

---

## 14. Palier 10 — Moteur de règles scriptables (module Sampler uniquement)

**Portée confirmée restreinte** : interactions entre samples/voix au sein d'une instance du module Sampler uniquement (zones, Groupes, modulateurs de cette instance). Pas d'action sur les paramètres d'autres pistes/bus du projet — portée volontairement écartée après discussion, pour limiter la complexité de détection de cycle et rester cohérent avec l'intention initiale du palier.

### Principe général
Une liste de règles, pas un langage imposé. Chaque règle : **"Quand [déclencheur] (et éventuellement [condition]) → Alors [action]"**, construite par menus déroulants. Une règle = une ligne dans une liste, avec case d'activation, nom personnalisable, réorganisable par glisser-déposer.

### Déclencheurs possibles
- Note On / Note Off d'un sample, d'une zone, ou d'un Groupe précis (au sein de l'instance)
- Changement d'état d'un modulateur/switch (activé, désactivé, ou bascule)
- Un index de round-robin précis atteint
- Vélocité franchissant un seuil
- Changement de valeur d'un paramètre de l'instance (réutilise la couche Paramètre générique du palier 0)
- Combinables via la logique booléenne déjà existante (ET/OU/XOR/NON, palier 5)

### Actions possibles (scope : éléments internes à l'instance du Sampler)
- Activer/désactiver/basculer un modulateur ou un Groupe de cette instance
- Modifier un paramètre de cette instance (valeur fixe, incrément/décrément, valeur aléatoire dans une plage)
- Déclencher manuellement la lecture d'un sample/zone de cette instance, indépendamment du MIDI reçu
- Couper/étouffer une voix ou un Groupe (choke)
- Émettre un message MIDI/OSC sortant (réutilise le feedback déjà prévu)

### Exécution de plusieurs règles simultanées
**Toutes les règles correspondantes s'exécutent** (indépendantes, pas de priorité par ordre de liste).

### Mode "Code" optionnel, en complément du mode menus déroulants
Chaque règle dispose d'un interrupteur **Menus déroulants / Code**, au choix, les deux écrivant dans le même système sous-jacent (couche Paramètre générique).
- **Lua** retenu comme langage embarqué : bibliothèque C légère conçue pour l'embarquement, mature, bien documentée, sandboxable (restriction précise de ce que le script peut faire — pas d'accès au système de fichiers, pas de mémoire arbitraire).
- API exposée simple, alignée sur la couche Paramètre générique (`getParametre(id)`, `definirParametre(id, valeur)`, etc.) — mode code et mode menus déroulants manipulent les mêmes objets sous le capot.
- **Garde-fou** : compteur d'instructions avec coupure automatique (nativement supporté par Lua) pour arrêter un script en boucle infinie sans geler le plugin. Erreur de script → la règle se désactive proprement avec message, sans planter le reste.

### Garde-fous techniques (portée restreinte à l'instance)
- **Détection de cycle à l'exécution** : compteur de profondeur d'évaluation, coupure propre avec message ("Boucle détectée, règle interrompue") si une chaîne de règles se redéclenche elle-même — périmètre limité à l'instance du Sampler, donc plus simple à garantir robuste qu'une portée globale au projet.
- **Évaluation à taux de contrôle, pas à taux audio** (même distinction control-rate/audio-rate qu'au palier 5) — cohérent avec la priorité basse latence, évite qu'une règle mal conçue ne devienne un gouffre CPU.

### Exemples d'usage
- "Si la vélocité dépasse 100 sur 3 notes consécutives → activer un Groupe de samples plus intenses"
- "Après 30 secondes sans note → désactiver automatiquement un Groupe gourmand en mémoire"
- "Quand le Groupe A est activé → désactiver automatiquement le Groupe B" (exclusion mutuelle)

---

## 15. La Façade (interface personnalisée par instrument, fonctionnalité transverse)

Onglet supplémentaire disponible par instrument, à côté du Mixeur — nom retenu pour ce module dans tout le projet : **la Façade**.

- Image de fond importable
- Contrôles positionnés librement dessus, chacun lié à un paramètre existant (n'importe lequel, via la couche Paramètre générique) — aucun nouveau moteur de contrôle, juste une vue alternative des objets déjà existants
- Position/taille ajustables par glisser-déposer
- **Palette de widgets** :
  - Potard/slider générique
  - Bouton/switch (bascule un paramètre booléen)
  - **Bouton d'action/préréglage** : déclenche une action précise (typiquement charger un preset/une combinaison) plutôt que de basculer un simple booléen — nécessaire pour restaurer un instantané complet (ex. présélections d'orgue)
  - Menu déroulant
  - Éditeur de courbe (réutilise le composant déjà construit — utile notamment pour exposer une courbe de dynamique/vélocité directement sur le panneau)
  - Étiquette de texte
  - **Clavier virtuel superposable** : contrairement au clavier virtuel global du moniteur MIDI/OSC (outil de test unique), celui-ci peut être placé en **plusieurs exemplaires** sur une même Façade. Chaque exemplaire est lié à un canal/piste MIDI précis (un manuel = un exemplaire), affiche les notes actuellement jouées en couleur en temps réel (quelle que soit la source : clavier MIDI physique ou clic sur le clavier virtuel lui-même), reste jouable au clic, et est positionnable/redimensionnable librement — empilable verticalement pour reproduire plusieurs claviers superposés façon console d'orgue.

### Flux d'édition
1. **Mode Édition** : bascule entre mode "Jeu" (usage normal, cliquable/jouable) et mode "Édition" (évite de déplacer un contrôle par accident en jouant)
2. **Palette d'outils** : glisser-déposer le type de widget voulu sur le fond d'image
3. **Panneau de propriétés** : choix du paramètre à lier (liste recherchable/filtrable par piste/groupe/sample/plugin), apparence (taille, style, texte d'étiquette) ; pour le clavier virtuel, choix du canal/piste MIDI à refléter
4. **Manipulation** : redimensionnement par poignées, alignement/aimantation, sélection multiple, duplication, ordre d'empilement, suppression, outils d'alignement/matrice (voir ci-dessous)
5. **Import d'image de fond** : sélecteur de fichier standard (PNG/JPG/SVG), avec options d'ajustement au canevas (voir ci-dessous)
6. **Undo/redo** : réutilise le système du palier 0

### Cas d'usage orgue liturgique (page d'accueil façon GrandOrgue)
La Façade permet de construire une console d'orgue complète : boutons/switches déjà existants pour les tirants de jeux (liés aux mutes de Groupe ou de piste), boutons d'action pour les présélections/combinaisons, claviers superposés pour chaque manuel. S'appuie directement sur le **routeur de notes centralisé** déjà défini au palier 7 — chaque clavier de la Façade affiche simplement l'état du manuel correspondant dans ce routeur, aucune nouvelle logique nécessaire, juste un nouveau type d'affichage.

### Ajout d'un contrôle depuis n'importe où dans l'application
En plus de l'ajout depuis l'éditeur de Façade (palette + liaison par recherche de paramètre), **clic droit / appui long sur n'importe quel contrôle existant** (dans le mixeur, l'éditeur de sample, un plugin stock...) propose "Ajouter à la Façade" — ouvre un petit menu déroulant demandant le type de widget à créer (potard, bouton, etc.), puis place directement le widget correspondant sur la Façade, déjà lié au paramètre d'origine. Un raccourci qui réutilise le même mécanisme de liaison, juste déclenché depuis un point d'entrée différent.

### Outils d'organisation de la mise en page
Sélection multiple de widgets + outils d'alignement (gauche/droite/haut/bas/centre), répartition régulière, disposition automatique en matrice/grille — standard dans tout éditeur de mise en page, s'ajoute à la manipulation déjà prévue (redimensionnement, aimantation, duplication, ordre d'empilement).

### Redimensionnement de l'image de fond au canevas
Options d'ajustement de l'image importée à la taille de canevas choisie (ajuster/étirer/rogner), plutôt que de forcer la taille du canevas à la taille native de l'image.

Risque technique faible : le vrai travail (contrôles, liaison aux paramètres, routeur de notes) existe déjà ailleurs dans le projet, l'ajout principal est un éditeur de mise en page visuelle simple.

---

## 16. Moniteur MIDI/OSC (outil de diagnostic transverse)

- Journal défilant des messages MIDI et OSC entrants/sortants, avec horodatage, type, canal, valeur, source/destination
- **Filtres conditionnels sur l'affichage** : réutilise le même composant de règle de filtre MIDI déjà construit ailleurs dans le projet (canal + type de message + plage de valeurs) — ex. "Note On avec vélocité entre 80 et 120". Filtrable aussi par piste/port source, par plage de temps, par valeur de CC précise.
- **Bouton "Panic"** : all notes off + réinitialisation de tous les switches/modulateurs
- Rattaché au palier 3 (MIDI/OSC transverse) dans la roadmap

### Clavier virtuel et outils d'envoi de test (compagnon du moniteur)
- Clavier virtuel jouable, canal(aux) de sortie sélectionnable(s) — plusieurs canaux simultanément possible (utile pour tester du multitimbral ou du MPE)
- Potards assignables à des CC arbitraires (réutilise le composant potard générique, envoie un message MIDI brut plutôt que de moduler un paramètre du plugin)
- **Boîte de dialogue message libre**, y compris saisie sysex en hexadécimal — utile pour tester keyswitches, réponses à un CC, et réutilisable pour l'import/export de patches FM (voir section 17)

---

## 17. Module FM (4e type de génération de piste)

### Contexte et licence
Architecture inspirée du Yamaha DX7 (nom de marque non réutilisable dans le produit — nommage générique à définir, ex. "FM 6-Op"). **Compatibilité sysex avec les patches DX7 existants visée** : format technique de fait, pas une marque, comparable à supporter WAV ou MIDI standard — légitime à cibler.

Référence technique retenue : le composant **msfa** ("music synthesizer for android", moteur de synthèse au cœur de Dexed), sous licence **Apache 2.0** — permissive, réutilisable sans contaminer le reste du projet. Dexed lui-même est en GPL v3 (virale) : code source à éviter de réutiliser directement, même si le projet est prévu open source et gratuit, pour préserver la liberté de décision future (une dépendance GPL empêcherait définitivement toute édition fermée/payante ultérieure, y compris partielle). Interface et intégration entièrement réécrites par-dessus msfa dans tous les cas.

### Architecture technique visée (classique DX7)
- 6 opérateurs
- 32 algorithmes de routage fixes (combinaisons porteuse/modulatrice)
- Enveloppe à 4 taux/4 niveaux par opérateur — **piste à explorer** : possible réutilisation du moteur d'enveloppe généralisé déjà construit (segments masquables) plutôt qu'un système séparé, à vérifier en détail le moment venu
- LFO global (pitch/amplitude)
- Mise à l'échelle clavier par opérateur (rate scaling + level scaling)
- Feedback réglable sur les opérateurs qui le permettent selon l'algorithme

### Fidélité sysex — deux blocs couverts
- **Dump de patch/voix** (155 octets single voice / 4096 octets bulk 32 voix, encodage 7-bit packé + checksum) : par opérateur (mode ratio/fixe, coarse, fine, detune, niveau de sortie, enveloppe 4 taux/4 niveaux, mise à l'échelle clavier avec point de rupture + profondeur/courbe gauche-droite, sensibilité vélocité, rate scaling, sensibilité à la modulation d'amplitude), plus réglages globaux du patch (algorithme 1-32, feedback, LFO complet, enveloppe de pitch, transposition, nom de voix 10 caractères, matrice on/off des opérateurs).
- **Bloc "function parameters"** (réglages globaux matériel) : accordage général, canal MIDI, mode poly/mono, portamento, assignation des contrôleurs (molette de modulation, contrôleur au pied, etc. à la sensibilité pitch/amplitude).
- Modèle de données interne fidèle à la structure DX7 sur ces deux blocs, même si l'affichage est entièrement repensé (voir ci-dessous).

### Interface — repenser l'agencement, pas les données
- **Diagramme d'algorithme visuel et interactif** : 6 opérateurs positionnés selon l'algorithme choisi (1-32), flèches porteuse→modulatrice, boucle de feedback visible sur les opérateurs concernés. Clic sur un opérateur ouvre son panneau de détail.
- **Panneau de détail par opérateur** : oscillateur (mode ratio/fixe, coarse, fine, detune), niveau de sortie, **enveloppe visuelle en graphique** (réutilise le composant de courbe du projet, 4 points déplaçables représentant les taux/niveaux, valeurs fidèles au format DX7 en interne), mise à l'échelle clavier avec **mini-courbe visuelle** sur l'étendue du clavier, sensibilité vélocité, rate scaling, sensibilité à la modulation d'amplitude, interrupteur on/off rapide.
- **Section globale** : sélecteur d'algorithme visuel, feedback, LFO complet (vitesse/délai/PMD/AMD/sync/forme d'onde), enveloppe de pitch (même composant graphique), transposition, nom de patch, matrice de vue d'ensemble de l'état on/off des 6 opérateurs.
- **Gestionnaire de presets** : réutilise le gestionnaire déjà conçu (favoris, tags, comparaison A/B), import/export au format sysex DX7 natif. La boîte de dialogue sysex du moniteur MIDI/OSC (section 16) devient un outil bonus pour envoyer un patch vers un vrai DX7.

### Macros modernes (couche additionnelle, ne remplace pas les vrais paramètres)
- **Brillance** : agit sur l'équilibre des niveaux de sortie des opérateurs modulateurs selon leur ratio de fréquence
- **Vitesse d'enveloppe globale** : multiplicateur appliqué à tous les taux d'enveloppe des 6 opérateurs simultanément
- **Feedback/Drive** : accès rapide au feedback sans ouvrir le panneau d'opérateur

### Polyphonie et unisson
- Réutilise le système de polyphonie déjà défini au niveau piste (nombre de voix, mode de vol de voix, retrigger/legato, groupes de choke) — aucune nouvelle brique.
- **Unisson ajouté** (voix/détune/spread, même moteur générique que le Synthé soustractif) — extension par rapport au DX7 original mono-voix, pour un son plus épais façon réinterprétation moderne.

---

## 18. Plugin stock — Delay

Blocs/taps ajoutables, même principe de pool que l'égaliseur : **16 blocs maximum** (UI n'affiche que les blocs actifs, même logique de coût CPU nul pour les blocs inactifs que l'égaliseur).

### Par bloc
- Temps : millisecondes, ou musical synchronisé à l'hôte (extension de transport CLAP, aucun impact latence — change seulement le calcul de la valeur, pas le traitement)
- Feedback
- Volume, pan
- **4 emplacements d'insert internes** (EQ / Chorus / Compresseur / Distorsion — réutilisation directe du code des plugins stock déjà construits, appel interne entre nos propres classes, pas d'hébergement externe, zéro complexité d'IPC ou de gestion d'état séparée)

---

## 19. Plugin stock — MicroPitch (inspiré du H3000 / lignée H910-H949)

Plugin dédié (pas un mode caché dans le Delay), mais **réutilise le même moteur de ligne à délai** que le plugin Delay sous le capot.

### Principe technique
Ligne à délai à vitesse de lecture variable (pas de FFT ni de vocodeur de phase) — même famille de technique que la lecture à vitesse variable déjà utilisée pour le pitch des samples. Léger, low-latency par nature. Sur la plage "micro" (quelques cents), les artefacts de rebouclage du buffer restent inaudibles — ne deviennent gênants qu'à des décalages beaucoup plus extrêmes, hors du cas d'usage visé.

### Contrôles
- **Deux voix**, chacune avec :
  - Détune fin en cents (plage large disponible, mais calibrée par défaut autour de ±9 à ±11 cents pour le caractère "micro" signature)
  - Délai en millisecondes (couramment 15-310ms selon usage, du "fattening" subtil au slapback/dive plus marqué)
- **Modulation (vitesse + profondeur)** appliquée **uniquement aux voix décalées/mouillées, jamais au signal sec**
- **Mix** dry/wet
- Feedback par voix (permet des effets créatifs plus extrêmes type spirale montante/descendante en usage poussé)

---

## 20. Stockage et gestion des données (presets, samples, structure interne)

### Séparation projet vs bibliothèque de presets
- **État du projet** (embarqué dans le fichier Reaper via notre plugin) : toujours une copie complète et résolue de l'état de chaque instrument, jamais une simple référence vers un preset externe. Modifier ou supprimer un preset de bibliothèque plus tard ne doit **jamais** affecter un projet qui l'a déjà chargé.
- **Bibliothèque de presets** (fichiers séparés sur disque) : sert uniquement de point de départ réutilisable au moment du chargement — une fois chargé dans une piste, c'est une copie indépendante.

### Résolution des chemins de fichiers samples
Ordre de résolution au chargement d'un preset (du plus prioritaire au dernier recours) :
1. **Chemin relatif au dossier du preset** : si un fichier du même nom existe à côté du `.neptunepreset` (l'utilisateur peut l'y coller manuellement), il est utilisé en priorité — même si le chemin absolu d'origine est encore valide. Permet une "portabilité artisanale" gratuite sans passer par l'export ZIP formel.
2. **Chemin absolu** (emplacement d'origine du fichier).
3. **Recherche par nom de fichier** dans le **dossier samples** défini dans le menu de réglages généraux (dernier recours automatique).
4. Si tout échoue : boîte de dialogue invitant l'utilisateur à **relocaliser** le fichier manquant, plutôt que d'échouer silencieusement ou de planter.

### Structure de sauvegarde d'un instrument
- **Sauvegarde normale** : un dossier est créé pour l'instrument dans le dossier presets, avec le fichier de description à l'intérieur — pas un fichier isolé à plat :
  ```
  [Dossier presets]/
    MonPiano/
      MonPiano.neptunepreset   ← fichier de description JSON
  ```
  Les fichiers samples **ne sont ni copiés ni déplacés** à cette étape — ils restent dans la bibliothèque de samples de l'utilisateur, le preset se contente d'y pointer (voir résolution des chemins ci-dessus). Copier automatiquement des gigaoctets de samples à chaque sauvegarde serait surprenant et gourmand en espace disque.
- **Export portable (ZIP)**, sur action explicite de l'utilisateur ("Exporter en portable"), pas automatique à chaque sauvegarde — pour une distribution robuste et propre :
  ```
  MonPiano.zip
    preset.json          ← chemins réécrits en relatif au ZIP
    samples/
      kick.wav            ← copies réelles des fichiers, cette fois-ci
      snare.wav
  ```
  C'est le moment précis où les fichiers sont réellement dupliqués — un choix conscient de l'utilisateur, pas un effet de bord silencieux de la sauvegarde normale. Reste le moyen recommandé pour partager un instrument avec quelqu'un d'autre ou changer de machine.

### Structure interne d'un instrument Sampler (Groupes → Zones)
- **Zone = terme formel pour "un sample individuel mappé"** (un fichier audio avec sa plage de notes/vélocité et tous ses réglages propres) — équivalence à retenir dans tout le projet, "sample" et "Zone" désignent la même chose dans notre vocabulaire, cohérent avec l'éditeur de mapping façon Kontakt de la section 3.
- Chaque **Groupe** porte : identifiant, nom, état de liaison par bloc (filtre/enveloppe/effets — "suit le défaut" ou "personnalisé", avec les valeurs propres si personnalisé), liste de ses Zones.
- Chaque **Zone** porte : référence au fichier sample (chemin relatif + absolu de secours), note racine, plages de notes/vélocité, tuning, points de lecture/boucles, type de déclenchement, délai de lecture, round-robin, et son propre état de liaison par bloc vers le Groupe parent.

---

## 21. Compléments issus de la comparaison aux samplers du marché (Kontakt/HALion/Falcon)

### Time-stretch / pitch-shift indépendants en lecture temps réel
- Ajouté au périmètre du projet : possibilité de jouer un sample plus lent/rapide sans changer sa hauteur, ou de transposer sur une large plage sans effet "chipmunk" — distinct du varispeed déjà en place (qui change vitesse et hauteur ensemble, léger et zéro latence).
- Algorithme envisagé : méthode ouverte sans coût de licence (type WSOLA ou vocodeur de phase), à déterminer précisément en implémentation.
- **Ajoute une latence de traitement propre**, comme la convolution — suit le même principe que tout le reste du projet : lié au **profil de performance global**, désactivable en mode basse latence (retour au varispeed classique dans ce cas).
- Réglage par Zone ou global à l'instrument, à préciser en détail le moment venu.

### Enregistrement audio direct dans le Sampler
- Bouton "Enregistrer" dans le Sampler capturant le flux entrant (réutilise le routage "Entrée audio" déjà existant) dans un buffer, écrit en fichier, crée automatiquement la Zone correspondante.
- Simple capture parallèle, aucune interférence avec le chemin de lecture audio principal, aucun impact latence.

### Purge et prévisualisation sans chargement
- **Prévisualisation** : parcourir/prévisualiser un instrument sans charger tous ses samples en mémoire — s'appuie sur le cache de forme d'onde/métadonnées déjà défini (section 20/21), lecture du cache seule sans déclencher le chargement RAM/streaming réel.
- **Purge** : action qui décharge les samples non utilisés par le mapping actuellement actif (mute/groupes désactivés) — optimisation mémoire, aucun impact sur le chemin temps réel puisqu'il s'agit uniquement d'un état de chargement, pas de traitement audio.

### Limite globale de voix à l'échelle du plugin
- Ajoutée en complément des limites déjà existantes par instrument et par piste : un plafond de voix actives simultanées sur **toute l'instance du plugin** (toutes pistes confondues) — garde-fou de sécurité CPU/latence en usage réel avec beaucoup de pistes actives simultanément.

### Mode granulaire — écarté
- Mentionné pour mémoire (présent chez Falcon), **non retenu** pour ce projet — cohérent avec la décision déjà prise d'écarter le module Wavetable pour les mêmes raisons de charge de développement.

---

## 22. Précisions complémentaires — mécaniques implicites à clarifier

### Duplication "par référence" des Groupes — précision
Seul le **fichier audio en mémoire** est partagé entre l'original et la copie (un seul buffer chargé, pointé par plusieurs Zones) — pas les réglages. Modifier un paramètre (ex. points de boucle) dans la copie n'affecte jamais l'original. Seule la donnée audio brute est mutualisée pour économiser la RAM.

### Automation Reaper "orpheline" au changement de type de piste
Le pool de paramètres étant fixe, changer le type d'une piste ne supprime aucune automation déjà enregistrée sur les anciens paramètres — elle reste présente mais inactive. **Avertissement explicite** affiché au moment du changement de type ("des automations existantes vont devenir inactives"), plutôt que découverte silencieuse plus tard.

### Dépassement d'un pool (32 Groupes, 8 inserts, etc.)
Bouton "ajouter" grisé avec message explicite au survol, plutôt qu'erreur silencieuse ou plantage.

### Portée de l'undo/redo
Par instance de plugin (chaque piste a son propre historique), séparé de l'undo global de Reaper — comportement standard pour un plugin, mais explicité pour éviter toute surprise (Ctrl+Z dans Reaper n'annule pas une action faite dans la Façade, et inversement).

### Voix en cours de lecture au chargement d'un nouveau preset
Fondu de sortie très court plutôt que coupure nette, pour éviter un clic audible — cohérent avec le soin apporté ailleurs aux crossfades de boucle.

### Cache de forme d'onde/analyse
Fichier de cache léger et **persistant sur disque à côté du sample** (ex. `piano_C4_v3.wav.neptunecache`), contenant : version de schéma, date de modification + taille du fichier source (invalidation rapide), miniature de forme d'onde pour l'affichage, résultat de détection de pitch, et une **liste de mesures de niveau par fenêtre d'analyse** (voir format ci-dessous).

**Processus de calcul d'une mesure de niveau** : lecture temporaire du fichier source → extraction du segment correspondant à la fenêtre d'analyse (début + durée) → réduction en mono (moyenne des canaux si stéréo) → calcul du niveau **Pic et RMS simultanément** (coût négligeable de calculer les deux ensemble, évite un recalcul si le choix Pic/RMS de la Zone change plus tard) → conversion en dBFS.

**Partage entre Zones/instruments** : un même fichier audio peut être référencé par plusieurs Zones dans plusieurs instruments, chacune avec potentiellement sa propre fenêtre d'analyse. Le cache stocke donc une **liste de mesures indexées par fenêtre** (début + durée), pas une valeur unique :
```
{
  "fenêtres": [
    { "début_ms": 0, "durée_ms": 80, "pic_dB": -3.2, "rms_dB": -8.7 },
    { "début_ms": 120, "durée_ms": 50, "pic_dB": -6.1, "rms_dB": -11.4 }
  ]
}
```
Une Zone recherche une entrée correspondant exactement à sa fenêtre ; si absente, elle est calculée et **ajoutée** à la liste plutôt que d'écraser les entrées existantes — reste léger en pratique (fenêtres souvent proches/identiques entre Zones) tout en restant correct dans tous les cas.

**Créé une seule fois** — au moment où la fenêtre d'analyse est fixée (ajout du sample dans une Zone, ou automatiquement à l'import/remplacement) — puis simplement **lu** aux lancements suivants de l'instrument, jamais recalculé par défaut. Essentiel à l'échelle de gros instruments (10 000+ samples) : recalculer l'analyse à chaque chargement ruinerait le temps de démarrage, alors qu'une lecture de cache reste quasi instantanée. Cohérent avec le mode streaming : le cache étant minuscule (quelques valeurs numériques, pas l'audio), il est consultable sans charger le sample réel en RAM.

**Régénération** déclenchée par : fichier source modifié, nouvelle fenêtre d'analyse jamais rencontrée pour ce fichier, ou cache absent/corrompu. Vérification de "fichier source modifié" basée sur **date de modification + taille** (rapide à grande échelle) plutôt qu'une empreinte de contenu complète — compromis assumé pour la performance de chargement, risque de faux négatif quasi nul en usage réel.

Permet aussi de connaître à l'avance (hors temps réel) le niveau de référence d'un sample statique comme B dans le mécanisme de volume de relâchement — seul le niveau de la voix source (A, en direct) a besoin d'une mesure en temps réel, jamais celui d'un fichier statique déjà analysé.

### Compatibilité de version d'un preset
- Preset **plus ancien** que la version du plugin : migration automatique et silencieuse, grâce au schéma JSON versionné déjà prévu.
- Preset **plus récent** (créé par une version future du plugin) : refus de chargement avec message clair, plutôt qu'un chargement partiel ou corrompu silencieusement.

### RAM vs streaming — confirmation et précision du préchargement
Deux modes confirmés, sélectionnables par preset/instrument avec préséance utilisateur en session :
- **Tout-RAM** : fichier entier chargé en mémoire.
- **Hybride (type Kontakt)** : court préchargement en RAM du début du sample (réponse instantanée à l'attaque, sans attendre le disque) puis le reste streamé depuis le disque au fur et à mesure de la lecture. **Durée de préchargement par défaut : 200-500ms**, réglable dans le menu de réglages généraux (plus long = plus de marge si le disque est lent, plus court = économie de RAM sur les instruments à très nombreux samples chargés simultanément).

---

## 23. Schéma de métadonnées embarquées — écosystème Neptune / Flibuste Suite

### Empreinte de volume — concept généralisé (à détailler précisément en prochaine session)
Généralisation du cache de volume ponctuel (section 22) en une brique réutilisable pour plusieurs usages futurs identifiés : cohérence entre couches de vélocité, cohérence intra-round-robin, calibrage de crossfade entre couches, correspondance aux points de boucle (éviter un saut de volume audible à la couture), suggestion de gain à l'import.

**Structure envisagée** : contour continu sur tout le fichier plutôt qu'une liste de fenêtres ponctuelles calculées à la demande — échantillonné à basse résolution temporelle (ex. un point toutes les 10-20ms, pas à chaque sample), stocké en demi-flottants 16 bits (suffisant, ce n'est pas de l'audio à restituer, juste une valeur de niveau à comparer). N'importe quelle fenêtre demandée par n'importe quelle Zone se lit directement dans ce contour déjà calculé, sans jamais recharger ni ré-analyser le fichier source. Taille estimée : environ 1,2 Ko par sample de 3 secondes (Pic+RMS+LUFS combinés), de l'ordre de 12 Mo sur un instrument de 10 000 samples — négligeable.

**Trois contours parallèles**, chacun pour un usage distinct (pas redondants) :
- **Pic** : détection de risque de saturation, alignement de transitoires
- **RMS** (non pondéré) : énergie réelle du signal, toutes fréquences à égalité — pertinent pour du gain-staging technique ou une détection cohérente avec celle d'un compresseur (qui doit réagir au niveau réel, pas à une version "humanisée")
- **LUFS** (pondéré K, norme EBU R128) : sonie perçue, filtrée selon la sensibilité fréquentielle réelle de l'oreille — **défaut retenu pour l'usage "volume tel qu'entendu"** (ex. volume de relâchement lié au niveau audio réel, voir Module Sampler ci-dessus)

**Bibliothèque retenue** : **libebur128**, licence MIT (permissive, aucun souci de compatibilité). Expose `ebur128_loudness_window(état, fenêtre_en_ms, sortie)` — fenêtre de durée arbitraire, pas seulement les fenêtres standards fixes de la norme broadcast (400ms/3s) — permet de calculer directement le contour à la granularité voulue (10-20ms). Calcul K-weighted plus coûteux que le RMS simple, mais sans impact car toujours calculé hors temps réel, une seule fois, dans le pipeline de création du cache.

### Principe validé
Pas un nouveau format de fichier propriétaire (perdrait la lisibilité universelle) — un **schéma JSON embarqué dans un chunk/bloc personnalisé** des formats déjà supportés :
- **WAV** : chunk RIFF personnalisé
- **AIFF** : chunk IFF personnalisé
- **FLAC** : bloc de métadonnées de type **APPLICATION**, avec un identifiant enregistré proprement auprès du registre officiel Xiph plutôt que choisi au hasard

Le fichier reste un WAV/AIFF/FLAC standard, lisible par n'importe quel logiciel audio (qui ignore simplement notre bloc, comme prévu par les normes) ; notre plugin y lit en plus une couche de métadonnées riches.

### Structure envisagée — schéma complet (finalisé)

**Convention retenue** : noms de champs en **anglais** (comme tout le code et les identifiants techniques du projet), cohérent avec l'anglais par défaut de l'interface. Le français reste réservé à l'UI (via i18n) et au nom du projet lui-même.

**Bloc universel** (présent sur tout fichier, indépendant de la famille) :
- `schema_version` (entier, migration future)
- `families` (liste des espaces de noms présents, ex. `["organ"]` — permet de savoir quoi lire sans tout parser)
- Mapping : `root_note`, `low_key`, `high_key`, `low_velocity`, `high_velocity`, `tune_cents`, `transpose_semitones`
- Lecture : `loop_start`, `loop_end`, `loop_crossfade_ms`, `trigger_type` (attack/release/first/legato), `playback_delay_ms`
- Round-robin : `round_robin_group`, `round_robin_index`
- `group_name` (Groupe d'appartenance recommandé, utile à l'import d'un dossier entier de samples liés)
- **`sync_marker_ms`** + `sync_group_id` : marqueur de synchronisation — voir mécanisme ci-dessous
- `volume_footprint` : contours Pic/RMS/LUFS (section 22/23), liste extensible de mesures indexées par fenêtre — accepte nativement plusieurs RMS à temps d'intégration différents (chaque entrée porte déjà sa propre durée), aucune limite structurelle
- `detected_pitch`, `pitch_confidence`
- Provenance : `author`, `license`, `date`

**Marqueur de synchronisation** (`sync_marker_ms`) : réutilise directement le champ `playback_delay_ms` déjà existant — pas un nouveau mécanisme de lecture temps réel. Positionnable manuellement dans l'éditeur visuel ou suggéré automatiquement au point de plus forte énergie (via les courbes de volume déjà construites — synergie directe avec la navigation "aller au plus fort"). Pour toutes les Zones partageant un même `sync_group_id` (ex. couches multi-micros d'un même événement, distinct du round-robin même si les deux peuvent coïncider), l'application calcule au moment du mapping : `playback_delay_ms` = (décalage maximal du groupe) − (décalage de cette Zone), et écrit automatiquement le résultat dans le champ existant. Aligne les samples sur ce que l'oreille perçoit comme le moment fort, pas sur leur alignement technique brut.

**Espace de noms `organ`** : `rank`, `pipe_id`, `foot` (8'/16'/4'/2⅔'...), `manual`, `stop_family`, `harmonic` (jeux de mixture), et champs optionnels vérifiés contre la structure réelle de GrandOrgue (`ChurchName` confirmé comme champ existant, `WindchestGroup` confirmé comme objet de premier niveau) : `organ_name`, `church_building`, `address`, `organ_builder`, `windchest`.

**Espace de noms `hammond`** : `drawbars` (tableau des 9 valeurs 0-8), `percussion`, `vibrato_chorus`, `click_level`.

**Espace de noms `orchestral`** : `articulation` (legato/staccato/pizzicato/tremolo/spiccato...), `nominal_dynamic` (étiquette, distincte de la plage de vélocité numérique), `position` (optionnel).

**Espace de noms `percussion`** : `piece_type` (kick/snare/hihat_open/hihat_closed/tom/cymbal...), `mic_position` (overhead/close/room).

**Espace de noms `custom`** : clé-valeur libre défini par l'utilisateur, toujours disponible quelle que soit la famille — intégré à la recherche dans l'éditeur de mapping (section 24).

**Taxonomie `family`/`subfamily`** (menu déroulant + option "Autre : texte libre", jamais une liste fermée) : Pipe Organ (Principal/Flute/Bourdon/Reed/Mixture/Cornet), Electronic Organ (Hammond et assimilés), Acoustic Keyboard (Piano/Harpsichord/Electric Piano/Celesta), Drums/Percussion (Kick/Snare/Hihat/Tom/Cymbal/Auxiliary), Melodic Percussion (Vibraphone/Marimba/Xylophone/Bells), Orchestral (Strings/Woodwinds/Brass/Orchestral Percussion), Vocal (Choir/Solo), Synthetic/Electronic (Synth/FX/Texture), Field Recording/Ambience, Other/Uncategorized.

### Articulation avec le principe non-destructif (modèle en 3 cas)
1. **Fichiers créés par le plugin** (enregistrement direct, section 21) : schéma écrit dès la création — pas une modification d'un fichier existant de l'utilisateur.
2. **Fichiers importés par l'utilisateur** : jamais modifiés automatiquement, comportement actuel inchangé (cache externe `.neptunecache`), sauf conversion explicite demandée par l'utilisateur.
3. **Export portable ZIP** (section 20) : moment idéal pour embarquer le schéma, puisque ce mécanisme duplique déjà les fichiers pour la distribution — enrichit une copie déjà créée, sans nouvelle exception au principe non-destructif. Rend les fichiers partagés auto-descriptifs même en dehors du JSON de preset.

### Portée assumée
Vise l'écosystème du projet en priorité (nos fichiers, notre plugin), pas une tentative de faire adopter un standard par le reste de l'industrie — publication éventuelle comme standard ouvert documenté est une ambition séparée, pas un acquis immédiat, cohérente avec le fait que le projet est déjà open source.

---

## 24. Compléments finaux — audit de fin de conception

### Import de bibliothèques SFZ existantes
Le Sampler peut **importer directement des fichiers `.sfz` de tiers** (pas seulement s'en inspirer conceptuellement comme fait jusqu'ici) — parsing du format texte SFZ (bien documenté), conversion automatique en Groupes/Zones internes. Gros bénéfice en contenu immédiatement utilisable, vu le nombre de bibliothèques gratuites/libres existant dans ce format. Reste un vrai morceau de parsing à construire (opcodes, régions, hiérarchie group/region), pas un ajout gratuit.

### Import SF2/SF3 (SoundFont) et GrandOrgue (.organ)
**Conversion native complète en Groupes/Zones**, pas de mode cloisonné/limité — les trois formats se prêtent bien à une vraie conversion plutôt qu'à un simple viewer en lecture seule.
- **SF2** : conteneur RIFF, hiérarchie samples → instruments → presets, enveloppes/LFO/filtres déjà embarqués. Spécification (2.01/2.04) **publiée et ouverte** — parseur écrit directement depuis la norme, sans dépendance à du code tiers. **Courbes d'enveloppe** : attaque convexe (formule spécifique fixe, approximation proche mais pas garantie identique via notre système concave/convexe existant — précédent connu, la documentation SFZ admet la même limite avec un système de courbe similaire) ; decay et relâchement **linéaires en dB** (pas "exponentiels" en amplitude comme on le pensait initialement) — ajout d'un véritable **type de courbe "Linéaire en dB"** à notre moteur d'enveloppe généralisé pour une conversion **mathématiquement exacte** sur ces deux segments, plutôt qu'une approximation. Ce type de courbe a sa propre valeur au-delà de la compatibilité SF2 (comportement de decay perçu comme naturel sur de nombreux synthés pro).
- **SF3** : extension non officielle de SF2, samples compressés en Ogg Vorbis (avec perte) — format de diffusion finale, pas destiné à être réédité tel quel (dégradation à chaque recompression). **Décompression une seule fois à l'import**, conversion en PCM natif comme un sample WAV classique — plus aucune différence avec un import SF2 après cette étape unique, pipeline natif jamais recompressant.
- **GrandOrgue (.organ)** : format texte ODF, référence des WAV externes, jeux groupés par touches. Correspond directement aux briques déjà construites pour le mode orgue (palier 7, Façade avec claviers superposables, routeur de notes centralisé) — l'import vient remplir une architecture déjà prête plutôt que d'en nécessiter une nouvelle.
- **Polyphone** (éditeur SoundFont open source) : utilisé comme **outil de référence pour valider notre parseur** (test d'aller-retour), pas comme dépendance de code — cohérent avec la prudence déjà appliquée sur Dexed/msfa, mais ici la norme SF2 étant publique, aucune dépendance à du code tiers n'est de toute façon nécessaire.

### Édition en lot (batch) sur plusieurs Zones
**Sélection multiple de Zones** dans l'éditeur de mapping, avec application groupée d'un réglage (gain, fondu, décalage de tuning, sortie interne, etc.) à toute la sélection en une fois. Essentiel à l'échelle des instruments à 10 000+ samples déjà évoquée (section 21/22) — éditer une Zone à la fois y serait impraticable.

### Recherche/filtre dans l'éditeur de mapping
Champ de recherche pour retrouver une Zone précise par nom, note, ou Groupe dans un instrument à très nombreux samples.

### MIDI Learn
Flux dédié en plus du contrôle MIDI/OSC déjà complet sur tous les paramètres (palier 3) : clic droit sur un paramètre → "Apprendre" → mouvement d'un contrôleur physique → liaison automatique. Plus rapide à l'usage que de sélectionner un numéro de CC dans une liste déroulante.

### Cache de samples partagé entre instances du plugin
Si le même instrument est chargé dans plusieurs pistes ou projets simultanément, un **cache mémoire partagé entre les instances du plugin** (même portée que le switch de profil de performance partagé — toutes les instances chargées dans le processus Reaper en cours, section 10) évite de charger deux fois le même fichier audio en RAM.

### Indicateur de charge (voix actives / CPU)
Affichage en direct du nombre de voix actives et de la distance par rapport à la limite globale de voix (section 21) — aide à comprendre une montée de charge CPU en test, complète le métrage déjà prévu ailleurs dans le projet.

---

## Bilan — conception détaillée complète

Tous les modules prévus sont désormais définis : Piste (unité de base), Synthé soustractif (avec enveloppe de pitch dédiée), FM, Sampler (avec time-stretch, enregistrement direct, purge/prévisualisation, import SFZ/SF2/SF3/GrandOrgue, édition en lot, recherche), Bus, Master, les huit plugins stock (Utilitaire, Égaliseur, Compresseur/Limiteur/Expandeur/Gate, Chorus, Distorsion/Saturation, IR Loader, Delay, MicroPitch), le moteur de règles scriptables (palier 10), la Façade, le moniteur MIDI/OSC avec ses outils compagnons (dont MIDI Learn), la structure de stockage des données (presets, chemins, Groupes/Zones), les compléments issus de la comparaison au marché (section 21), les précisions complémentaires sur les mécaniques implicites (section 22), l'éditeur de sample intégré non-destructif, les compléments finaux de l'audit de fin de conception (section 24), et le schéma complet de métadonnées embarquées (section 23, champs en anglais). La conception détaillée est maintenant intégralement close.
