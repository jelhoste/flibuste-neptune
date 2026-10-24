#pragma once
// GENERE par scripts/gen_facade_spec_header.py : ne pas modifier a la main (modifier les documents de docs/ puis relancer le script).
#include <juce_core/juce_core.h>

namespace facade_spec
{
inline juce::String french()
{
    return juce::String::fromUTF8 (
        R"FSPEC(# Spécification des styles d'images de la Façade (format `.neptunestyle`)

Version du format : **1**. Ce document est écrit pour être lu par un humain **ou donné tel quel à une IA** (ChatGPT, Claude, etc.) pour qu'elle crée des styles. La section 12 contient un message prêt à coller.

## 1. En dix lignes

- La **Façade** de Neptune est une page d'interface personnalisée : on y pose des widgets (potards, boutons, curseurs…) liés aux paramètres des instruments.
- Un **style** est un jeu d'images qui change l'*apparence* d'un widget. Il ne change **jamais** sa *fonction* (valeurs, crans, sens : ça se règle sur le widget).
- Un **pack** (fichier `.neptunestyle`) est une archive **zip** qui contient un ou plusieurs styles.
- Formats d'image : **SVG** (recommandé), **PNG**, **JPG**.
- Chaque image a un **rôle**, donné par son **nom de fichier** : `off`, `on`, `track`, `handle`, `strip_N`.
- Boutons et actions : `off` + `on`. Curseurs : `track` + `handle`. Potards : `strip_N` (une bande de N images).
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
| Bouton, Action | `off` et `on` | Deux états seulement (éteint/fermé, allumé/ouvert). Pas d'image « survol » ni « appuyé ». |
| Curseur (tirant) | `track` (rail) et `handle` (poignée) | Horizontal ou vertical, au choix de l'utilisateur. |
| Potard | `strip_N` | Une bande de N images, une par position. |
| Menu, Zone, Étiquette, Clavier, Courbe | aucune | Ces widgets ne prennent pas de style. |

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
| bande de potard | `strip_N`, `strip_N_h`, `filmstrip_N`, `filmstrip_N_h` | Potard |

Pour la bande, **N est le nombre d'images** (de 2 à 512) : `strip_32.svg` = 32 images empilées **verticalement** ; `strip_32_h.svg` = 32 images **côte à côte**. Si N n'est pas dans le nom, il faut l'indiquer dans `style.json` (clé `frames`), sinon la bande est ignorée avec un avertissement.

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

## 5. Dimensions et proportions

Neptune **adapte** les images à la taille du widget ; les valeurs ci-dessous sont des *proportions de référence*, pas des tailles imposées. Dessinez en grand (le SVG s'adapte sans perte).

| Rôle | Comment Neptune place l'image | Conseil |
|---|---|---|
| `off`, `on` | Centrée dans le widget, **proportions conservées**. | Les deux images doivent avoir **exactement la même taille et les mêmes proportions**. Exemple : 120 × 40 (3:1). |
)FSPEC"
        R"FSPEC(| `track` | **Étirée** sur tout le widget (largeur et hauteur). | Curseur horizontal : environ 300 × 24 (large et bas). Vertical : 24 × 300. Dessinez le rail pour qu'un étirement léger ne le déforme pas (extrémités arrondies simples). |
| `handle` | Curseur **horizontal** : hauteur = hauteur du curseur, largeur selon les proportions de l'image. Curseur **vertical** : largeur = largeur du curseur, hauteur selon les proportions. Centrée sur sa position. | Horizontal : poignée haute et étroite (ex. 24 × 48). Vertical : large et basse (ex. 48 × 24). La poignée ne dépasse jamais la moitié de la course. |
| `strip_N` | Une image de la bande est **centrée** dans le widget, proportions conservées. | Images **carrées** pour un potard rond (ex. 64 × 64). |

Conseils généraux : fond **transparent** autour du dessin ; pas de grandes marges vides (elles réduisent la partie visible) ; une marge d'un ou deux pour cent suffit pour les contours épais.

## 6. La bande de potard

- La bande contient **N images identiques en taille**, de la position **minimale** à la position **maximale** du potard.
- Vertical (`strip_N`) : l'image 1 est **en haut**, l'image N **en bas**. Hauteur totale = N × hauteur d'une image ; largeur = largeur d'une image.
- Horizontal (`strip_N_h`) : l'image 1 est **à gauche**, l'image N **à droite**.
- Neptune affiche l'image numéro `round(position × (N − 1))` (en comptant à partir de 0), où `position` va de 0 (minimum) à 1 (maximum). Avec des crans personnalisés, la position suit le cran.
- Pour un potard classique, faites tourner l'aiguille de **−135° à +135°** (course de 270°, 0° = vers le haut). Plus N est grand, plus le mouvement est doux ; 32 à 64 images suffisent.
- Les images doivent se *superposer exactement* (même centre de rotation) pour que le potard ne « tremble » pas.
- En SVG, la hauteur totale est N × hauteur d'une image : par exemple `viewBox="0 0 64 2048"` pour 32 images de 64 × 64. Placez chaque image en coordonnées **absolues** (évitez de dépendre de `translate` imbriqués).
- Il est **fortement conseillé de faire générer la bande par un petit script** (voir l'exemple de la section 11) plutôt que de la dessiner à la main.

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
)FSPEC"
        R"FSPEC(| « a knob strip needs its number of frames » | `strip.svg` sans nombre : renommez en `strip_32.svg` ou ajoutez `frames` dans `style.json`. |
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
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="48" viewBox="0 0 24 48">
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

- Boutons : deux états seulement. Curseurs : la poignée est une seule image. Potards : bande d'images (pas de potard en deux couches pour l'instant).
- Pas de découpe en neuf zones : les rails sont étirés tels quels.
- Les images des packs ne sont pas encore copiées dans les presets ni dans le ZIP portable d'une Façade ; le projet garde la référence au pack.
- Le nom du fichier du pack est son identifiant : le renommer casse les références.
)FSPEC");
}

inline juce::String english()
{
    return juce::String::fromUTF8 (
        R"FSPEC(# Facade image style specification (`.neptunestyle` format)

Format version: **1**. This document is written to be read by a human **or handed as-is to an AI** (ChatGPT, Claude, etc.) so that it can create styles. Section 12 contains a ready-to-paste message.

## 1. In ten lines

- Neptune's **Facade** is a custom interface page: you place widgets (knobs, buttons, sliders...) bound to instrument parameters.
- A **style** is a set of images that changes a widget's *appearance*. It never changes its *function* (values, notches, direction: those are set on the widget).
- A **pack** (a `.neptunestyle` file) is a **zip** archive holding one or several styles.
- Image formats: **SVG** (recommended), **PNG**, **JPG**.
- Each image has a **role**, given by its **file name**: `off`, `on`, `track`, `handle`, `strip_N`.
- Buttons and actions: `off` + `on`. Sliders: `track` + `handle`. Knobs: `strip_N` (a strip of N pictures).
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
| Button, Action | `off` and `on` | Two states only (off/closed, on/open). No hover or pressed image. |
| Slider (drawbar) | `track` (rail) and `handle` | Horizontal or vertical, chosen by the user. |
| Knob | `strip_N` | A strip of N pictures, one per position. |
| Menu, Zone, Label, Keyboard, Curve | none | These widgets take no style. |

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
| knob strip | `strip_N`, `strip_N_h`, `filmstrip_N`, `filmstrip_N_h` | Knob |

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

All fields are optional. Without `name`, the pack is named after its file. `licence` may also be written `license`. Always give the author and the licence if you share the pack.

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

## 5. Sizes and proportions

Neptune **fits** images to the widget's size; the values below are *reference proportions*, not imposed sizes. Draw large (SVG scales without loss).

| Role | How Neptune places the image | Advice |
|---|---|---|
| `off`, `on` | Centred in the widget, **proportions kept**. | Both images must have **exactly the same size and proportions**. Example: 120 × 40 (3:1). |
| `track` | **Stretched** over the whole widget (width and height). | Horizontal slider: about 300 × 24 (wide and low). Vertical: 24 × 300. Draw the rail so a slight stretch does not distort it (simple rounded ends). |
| `handle` | **Horizontal** slider: height = the slider's height, width from the image's proportions. **Vertical** slider: width = the slider's width, height from the proportions. Centred on its position. | Horizontal: tall narrow handle (e.g. 24 × 48). Vertical: wide low handle (e.g. 48 × 24). The handle never exceeds half the travel. |
)FSPEC"
        R"FSPEC(| `strip_N` | One picture of the strip is **centred** in the widget, proportions kept. | **Square** pictures for a round knob (e.g. 64 × 64). |

General advice: **transparent** background around the drawing; no large empty margins (they reduce the visible part); one or two percent of margin is enough for thick outlines.

## 6. The knob strip

- The strip holds **N pictures of identical size**, from the knob's **minimum** to its **maximum** position.
- Vertical (`strip_N`): picture 1 is **at the top**, picture N **at the bottom**. Total height = N × one picture's height; width = one picture's width.
- Horizontal (`strip_N_h`): picture 1 is **on the left**, picture N **on the right**.
- Neptune shows picture number `round(position × (N − 1))` (counting from 0), where `position` runs from 0 (minimum) to 1 (maximum). With custom notches the position follows the notch.
- For a classic knob, turn the pointer from **−135° to +135°** (270° of travel, 0° = pointing up). The larger N, the smoother the motion; 32 to 64 pictures are enough.
- Pictures must *overlap exactly* (same centre of rotation) so the knob does not "wobble".
- In SVG the total height is N × one picture's height: for example `viewBox="0 0 64 2048"` for 32 pictures of 64 × 64. Place each picture in **absolute** coordinates (avoid relying on nested `translate`).
- It is **strongly advised to have a small script generate the strip** (see the example in section 11) rather than draw it by hand.

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
- Embedded images (`<image>`), animations, scripts.
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

)FSPEC"
        R"FSPEC(### 11.2 Horizontal slider (`slider-h/track.svg` 300 × 24, `slider-h/handle.svg` 24 × 48)

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

- Buttons: two states only. Sliders: the handle is a single image. Knobs: a strip of pictures (no two-layer knob yet).
- No nine-slice: rails are stretched as they are.
- Pack images are not yet copied into presets or into a Facade's portable ZIP; the project keeps the reference to the pack.
- The pack's file name is its identifier: renaming it breaks references.
)FSPEC");
}

} // namespace facade_spec
