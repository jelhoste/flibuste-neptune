# Prompt pour reprendre le projet dans une nouvelle discussion

Copier-coller le texte ci-dessous au début de la nouvelle discussion.

---

On continue le projet Neptune (sampler CLAP en C++/JUCE, dépôt `flibuste-neptune`, dossier `/home/jean/dev/sampler-sans-reproche`). Lis d'abord `docs/passage-de-relais.md` (état complet, pièges déjà rencontrés, décisions, une section par jalon), puis `docs/roadmap-sampler-clap.md` et, au besoin, `docs/spec-modules.md` (§1 piste, §3 Sampler, §4 Bus, §5 Master, §6 routage, §7 plugins stock). Vérifie l'état avec `git log` et `git status` avant de commencer.

**Règles de travail** (elles viennent de moi, l'utilisateur) :
- Règle absolue : ne jamais deviner. Regarder le code ou la documentation, ou me demander. Quand une information n'est pas vérifiée, le dire.
- Réponds en français. Dis-moi ce que je verrai ou testerai dans Reaper, pas le détail du code (je ne le lis pas).
- Préviens-moi quand une décision de design entre en conflit avec une fonctionnalité de la spec.
- Avant un gros morceau, pose-moi les questions de conception que la spec laisse ouvertes (outil de questions à choix) ; une fois que j'ai répondu, avance sans redemander.
- Commiter, pousser et installer le `.clap` seulement quand je le demande. Après un push : attendre la CI (les trois plateformes), puis installer le binaire Linux dans `~/.clap/` en déplaçant l'ancien dans `~/neptune-anciens/` (Reaper scanne tout ce qui est dans `~/.clap/`), comme décrit dans le piège 9 du passage de relais. Je teste sous Linux et Windows (souris et tactile).
- Si la CI échoue : lire le journal, corriger, repousser, sans me demander.

**État (2026-10-13)** : palier 0 complet. Palier 1 : Sampler complet en premier jet, tout validé par moi sauf le dernier jalon. Fait : lecture WAV/AIFF/FLAC, zones multiples, import par gabarit de nom, round-robin, relâchement, enveloppe AHDSHR graphique, chargement sélectif, estimation exacte de la mémoire à l'import, streaming disque (préchargement 128 Ko, thread disque, tampons, pool partagé), annulation des modifications d'instrument (par différences), vue Mapping avec matrice notes x vélocité (déplacer, étirer, dupliquer, hachures de chevauchement, clavier jouable, curseur de jeu), fusion des couches de vélocité (fondu automatique et explicite, courbe de vélocité), polyphonie (limite 1 à 256, modes de vol, même touche, mono / legato, glissando), choke par zone (coupe / est coupé par), conditions Premier / Legato, liste de zones éditable avec bouton d'écoute, moniteur de voix à la demande, bouton « Tout arrêter », filtres par voie (2 filtres SVF / échelle, série ou parallèle, enveloppes, héritage instrument -> zone, coupure et résonance automatisables). **Dernier jalon livré, À VALIDER PAR MOI dans Reaper : le mixeur multi-pistes** (16 pistes avec leur instrument et leur canal MIDI, 16 bus, Master, envois avec points de captation, sorties directes, format d'état v5, vue Mixeur ; voir la section « Mixeur multi-pistes » du passage de relais, dont les points à confirmer : loi du « pan vrai », trim de piste).

**À faire maintenant :**
1. Attendre mon retour de test du mixeur (anciens projets, deuxième piste sur un autre canal MIDI, envois / bus / sorties directes, sauvegarde et réouverture) et corriger.
2. Ensuite, dans l'ordre que je choisirai : **pistes parallèles** (arbre de pistes, spec §1), **inserts** et premiers **plugins stock** (spec §7 à §13 : utilitaire, égaliseur, compresseur, etc. : les emplacements d'inserts sont réservés, aucun module n'existe), **synthé soustractif** (2e type de génération de piste, spec §2 : réutiliser le moteur de filtre et d'enveloppe), **limite globale de voix à l'échelle du plugin**, LUFS au Master (libebur128), filtre MIDI avancé par piste.

**Chiffres de performance** (outil `neptune_perf_callback`, cette machine, bloc 256) : 64 voix sans filtre ~8 % du budget temps réel ; 256 voix ~30 % ; un filtre SVF 24 dB +3 à +5 points à 64 voix ; deux filtres ~+12 ; 256 voix avec deux filtres ~100 %.

**Méthode de travail déjà en place** : cœur C++ portable testé avec Catch2 (252 tests) ; compilation locale avec un `cmake` en environnement virtuel (voir « Build, tests, CI » du passage de relais) ; l'adaptateur JUCE ne se compile que par la CI, mais on le contrôle à la syntaxe en local contre les vrais en-têtes JUCE 8.0.11 (piège 8 : clone partiel de JUCE et de clap-juce-extensions dans le dossier temporaire de la session, qu'il faut refaire si le dossier a été nettoyé) ; ne pas poser de grosses structures sur la pile (limite de 1 Mo sous Windows : voix et emplacements de streaming sont sur le tas).
