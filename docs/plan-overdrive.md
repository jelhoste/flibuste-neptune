# Plan : Fournaise (nom retenu par Jean le 2026-10-31), plugin d'overdrive et de saturation à plusieurs modèles

Écrit le 2026-10-31. Décisions de Jean : nom Fournaise ; mode milieu / côtés ; entrée latérale ; un son principal « pédale à lampe » ; plusieurs modèles (lampe, bande, transistor, fuzz) ; suréchantillonneur obligatoire ; filtres avant et après la distorsion ; un égaliseur simple à trois bandes (cloche, passe-haut, passe-bas) ; réglage de secteur traité (mélange sec / traité). Aucun code n'est écrit à ce stade.

## Constat : ce qui existe déjà

- **Rien en saturation** dans le cœur (aucun module d'overdrive, de distorsion ou de forme d'onde à reprendre). Le seul voisin est le compresseur / shaper de transitoires (La Flèche), qui ne déforme pas.
- **`Oversampler`** (`core/.../Oversampler.h`) : 2× et 4× à phase linéaire, latence 31 (2×) et 39 (4×) échantillons, sans allocation hors `prepare`. Le `ModuleRunner` déclare la latence à l'hôte et aligne le signal sec (`latencySamples`, anneau de retard du sec) : le mélange sec / traité reste en phase.
- **`Biquad`**, **`ButterworthCascade`** (passe-haut et passe-bas à pente choisie).
- **EQ de Convolution** (`llc::ParametricEQ`, `conv/eq.hpp`) : trois bandes (cloche, plateau grave, plateau aigu) plus un passe-haut et un passe-bas, paramètres lissés. C'est presque exactement l'égaliseur demandé ; on le réutilise tel quel (les plateaux sont un bonus par rapport à la demande).
- **Socle d'interface NOVA** : en-tête, pages, mètres (`VMeter`, dégradé vert / jaune / rouge de Mirador), fenêtre étirable en largeur (`stretchAuto`), spectre (`SpectrumAnalyzer`), courbes.
- **`StockModule` / `ModuleRunner`** : gains d'entrée et de sortie, mélange sec / traité, contournement, communs à tous les modules. Le plugin n'a donc à écrire que la voie traitée.

## Chaîne de traitement

```
entrée -> [filtres d'entrée : passe-haut, passe-bas] -> gain de drive
       -> suréchantillonnage (Off, 2x, 4x) -> étage de distorsion du modèle -> blocage du continu
       -> retour à la cadence de base -> [filtres de sortie : passe-haut, passe-bas]
       -> égaliseur 3 bandes -> niveau de sortie -> (mélange sec / traité du ModuleRunner)
```

Le gain de drive est appliqué **avant** le suréchantillonnage : c'est un gain linéaire, qui commute avec les filtres du suréchantillonneur ; le faire à la cadence de base coûte moins cher, et le lissage du réglage s'y fait sans difficulté. Seul l'étage de distorsion, non linéaire, tourne à la cadence élevée.

**Modes de voies** (réglage « Mode ») : Stéréo (gauche et droite séparées, mêmes réglages), **Milieu / côtés** (les deux signaux milieu et côtés sont saturés séparément, puis recombinés), « Milieu seul » et « Côtés seuls » (l'autre signal passe sans traitement, à la même latence).

**Entrée latérale (sidechain)** : un suiveur d'enveloppe lit l'entrée latérale (ou, au choix, l'entrée principale) et **module le drive** : plus la clé est forte, plus le drive monte (ou baisse, réglage négatif). Réglages : source (Off, entrée principale, entrée latérale), quantité (-100 à +100 %), attaque, retombée, passe-haut de la clé. Même mécanique que la clé externe du compresseur et du shaper (option `externalKey` de l'éditeur).

## Les quatre modèles (calculés, pas des copies de circuits)

Chaque modèle est une courbe de transfert, un peu de mémoire (préaccentuation, polarisation) et un réglage d'asymétrie. Les noms de matériel réel ne sont pas utilisés.

1. **Lampe** (le modèle principal) : triode à étage de polarisation. Écrêtage doux asymétrique (partie positive plus raide, négative plus ronde), harmoniques paires et impaires, « bias » réglable, léger affaissement de l'alimentation (« sag » : le gain baisse un peu sous un signal fort, enveloppe lente). Deux étages en cascade quand le drive dépasse un seuil, pour le grain « crunch ».
2. **Bande** : saturation symétrique douce (arc-tangente), surtout des harmoniques impaires, avec perte progressive des aigus quand le drive monte (préaccentuation / désaccentuation) et un petit gonflement du grave (« head bump »). Compression naturelle, pas de dureté.
3. **Transistor** : écrêtage à diodes, plus tranché que la lampe (courbe en exponentielle ou argument sinus hyperbolique), symétrique ou non selon le réglage ; filtre de tonalité avant l'étage (bosse des médiums).
4. **Fuzz** : écrêtage dur asymétrique, avec « gate » (polarisation affamée : coupe les petits signaux) et sortie presque carrée au maximum ; pas de blocage du continu séparé d'avant, un suivi léger du continu en plus.

Selon le modèle, certains réglages changent de rôle (« Bias » est la polarisation pour la lampe et le fuzz, l'asymétrie pour le transistor ; « Feel » a un sens propre à chaque modèle, voir le tableau). L'interface affiche les bons noms (comme les potards de Diapason qui suivent le signal choisi). Décision du 2026-10-31 : un potard « Feel » commun dont le sens suit le modèle, plutôt que des réglages séparés d'affaissement et de seuil.

## Réglages (paramètres d'hôte, tous automatisables)

| Groupe | Réglage | Plage |
|---|---|---|
| Modèle | Modèle | Lampe, Bande, Transistor, Fuzz |
| Distorsion | Drive | 0 à 60 dB (gain avant l'étage) |
| | Bias / asymétrie | -100 à +100 % |
| | Caractère (« Bite ») | 0 à 100 % : douceur de l'écrêtage, selon le modèle |
| | Sensation (« Feel ») | 0 à 100 % ; un seul potard dont le rôle suit le modèle : affaissement de l'alimentation (lampe), perte des aigus et gonflement du grave (bande), bosse des médiums (transistor), seuil de coupure (fuzz) |
| Tonalité | Tonalité | bascule entre grave et aigu, une pente autour de 1 kHz |
| Filtres | Entrée : passe-haut, passe-bas | 20 Hz à 1 kHz ; 1 kHz à 20 kHz, 12 ou 24 dB/oct |
| | Sortie : passe-haut, passe-bas | idem |
| Égaliseur | 3 bandes : actif, type (cloche, plateau grave, plateau aigu), fréquence, gain, Q | comme l'EQ de Convolution |
| | Passe-haut et passe-bas de l'égaliseur | idem |
| Niveau | Niveau de sortie | -24 à +12 dB |
| | Compensation automatique du gain | bascule |
| Voies | Mode | Stéréo, Milieu / côtés, Milieu seul, Côtés seuls |
| Entrée latérale | Source de la clé, quantité, attaque, retombée, passe-haut | Off / principale / latérale ; -100 à +100 % ; 1 à 200 ms ; 10 à 1000 ms ; 20 Hz à 2 kHz |
| Qualité | Suréchantillonnage | Off, 2×, 4× |
| Mélange | Sec / traité | ceux du `ModuleRunner` (commun) |

Environ 60 paramètres.

## Qualité sonore

- **Repliement (aliasing)** : le suréchantillonnage 4× est le réglage par défaut du modèle fuzz et du transistor, 2× pour la lampe et la bande, modifiable. Seuls les modèles très durs ont vraiment besoin du 4×. Je mesurerai le repliement dans les tests, pas à l'oreille.
- **Blocage du continu** : un passe-haut de 8 à 10 Hz après la distorsion (les courbes asymétriques créent un décalage).
- **Pas de clics** : tous les réglages sont lissés (drive, bias, niveau, fréquences de filtres).
- **Réglage de gain** : avec « compensation automatique » activée, le niveau de sortie est ramené vers celui d'entrée (calibré sur un sinus à -12 dBFS à drive moyen). Sans elle, le drive monte aussi le volume.
- Une amélioration possible plus tard : l'anti-repliement par primitive de la courbe (ADAA) à la place du suréchantillonnage seul, pour baisser le coût.

## Interface NOVA

Pages : **SATURATION** (choix du modèle par pastilles, grand potard de drive, bias, caractère, sag / gate, tonalité, niveau, courbe de transfert avec un point qui montre l'instant), **FILTRES** (entrée et sortie), **ÉGALISEUR** (trois bandes avec courbe de réponse, passe-haut, passe-bas), **SETTINGS** (suréchantillonnage, mélange, langue, thème). Mètres d'entrée et de sortie à gauche, avec les zones vert / jaune / rouge. Fenêtre étirable en largeur. Une vue de spectre avant / après est possible dans une version suivante (réutilise `SpectrumAnalyzer`).

## Presets d'usine

Quelques réglages de départ : « Lampe chaude », « Crunch », « Bande colle », « Basse overdrive », « Fuzz rond », « Fuzz carré », « Guitare solo ». Ils s'appuient sur le système de presets de l'égaliseur / compresseur.

## Tests prévus (par calcul)

- Courbe de transfert : monotone, bornée, passe par zéro ; asymétrique pour la lampe, le transistor réglé en asymétrique et le fuzz, symétrique pour la bande.
- Harmoniques par FFT sur un sinus : la lampe produit des harmoniques paires (2e), la bande presque seulement des impaires, le fuzz beaucoup de 3e et 5e.
- Repliement : à 4×, l'énergie repliée d'un sinus aigu fortement saturé est beaucoup plus basse (plus de 30 dB) qu'à Off.
- Latence : celle déclarée à l'hôte (0, 31, 39) est celle mesurée au bout d'une impulsion.
- Pas de continu à la sortie d'un signal centré (courbe asymétrique) ; pas de NaN, ni d'infini, même au drive maximal ; déterminisme.
- Mélange sec / traité aligné quand le suréchantillonnage est actif (pas de peigne).
- Compensation de gain : écart de niveau d'un sinus à -12 dBFS inférieur à un seuil, pour chaque modèle.

## Étapes

1. Cœur : `OverdriveModule` (paramètres, quatre modèles, filtres, égaliseur réutilisé, suréchantillonnage, présets) et tests.
2. Adaptateur : plugin CLAP effet stéréo (`FournaisePlugin`), état JSON, ajout dans CMake et le workflow (sans lancer la CI).
3. Interface NOVA : pages, courbe de transfert, mètres ; rendu hors ligne (NovaGallery, normal et large) pour vérifier la mise en page.
4. Compilation Linux et Windows, installation, état des versions, message au banc (quand Jean le demandera).

## Limites et risques

- Rien n'est écouté de mon côté. Les modèles sont des approximations calculées ; le « son lampe » se juge à l'oreille. Je peux vérifier la forme des courbes, les harmoniques et le repliement, pas le plaisir.
- Le suréchantillonnage ajoute de la latence (déclarée) et de la charge CPU ; le 4× avec les deux voies et un égaliseur reste modeste, à mesurer.
- Les quatre modèles partagent la même chaîne ; si l'un d'eux se révèle décevant, on peut l'affiner sans toucher les autres.
- Pas de modélisation de circuits réels ni de noms de produits.

## Décisions de Jean (2026-10-31)

- Nom : **Fournaise**.
- Mode milieu / côtés : oui. Entrée latérale : oui.
- Niveau de sortie final : oui (déjà prévu).
- Réglages d'affaissement et de seuil : propres à leur modèle, regroupés dans le potard « Feel ».
- Pas de vue en LU relatives à la cible dans Mirador (abandonné).
