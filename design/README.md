# design/ — maquette visuelle de Neptune

`neptune_ui.html` : maquette HTML (données simulées) de Neptune, dans l'identité visuelle des plugins NOVA de la Flibuste Suite.
Trois vues, choisies par des onglets dans l'en-tête (Ctrl+1/2/3) : **Mixer** (pistes, inserts, envois, pan, fader, bus, master),
**Mapping** (plein écran, liste de Zones rétractable, potards du Groupe en bas) et **Sample** (éditeur plein écran : forme d'onde,
vue d'ensemble, empreinte de volume, outils non destructifs, propriétés de la Zone). Les vu-mètres sont dans la barre du bas. Quatre thèmes (sombre, bois, clair, haut contraste) et réglage de contraste 0–4.

Les quatre fichiers `nova-*` sont des **copies telles quelles** des briques partagées des plugins NOVA (contrôles, potards, contraste,
barre de défilement) : ils servent uniquement à garder la maquette fidèle. Ce n'est pas du code du plugin : l'interface JUCE sera
réécrite à partir de ces jetons et de cette maquette.

Pour l'ouvrir correctement, servir le dossier en HTTP (les scripts voisins ne se chargent pas toujours depuis `file://`) :

    python3 -m http.server 8765 --directory design   # puis http://127.0.0.1:8765/neptune_ui.html
