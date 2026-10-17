# Couche de contrôle : CC, OSC, paramètres de l'hôte (proposition de conception)

Statut : **étapes 1 à 6 faites (cœur testé, CC, OSC, macros, écran de gestion, retour OSC, NRPN et relatif branchés), le reste à faire** (2026-10-17). Voir « Avancement » en fin de document. Elle répond à la question : « une couche d'abstraction qui permette de commander
n'importe quel paramètre, par CC, par OSC ou par l'hôte, et de l'assigner à ce dont on a besoin ? ». Réponse : oui.

## 1. Ce qui existe déjà

- `ParameterSet` : la table unique de tous les paramètres (identifiant stable, nom, unité, plage, courbe, valeur atomique, numéro de version
  par paramètre). L'interface, l'automation de l'hôte (`NeptuneParameter`) et le moteur lisent et écrivent là.
- Les paramètres peuvent déjà être marqués `automatable`, `modulatable` ou `hidden`.
- La spec (`spec-modules.md`) prévoit que tout paramètre est « contrôlable en MIDI et OSC, retour d'information inclus ».
- Les emplacements assignables du Sampler sont un premier exemple du principe « un petit nombre de paramètres d'hôte qu'on assigne à ce
  qu'on veut ».

## 2. Principe

Trois étages, qui ne se connaissent pas :

```
 SOURCES                      ASSIGNATIONS (table enregistrée)             CIBLES
 CC MIDI (voie, numéro)  ──┐                                         ┌──  n'importe quel paramètre de ParameterSet
 message OSC (adresse)   ──┼──►  source → cible + échelle/courbe  ──►┤
 paramètre d'hôte (pool) ──┘                                         └──  actions (preset suivant, panique, banque...)
```

- **Source** : une façon de recevoir une valeur. Chaque source sait seulement produire « source X vaut v (0..1) ».
- **Cible** : un paramètre (par identifiant) ou une action nommée.
- **Assignation** : relie les deux, avec un minimum de réglages (voir §4).
- La table des assignations est **enregistrée avec le projet** (JSON versionné, comme le reste de l'état).

## 3. Les sources

### 3.1 OSC (recommandé en premier pour « tout commander »)

- **Réception** : un petit serveur UDP (module `juce_osc` de JUCE, déjà de la famille de ce qu'on utilise) sur son **propre thread réseau**.
  Il n'entre **jamais** dans le thread audio : il écrit dans `ParameterSet` (écritures atomiques), puis un relais côté interface prévient l'hôte
  (§5). C'est le point qui le rend plus simple et plus sûr que le CC.
- **Adresses** : tout paramètre est atteignable sans rien assigner, par une adresse déduite de son identité stable, par exemple
  `/neptune/track/3/organ/reverb`, `/neptune/master/volume`, `/neptune/track/1/slot/4`. Les chemins viennent d'une table de noms écrite une seule
  fois (la même que celle des noms d'hôte), pas devinés. Une adresse « catalogue » (`/neptune/list`) renvoie la liste.
- **Valeurs** : normalisées 0..1 par défaut ; une variante en unités réelles (`.../real`) pour les logiciels qui préfèrent des dB ou des Hz.
- **Retour d'information** (facultatif) : envoi des changements vers une adresse IP:port choisie (tablette, surface de contrôle). Non nécessaire
  pour commencer.
- **Réseau** : écoute sur `localhost` par défaut (pas de port ouvert au réseau sans l'avoir demandé) ; un port par instance (le premier libre à
  partir d'un port de base, affiché dans l'interface) car deux instances ne peuvent pas écouter le même port. **Désactivé par défaut.**

### 3.2 CC MIDI

- **Désactivé par défaut**, comme demandé : un réglage global « Contrôleurs MIDI » (interrupteur). Raison pratique en plus de la raison de
  principe : un clavier envoie des CC sans qu'on le veuille (molette = CC 1, pédale = CC 64, volume = CC 7) ; aujourd'hui, dans **Orgamon**,
  la molette change la vitesse du Leslie. L'interrupteur doit donc aussi s'appliquer à Orgamon.
- Quand il est activé, le travail dans le thread audio est minime : chercher le CC dans la table, écrire une valeur atomique. Rien d'autre
  (pas d'allocation, pas de verrou, pas d'appel à l'hôte).
- Assignation par **apprentissage MIDI** (clic sur un potard, on bouge le fader) ou par saisie ; jeux par défaut livrés (par exemple la
  correspondance de setBfree pour l'orgue).
- Limites assumées : 7 bits (128 pas) sauf paire MSB/LSB ou NRPN (plus tard si utile) ; pas de précision à l'échantillon.

### 3.3 Paramètres d'hôte (le « pool »)

- C'est ce qu'on a fait pour les emplacements du Sampler : un nombre **restreint** de paramètres que l'hôte voit et automatise, qu'on assigne
  à ce qu'on veut dans Neptune.
- Proposition : un **pool de 32 « macros »** par instance (nom d'hôte « Macro 1 » à « Macro 32 », 0..100 %), chacune assignable à n'importe quelle
  cible. Les paramètres « propres » (volume, pan, mute, solo, envois) restent visibles de l'hôte comme aujourd'hui.
- Pour alléger la liste : un réglage « Paramètres visibles de l'hôte » qui permet de masquer le reste (les assignations par OSC/CC continuent de
  fonctionner). Attention : masquer un paramètre casse l'automation déjà enregistrée dans les projets existants ; la valeur par défaut reste
  « tout visible ».
- Le contrôle de l'hôte lui-même par OSC (par exemple la surface OSC de Reaper) fonctionne déjà sur tout ce que l'hôte voit : le pool
  rend donc cette voie utilisable sans rien coder de plus.

## 4. Les assignations

Chaque assignation : { source, cible, mode, plage, courbe, inversion }.

- **Mode** : absolu (la valeur de la source est la valeur du paramètre) ou « reprise » (le paramètre ne bouge que lorsque la source
  passe par sa valeur actuelle : pas de saut avec un fader physique pas calé) ; relatif plus tard (encodeurs sans fin).
- **Plage** : sous-intervalle du paramètre (par exemple n'utiliser que 20..80 % de sa course). **Courbe** : linéaire / exponentielle.
- **Une source peut viser plusieurs cibles** (une macro qui ouvre le filtre et ferme la réverb) ; une cible peut avoir plusieurs sources
  (le fader physique et la tablette).
- **Actions** : preset suivant / précédent, banque, panique, changement de piste sélectionnée, mute/solo d'une piste.

## 5. Du thread de la source à l'hôte

Règle : **l'audio ne parle jamais à l'hôte**.

1. La source écrit dans `ParameterSet` (CC : thread audio ; OSC : thread réseau). Chaque paramètre a un numéro de version qui change.
2. Un relais sur le **thread de message** (timer ~30 Hz, comme les vues qui suivent déjà l'automation) compare les versions et, pour les
   paramètres modifiés par une source externe, prévient l'hôte avec `setValueNotifyingHost` (avec les gestes begin/end). Résultat : l'affichage
   de l'hôte suit avec un retard de quelques dizaines de ms, et l'enregistrement d'automation par ces mouvements est possible.
3. Les deux sens sont protégés contre la boucle (un changement qui vient de l'hôte ne repart pas en OSC/CC).

## 6. Enregistrement et migration

- Une section `control` dans l'état du plugin : réglages globaux (CC activés ? OSC activé, port, adresse de retour), table des assignations,
  macros du pool.
- Format versionné, lecture tolérante (une cible inconnue est ignorée avec un avertissement), comme les presets.
- Aucune migration nécessaire pour les projets existants (la section absente = rien d'assigné, CC et OSC désactivés).

## 7. Étapes proposées

1. **Fondations** (cœur, testé) : table des assignations, cibles (paramètre ou action), échelle/courbe/reprise, enregistrement JSON, relais
   vers l'hôte. Tests unitaires complets (aucune interface).
2. **CC dans Neptune** : acheminement des CC jusqu'à la table (désactivé par défaut), apprentissage MIDI, interrupteur aussi dans Orgamon.
3. **OSC en réception** : adresses déduites de l'identité des paramètres, catalogue, port par instance, désactivé par défaut.
4. **Pool de macros** : 32 macros assignables, option « paramètres visibles de l'hôte ».
5. **Écran de gestion** : liste des assignations, apprentissage, jeux par défaut (orgue).
6. **OSC en émission** (retour d'information) et NRPN/relatif, plus tard.

## 8. Décisions prises pour commencer (2026-10-17, modifiables)

- **Pool** : 32 macros par instance (choix de l'utilisateur).
- **Adresses OSC** : les deux. Le nom hiérarchique est la forme principale (`/neptune/track/3/organ/reverb`), écrit une fois dans une table
  figée par un test (jamais déduit des noms affichés, jamais traduit, jamais renommé sans alias) ; l'identifiant (`/neptune/id/9033`) reste
  toujours valable, car les identifiants ne changent jamais.
- **Visibilité pour l'hôte** : tout visible par défaut (compatibilité avec l'automation déjà enregistrée) ; un mode « liste réduite » facultatif
  qui ne montre que les paramètres de bande et le pool.
- **Actions au départ** : preset suivant, preset précédent, panique (toutes les notes), piste suivante, piste précédente. Mute / solo / contournement
  sont déjà des paramètres, donc des cibles ordinaires. Pas d'annuler/rétablir à distance.

## 9. Règles de temps réel (à respecter, vérifiées par des tests)

1. Le thread audio n'alloue pas, ne prend pas de verrou, ne parle pas à l'hôte, n'ouvre aucun fichier : il cherche le CC dans un tableau
   direct (voie x numéro) et écrit une valeur atomique.
2. Une **action** (preset suivant, banque...) n'est jamais exécutée par le thread qui la reçoit : elle est posée dans une file sans verrou et
   exécutée par le thread de message (charger un preset alloue et lit des fichiers).
3. L'**apprentissage MIDI** se fait sur le thread de message : l'audio ne fait que mémoriser « dernier CC reçu » dans une variable atomique.
4. Le **relais vers l'hôte** regroupe les changements (une notification par paramètre et par tic de ~30 Hz, jamais une par message reçu) et ne
   regarde que les paramètres signalés « modifiés par une source externe ».
5. L'**OSC** regroupe aussi (dernière valeur par adresse) et plafonne le nombre de messages traités par tic : un envoi trop rapide ne peut pas
   saturer le thread de message.
6. Ce qui reconstruit un moteur (accord de l'orgue, câblage, repli) n'est **pas** une cible de télécommande : seulement les paramètres de la table.

## 10. Avancement

- **Étape 1 faite** : `core/include/neptune/core/Control.h`, `core/src/Control.cpp`, 12 tests (`tests/test_control.cpp`, dont un test avec un thread audio simulé qui
  reconfigure en boucle ; lancé aussi sous ThreadSanitizer : aucune course signalée). Contient : assignations (CC, OSC, macro) vers un paramètre ou une
  action, plage / courbe / inversion / reprise, apprentissage du dernier CC, file d'actions sans verrou, signalement groupé des paramètres modifiés par une
  source (`drainChanges`), enregistrement JSON tolérant, tout désactivé par défaut.
- **Étape 2 faite (en attente de validation sur Windows/Linux)** :
  - Neptune : les CC des pistes passent par le moteur de contrôle (`NeptuneProcessor::processBlock`), **aucune assignation et tout désactivé par défaut**.
    Bouton « CC » dans la barre du haut : interrupteur, « assigner les contrôleurs de setBfree à la piste Orgue sélectionnée »
    (drawbars 70–78, pédale, réverb, percussion, vibrato, Leslie sur la molette...), « effacer toutes les assignations ». Enregistré avec le projet
    (section `control`). Les actions en file (preset suivant/précédent, panique, piste suivante/précédente) sont exécutées sur le thread de message ;
    les paramètres modifiés par un CC sont signalés à l'hôte à ~30 Hz (gestes begin/end). Les assignations « reprise » sont réarmées au chargement d'un preset.
  - Orgamon : les CC (sauf banque 0 et notes) sont **ignorés par défaut** (la molette ne touche plus le Leslie) ; bouton « CC » en haut pour les activer,
    état enregistré (`cc_enabled`).
  - Pas encore : apprentissage MIDI dans l'interface, écran de gestion des assignations, pool de macros déclaré à l'hôte, serveur OSC.
- **Étape 3 faite (à valider dans Reaper, jamais lancée ; compilation de l'adaptateur contrôlée à la syntaxe seulement)** :
  - Cœur (`Osc.h/.cpp`, 10 tests) : codec OSC (messages, paquets groupés, types i f d h s T F N ; paquets malformés refusés), table figée des adresses
    (`/neptune/master/...`, `/neptune/track/N/...`, `/bus/N`, `/parallel/N`, orgue, synthé, FM, sampler, emplacements ; numéros comptés à partir de 1 ;
    chaque paramètre du pool complet en a une, test), répartiteur (valeur normalisée, variante `/real` en unités réelles, `/neptune/id/<id>[/real]`,
    adresses assignées dans la table des assignations, catalogue `/neptune/list [préfixe]`), plafond de messages par fenêtre de 100 ms.
  - Catalogue : réponse en paquets groupés (< 1,1 Ko) renvoyés à l'expéditeur : `/neptune/list/item` (adresse, id, nom, type, min, max, défaut) puis
    `/neptune/list/done` (nombre).
  - Adaptateur (`OscServer`) : thread UDP, désactivé par défaut, port de base 9000 puis le premier libre jusqu'à +99 (pas de partage de port),
    `127.0.0.1` sauf case « réseau local » ; bouton « OSC » (à droite de « CC ») : interrupteur, réseau local, port affiché. Enregistré avec le projet
    (`osc_enabled`, `osc_all_interfaces`). Les paramètres modifiés par OSC sont signalés à l'hôte comme ceux des CC (~30 Hz, gestes begin/end).
  - Pas encore : choix du port de base dans l'interface (seulement dans la section `control` du projet), Orgamon et les autres plugins, retour d'information.
- **Étape 4 faite (à valider dans Reaper ; adaptateur contrôlé à la syntaxe seulement)** :
  - **Pool de 32 macros** : paramètres « Macro 1 » à « Macro 32 » (0..100 %, ids 13000 + n, `control::addMacroParams`), visibles de l'hôte, enregistrés comme
    les autres paramètres, adressables en OSC (`/neptune/macro/N`). Le thread audio compare leurs valeurs à chaque bloc et appelle `macroChanged` quand
    l'une a bougé ; après le chargement d'un état, les valeurs sont mémorisées sans rien appliquer (les cibles ont déjà leurs valeurs enregistrées ;
    très petite fenêtre de course si un bloc passe entre le chargement des paramètres et le drapeau de resynchronisation).
  - **Assignation** : clic droit SANS déplacement sur un potard (le glissement au bouton droit reste le réglage fin) -> « Assigner à une macro » (sous-menu 1..32,
    coché si assignée ; plusieurs macros possibles) et « Retirer les assignations de macro ». Seuls les potards (`NeptuneKnob`) ont ce menu pour l'instant, pas
    encore les faders du mixeur. Une macro ne peut pas viser une macro (pas de cycle). Assignation : plage 0..100 %, linéaire, absolue.
  - **Liste réduite pour l'hôte** : CONFLIT avec la conception initiale (réglage du projet). L'hôte lit la liste des paramètres une seule fois, à la création,
    avant le chargement du projet : le réglage est donc **global** (`reduced_host_list` dans `settings.json`, case dans le menu « CC ») et ne joue qu'au
    prochain chargement du plugin. Il garde le master, les bandes (volume, pan, mute, solo, type, bypass d'inserts, envois des pistes, bus, pistes parallèles)
    et les macros (`control::isBandOrMacroParam`). Les autres paramètres restent dans le moteur, l'état du projet, l'interface, les CC et l'OSC, mais
    l'hôte ne les voit plus : l'automation déjà enregistrée dessus ne s'applique plus.
- **Étape 5 faite (à valider dans Reaper ; adaptateur contrôlé à la syntaxe seulement)** : menu « CC » -> « Gérer les assignations... » ouvre une fenêtre flottante
  (`ControlPanel`). Une ligne par assignation : source (bouton : apprendre un CC, macro 1..32, adresse OSC saisie), cible (menu hiérarchique construit sur les
  adresses OSC, ou action), min / max (%), courbe, inversion, mode Absolu / Reprise, suppression. « Nouvelle assignation » apprend tout de suite : bouger
  un contrôleur capte son numéro de CC (toutes les voies ; la voie précise se règle dans le JSON du projet), même si les CC sont désactivés. Une assignation
  n'est enregistrée qu'une fois sa cible choisie (le moteur ignore celles sans cible). Pas d'export / import de jeux (décision), pas d'apprentissage par clic droit
  sur un potard, voie MIDI non éditable dans l'écran.
- **Étape 6 faite (à valider dans Reaper et avec une vraie surface ; adaptateur contrôlé à la syntaxe seulement)** :
  - **Retour d'information OSC** : menu « OSC » -> « Retour d'information vers une surface... » (saisie `hôte:port`, enregistrée avec le projet) ; ou une surface
    envoie `/neptune/sync [port]` : l'expéditeur devient le destinataire (non enregistré, effacé à l'arrêt du serveur) et reçoit tout l'état. Ensuite chaque
    paramètre qui change, quelle qu'en soit l'origine (interface, hôte, CC, OSC), est renvoyé à son adresse, valeur normalisée 0..1, regroupé à ~30 Hz
    (400 messages au plus par tic ; l'état complet part par tranches). Le changement venu de la surface est aussi renvoyé (écho) : sans effet, la valeur étant la même.
    Entrée de la surface et sortie du retour utilisent deux sockets distincts : la surface voit le retour arriver d'un autre port que celui où elle écrit.
  - **NRPN 14 bits** : nouvelle source (CC 99/98 choisissent, 6/38 portent la valeur ; CC 100/101 désélectionnent), par voie ou toutes les voies, apprise comme un
    CC (les CC 99/98/6/38/100/101 ne sont plus appris comme CC simples). Pas de NRPN relatif.
  - **Encodeurs relatifs** (sources CC vers un paramètre) : trois codages (complément à deux, décalé 64, bit de signe), 1/127 de la course min..max par cran,
    borné au sous-intervalle ; inversion respectée. Choix dans la colonne « Mode » de l'écran de gestion (grisé pour les autres sources).
  - Pas fait : sensibilité réglable des encodeurs relatifs, retour vers plusieurs destinataires, retour des actions.
- Limite connue : le suivi des changements couvre 32768 paramètres au plus (Neptune en déclare environ 3000).
