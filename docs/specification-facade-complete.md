# Spécification complète d'une Façade de Neptune (fichier `.neptunefacade`)

Version du format : **1** (enveloppe `facade.json`). Ce document est écrit pour être lu par un humain **ou donné tel quel à une IA** (ChatGPT, Claude, etc.) pour qu'elle fabrique une Façade entière : fond, disposition des widgets, liens vers les réglages, styles d'images. La section 13 contient un message prêt à coller.

Il complète `specification-styles-facade.md` (qui décrit seulement le dessin des images d'un style) et s'appuie sur `parametres-facade.md` (la liste des réglages qu'un widget peut lier, avec leurs identifiants).

Tout ce qui est écrit ici a été relu dans le code de Neptune (`core/src/Facade.cpp`, `core/include/neptune/core/Facade.h`, `adapter-juce/src/FacadeBundle.h`). Ce qui n'a pas pu être vérifié est dit à la section 12.

## 1. En dix lignes

- Une **Façade** est une page d'interface personnalisée pour une piste de Neptune : une image de fond, et des **widgets** posés dessus (potards, boutons, curseurs, claviers…), chacun lié à un réglage existant.
- Une Façade ne change jamais le son par elle-même : elle ne fait que montrer et commander des réglages qui existent déjà.
- Un fichier **`.neptunefacade`** est une archive **zip**. Il contient `facade.json` (obligatoire), un dossier `assets/` (les images) et, facultativement, un dossier `packs/`.
- Le canevas est un rectangle **logique de 1000 × 600**. Neptune le met à l'échelle de la place disponible.
- Un widget a un **type** (nombre de 0 à 12), une position (`x`, `y`), une taille (`w`, `h`) et, selon son type, un **réglage lié** (`param`, un identifiant numérique).
- Les identifiants viennent de **`parametres-facade.md`**. Ne jamais en inventer.
- Une Façade peut avoir jusqu'à **16 pages** ; chaque page a ses widgets, son fond et sa bibliothèque de styles.
- Une valeur invalide ou absente prend sa valeur par défaut ; les bornes sont appliquées au chargement. Rien n'est exécuté : le fichier ne contient que du texte JSON et des images.
- Au chargement sur une piste, tous les identifiants sont rebranchés sur cette piste. On fabrique donc toujours la Façade avec les identifiants de la **piste 1**.
- Les widgets sont dessinés dans l'ordre de la liste : le dernier est dessus.

## 2. Le fichier `.neptunefacade`

```
Orgue.neptunefacade           (zip)
├── facade.json               obligatoire
├── assets/                   les images, quelle que soit leur origine
│   ├── 1_fond.png
│   ├── 2_tirant_off.svg
│   └── 3_tirant_on.svg
└── packs/                    facultatif : packs de styles (.neptunestyle)
```

- `facade.json` est à la racine de l'archive. Sans lui, Neptune refuse le fichier (« Not a Facade file »).
- Les chemins d'images dans `facade.json` s'écrivent **`asset:assets/<nom du fichier>`**. Neptune les remplace, au chargement, par le vrai chemin des images extraites.
- Un style peut aussi désigner une image d'un pack déjà installé : `pack:<identifiant du pack>!<entrée>` (par exemple `pack:organ-classic!drawbar-tab/off.svg`). Un pack qui n'est pas dans l'archive ni installé chez l'utilisateur donne une image manquante.
- Pas de caractère `\` ni de `..` dans les noms. Noms de fichiers sans espace de préférence.
- Pour créer l'archive : zipper le contenu du dossier (pas le dossier lui-même) et renommer en `.neptunefacade`.

## 3. L'enveloppe `facade.json`

```json
{
  "format": 1,
  "track": 0,
  "facade": { ...la première page, voir section 4... }
}
```

- `format` : toujours 1.
- `track` : l'indice de la piste d'origine des identifiants, de 0 à 15. **Toujours 0** pour une Façade fabriquée avec la table de `parametres-facade.md` (piste 1).
- `facade` : un objet « page », avec en plus, si la Façade a plusieurs pages, la liste `pages` (section 4).

## 4. Une page

```json
{
  "name": "Registres",
  "image": "asset:assets/1_fond.png",
  "fit": 0,
  "styles": [ ... ],
  "widgets": [ ... ],
  "pages": [ { "name": "Effets", "widgets": [ ... ] } ]
}
```

| Clé | Sens | Valeurs |
|---|---|---|
| `name` | nom de la page (onglet). Vide : « 1 », « 2 »… | texte, 64 caractères au plus |
| `image` | image de fond. Absente : fond uni. | chemin `asset:...`, **PNG ou JPG** |
| `fit` | ajustement du fond au canevas | 0 ajuster (bandes autour), 1 étirer, 2 remplir (rogner) |
| `styles` | bibliothèque de styles d'images de la page (section 9) | liste, 64 au plus |
| `widgets` | les widgets, du dessous au dessus | liste, **200 au plus** |
| `pages` | pages suivantes : chacune est un objet « page » comme celui-ci (sans `pages`) | 15 au plus, donc 16 pages en tout |

Le fond fait idéalement **1000 × 600** (ou un multiple : 2000 × 1200). Avec `fit` à 0, une image d'un autre rapport laisse des bandes.

## 5. Un widget : les clés communes

| Clé | Sens | Défaut | Bornes |
|---|---|---|---|
| `id` | identifiant du widget, **unique dans la page** | — | entier ≥ 1 (sinon renuméroté) |
| `type` | type de widget | 0 | 0 à 12 (section 6) |
| `x`, `y` | coin haut gauche, dans le canevas | 0 | 0 à 1000−`w` ; 0 à 600−`h` |
| `w`, `h` | largeur, hauteur | selon le type | 16 à 1000 ; 16 à 600 |
| `param` | identifiant du réglage lié (`parametres-facade.md`) | absent = non lié | entier > 0 |
| `text` | étiquette ou légende | vide | 64 caractères au plus |
| `style` | identifiant d'un style de la page (section 9) | 0 = aucun | doit exister, sinon ignoré |
| `parent` | identifiant de la **zone** qui contient ce widget (section 7) | 0 = aucune | doit être une zone existante |

Un widget qui sort du canevas est ramené dedans. Un widget non lié (sans `param`) s'affiche mais ne commande rien.

## 6. Les types de widget

| Type | Nom | Taille par défaut (w × h) | Lien (`param`) | Rôle |
|---|---|---|---|---|
| 0 | Potard | 90 × 110 | un réglage continu, à pas ou à choix | bouton rotatif ; `text` = légende |
| 1 | Bouton | 120 × 44 | un interrupteur (0/1) | bouton qui bascule ; `text` = son nom |
| 2 | Étiquette | 160 × 32 | aucun | texte seul |
| 3 | Menu | 170 × 56 | un réglage **à choix** (liste) ou à pas | liste déroulante |
| 4 | Action | 130 × 44 | aucun (voir `snap`, `mode`) | piston : applique d'un coup un instantané de valeurs |
| 5 | Courbe | 340 × 260 | aucun | éditeur de la courbe de vélocité d'entrée de la piste |
| 6 | Zone | 320 × 200 | aucun | cadre avec titre qui range ses widgets (section 7) |
| 7 | Clavier | 520 × 110 | aucun (voir `ch`, `first`…) | clavier virtuel lié à un canal MIDI |
| 8 | Curseur | 260 × 44 | un réglage continu, à pas | tirant, horizontal ou vertical |
| 9 | Voyant | 40 × 40 | selon `source` | témoin allumé / éteint |
| 10 | Set | 90 × 44 | aucun | touche « Set » d'une console : arme la mémorisation du prochain piston |
| 11 | Message MIDI | 150 × 44 | aucun | envoie un message MIDI |
| 12 | Vumètre | 44 × 180 | aucun (voir `source`) | niveau ou réduction de gain |

### 6.1 Potard (0) et Curseur (8)

| Clé | Sens | Valeurs |
|---|---|---|
| `steps` | comportement des crans | 0 automatique (le curseur saute de cran en cran sur un réglage à pas ; le potard est continu) ; 1 toujours continu ; 2 crans personnalisés (`notches`) |
| `notches` | valeurs **réelles** du réglage (celles du tableau, par exemple des dB), triées, sans doublon, 128 au plus. Avec `steps: 2` il en faut au moins 2. Les positions sont réparties à égale distance. | liste de nombres |
| `invert` | 1 : sens inversé | 0 ou 1 |
| `scale` | comment la position du widget devient une valeur, sur un réglage continu | 0 l'échelle propre du réglage ; 1 linéaire ; 2 logarithmique ; 3 exponentielle |
| `response` | courbe libre croissante appliquée à la position avant l'échelle (facultatif). Objet `{"shape": -1..1, "points": [[x,y],...]}`. | voir `VelocityResponse.h` ; en cas de doute, l'omettre |
| `mirror` | autres réglages qui reçoivent la **même position** (une boîte expressive qui commande plusieurs claviers) | liste d'identifiants, 16 au plus |
| `vertical` | Curseur seulement : 1 vertical | 0 ou 1 |

### 6.2 Menu (3)

Lié à un réglage **à choix** (par exemple `9024`, le vibrato de l'orgue : 0=Off … 6=C3). Le menu liste les valeurs du réglage ; l'IA n'a pas à les écrire.

### 6.3 Action (4) et Set (10) : les pistons

- `snap` : l'instantané, une liste de paires `[identifiant, valeur réelle]` (64 au plus). Exemple : `[[9000, 8.0], [9001, 0.0]]`. Au clic, ces réglages prennent ces valeurs.
- `mode` : 0 restaure l'instantané ; 1 remet à leur valeur par défaut les réglages de `snap` (annulation générale) ; 2 et plus sont des commandes de combinaison (voir `specification-combinaisons.md`).
- Le Set arme le piston : le prochain piston cliqué mémorise les valeurs **courantes** de ses réglages. Une Façade livrée peut donc fournir des pistons vides (`snap` absent) que l'utilisateur programme.

### 6.4 Clavier (7)

| Clé | Sens | Valeurs |
|---|---|---|
| `ch` | canal MIDI | 1 à 16 |
| `first` | première touche affichée à gauche (un do) | 0 à 120, défaut 36 |
| `hold` | 1 : une touche cliquée reste enfoncée jusqu'au clic suivant | 0 ou 1 |
| `vel` | vélocité envoyée | 1 à 127, défaut 100 |
| `wheels` | 1 : deux **molettes** à gauche des touches, hauteur (pitch bend, revient au milieu quand on la lâche) et modulation (CC 1, reste où on la laisse). Elles envoient du MIDI sur le canal `ch` du clavier, vers Neptune. Elles prennent 60 unités de la largeur du widget. Absent = pas de molettes. | 0 ou 1 |

Le nombre de touches dépend de la largeur (moins la bande des molettes, s'il y en a).

### 6.5 Voyant (9)

`source` : 0 allumé par le réglage `param` ; 1 allumé par les notes jouées sur le canal `ch` (1 à 16) ; 2 allumé par les notes de la piste.

### 6.6 Message MIDI (11)

```json
{ "id": 20, "type": 11, "x": 40, "y": 500, "text": "Panique",
  "midi": { "kind": 2, "channel": 1, "d1": 123, "d2": 0 }, "target": 1, "fader": 0 }
```

- `kind` : 0 Note On, 1 Note Off, 2 Control Change, 3 Program Change, 4 Pitch Bend, 5 Pression de canal, 6 Pression polyphonique, 7 SysEx.
- `d1`, `d2` : selon le type. Note On/Off : note et vélocité. Control Change : contrôleur et valeur. Program Change : programme. Pitch Bend : `d1` sur 14 bits (0 à 16383, repos 8192). SysEx : `"sysex": "F0 7E 7F 09 01 F7"` (octets en hexadécimal).
- `target` : 1 vers Neptune (comme un clavier MIDI), 2 vers la sortie MIDI de l'hôte, 3 les deux.
- `fader` : 1 transforme le widget en curseur (la valeur suit le geste) au lieu d'un bouton.

### 6.7 Vumètre (12)

`vertical` : 1 vertical (défaut), 0 horizontal. `source` (entier) :

| Valeur | Source |
|---|---|
| 0 | la piste de la Façade (niveau) |
| 1 | le Master |
| 2 | la réduction de gain du compresseur de la piste de la Façade |
| 100 + i | niveau de la piste i (0 à 15) |
| 200 + i | niveau du bus i |
| 300 + i | niveau de la piste parallèle i |
| 400 + i, 500 + i, 600 + i | réduction de gain du compresseur de la piste, du bus, de la piste parallèle i |
| 700 | réduction de gain du compresseur du Master |

Une source inconnue devient 0.

### 6.8 Courbe (5) et Étiquette (2)

La courbe n'a pas de clé propre : c'est l'éditeur de la courbe de vélocité d'entrée de la piste. L'étiquette n'a que `text` (et un style, pour la couleur et la taille du texte).

## 7. Les zones : regrouper et ranger

Une **zone** (type 6) est un cadre avec un titre (`text`). Les widgets posés dedans lui sont rattachés (`"parent": <id de la zone>`). La zone sert à deux choses : regrouper visuellement, et **ranger ses widgets** quand l'utilisateur les déplace ou les redimensionne dans Neptune.

| Clé | Sens | Valeurs |
|---|---|---|
| `layout` | rangement | 0 libre (rien n'est déplacé) ; 1 en ligne ; 2 en colonne ; 3 en matrice ; 4 en cercle |
| `align` | sur l'axe transversal (ligne : haut/centre/bas ; colonne : gauche/centre/droite) | 0 début, 1 centre, 2 fin |
| `justify` | sur l'axe principal | 0 début, 1 centre, 2 fin, 3 répartition égale. En cercle : place du premier widget (0 gauche, 1 haut, 2 droite, 3 bas) |
| `gap` | espace entre widgets rangés | 0 à 64, défaut 8 |
| `cols` | matrice : nombre de colonnes | 1 à 12, défaut 2 |

- **Important : le rangement n'est pas recalculé au chargement du fichier.** Neptune prend les `x`, `y`, `w`, `h` tels qu'ils sont écrits ; il ne range une zone que lorsque l'utilisateur modifie quelque chose dans la Façade (d'après le code de l'éditeur, qui appelle le rangement après chaque modification de géométrie). **L'IA doit donc calculer elle-même des positions exactes**, à l'intérieur de la zone, qui correspondent à la disposition qu'elle déclare. Si les positions écrites ne suivent pas la disposition, la page s'affiche comme écrite, puis saute au premier déplacement.
- Une zone a une marge intérieure de 10 et, si elle a un titre, un bandeau de 24 de haut : placer les widgets à partir de `x + 10` et `y + 24 + 10`.
- Les zones peuvent être **imbriquées**. Un `parent` qui ne désigne pas une zone, qui est le widget lui-même ou qui fait une boucle est ignoré.
- Donnez toujours `parent` explicitement pour les widgets d'une zone : n'attendez pas que Neptune les rattache.
- Une zone qui sert seulement de cadre (sans rangement) prend `layout: 0`.

## 8. Lier un widget à un réglage

1. Ouvrir `parametres-facade.md`. Chercher le réglage voulu par son **nom** (par exemple « Leslie Speed »).
2. Copier son **identifiant** (9027) dans `"param"`.
3. Choisir le type de widget d'après la colonne « Type » du tableau :
   - *interrupteur* → Bouton (1), ou Voyant (9) pour un simple témoin ;
   - *choix* → Menu (3), ou Potard (0) s'il y a peu de choix ;
   - *continu* → Potard (0) ou Curseur (8) ;
   - *pas* → Curseur (8) (les crans suivent les pas), ou Potard (0).
4. Pour un piston (Action), écrire `snap` avec des **valeurs réelles** dans la plage du tableau (des dB pour un volume, 0 ou 1 pour un interrupteur, le numéro du choix pour un menu).

Règles :
- N'utiliser que des identifiants de la table. Un identifiant inconnu laisse le widget non lié.
- Les réglages à l'intérieur des inserts, des bus ou du Master ne sont pas dans la table : ne pas les lier (l'utilisateur les liera dans Neptune).
- Les identifiants sont ceux de la piste 1 ; `"track": 0` dans l'enveloppe dit à Neptune de les rebrancher sur la piste où on charge la Façade.
- Un même réglage peut être lié par plusieurs widgets (un potard et un voyant, par exemple).

## 9. Les styles d'images

Un **style** est un jeu d'images qui change l'apparence d'un widget, jamais sa fonction. Les styles se déclarent dans `styles` de la page, et un widget en choisit un par `"style": <id>`. Un même style peut servir à plusieurs widgets. Le dessin des images (tailles, rapport, bandes de potard, cadres en neuf zones, texte) est détaillé dans **`specification-styles-facade.md`** ; cette section donne seulement la forme JSON.

```json
"styles": [
  { "id": 1, "name": "Tirant",
    "off": "asset:assets/2_tirant_off.svg", "on": "asset:assets/3_tirant_on.svg",
    "text_colour": "#2a2a2a", "text_size": 50, "text_position": 0, "text_bold": true },
  { "id": 2, "name": "Potard",
    "body": "asset:assets/4_potard.svg", "pointer": "asset:assets/5_aiguille.svg",
    "angles": [-135, 135], "pivot": [50, 50] }
]
```

Clés des images (une image absente laisse le dessin d'origine de Neptune pour cette partie) :

| Clé | Utilisée par |
|---|---|
| `off`, `on`, `off_hover`, `on_hover`, `disabled` | Bouton, Action, Voyant (éteint, allumé, survol, widget non lié) |
| `track`, `handle` | Curseur (rail, poignée) |
| `strip` (+ `frames`, `strip_h`) | Potard : une bande de `frames` images (2 à 512), empilées verticalement, ou côte à côte si `strip_h` est vrai |
| `body`, `pointer` (+ `angles`, `pivot`) | Potard en deux couches : corps fixe, aiguille tournée entre les deux angles (degrés, 0 = vers le haut) autour du pivot (en % de l'image). L'aiguille prime sur la bande. |
| `key_white`, `key_white_on`, `key_black`, `key_black_on` | Clavier |
| `frame` (+ `slice`, `slice_scale`) | Zone : cadre découpé en neuf zones. `slice` = `[gauche, haut, droite, bas]` en % de l'image, chacun 0 à 45, défaut 25 ; `slice_scale` 25 à 400 (%). |

Texte posé par Neptune sur les widgets : `text_colour` (`"#rrggbb"`, vide = couleur du thème), `text_size` (0 à 100, % de la hauteur du widget ; 0 = automatique), `text_position` (0 centre, 1 haut, 2 bas, 3 gauche, 4 droite), `text_bold` (faux pour du maigre), `key_lit` (teinte d'une touche allumée quand le style n'a pas d'image pour elle).

- Formats : **SVG** (recommandé), PNG, JPG. Le **fond** de la page, lui, est PNG ou JPG.
- Un style supprimé ou inexistant (`style` qui ne correspond à aucun `id`) est ignoré.
- Les SVG peuvent utiliser les couleurs du thème (section 18 de `specification-styles-facade.md`).

## 10. Un exemple complet et valide

Une page « Orgue » : un fond, une zone « Vibrato et Leslie » (cadre sans rangement), un menu, deux potards, un bouton.

```json
{
  "format": 1,
  "track": 0,
  "facade": {
    "name": "Orgue",
    "fit": 2,
    "styles": [
      { "id": 1, "name": "Cadre", "frame": "asset:assets/1_cadre.png", "slice": [20, 30, 20, 20] }
    ],
    "widgets": [
      { "id": 1, "type": 6, "x": 40, "y": 40, "w": 520, "h": 220, "text": "Vibrato et Leslie",
        "layout": 0, "style": 1 },
      { "id": 2, "type": 3, "x": 60, "y": 90, "w": 170, "h": 56, "param": 9024, "text": "Vibrato", "parent": 1 },
      { "id": 3, "type": 0, "x": 250, "y": 80, "w": 90, "h": 110, "param": 9034, "text": "Profondeur", "parent": 1 },
      { "id": 4, "type": 0, "x": 360, "y": 80, "w": 90, "h": 110, "param": 9028, "text": "Leslie", "parent": 1 },
      { "id": 5, "type": 1, "x": 60, "y": 300, "w": 120, "h": 44, "param": 9029, "text": "Drive" }
    ]
  }
}
```

(Les identifiants 9024, 9034, 9028 et 9029 viennent de `parametres-facade.md` : vibrato, profondeur du vibrato, mélange Leslie, drive.)

## 11. Liste de contrôle avant de rendre le fichier

1. Le zip contient `facade.json` **à la racine**, et chaque image citée dans `asset:assets/...` est dans `assets/`.
2. `facade.json` est du JSON valide (guillemets droits, pas de virgule finale, pas de commentaires).
3. `"format": 1` et `"track": 0`.
4. Aucun `id` de widget en double dans une page ; au plus 200 widgets, 64 styles, 16 pages.
5. Tout `param` et tout identifiant de `snap` ou `mirror` existe dans `parametres-facade.md`.
6. Chaque widget tient dans le canevas : `x + w ≤ 1000` et `y + h ≤ 600` ; `w, h ≥ 16`.
7. Chaque `parent` est l'`id` d'une zone (type 6) de la même page.
8. Chaque `style` d'un widget est l'`id` d'un style de la même page.
9. Textes de 64 caractères au plus.
10. Aucun widget ne se cache entièrement derrière un autre (l'ordre de la liste est l'ordre d'empilement).
11. L'interface reste lisible : zones d'au moins 16 de côté, texte suffisamment grand, contraste entre le texte et le fond.

## 12. Ce qui n'est pas vérifié

- Je n'ai pas testé le chargement d'un fichier fabriqué par une IA dans Neptune : les clés ont été relues dans le code de lecture (`pageFromJson`), qui est tolérant (une clé inconnue ou invalide est ignorée, une valeur hors borne est ramenée dans la borne).
- La table des réglages vient du programme, mais ne contient que ceux d'une piste. Les réglages d'un insert se lient dans Neptune.
- Le fond est décrit PNG ou JPG d'après le modèle de données ; les styles acceptent le SVG d'après la spécification des styles. Si un SVG de fond est refusé, utiliser un PNG.
- Les modes d'Action à partir de 2 (commandes de combinaison) renvoient à `specification-combinaisons.md`, que cette spécification ne recopie pas.
- Le rangement automatique des zones : j'ai constaté dans le code qu'il n'est appelé qu'à l'édition, pas au chargement ; je ne l'ai pas observé dans Neptune.
- Le rendu final (échelle, texte, rangement des zones) ne se juge qu'en ouvrant la Façade dans Neptune.

## 13. Message prêt à coller dans une IA

> Tu vas fabriquer une **Façade** pour Neptune (un instrument CLAP), au format `.neptunefacade`. Je te joins trois documents : `specification-facade-complete.md` (le format), `parametres-facade.md` (les réglages et leurs identifiants) et `specification-styles-facade.md` (le dessin des images).
>
> Ce que je veux : *[décrire ici : l'instrument visé, l'ambiance graphique, les réglages à montrer, le nombre de pages]*.
>
> Règles : suis la spécification à la lettre ; n'utilise que des identifiants de `parametres-facade.md` ; regroupe par **zones** (section 7) et calcule des coordonnées exactes (Neptune ne range pas au chargement) ; dessine les images en **SVG** (sauf le fond, en PNG ou JPG de 1000 × 600). Rends-moi : (1) le contenu complet de `facade.json`, (2) chaque image, avec son nom exact dans `assets/`, (3) la liste de contrôle de la section 11 passée en revue, case par case, en disant ce que tu n'as pas pu vérifier. Ne devine pas : si une information manque, pose-moi la question.
>
> Je m'occuperai de zipper le dossier et de renommer le zip en `.neptunefacade`.
