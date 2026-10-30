# Plan : Diapason (nom provisoire), générateur de signaux de test et petite boîte à rythme

Écrit le 2026-10-30. Décision de Jean : « ce que tu proposes est super », trois façons de déclencher, plugin CLAP instrument à sortie stéréo unique, panoramique par son de batterie.

## But

Un plugin CLAP instrument qui produit à la demande des signaux de test (formes d'onde, bruits, balayages) et une mesure de batterie en boucle, pour essayer des effets et des chaînes audio sans avoir à chercher des fichiers.

## Le signal

| Choix | Détail |
|---|---|
| Formes d'onde | sinus, carré / rectangulaire (rapport cyclique réglable), triangle, dent de scie montante et descendante (carré et dents de scie sans repliement : correction PolyBLEP) |
| Autres | impulsion (un échantillon à 1, répétée à la fréquence réglée), deux sinus (intermodulation : fréquence et niveau relatif de la seconde), continu (DC) |
| Bruits | blanc, rose, brun, bleu, violet ; niveau = valeur efficace (RMS) en dBFS, limité à ±1 |
| Balayages | logarithmique et linéaire, fréquence de départ, d'arrivée, durée |
| Niveau | en dBFS crête pour les signaux périodiques ; panoramique (équilibre : le centre envoie le même niveau des deux côtés) ; polarité (normale, inversée à gauche, à droite, des deux) |

Fondus de 4 ms au début et à la fin (sauf l'impulsion) pour éviter les clics.

## Trois modes

- **Continu** : le signal sonne tant que « Marche » est actif ; balayage en boucle ; option de salves (durée sonore / silence en ms).
- **Déclenché** : un coup par déclenchement (bouton, note MIDI, passage de « Déclencheur » à 1) ; durée réglable (le balayage dure sa durée).
- **Note MIDI** : le signal suit la note tenue (fréquence de la note, dernière note prioritaire).

## La batterie

Huit sons calculés (grosse caisse, caisse claire, charleston fermé, charleston ouvert, tom, clap, rim, cloche) ; chacun a niveau, accordage, durée, panoramique et sourdine. Le charleston fermé coupe le charleston ouvert.

Séquenceur : une mesure à quatre temps, 4 à 16 pas, trois états par pas (vide, normal, accent), swing. Départ interne (tempo réglé, bouton Marche) ou suivi du transport de l'hôte (position et tempo de Reaper).

## Déclenchement par commande

1. **Interface** : boutons de l'écran.
2. **MIDI** : huit notes consécutives à partir de la note réglée (36 par défaut) jouent les huit sons de batterie ; toute autre note déclenche ou joue le signal selon le mode.
3. **Paramètres d'hôte** : tous les réglages, y compris les 128 pas, la marche, le déclencheur, sont des paramètres automatisables, donc pilotables depuis un script Reaper.

## Étapes

1. Cœur (`SignalGenerator.h/.cpp`) : paramètres, générateurs, voix de batterie, séquenceur, moteur. Tests du cœur.
2. Adaptateur : `DiapasonPlugin`, état JSON, CLAP instrument.
3. Interface NOVA : page SIGNAL, page BATTERIE (grille de pas), page SETTINGS. Rendu hors ligne (NovaGallery) pour vérifier la mise en page.
4. Compilation Linux et Windows, installation, état des versions, message au banc (quand Jean le demandera).

## Limites

- Rien n'est écouté de mon côté : la justesse des fréquences, des niveaux et des positions des pas se vérifie par calcul dans les tests ; le son des batteries se juge à l'oreille.
- Le sinus et le triangle ne sont pas à bande limitée (le sinus n'en a pas besoin ; le triangle replie très peu).
- Le rose est une approximation (environ 0,5 dB) ; les bruits sont normalisés par calcul au démarrage.
