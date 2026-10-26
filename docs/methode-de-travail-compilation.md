# Méthode de travail : compiler sans geler l'ordinateur, et passer par GitHub Actions

Document de référence, à coller au début d'une nouvelle conversation (ou à donner à Claude Code) pour qu'il travaille de la même façon sur tous les projets. Rédigé le 2026-10-26 sur Linux (Ubuntu récent, 4 cœurs, 11 Go de RAM), à partir de la reprise du projet Neptune.

## 1. Le principe en une phrase

**Le code du cœur (C++ sans interface) se teste en local, avec des limites de ressources ; le plugin complet (interface JUCE, trois systèmes) est compilé par GitHub Actions, et Claude surveille, corrige et installe le résultat.**

## 2. Pourquoi cette règle : l'ordinateur gelait

Constat du 2026-10-26 : l'ordinateur a gelé ou planté plusieurs fois pendant des compilations. Le journal du système (`journalctl -b -1`) montrait « Out of memory: Killed process claude-desktop ». Ce n'est donc **pas seulement le processeur : c'est la mémoire**. L'étape finale de la compilation d'un plugin JUCE (l'édition de liens avec optimisation LTO, « 128 LTRANS jobs ») consomme beaucoup de RAM, et 4 cœurs en parallèle aggravent le problème. Avec plusieurs projets qui compilent en même temps sur 11 Go, la mémoire manque.

Règle : **jamais deux compilations lourdes en même temps**, et jamais de compilation du plugin complet en local sauf demande explicite de l'utilisateur (après avoir vérifié qu'aucun autre projet ne compile).

## 3. Ce qui se fait en local (léger, avec limites)

Les **tests du cœur** (logique, sans JUCE, sans fenêtre) : ils vérifient le modèle de données, les règles, le JSON, les conversions. Ils sont légers.

On les lance toujours avec des limites de ressources, grâce à `systemd-run` (disponible sur Ubuntu, sans `sudo`) :

```bash
LIM="systemd-run --user --scope --quiet -p CPUQuota=200% -p MemoryMax=3G -p MemorySwapMax=0 nice -n 19"
$LIM cmake -S . -B ~/Bureau/FlibusteSuite/build-core -G Ninja -DNEPTUNE_BUILD_PLUGIN=OFF -DNEPTUNE_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
$LIM cmake --build ~/Bureau/FlibusteSuite/build-core --target neptune_core_tests -- -j2
$LIM ~/Bureau/FlibusteSuite/build-core/tests/neptune_core_tests
```

- `CPUQuota=200%` : au plus 2 cœurs. `MemoryMax=3G` : au plus 3 Go ; si la limite est atteinte, **seule la compilation est arrêtée**, pas la session. `nice -n 19` : priorité la plus basse, l'ordinateur reste utilisable.
- À adapter au projet : une machine de 11 Go partagée entre deux compilations ne doit pas dépasser environ 4 Go chacune.
- **Dossier de build permanent**, pas dans `/tmp` (qui est vidé à chaque redémarrage) : par exemple `~/Bureau/<Suite>/build-core`, hors du dépôt git.
- Tout ce qui est dans `core/` et `tests/` se vérifie ainsi. L'interface (`adapter-juce/`) **n'est pas** compilée en local.

## 4. Ce que fait GitHub Actions

À chaque `git push`, la CI compile le cœur, les tests et le plugin pour **Linux, Windows et macOS** (35 à 40 minutes). Elle produit les binaires dans les « artefacts » du run.

Avantage : l'ordinateur ne travaille pas. Inconvénient : une erreur de compilation dans l'interface coûte un tour de CI. D'où le soin demandé à Claude : **relire le code de l'interface avant de pousser** (MSVC, le compilateur Windows, est plus strict : par exemple une lambda ne peut pas capturer implicitement une constante locale ; utiliser `static constexpr`).

## 5. Le cycle de travail de Claude

1. **Coder** : modifier le cœur et l'interface. Mettre à jour la documentation (`docs/`) et les textes de traduction (`I18n`).
2. **Tester en local** (section 3) : les tests du cœur doivent tous passer.
3. **Commiter et pousser** sur `main`. (Chaque projet a ses règles : confirmer d'abord ce qui sort de la machine, sauf consigne contraire durable de l'utilisateur.) Message de commit court, en anglais, avec la ligne de co-auteur.
4. **Surveiller la CI** : une seule commande de fond, jamais plusieurs boucles, un contrôle toutes les 5 minutes au plus (l'API GitHub est limitée, la limite a déjà été atteinte) :

   ```bash
   until [ "$(gh run view <id> --json status -q .status 2>/dev/null)" = completed ]; do sleep 300; done
   gh run view <id> --json conclusion,jobs -q '.conclusion, (.jobs[]|"\(.name) \(.conclusion)")'
   ```

   Un push ne déclenche parfois aucun run : `gh workflow run build.yml --ref main`.
5. **Si la CI échoue** : lire le journal (`gh run view <id> --log-failed`), observer, corriger, repousser, relancer la surveillance. Sans redemander, tant que la correction reste dans le périmètre demandé.
6. **Si tout est vert** : vérifier **les trois plateformes** (Linux, Windows, macOS) avant d'installer. Ne jamais installer si l'une a échoué.
7. **Installer les binaires Linux** pour que l'utilisateur teste :
   - télécharger tous les artefacts `<nom>-Linux` (`gh run download <id> -n <nom>-Linux`) dans un dossier neuf, avant de déplacer quoi que ce soit ;
   - ranger les anciens `.clap` dans `~/neptune-anciens/<horodatage>/` ;
   - extraire les nouveaux dans `~/.clap/` (créé au besoin), vérifier avec `ldd` qu'aucune bibliothèque ne manque ;
   - **prévenir l'utilisateur** : fermer les instances ouvertes dans Reaper, puis *Re-scan* des plugins CLAP.
8. **Dire honnêtement** ce qui est testé et ce qui ne l'est pas (« non testé dans Reaper »).

## 6. Après un plantage ou un redémarrage

- Les fichiers du projet sont intacts (`git status` pour le vérifier) ; seul ce qui était dans `/tmp` (dossier de travail de Claude) disparaît.
- Les tâches de fond sont interrompues : les relancer, avec limites.
- Regarder la cause : `journalctl -b -1 -p err --no-pager | tail` (mémoire saturée ? disque ?).
- Les commits poussés sont sur GitHub : `git fetch && git status -sb`.

## 7. Règles de sécurité habituelles

- Pas de push forcé (`--force`), sauf demande explicite.
- Les mots de passe et les commandes `sudo` se saisissent dans le terminal de l'utilisateur, jamais dans la conversation.
- Ne pas supprimer de sauvegardes avant d'avoir vérifié que tout est sur GitHub.
- Ne jamais deviner : lire le code ou la documentation, ou dire « non vérifié ».
- Une demande d'installation ou de publication est confirmée avant d'être faite quand elle sort de la machine.

## 8. Ce qui est propre à chaque projet

Cette méthode vaut pour les projets C++ avec CI GitHub (Neptune et la Flibuste Suite). Pour une application web (PWA comme Scorsaire), la base reste la même : tests en local avec limites, relecture avant d'envoyer, historique git propre. Les spécificités (versions de Node, Playwright, etc.) sont dans le fichier de réinstallation du projet.
