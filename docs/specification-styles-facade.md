# Spécification des styles d'images de la Façade (format `.neptunestyle`)

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
| `track` | **Étirée** sur tout le widget (largeur et hauteur). | Curseur horizontal : environ 300 × 24 (large et bas). Vertical : 24 × 300. Dessinez le rail pour qu'un étirement léger ne le déforme pas (extrémités arrondies simples). |
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
