# Intégration Git et Git LFS — version 0.6

`CyUEDiff::Git` est un adaptateur séparé, au-dessus de `CyUEDiff::Core`. Il extrait un package et ses compagnons depuis chaque commit, résout les pointeurs LFS dans le cache local, puis réutilise le lecteur et le diff existants. La lecture directe de fichiers du core ne dépend pas de Git.

```powershell
cyuediff git-inspect D:/MonJeu HEAD Content/M_Water.uasset --out inspection.json
cyuediff git-diff D:/MonJeu HEAD~1 HEAD Content/M_Water.uasset --out diff.json
# Chemin différent dans la seconde révision, à fournir explicitement :
cyuediff git-diff D:/MonJeu ancien nouveau Content/Old.uasset --after-path Content/New.uasset
```

Les chemins sont relatifs à la racine du dépôt, avec `/`, extension `.uasset` ou `.umap`. Espaces, accents et crochets sont pris en charge. Les chemins absolus, traversées `..`, noms réservés Windows et caractères non portables sont refusés. Les liens symboliques et sous-modules ne sont pas matérialisés comme packages. Un dépôt normal, un worktree lié ou un dépôt bare peut fournir les objets.

Git 2.45+ doit être accessible dans PATH, ou via `GitOptions::executable`. Le programme `git-lfs` n'est pas nécessaire pour cette lecture : les pointeurs canoniques SHA-256 sont interprétés directement. Les téléchargements et la récupération d'objets manquants restent à la charge de l'hôte ou de son client Git/LFS.

## Révisions et fichiers compagnons

Le commit-ish est résolu une fois en identifiant complet. L'arbre est interrogé avec des chemins littéraux et des séparateurs NUL. Les blobs sont lus directement par `cat-file`, sans checkout, smudge, textconv ou hook. Les objets Git SHA-1 et SHA-256 sont pris en charge. Les références de remplacement sont désactivées afin de conserver le contenu désigné par le commit.

Chaque révision dispose d'un dossier temporaire distinct contenant le package et les compagnons présents dans son propre arbre : `.uexp`, `.ubulk`, `.uptnl`, `.m.ubulk`, `.o.ubulk`, `.upayload`. Aucun compagnon du checkout ou d'une autre révision ne sert de repli. Un `.uexp` requis absent produit une lecture incomplète ; une référence LFS introuvable interrompt la matérialisation avec `lfs-missing` et l'OID concerné.

Le cache par défaut est `lfs/objects` dans le répertoire Git commun. `lfs.storage` est respecté, avec résolution des chemins relatifs à ce répertoire ; `--lfs-objects DIR` ou `GitOptions::lfs_objects` fournit explicitement le répertoire `objects`. La taille et le SHA-256 de chaque contenu LFS copié sont vérifiés avant lecture. Les extensions de pointeurs et formes non canoniques demandent un autre adaptateur et sont refusées explicitement.

Aucun fetch implicite n'est effectué, y compris pour les clones partiels : les processus reçoivent `GIT_NO_LAZY_FETCH=1`. L'environnement Git propre au processus appelant, notamment `GIT_DIR` et `GIT_WORK_TREE`, n'est pas transmis. Les commandes s'exécutent directement, sans shell et sans fenêtre sous Windows. L'adaptateur ne change ni checkout, ni index, ni configuration, ni références. La CLI peut écrire le rapport demandé avec `--out`, mais refuse d'écraser les packages d'entrée, leurs compagnons, les métadonnées Git et le stockage LFS.

## Résultat

Le schéma `cyuediff.git-diff/1` contient `before`, `after` et `comparison`. Les manifestes enregistrent le commit, le chemin, la présence du package, les OID de blobs, tailles et provenance `git` ou `lfs-local`. `comparison` est le résultat ordinaire du core : propriétés, graphes Blueprint, textures, meshes et matériaux.

| Statut principal | Code CLI | Sens |
| --- | ---: | --- |
| `identical` | 0 | Mêmes contenus observés et empreintes ; les commits peuvent différer |
| `changed` / `binary-only` | 1 | Changements sémantiques ou uniquement binaires |
| `added` / `removed` | 1 | Package présent dans une seule des deux révisions |
| `incomplete` / `unverified` | 2 | La lecture ne permet pas d'établir une comparaison complète |
| Erreur JSON sur stderr | 2 | Révision invalide, LFS manquant/corrompu, budget dépassé, format non pris en charge, etc. |

Un package absent des deux arbres est une erreur. Un renommage fourni avec `--after-path` et sans changement de contenu peut donner `identical` : les deux chemins restent visibles dans les manifestes. La détection automatique de renommages n'est pas encore implémentée.

## Intégration dans une application

```cmake
# add_subdirectory : activer CYUEDIFF_BUILD_GIT avant d'ajouter le projet.
# SDK installé :
find_package(CyUEDiff CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE CyUEDiff::Git)
```

```cpp
#include <cyuediff/git.hpp>

auto repo = cyuediff::GitRepository::open("D:/MonJeu");
auto result = cyuediff::compare_git(repo, "HEAD~1", "HEAD", "Content/Asset.uasset");
std::cout << result.to_json().dump(2);

// Lecture d'une seule révision, notamment pour décoder ensuite une image ou un mesh :
auto revision = repo.materialize("HEAD", "Content/Asset.uasset");
auto package = revision.open();
```

`GitRevision` possède le stockage temporaire par référence partagée. Les copies de cette révision prolongent sa durée de vie ; son dernier propriétaire supprime les fichiers. **Garder la révision ou le `GitComparison` vivant tant que le package, une image ou un mesh peut relire ses payloads.** Copier seulement `Package` ne conserve pas le propriétaire du dossier. `asset_path()` donne le chemin temporaire, `GitRevision::to_json()` le manifeste stable. Les chemins temporaires du rapport d'inspection cessent d'exister après la commande.

`GitComparison::before_package` et `after_package` sont des `shared_ptr<const Package>` prêts pour les vues CyUI ; le côté absent est nul. L'hôte garde le `GitComparison` dans son document et prépare les vues hors du rendu. Il choisit une présentation adaptée pour un ajout ou une suppression. Aucun processus Git ne s'exécute dans `draw`.

L'exemple [git_consumer](../examples/git_consumer/) consomme l'adaptateur installé avec `find_package`, sans CyUI. Le core et le module Git s'installent séparément dans la même exportation CMake. Pour désactiver le module : `CYUEDIFF_BUILD_GIT=OFF` ou `python scripts/build.py --no-git --build-dir build-core`.

`GitOptions` fournit les limites par fichier (2 Gio), par révision (4 Gio), le timeout par processus (30 s), le répertoire temporaire et le cache LFS explicite. Les blobs vont directement sur disque ; les copies LFS utilisent des blocs de 64 Kio, puis une vérification SHA-256. Les limites de sortie des processus sont surveillées pendant leur exécution ; elles ne constituent pas un quota disque strict. Le dossier temporaire doit déjà exister si l'hôte le fournit. Son contenu alloué est nettoyé après réussite et après exception ; une interruption brutale du programme peut laisser des dossiers `cyuediff-git-*`.

Cette tranche compare les versions enregistrées dans les packages. Depuis la version 0.7, les `.uproject` et descripteurs de plugins historiques sont disponibles avec des moteurs distincts par côté ; voir [PROJECTS.md](PROJECTS.md). Les règles de compilation et configurations INI complètes ne sont pas reconstituées. Aucun objet n’est téléchargé ; les renommages automatiques, le cache persistant et l’annulation globale restent à développer. Les limites du lecteur Unreal restent applicables. La plateforme vérifiée est Windows x64 ; le chemin POSIX existe mais n'a pas été validé ici.

## Validation

La suite `git` réalise 56 vérifications dans des dépôts jetables : exports séparés avec `.uexp` LFS, compagnons propres à chaque commit, identités immuables, ajout/suppression/renommage, Unicode, worktree sale préservé, worktree lié, dépôt bare, stockage LFS configurable, taille/hash erronés, absence de contenu, références de remplacement, Git SHA-256, budgets, durée de vie et timeouts. Elle est enregistrée par CTest quand Git 2.45+ est disponible.

```powershell
python scripts/validate_git.py --before chemin/MI_Water.uasset --after copie/parameter-changed.uasset
# Avec le client SDK compilé séparément :
python scripts/validate_git.py --before avant.uasset --after apres.uasset --consumer build-native/git-consumer/Release/git_compare.exe
```

Le script compare d'abord les deux fichiers directement, les place dans un dépôt temporaire sous forme de pointeurs LFS, puis vérifie l'égalité des résultats via les révisions. Il contrôle les refus de CLI, le stockage externe configuré, le checkout et l'index intacts, le diagnostic LFS manquant et le nettoyage des temporaires. Les originaux restent intacts. L'essai réel sur `MI_Water` retrouve un seul paramètre scalaire changé, de `100` à `101`. Rapports locaux : `build-native/git-validation/`.

## Textconv fichier par fichier

`cyuediff textconv chemin.uasset` reste disponible pour le snapshot JSON ordonné. Il ne lance aucune commande Git et ne récupère aucun objet LFS. Un pointeur non matérialisé est refusé avec `lfs-pointer`.

```ini
# Configuration locale optionnelle, à appliquer dans le dépôt client :
[diff "unreal"]
    textconv = "C:/Tools/CyUEDiff/cyuediff.exe" textconv
    cachetextconv = true
```

```gitattributes
# Conserver les attributs LFS existants :
*.uasset filter=lfs diff=unreal merge=lfs -text
*.umap   filter=lfs diff=unreal merge=lfs -text
```

Cette configuration remplace `diff=lfs` pour ces extensions et doit être adaptée aux attributs du projet. Git textconv ne reconstitue pas automatiquement les compagnons historiques : utiliser `git-diff` pour cette comparaison groupée. L'outil ne fusionne ni ne réécrit les packages Unreal.

Références de format et de commandes : [ls-tree](https://git-scm.com/docs/git-ls-tree), [cat-file](https://git-scm.com/docs/git-cat-file), [spécification des pointeurs et objets LFS](https://github.com/git-lfs/git-lfs/blob/main/docs/spec.md), [configuration lfs.storage](https://github.com/git-lfs/git-lfs/blob/main/docs/man/git-lfs-config.adoc). Le refus du fetch paresseux est également vérifié dans [Git 2.45.1](https://github.com/git/git/blob/v2.45.1/promisor-remote.c).

## Contextes de projets 0.7

`git-project` inspecte un `.uproject` historique. `git-inspect` et `git-diff` acceptent `--project` et les noms `/Game`, de plugins ou `/Engine`. Le diff peut prendre des projets et racines moteur distincts par côté ; `contextChanges` décrit alors les différences même avec un asset identique. Les contenus du moteur sélectionné restent identifiés comme locaux. Voir [PROJECTS.md](PROJECTS.md) pour le contrat, les bornes et les limites du catalogue de plugins.

## Commit history — 0.23

`GitRepository::history(revision, GitHistoryOptions)` reads one topologically ordered, newest-first page of local commit metadata. Each row contains a full object ID, parent IDs, author, author ISO date and subject. `head` resolves the starting revision once; pass that returned ID, `next_skip` and the same page size for subsequent pages. A moving branch does not change those pinned pages. `has_more` is established by one additional record. Dates are author dates, not the history ordering criterion. Shallow repositories expose only their available ancestry.

```powershell
cyuediff git-history D:/Projects/Game HEAD --max-commits 100
cyuediff git-history D:/Projects/Game COMMIT_ID --skip 100 --max-commits 100 --out history.json
```

Pages accept 1–1,000 commits, skip up to 1,000,000 and 1–64 MiB of metadata (8 MiB by default). Individual fields are limited to 128 KiB. The existing per-process timeout, cancellation and output bounds apply. NUL-delimited records preserve subjects containing punctuation; malformed or non-UTF-8 metadata is rejected. SHA-1 and SHA-256 repository IDs are supported. History does not read package blobs, verify LFS payloads, fetch, check out or run filters. CLI output retains the adapter's Git-storage protections.
