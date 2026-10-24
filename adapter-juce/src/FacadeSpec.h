#pragma once
// GENERE par scripts/gen_facade_spec_header.py : ne pas modifier a la main (modifier les documents de docs/ puis relancer le script).
#include <juce_core/juce_core.h>

namespace facade_spec
{
inline juce::String french()
{
    return juce::String::fromUTF8 (
        R"FSPEC(# Spécification des styles d'images de la Façade (format `.neptunestyle`)

Version du format : **2** (les packs du format 1 restent valides ; le format 2 ajoute les couches de potard, les touches de clavier, les cadres de zone, le voyant, les états survol/inactif, le style du texte et les couleurs du thème dans les SVG). Ce document est écrit pour être lu par un humain **ou donné tel quel à une IA** (ChatGPT, Claude, etc.) pour qu'elle crée des styles. La section 12 contient un message prêt à coller.

## 1. En dix lignes

- La **Façade** de Neptune est une page d'interface personnalisée : on y pose des widgets (potards, boutons, curseurs…) liés aux paramètres des instruments.
- Un **style** est un jeu d'images qui change l'*apparence* d'un widget. Il ne change **jamais** sa *fonction* (valeurs, crans, sens : ça se règle sur le widget).
- Un **pack** (fichier `.neptunestyle`) est une archive **zip** qui contient un ou plusieurs styles.
- Formats d'image : **SVG** (recommandé), **PNG**, **JPG**.
- Chaque image a un **rôle**, donné par son **nom de fichier** : `off`, `on`, `track`, `handle`, `strip_N`, `body`, `pointer`, `key_white`, `frame`...
- Boutons, actions et voyants : `off` + `on` (+ `off_hover`, `on_hover`, `disabled`). Curseurs : `track` + `handle`. Potards : `strip_N` (une bande de N images) **ou** `body` + `pointer` (deux couches). Claviers : `key_white`, `key_white_on`, `key_black`, `key_black_on`. Zones : `frame`.
- Une image absente = Neptune garde son dessin d'origine pour cette partie.
- Aucun code n'est exécuté : un pack ne contient que des images et du texte JSON.

## 2. Vocabulaire

| Terme | Sens |
|---|---|
| Façade | La page d'interface personnalisée d'une piste de Neptune. |
| Widget | Un élément posé sur la Façade : potard, bouton, action, curseur, menu, zone, clavier… |
| Style | Un jeu d'images (une par rôle) qui habille un widget. |
| Pack | Un fichier `.neptunestyle` (ou un dossier de même structure) qui contient des styles. |
| Bibliothèque | Le dossier où Neptune range les packs installés. |

## 3. Quels widgets utilisent quelles images

| Widget | Images utilisées | Remarques |
|---|---|---|
| Bouton, Action | `off` et `on` ; facultatifs `off_hover`, `on_hover`, `disabled` | Éteint/fermé et allumé/ouvert ; survol par la souris ; widget non lié (en mode Jeu). |
| Set, Message MIDI | `off` et `on` | Comme un bouton (Set : allumé quand il est armé ; Message MIDI : allumé pendant l'appui). |
| Voyant | `off` et `on` ; facultatif `disabled` | Allumé par un paramètre, les notes d'un canal ou les notes de la piste. Pas cliquable. |
| Curseur (tirant) | `track` (rail) et `handle` (poignée) | Horizontal ou vertical, au choix de l'utilisateur. |
| Potard | `strip_N` **ou** `body` + `pointer` | Une bande de N images, une par position ; ou un corps fixe et une aiguille que Neptune fait tourner. |
| Clavier | `key_white`, `key_white_on`, `key_black`, `key_black_on` | La forme du dessin donne la forme de la touche. |
| Zone | `frame` | Cadre découpé en neuf zones (section 14). |
| Menu, Étiquette, Courbe | aucune | Ces widgets ne prennent pas de style. |

Le **texte** posé sur les widgets (nom d'un tirant, légende d'un potard) est dessiné par Neptune par-dessus l'image : sa couleur, sa taille et sa place se règlent dans le style (section 15).

Un même style peut contenir toutes les images : chaque widget utilise seulement celles qui le concernent.

## 4. Contenu d'un pack

Un pack est une archive zip renommée en `.neptunestyle`. Le **nom du fichier** (sans extension, en minuscules) est l'**identifiant** du pack : `organ-classic.neptunestyle` → `organ-classic`. Ne le renommez pas après usage : les Façades qui l'utilisent le retrouvent par ce nom.

### 4.1 Disposition

```
organ-classic.neptunestyle        (zip)
├── pack.json                     facultatif : description du pack
├── drawbar-tab/                  un dossier = un style
│   ├── style.json                facultatif : nom, etc.
│   ├── off.svg
│   └── on.svg
├── slider-horizontal/
│   ├── track.svg
│   └── handle.svg
└── knob/
    └── strip_32.svg
```

- **Un dossier = un style.** Tout dossier qui contient au moins une image reconnue est un style.
- Les images posées **à la racine** de l'archive forment un style unique, qui prend le nom du pack.
- Les sous-dossiers de niveau plus profond sont acceptés (ils comptent chacun comme un style). Les dossiers sans image sont ignorés.
- Maximum **64 styles** par pack.
- Les fichiers inconnus (`LISEZMOI.txt`, `.DS_Store`, `__MACOSX/`, etc.) sont ignorés.
- Les chemins ne doivent pas contenir `..`, de barre inverse `\` ni de lettre de lecteur.
- Un dossier ordinaire (non zippé) de même structure, posé dans la bibliothèque, est aussi accepté : pratique pour travailler.

### 4.2 Noms de fichiers reconnus

Casse ignorée. Extensions : `.svg`, `.png`, `.jpg`, `.jpeg`.

| Rôle | Noms acceptés | Utilisé par |
|---|---|---|
| éteint / fermé | `off`, `closed`, `released` | Bouton, Action |
| allumé / ouvert | `on`, `open`, `pressed` | Bouton, Action |
| rail | `track`, `rail`, `groove` | Curseur |
| poignée | `handle`, `thumb` | Curseur |
| éteint, souris dessus | `off_hover`, `hover` | Bouton, Action |
| allumé, souris dessus | `on_hover` | Bouton, Action |
| non lié (inactif) | `disabled`, `inactive` | Bouton, Action, Voyant |
| bande de potard | `strip_N`, `strip_N_h`, `filmstrip_N`, `filmstrip_N_h` | Potard |
| corps de potard (fixe) | `body`, `dial` | Potard |
| aiguille de potard (tournée) | `pointer`, `needle` | Potard |
| touche blanche, au repos / enfoncée | `key_white`, `key_white_on` | Clavier |
| touche noire, au repos / enfoncée | `key_black`, `key_black_on` | Clavier |
| cadre de zone | `frame`, `panel` | Zone |

)FSPEC"
        R"FSPEC(Pour la bande, **N est le nombre d'images** (de 2 à 512) : `strip_32.svg` = 32 images empilées **verticalement** ; `strip_32_h.svg` = 32 images **côte à côte**. Si N n'est pas dans le nom, il faut l'indiquer dans `style.json` (clé `frames`), sinon la bande est ignorée avec un avertissement.

Si un rôle existe en plusieurs formats dans le même dossier (`off.svg` et `off.png`), **le SVG est préféré**, puis le PNG, puis le JPG.

### 4.3 `pack.json` (facultatif)

```json
{
  "format": 1,
  "name": "Orgue classique",
  "author": "Votre nom",
  "licence": "CC BY 4.0",
  "version": "1.2",
  "description": "Tirants, curseurs et potards pour console d'orgue."
}
```

Tous les champs sont facultatifs. Sans `name`, le nom du pack est celui du fichier. `licence` peut aussi s'écrire `license`. Indiquez toujours l'auteur et la licence si vous partagez le pack.

### 4.4 `style.json` (facultatif, dans le dossier du style)

```json
{
  "name": "Plaquette de tirant",
  "frames": 32,
  "strip_direction": "vertical"
}
```

- `name` : nom affiché du style (sinon : le nom du dossier).
- `frames` : nombre d'images de la bande (prioritaire sur le nom du fichier).
- `strip_direction` : `"vertical"` (images empilées, défaut) ou `"horizontal"` (côte à côte). Prioritaire sur `_h`.
- `pointer_angles` : `[-135, 135]`, angles de l'aiguille au minimum et au maximum, en degrés (0 = l'aiguille dessinée vers le haut).
- `pivot` : `[50, 50]`, centre de rotation de l'aiguille, en % de l'image.
- `slice` : `[25, 25, 25, 25]`, marges gauche, haute, droite, basse de `frame`, en % de l'image (section 14). `slice_scale` : échelle d'affichage des coins, en % (100 par défaut).
- `text` : `{ "colour": "#ffffff", "size": 45, "position": "center", "bold": true }` (section 15).
- `key_lit_colour` : `"#f0b429"`, teinte d'une touche jouée quand le style n'a pas d'image `_on` pour elle.

## 5. Dimensions et proportions

Neptune **adapte** les images à la taille du widget ; les valeurs ci-dessous sont des *proportions de référence*, pas des tailles imposées. Dessinez en grand (le SVG s'adapte sans perte).

| Rôle | Comment Neptune place l'image | Conseil |
|---|---|---|
| `off`, `on` | Centrée dans le widget, **proportions conservées**. | Les deux images doivent avoir **exactement la même taille et les mêmes proportions**. Exemple : 120 × 40 (3:1). |
| `track` | **Étirée** sur tout le widget (largeur et hauteur). | Curseur horizontal : environ 300 × 24 (large et bas). Vertical : 24 × 300. Dessinez le rail pour qu'un étirement léger ne le déforme pas (extrémités arrondies simples). |
| `handle` | Curseur **horizontal** : hauteur = hauteur du curseur, largeur selon les proportions de l'image. Curseur **vertical** : largeur = largeur du curseur, hauteur selon les proportions. Centrée sur sa position. | Horizontal : poignée haute et étroite (ex. 24 × 48). Vertical : large et basse (ex. 48 × 24). La poignée ne dépasse jamais la moitié de la course. |
| `strip_N` | Une image de la bande est **centrée** dans le widget, proportions conservées. | Images **carrées** pour un potard rond (ex. 64 × 64). |
| `body`, `pointer` | **Centrées** dans le widget, proportions conservées, à la même place l'une sur l'autre. | Même `viewBox` pour les deux (ex. 64 × 64). |
| `off_hover`, `on_hover`, `disabled` | Comme `off` / `on`. | Même taille et mêmes proportions que `off` et `on`. |
| `key_white`, `key_white_on` | **Étirées** sur chaque touche blanche (largeur d'une touche × hauteur du clavier). | Rapport 1:5 environ (ex. 22 × 110). |
| `key_black`, `key_black_on` | **Étirées** sur chaque touche noire (60 % de la hauteur du clavier, 60 % de la largeur d'une blanche). | Rapport 1:5 environ (ex. 13 × 66). |
| `frame` | Découpée en neuf zones, voir la section 14. | Dessin moyen (ex. 120 × 80) avec des coins soignés. |
| `off`, `on` d'un voyant | Centrées, proportions conservées. | Carrées (ex. 40 × 40). |

Conseils généraux : fond **transparent** autour du dessin ; pas de grandes marges vides (elles réduisent la partie visible) ; une marge d'un ou deux pour cent suffit pour les contours épais.

## 6. La bande de potard (et le potard en deux couches)

- La bande contient **N images identiques en taille**, de la position **minimale** à la position **maximale** du potard.
- Vertical (`strip_N`) : l'image 1 est **en haut**, l'image N **en bas**. Hauteur totale = N × hauteur d'une image ; largeur = largeur d'une image.
- Horizontal (`strip_N_h`) : l'image 1 est **à gauche**, l'image N **à droite**.
- Neptune affiche l'image numéro `round(position × (N − 1))` (en comptant à partir de 0), où `position` va de 0 (minimum) à 1 (maximum). Avec des crans personnalisés, la position suit le cran.
- Pour un potard classique, faites tourner l'aiguille de **−135° à +135°** (course de 270°, 0° = vers le haut). Plus N est grand, plus le mouvement est doux ; 32 à 64 images suffisent.
- Les images doivent se *superposer exactement* (même centre de rotation) pour que le potard ne « tremble » pas.
- En SVG, la hauteur totale est N × hauteur d'une image : par exemple `viewBox="0 0 64 2048"` pour 32 images de 64 × 64. Placez chaque image en coordonnées **absolues** (évitez de dépendre de `translate` imbriqués).
- Il est **fortement conseillé de faire générer la bande par un petit script** (voir l'exemple de la section 11) plutôt que de la dessiner à la main.

)FSPEC"
        R"FSPEC(**Potard en deux couches (`body` + `pointer`).** Plus léger et plus net qu'une bande : `body.svg` est le corps fixe, `pointer.svg` l'aiguille. Les deux ont **la même taille** (même `viewBox`) ; l'aiguille est dessinée **vers le haut** (0°) et Neptune la fait tourner autour de son centre (ou de `pivot`) entre `pointer_angles`. Si un style a une aiguille, elle est utilisée à la place de la bande. La bande reste la solution la plus robuste (aucune rotation du côté de Neptune, rendu identique partout) : choisissez l'une ou l'autre.

## 7. SVG : ce qui fonctionne

Neptune affiche les SVG avec le moteur de **JUCE**, qui ne gère qu'un **sous-ensemble** de SVG. Restez dans cette liste ; testez toujours dans la fenêtre *Styles… → aperçu*.

**Sûr :**
- `<svg>` avec `width`, `height` et `viewBox` ; `<g>` ; `<defs>`.
- Formes : `<rect>` (avec `rx`, `ry`), `<circle>`, `<ellipse>`, `<line>`, `<polyline>`, `<polygon>`, `<path>` (toutes les commandes de chemin).
- Remplissage et trait : `fill`, `stroke`, `stroke-width`, `stroke-linecap`, `stroke-linejoin`, `opacity`, `fill-opacity`, `stroke-opacity`.
- Dégradés linéaires et radiaux simples (`<linearGradient>`, `<radialGradient>` avec des `<stop>`).
- Transformations : `translate`, `scale`, `rotate`, `matrix`.
- Couleurs en `#rrggbb`, `#rgb`, `rgb(...)` ou noms de couleurs.

**À éviter (peut s'afficher faux ou pas du tout) :**
- Filtres (`<filter>`, flou, ombres portées), masques (`<mask>`), motifs (`<pattern>`), modes de fusion (`mix-blend-mode`).
- Feuilles de style CSS (`<style>`) et variables CSS : mettez les couleurs directement en attributs.
- Texte (`<text>`) : la police dépend de la machine. **Convertissez le texte en tracés** avant d'enregistrer.
- Images embarquées (`<image>`), animations, scripts.
- Fichiers énormes : visez moins de 500 Ko par image.

Astuce : dans Inkscape, enregistrez en « SVG simple », après *Objet → Convertir en chemin* pour les textes et *Chemin → Objet en chemin* pour les formes complexes.

## 8. PNG et JPG

- **PNG** : garde la transparence (idéal pour un tirant posé sur un fond de console). Visez au moins 2× la taille d'affichage pour rester net.
- **JPG** : pas de transparence. Utile pour des photos (par exemple le fond de console, qui n'est pas dans un pack).
- Une bande de potard en PNG : mêmes règles que pour le SVG (images de même taille, empilées ou côte à côte).
- Poids raisonnable : moins de 2 Mo par image.

## 9. Installer un pack

Dans Neptune, onglet **Façade → Modifier la mise en page → Styles… → Bibliothèque… → Installer un pack…**, ou **glissez le fichier `.neptunestyle` sur la fenêtre de la Façade**. Le fichier est copié dans la bibliothèque :

- Linux : `~/.config/Neptune/styles/`
- Windows : `%APPDATA%\Neptune\styles\`
- macOS : `~/Library/Application Support/Neptune/styles/`

Le bouton *Ouvrir le dossier des packs* l'ouvre. Ensuite, **Bibliothèque →** *(nom du pack)* *→ (nom du style)* ajoute le style à la Façade courante ; on le donne aux widgets avec **Style…**.

Les packs sont **lus à l'intérieur de l'archive** : il n'y a rien à décompresser. Un style ajouté à une Façade garde une *référence* au pack (`pack:<identifiant>!<chemin>`) : si le pack disparaît ou est renommé, le widget retrouve son dessin d'origine.

Pour partager un style que vous avez construit à la main, utilisez **Bibliothèque… → Exporter le style sélectionné…**.

## 10. Vérifier son pack (liste de contrôle)

1. Le fichier se termine par `.neptunestyle` et s'ouvre comme un zip.
2. Chaque style est dans un dossier (ou à la racine), avec des noms de fichiers de la section 4.2.
3. `off` et `on` ont **la même taille** et les mêmes proportions.
4. Les SVG ne contiennent ni texte, ni filtre, ni CSS, ni image embarquée.
5. Une bande de potard indique son nombre d'images (`strip_32.svg`) et sa hauteur est un multiple exact de ce nombre (largeur pour `_h`).
6. Dans Neptune : *Styles…* → l'aperçu montre le bouton, le potard et les curseurs ; essayez-les à la souris.

**Avertissements possibles à l'installation :**

| Message | Cause |
|---|---|
| « No style found » | Aucun fichier ne porte un nom reconnu. Vérifiez les noms (section 4.2). |
| « a knob strip needs its number of frames » | `strip.svg` sans nombre : renommez en `strip_32.svg` ou ajoutez `frames` dans `style.json`. |
| « several files for the same image » | Deux formats du même rôle dans un dossier : le SVG est gardé. |
| « Entry ignored (unsafe path) » | Un chemin contient `..`, `\` ou une lettre de lecteur. |
| « newer format version » | Le pack annonce `"format"` supérieur à 1 : mettez Neptune à jour. |

## 11. Exemples complets

### 11.1 Plaquette de tirant, fermée et ouverte (`drawbar-tab/off.svg`, `drawbar-tab/on.svg`)

```xml
<svg xmlns="http://www.w3.org/2000/svg" width="120" height="40" viewBox="0 0 120 40">
  <rect x="2" y="2" width="116" height="36" rx="6" fill="#d9d2bd" stroke="#3a3a3a" stroke-width="2"/>
  <rect x="8" y="8" width="104" height="24" rx="3" fill="#b9b19a"/>
  <line x1="20" y1="20" x2="100" y2="20" stroke="#6a6450" stroke-width="3"/>
</svg>
```

```xml
<svg xmlns="http://www.w3.org/2000/svg" width="120" height="40" viewBox="0 0 120 40">
  <rect x="6" y="6" width="112" height="32" rx="6" fill="#5a5546"/>
  <rect x="2" y="2" width="112" height="32" rx="6" fill="#f1ead6" stroke="#3a3a3a" stroke-width="2"/>
  <rect x="8" y="8" width="100" height="20" rx="3" fill="#d9d2bd"/>
  <line x1="20" y1="18" x2="96" y2="18" stroke="#b3261e" stroke-width="4"/>
</svg>
```

### 11.2 Curseur horizontal (`slider-h/track.svg` 300 × 24, `slider-h/handle.svg` 24 × 48)

```xml
<svg xmlns="http://www.w3.org/2000/svg" width="300" height="24" viewBox="0 0 300 24">
  <rect x="0" y="0" width="300" height="24" rx="12" fill="#2a2a2a"/>
  <rect x="4" y="8" width="292" height="8" rx="4" fill="#0e0e0e"/>
</svg>
```

```xml
)FSPEC"
        R"FSPEC(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="48" viewBox="0 0 24 48">
  <rect x="1" y="1" width="22" height="46" rx="5" fill="#c8c0a8" stroke="#333333" stroke-width="2"/>
  <line x1="6" y1="24" x2="18" y2="24" stroke="#555555" stroke-width="2"/>
</svg>
```

### 11.3 Script Python qui génère une bande de potard (`knob/strip_32.svg`)

```python
import math
N, SIZE = 32, 64                      # 32 images de 64 x 64, empilées verticalement
out = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{SIZE}" height="{N*SIZE}" viewBox="0 0 {SIZE} {N*SIZE}">']
for i in range(N):
    a = math.radians(-135 + 270 * i / (N - 1))   # de -135 à +135 degres, 0 = vers le haut
    cy = i * SIZE + SIZE / 2
    x2, y2 = SIZE / 2 + 22 * math.sin(a), cy - 22 * math.cos(a)
    out.append(f'  <circle cx="{SIZE/2}" cy="{cy}" r="28" fill="#3a3a3a" stroke="#9a9a9a" stroke-width="2"/>')
    out.append(f'  <line x1="{SIZE/2}" y1="{cy}" x2="{x2:.2f}" y2="{y2:.2f}" stroke="#f0b429" stroke-width="4" stroke-linecap="round"/>')
out.append('</svg>')
open('strip_32.svg', 'w').write('\n'.join(out))
```

### 11.4 `pack.json`

```json
{ "format": 1, "name": "Mon pack", "author": "Moi", "licence": "CC BY 4.0", "version": "1.0" }
```

## 12. Message prêt à coller dans une IA

> Tu vas créer un **pack de styles d'images pour la Façade du plugin Neptune**. Le format est décrit dans la spécification ci-dessous : respecte-le strictement.
>
> **Ce que je veux :** *(décris ici le thème : par exemple « console d'orgue liturgique en bois sombre, tirants en ivoire avec bague dorée, potards en laiton »)*. Je veux ces styles : *(par exemple : un tirant fermé/ouvert ; un curseur horizontal avec rail et poignée ; un potard de 48 images)*.
>
> **Contraintes :** SVG uniquement, sans texte (convertis en tracés), sans filtre, sans CSS, sans image embarquée, couleurs en attributs `fill`/`stroke`. Fond transparent. `off` et `on` de même taille. Pour le potard, écris un **script Python** qui génère `strip_N.svg`. Réponds avec : (1) l'arborescence exacte du pack, (2) le contenu complet de chaque fichier, (3) le script éventuel, (4) la commande pour créer le zip (`zip -r mon-pack.neptunestyle .`, depuis le dossier du pack).
>
> *(Colle ici la spécification complète.)*

## 13. Limites connues du format 1

- Boutons : deux états, plus survol et inactif. Curseurs : la poignée est une seule image. Pas de police de caractères dans un pack : le texte utilise la police de Neptune (couleur, taille, place et gras réglables).
- Les rails de curseur sont étirés tels quels (pas de découpe en neuf zones, réservée aux cadres de zone).
- Les images des packs ne sont pas encore copiées dans les presets ni dans le ZIP portable d'une Façade ; le projet garde la référence au pack.
- Le nom du fichier du pack est son identifiant : le renommer casse les références.

## 14. Le cadre de zone découpé en neuf zones

Un cadre dessiné une fois doit pouvoir s'adapter à une zone petite ou grande **sans déformer ses coins arrondis**. On découpe donc l'image en **neuf rectangles** avec deux lignes verticales et deux horizontales :

```
+-----+-------------+-----+
|  1  |      2      |  3  |    1, 3, 7, 9 : les 4 coins   -> gardent leur taille
+-----+-------------+-----+    2, 8        : bords haut/bas -> s'étirent en largeur
|  4  |      5      |  6  |    4, 6        : bords gauche/droite -> s'étirent en hauteur
+-----+-------------+-----+    5           : le centre -> s'étire dans les deux sens
|  7  |      8      |  9  |
+-----+-------------+-----+
```

Dans `style.json`, `"slice": [gauche, haut, droite, bas]` donne la taille des coins **en % de l'image** : `[20, 20, 20, 20]` = 20 % de la largeur à gauche et à droite, 20 % de la hauteur en haut et en bas. Les coins sont affichés à leur taille naturelle (la taille déclarée du SVG, en unités du canevas) multipliée par `slice_scale` (100 % par défaut) ; si la zone est trop petite, ils rétrécissent ensemble. Choisissez une découpe qui **enferme tout ce qui est décoratif** (coins arrondis, bordures épaisses, ombres dessinées) dans les coins et les bords, et un centre **uni** : seul ce qui est uni peut s'étirer sans se voir.

## 15. Le texte posé sur l'image

Les noms (d'un tirant, d'un bouton, la légende d'un potard ou d'une zone) sont dessinés par Neptune **par-dessus** l'image, avec les réglages du style. Une même plaquette de tirant sert ainsi pour tous les jeux d'un orgue, chacun avec son nom. Dans `style.json` :

```json
{ "text": { "colour": "#2a2418", "size": 42, "position": "center", "bold": true } }
```

- `colour` : `#rrggbb` (ou `#aarrggbb`). Absent : la couleur du texte du thème.
- `size` : taille en **% de la hauteur du widget** (0 ou absent : automatique).
- `position` : `center` (défaut), `top`, `bottom`, `left` ou `right`.
- `bold` : `true` (défaut) ou `false`.

Le texte lui-même se saisit sur le widget (bouton **Texte…**). Les polices ne sont pas embarquées : si le nom doit avoir une forme particulière, dessinez-le dans l'image.

## 16. États supplémentaires et voyants

- `off_hover` / `on_hover` : l'image affichée quand la souris est sur un bouton ou une action (en mode Jeu). Absentes : l'image normale.
- `disabled` : l'image d'un bouton, d'une action ou d'un voyant **non lié** à quoi que ce soit, en mode Jeu. Absente : l'image normale.
- **Voyant** : un widget qui n'agit pas, il s'allume. Source au choix : un paramètre (allumé à 50 % ou plus), les notes jouées sur un canal MIDI (par exemple le manuel 2), ou n'importe quelle note de la piste. Il utilise `off` et `on` ; sans image, Neptune dessine une lampe ronde.

## 17. Les touches du clavier

)FSPEC"
        R"FSPEC(Un style de clavier remplace les touches par vos images : `key_white` et `key_black` au repos, `key_white_on` et `key_black_on` pour une touche enfoncée **ou jouée de l'extérieur** (clavier MIDI, séquenceur). Chaque image est **étirée** sur sa touche : la forme du dessin donne la forme de la touche (coins arrondis, biseau, ivoire, ébène...). Sans les images `_on`, Neptune pose une teinte translucide sur la touche au repos ; la couleur se règle par `key_lit_colour`.

## 18. Les couleurs du thème dans les SVG

Dans un SVG, écrivez un repère à la place d'une couleur : Neptune le remplace par la couleur du thème **avant** de lire le fichier. Un seul jeu de fichiers suit alors le thème clair, sombre ou à fort contraste.

| Repère | Couleur du thème |
|---|---|
| `{{accent}}`, `{{accent2}}` | couleurs d'accent |
| `{{text}}`, `{{text_dim}}` | texte, texte atténué |
| `{{bg}}`, `{{card}}`, `{{panel}}` | fonds de l'application, des cartes, des panneaux |
| `{{border}}` | bordures |
| `{{hot}}`, `{{face}}` | vif des potards, face des potards |

Exemple : `<circle cx="20" cy="20" r="14" fill="{{accent}}"/>`. Cela ne marche que dans les SVG (pas dans les PNG et JPG). Un SVG qui contient un repère n'est plus un SVG valide pour un éditeur de dessin : si vous voulez le garder éditable, mettez d'abord une vraie couleur, puis remplacez-la par le repère à la fin.
)FSPEC");
}

inline juce::String english()
{
    return juce::String::fromUTF8 (
        R"FSPEC(# Facade image style specification (`.neptunestyle` format)

Format version: **2** (format 1 packs remain valid; format 2 adds knob layers, keyboard keys, zone frames, the lamp, hover/disabled states, text styling and theme colours in SVG). This document is written to be read by a human **or handed as-is to an AI** (ChatGPT, Claude, etc.) so that it can create styles. Section 12 contains a ready-to-paste message.

## 1. In ten lines

- Neptune's **Facade** is a custom interface page: you place widgets (knobs, buttons, sliders...) bound to instrument parameters.
- A **style** is a set of images that changes a widget's *appearance*. It never changes its *function* (values, notches, direction: those are set on the widget).
- A **pack** (a `.neptunestyle` file) is a **zip** archive holding one or several styles.
- Image formats: **SVG** (recommended), **PNG**, **JPG**.
- Each image has a **role**, given by its **file name**: `off`, `on`, `track`, `handle`, `strip_N`, `body`, `pointer`, `key_white`, `frame`...
- Buttons, actions and lamps: `off` + `on` (+ `off_hover`, `on_hover`, `disabled`). Sliders: `track` + `handle`. Knobs: `strip_N` (a strip of N pictures) **or** `body` + `pointer` (two layers). Keyboards: `key_white`, `key_white_on`, `key_black`, `key_black_on`. Zones: `frame`.
- A missing image = Neptune keeps its default drawing for that part.
- No code is executed: a pack only holds images and JSON text.

## 2. Vocabulary

| Term | Meaning |
|---|---|
| Facade | The custom interface page of a Neptune track. |
| Widget | An element placed on the Facade: knob, button, action, slider, menu, zone, keyboard... |
| Style | A set of images (one per role) dressing a widget. |
| Pack | A `.neptunestyle` file (or a folder with the same structure) holding styles. |
| Library | The folder where Neptune keeps installed packs. |

## 3. Which widgets use which images

| Widget | Images used | Notes |
|---|---|---|
| Button, Action | `off` and `on`; optional `off_hover`, `on_hover`, `disabled` | Off/closed and on/open; mouse over; widget not linked (in Play mode). |
| Set, MIDI message | `off` and `on` | Like a button (Set: lit while armed; MIDI message: lit while pressed). |
| Lamp | `off` and `on`; optional `disabled` | Lit by a parameter, by the notes of a channel or by the notes of the track. Not clickable. |
| Slider (drawbar) | `track` (rail) and `handle` | Horizontal or vertical, chosen by the user. |
| Knob | `strip_N` **or** `body` + `pointer` | A strip of N pictures, one per position; or a fixed body and a pointer that Neptune turns. |
| Keyboard | `key_white`, `key_white_on`, `key_black`, `key_black_on` | The shape of the drawing gives the shape of the key. |
| Zone | `frame` | Frame cut in nine parts (section 14). |
| Menu, Label, Curve | none | These widgets take no style. |

The **text** laid on widgets (a drawbar's name, a knob's caption) is drawn by Neptune over the image: its colour, size and position are set in the style (section 15).

One style may hold every image: each widget only uses the ones that concern it.

## 4. Pack contents

A pack is a zip archive renamed to `.neptunestyle`. The **file name** (without extension, lower case) is the pack's **identifier**: `organ-classic.neptunestyle` → `organ-classic`. Do not rename it once in use: Facades that use it find it by this name.

### 4.1 Layout

```
organ-classic.neptunestyle        (zip)
├── pack.json                     optional: description of the pack
├── drawbar-tab/                  one folder = one style
│   ├── style.json                optional: name, etc.
│   ├── off.svg
│   └── on.svg
├── slider-horizontal/
│   ├── track.svg
│   └── handle.svg
└── knob/
    └── strip_32.svg
```

- **One folder = one style.** Any folder holding at least one recognised image is a style.
- Images placed at the **root** of the archive form a single style named after the pack.
- Deeper sub-folders are accepted (each counts as a style). Folders without images are ignored.
- At most **64 styles** per pack.
- Unknown files (`README.txt`, `.DS_Store`, `__MACOSX/`, etc.) are ignored.
- Paths must not contain `..`, a backslash `\` or a drive letter.
- A plain (unzipped) folder with the same structure placed in the library is also accepted: handy while working.

### 4.2 Recognised file names

Case is ignored. Extensions: `.svg`, `.png`, `.jpg`, `.jpeg`.

| Role | Accepted names | Used by |
|---|---|---|
| off / closed | `off`, `closed`, `released` | Button, Action |
| on / open | `on`, `open`, `pressed` | Button, Action |
| rail | `track`, `rail`, `groove` | Slider |
| handle | `handle`, `thumb` | Slider |
| off, mouse over | `off_hover`, `hover` | Button, Action |
| on, mouse over | `on_hover` | Button, Action |
| not linked (disabled) | `disabled`, `inactive` | Button, Action, Lamp |
| knob strip | `strip_N`, `strip_N_h`, `filmstrip_N`, `filmstrip_N_h` | Knob |
| knob body (fixed) | `body`, `dial` | Knob |
| knob pointer (turned) | `pointer`, `needle` | Knob |
| white key, rest / pressed | `key_white`, `key_white_on` | Keyboard |
| black key, rest / pressed | `key_black`, `key_black_on` | Keyboard |
| zone frame | `frame`, `panel` | Zone |

For the strip, **N is the number of pictures** (2 to 512): `strip_32.svg` = 32 pictures stacked **vertically**; `strip_32_h.svg` = 32 pictures **side by side**. If N is not in the name, give it in `style.json` (key `frames`), otherwise the strip is ignored with a warning.

If a role exists in several formats in the same folder (`off.svg` and `off.png`), **SVG is preferred**, then PNG, then JPG.

### 4.3 `pack.json` (optional)

```json
{
  "format": 1,
  "name": "Classic organ",
  "author": "Your name",
  "licence": "CC BY 4.0",
  "version": "1.2",
  "description": "Drawbars, sliders and knobs for an organ console."
}
```

)FSPEC"
        R"FSPEC(All fields are optional. Without `name`, the pack is named after its file. `licence` may also be written `license`. Always give the author and the licence if you share the pack.

### 4.4 `style.json` (optional, in the style's folder)

```json
{
  "name": "Drawbar tab",
  "frames": 32,
  "strip_direction": "vertical"
}
```

- `name`: displayed name of the style (otherwise the folder name).
- `frames`: number of pictures of the strip (takes priority over the file name).
- `strip_direction`: `"vertical"` (stacked, default) or `"horizontal"` (side by side). Takes priority over `_h`.
- `pointer_angles`: `[-135, 135]`, the pointer's angles at minimum and maximum, in degrees (0 = the pointer drawn pointing up).
- `pivot`: `[50, 50]`, the pointer's centre of rotation, in % of the image.
- `slice`: `[25, 25, 25, 25]`, left, top, right, bottom margins of `frame`, in % of the image (section 14). `slice_scale`: display scale of the corners, in % (100 by default).
- `text`: `{ "colour": "#ffffff", "size": 45, "position": "center", "bold": true }` (section 15).
- `key_lit_colour`: `"#f0b429"`, tint of a played key when the style has no `_on` image for it.

## 5. Sizes and proportions

Neptune **fits** images to the widget's size; the values below are *reference proportions*, not imposed sizes. Draw large (SVG scales without loss).

| Role | How Neptune places the image | Advice |
|---|---|---|
| `off`, `on` | Centred in the widget, **proportions kept**. | Both images must have **exactly the same size and proportions**. Example: 120 × 40 (3:1). |
| `track` | **Stretched** over the whole widget (width and height). | Horizontal slider: about 300 × 24 (wide and low). Vertical: 24 × 300. Draw the rail so a slight stretch does not distort it (simple rounded ends). |
| `handle` | **Horizontal** slider: height = the slider's height, width from the image's proportions. **Vertical** slider: width = the slider's width, height from the proportions. Centred on its position. | Horizontal: tall narrow handle (e.g. 24 × 48). Vertical: wide low handle (e.g. 48 × 24). The handle never exceeds half the travel. |
| `strip_N` | One picture of the strip is **centred** in the widget, proportions kept. | **Square** pictures for a round knob (e.g. 64 × 64). |
| `body`, `pointer` | **Centred** in the widget, proportions kept, exactly on top of each other. | Same `viewBox` for both (e.g. 64 × 64). |
| `off_hover`, `on_hover`, `disabled` | Like `off` / `on`. | Same size and proportions as `off` and `on`. |
| `key_white`, `key_white_on` | **Stretched** over each white key (one key's width × the keyboard's height). | About 1:5 (e.g. 22 × 110). |
| `key_black`, `key_black_on` | **Stretched** over each black key (60 % of the keyboard's height, 60 % of a white key's width). | About 1:5 (e.g. 13 × 66). |
| `frame` | Cut in nine parts, see section 14. | A medium drawing (e.g. 120 × 80) with careful corners. |
| `off`, `on` of a lamp | Centred, proportions kept. | Square (e.g. 40 × 40). |

General advice: **transparent** background around the drawing; no large empty margins (they reduce the visible part); one or two percent of margin is enough for thick outlines.

## 6. The knob strip (and the two-layer knob)

- The strip holds **N pictures of identical size**, from the knob's **minimum** to its **maximum** position.
- Vertical (`strip_N`): picture 1 is **at the top**, picture N **at the bottom**. Total height = N × one picture's height; width = one picture's width.
- Horizontal (`strip_N_h`): picture 1 is **on the left**, picture N **on the right**.
- Neptune shows picture number `round(position × (N − 1))` (counting from 0), where `position` runs from 0 (minimum) to 1 (maximum). With custom notches the position follows the notch.
- For a classic knob, turn the pointer from **−135° to +135°** (270° of travel, 0° = pointing up). The larger N, the smoother the motion; 32 to 64 pictures are enough.
- Pictures must *overlap exactly* (same centre of rotation) so the knob does not "wobble".
- In SVG the total height is N × one picture's height: for example `viewBox="0 0 64 2048"` for 32 pictures of 64 × 64. Place each picture in **absolute** coordinates (avoid relying on nested `translate`).
- It is **strongly advised to have a small script generate the strip** (see the example in section 11) rather than draw it by hand.

**Two-layer knob (`body` + `pointer`).** Lighter and sharper than a strip: `body.svg` is the fixed body, `pointer.svg` the pointer. Both have **the same size** (same `viewBox`); the pointer is drawn **pointing up** (0°) and Neptune turns it around its centre (or `pivot`) between `pointer_angles`. If a style has a pointer, it is used instead of the strip. The strip remains the most robust solution (no rotation on Neptune's side, identical rendering everywhere): choose one or the other.

## 7. SVG: what works

Neptune displays SVG with the **JUCE** engine, which only implements a **subset** of SVG. Stay within this list; always test in the *Styles... → preview* window.

**Safe:**
- `<svg>` with `width`, `height` and `viewBox`; `<g>`; `<defs>`.
- Shapes: `<rect>` (with `rx`, `ry`), `<circle>`, `<ellipse>`, `<line>`, `<polyline>`, `<polygon>`, `<path>` (all path commands).
- Fill and stroke: `fill`, `stroke`, `stroke-width`, `stroke-linecap`, `stroke-linejoin`, `opacity`, `fill-opacity`, `stroke-opacity`.
- Simple linear and radial gradients (`<linearGradient>`, `<radialGradient>` with `<stop>`).
- Transforms: `translate`, `scale`, `rotate`, `matrix`.
- Colours as `#rrggbb`, `#rgb`, `rgb(...)` or colour names.

**Avoid (may render wrongly or not at all):**
- Filters (`<filter>`, blur, drop shadows), masks (`<mask>`), patterns (`<pattern>`), blend modes (`mix-blend-mode`).
- CSS style sheets (`<style>`) and CSS variables: put colours directly in attributes.
- Text (`<text>`): the font depends on the machine. **Convert text to outlines** before saving.
)FSPEC"
        R"FSPEC(- Embedded images (`<image>`), animations, scripts.
- Huge files: aim for less than 500 KB per image.

Tip: in Inkscape, save as "Plain SVG", after *Object → Object to Path* for texts and *Path → Stroke to Path* for complex shapes.

## 8. PNG and JPG

- **PNG**: keeps transparency (ideal for a drawbar laid over a console background). Aim for at least 2× the display size to stay sharp.
- **JPG**: no transparency. Useful for photos.
- A PNG knob strip: same rules as SVG (same-size pictures, stacked or side by side).
- Reasonable weight: less than 2 MB per image.

## 9. Installing a pack

In Neptune, **Facade tab → Edit layout → Styles... → Library... → Install a pack...**, or **drop the `.neptunestyle` file onto the Facade window**. The file is copied into the library:

- Linux: `~/.config/Neptune/styles/`
- Windows: `%APPDATA%\Neptune\styles\`
- macOS: `~/Library/Application Support/Neptune/styles/`

*Open the packs folder* opens it. Then **Library → *(pack name)* → *(style name)*** adds the style to the current Facade; give it to widgets with **Style...**.

Packs are **read inside the archive**: nothing to unzip. A style added to a Facade keeps a *reference* to the pack (`pack:<identifier>!<path>`): if the pack disappears or is renamed, the widget falls back to its default drawing.

To share a style you built by hand, use **Library... → Export the selected style...**.

## 10. Checking a pack (checklist)

1. The file ends in `.neptunestyle` and opens as a zip.
2. Each style is in a folder (or at the root), with file names from section 4.2.
3. `off` and `on` have **the same size** and proportions.
4. SVGs contain no text, filter, CSS or embedded image.
5. A knob strip states its number of pictures (`strip_32.svg`) and its height is an exact multiple of that number (width for `_h`).
6. In Neptune: *Styles...* → the preview shows the button, the knob and the sliders; try them with the mouse.

**Possible warnings when installing:**

| Message | Cause |
|---|---|
| "No style found" | No file has a recognised name. Check the names (section 4.2). |
| "a knob strip needs its number of frames" | `strip.svg` without a number: rename to `strip_32.svg` or add `frames` to `style.json`. |
| "several files for the same image" | Two formats of the same role in a folder: SVG is kept. |
| "Entry ignored (unsafe path)" | A path contains `..`, `\` or a drive letter. |
| "newer format version" | The pack announces `"format"` above 1: update Neptune. |

## 11. Complete examples

### 11.1 Drawbar tab, closed and open (`drawbar-tab/off.svg`, `drawbar-tab/on.svg`)

```xml
<svg xmlns="http://www.w3.org/2000/svg" width="120" height="40" viewBox="0 0 120 40">
  <rect x="2" y="2" width="116" height="36" rx="6" fill="#d9d2bd" stroke="#3a3a3a" stroke-width="2"/>
  <rect x="8" y="8" width="104" height="24" rx="3" fill="#b9b19a"/>
  <line x1="20" y1="20" x2="100" y2="20" stroke="#6a6450" stroke-width="3"/>
</svg>
```

```xml
<svg xmlns="http://www.w3.org/2000/svg" width="120" height="40" viewBox="0 0 120 40">
  <rect x="6" y="6" width="112" height="32" rx="6" fill="#5a5546"/>
  <rect x="2" y="2" width="112" height="32" rx="6" fill="#f1ead6" stroke="#3a3a3a" stroke-width="2"/>
  <rect x="8" y="8" width="100" height="20" rx="3" fill="#d9d2bd"/>
  <line x1="20" y1="18" x2="96" y2="18" stroke="#b3261e" stroke-width="4"/>
</svg>
```

### 11.2 Horizontal slider (`slider-h/track.svg` 300 × 24, `slider-h/handle.svg` 24 × 48)

```xml
<svg xmlns="http://www.w3.org/2000/svg" width="300" height="24" viewBox="0 0 300 24">
  <rect x="0" y="0" width="300" height="24" rx="12" fill="#2a2a2a"/>
  <rect x="4" y="8" width="292" height="8" rx="4" fill="#0e0e0e"/>
</svg>
```

```xml
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="48" viewBox="0 0 24 48">
  <rect x="1" y="1" width="22" height="46" rx="5" fill="#c8c0a8" stroke="#333333" stroke-width="2"/>
  <line x1="6" y1="24" x2="18" y2="24" stroke="#555555" stroke-width="2"/>
</svg>
```

### 11.3 Python script generating a knob strip (`knob/strip_32.svg`)

```python
import math
N, SIZE = 32, 64                      # 32 pictures of 64 x 64, stacked vertically
out = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{SIZE}" height="{N*SIZE}" viewBox="0 0 {SIZE} {N*SIZE}">']
for i in range(N):
    a = math.radians(-135 + 270 * i / (N - 1))   # from -135 to +135 degrees, 0 = up
    cy = i * SIZE + SIZE / 2
    x2, y2 = SIZE / 2 + 22 * math.sin(a), cy - 22 * math.cos(a)
    out.append(f'  <circle cx="{SIZE/2}" cy="{cy}" r="28" fill="#3a3a3a" stroke="#9a9a9a" stroke-width="2"/>')
    out.append(f'  <line x1="{SIZE/2}" y1="{cy}" x2="{x2:.2f}" y2="{y2:.2f}" stroke="#f0b429" stroke-width="4" stroke-linecap="round"/>')
out.append('</svg>')
open('strip_32.svg', 'w').write('\n'.join(out))
```

### 11.4 `pack.json`

```json
{ "format": 1, "name": "My pack", "author": "Me", "licence": "CC BY 4.0", "version": "1.0" }
```

## 12. Message ready to paste into an AI

> You will create an **image style pack for the Facade of the Neptune plugin**. The format is described in the specification below: follow it strictly.
>
> **What I want:** *(describe the theme here: for example "a liturgical organ console in dark wood, ivory drawbars with a gold ring, brass knobs")*. I want these styles: *(for example: a closed/open drawbar; a horizontal slider with rail and handle; a 48-frame knob)*.
>
> **Constraints:** SVG only, no text (convert to outlines), no filters, no CSS, no embedded images, colours in `fill`/`stroke` attributes. Transparent background. `off` and `on` the same size. For the knob, write a **Python script** that generates `strip_N.svg`. Answer with: (1) the exact tree of the pack, (2) the full content of each file, (3) the script if any, (4) the command to create the zip (`zip -r my-pack.neptunestyle .`, from inside the pack folder).
>
> *(Paste the complete specification here.)*

## 13. Known limits of format 1

)FSPEC"
        R"FSPEC(- Buttons: two states, plus hover and disabled. Sliders: the handle is a single image. No fonts in a pack: text uses Neptune's font (colour, size, position and bold are adjustable).
- Slider rails are stretched as they are (nine-slice is for zone frames only).
- Pack images are not yet copied into presets or into a Facade's portable ZIP; the project keeps the reference to the pack.
- The pack's file name is its identifier: renaming it breaks references.

## 14. The zone frame cut in nine parts

A frame drawn once must fit a small or a large zone **without distorting its rounded corners**. So the image is cut into **nine rectangles** with two vertical and two horizontal lines:

```
+-----+-------------+-----+
|  1  |      2      |  3  |    1, 3, 7, 9 : the 4 corners -> keep their size
+-----+-------------+-----+    2, 8        : top/bottom edges -> stretch in width
|  4  |      5      |  6  |    4, 6        : left/right edges -> stretch in height
+-----+-------------+-----+    5           : the centre -> stretches both ways
|  7  |      8      |  9  |
+-----+-------------+-----+
```

In `style.json`, `"slice": [left, top, right, bottom]` gives the size of the corners **in % of the image**: `[20, 20, 20, 20]` = 20 % of the width on the left and right, 20 % of the height at the top and bottom. Corners are displayed at their natural size (the SVG's declared size, in canvas units) times `slice_scale` (100 % by default); if the zone is too small, they shrink together. Choose a cut that **encloses everything decorative** (rounded corners, thick borders, drawn shadows) in the corners and edges, and a **plain** centre: only plain areas can stretch without showing.

## 15. Text laid on the image

Names (of a drawbar, a button, the caption of a knob or a zone) are drawn by Neptune **over** the image, with the style's settings. One drawbar tab thus serves every stop of an organ, each with its own name. In `style.json`:

```json
{ "text": { "colour": "#2a2418", "size": 42, "position": "center", "bold": true } }
```

- `colour`: `#rrggbb` (or `#aarrggbb`). Absent: the theme's text colour.
- `size`: size in **% of the widget's height** (0 or absent: automatic).
- `position`: `center` (default), `top`, `bottom`, `left` or `right`.
- `bold`: `true` (default) or `false`.

The text itself is typed on the widget (**Text...** button). Fonts are not embedded: if the name needs a particular shape, draw it in the image.

## 16. Extra states and lamps

- `off_hover` / `on_hover`: the picture shown when the mouse is over a button or an action (in Play mode). Absent: the normal picture.
- `disabled`: the picture of a button, action or lamp **not linked** to anything, in Play mode. Absent: the normal picture.
- **Lamp**: a widget that does nothing but light up. Source of your choice: a parameter (lit at 50 % or more), the notes played on a MIDI channel (for example manual 2), or any note of the track. It uses `off` and `on`; without images, Neptune draws a round lamp.

## 17. Keyboard keys

A keyboard style replaces the keys with your images: `key_white` and `key_black` at rest, `key_white_on` and `key_black_on` for a key pressed **or played from outside** (MIDI keyboard, sequencer). Each image is **stretched** over its key: the shape of the drawing gives the shape of the key (rounded corners, bevel, ivory, ebony...). Without the `_on` images, Neptune lays a translucent tint on the resting key; the colour is set by `key_lit_colour`.

## 18. Theme colours in SVG

In an SVG, write a marker instead of a colour: Neptune replaces it with the theme's colour **before** reading the file. A single set of files then follows the light, dark or high-contrast theme.

| Marker | Theme colour |
|---|---|
| `{{accent}}`, `{{accent2}}` | accent colours |
| `{{text}}`, `{{text_dim}}` | text, dimmed text |
| `{{bg}}`, `{{card}}`, `{{panel}}` | backgrounds of the application, cards, panels |
| `{{border}}` | borders |
| `{{hot}}`, `{{face}}` | knob highlight, knob face |

Example: `<circle cx="20" cy="20" r="14" fill="{{accent}}"/>`. This only works in SVG (not PNG or JPG). An SVG containing a marker is no longer a valid SVG for a drawing editor: to keep it editable, first use a real colour, then swap in the marker at the end.
)FSPEC");
}

} // namespace facade_spec
