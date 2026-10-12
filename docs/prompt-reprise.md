# Prompt pour reprendre le projet dans une nouvelle discussion

Copier-coller le texte ci-dessous au début de la nouvelle discussion.

---

On continue le projet Neptune (sampler CLAP en C++/JUCE, dépôt `flibuste-neptune`, dossier `/home/jean/dev/sampler-sans-reproche`). Lis d'abord `docs/passage-de-relais.md` (état complet, pièges déjà rencontrés, décisions), puis `docs/roadmap-sampler-clap.md` et, au besoin, `docs/spec-modules.md` §3 (module Sampler). Vérifie l'état avec `git log` et `git status` avant de commencer.

**Règles de travail** (elles viennent de moi, l'utilisateur) :
- Règle absolue : ne jamais deviner. Regarder le code ou la documentation, ou me demander. Quand une information n'est pas vérifiée, le dire.
- Réponds en français. Dis-moi ce que je verrai ou testerai dans Reaper, pas le détail du code (je ne le lis pas).
- Préviens-moi quand une décision de design entre en conflit avec une fonctionnalité de la spec.
- Commiter, pousser et installer le `.clap` seulement quand je le demande. Après un push : attendre la CI, puis installer le binaire Linux dans `~/.clap/` (garder l'ancien hors de ce dossier), comme décrit dans le piège 9 du passage de relais. Je teste sous Linux et Windows (souris et tactile).
- Avant un gros morceau, pose-moi les questions que la spec laisse ouvertes ; une fois que j'ai répondu, avance sans redemander.

**État** : palier 0 complet ; palier 1 en cours : lecture de samples (WAV/AIFF/FLAC), zones multiples, import par gabarit de nom (positions, segments, expressions REL/SUS/RR), round-robin, déclenchement au relâchement, enveloppe complète avec éditeur graphique, chargement sélectif (principal seulement, relâchements, Groupes). Tout est validé par moi sous Windows et Linux. Le moteur joue 64 voix ; le coût mesuré est de l'ordre de 6,5 % du budget temps réel.

**À faire maintenant, dans cet ordre :**
1. **Estimation exacte de la mémoire à l'import** : lire l'en-tête de chaque fichier (frames, canaux, profondeur) pour calculer la mémoire réelle après décodage (int16 gardé, le reste en float32), l'afficher avec la RAM disponible de la machine, et caler le seuil de refus dessus. Aujourd'hui l'assistant estime avec la taille des fichiers sur disque, ce qui sous-estime les WAV 24 bits (×1,33) et le FLAC (×1,5 à ×2). Garde-fou actuel : avertissement au-delà de 1 000 Mo, refus au-delà de 3 000 Mo.
2. **Streaming disque** (obligatoire pour mon piano, moins de 3 Go : 88 notes × ~30 vélocités × round-robin, et d'autres gros instruments) : le début de chaque sample en RAM (préchargement), le reste lu depuis le disque par un thread dédié via des tampons circulaires, sans jamais lire le disque ni allouer sur le thread audio ; gestion du manque de données (sous-alimentation) ; taille de préchargement réglable ; réglable par instrument, le mode « tout en RAM » restant disponible. Concevoir l'accès aux samples pour que le streaming s'y branche (aujourd'hui `SampleData` est entièrement en mémoire). HISE et Kontakt peuvent inspirer l'approche, mais ne copie pas de code (HISE est sous GPLv3 ; j'ai déjà choisi la prudence sur les licences). Pose-moi les questions de conception avant de coder.

**Ensuite** (ordre proposé) : matrice notes × vélocité avec étirement des zones à la souris, fusion des couches de vélocité au niveau de l'instrument, modes de polyphonie (retrigger/legato, choke, limite de voix réglable), filtre par voie, mixeur multi-pistes, synthé soustractif.

**Méthode de travail déjà en place** : cœur C++ portable testé avec Catch2 (compilation locale avec un `cmake` en environnement virtuel, voir le passage de relais) ; l'adaptateur JUCE ne se compile que par la CI, mais on le contrôle à la syntaxe en local contre les vrais en-têtes JUCE 8.0.11 (piège 8) ; outil de mesure `neptune_perf_callback`.
