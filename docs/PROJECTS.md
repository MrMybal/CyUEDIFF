# Contextes de projets historiques — version 0.7

Le module `CyUEDiff::Git` sait lire le `.uproject` et les `.uplugin` du commit qui fournit l'asset. Les changements de configuration sont comparés séparément du contenu du package. Le core continue de lire le format réellement sérialisé dans chaque fichier : sélectionner une installation moteur ne convertit pas ses versions.

```powershell
# Projet et plugins enregistrés dans ce commit :
cyuediff git-project D:/MonJeu HEAD Game/Game.uproject --engine-root D:/Unreal/UE_5.5
# Résolution dans le contexte historique :
cyuediff git-inspect D:/MonJeu HEAD /Game/Characters/Hero --project Game/Game.uproject --no-engine
# Contexte propre à chaque côté :
cyuediff git-diff D:/MonJeu ancien nouveau /Game/Characters/Hero --before-project Old/Game.uproject --after-project Game/Game.uproject --before-engine-root D:/Unreal/UE_5.3 --after-engine-root D:/Unreal/UE_5.5
```

`--project` fournit le même chemin de descripteur aux deux côtés. `--before-project` et `--after-project` permettent un déplacement du projet. Le chemin est relatif à la racine du dépôt, avec `/`. `--engine-root` est le choix commun par défaut ; les deux options moteur par côté le remplacent. `--no-engine` désactive la découverte et exclut un choix moteur explicite. Sans contexte de projet, les commandes Git 0.6 conservent leur fonctionnement.

## Origine des données

| Donnée | Source |
| --- | --- |
| `.uproject` et plugins du projet | Objets Git/LFS du commit résolu |
| Contenu `/Game` et plugins du projet | Même commit, avec ses propres compagnons |
| `EngineAssociation` | Descripteur historique du projet |
| Moteur, `Build.version` et plugins moteur | Installation locale sélectionnée pour ce côté |
| Contenu `/Engine` et plugins moteur | Copie vérifiée de cette installation locale |

L'association moteur utilise le même résolveur que le core : racine explicite, variable `CYUEDIFF_ENGINE_ROOT`, association Windows personnalisée, puis installation Epic numérique disponible. `EngineInfo::resolve` est également accessible sans Git. Un moteur introuvable laisse les contenus Git lisibles, avec diagnostic. Une racine explicite invalide échoue. Une association différente de l'installation choisie produit un diagnostic ; elle ne réinterprète pas l'asset.

Le moteur installé est une source locale, **pas une révision historique reconstruite depuis Git**. Cette provenance apparaît avec `engineContentSource:"local-installation"` et `storage:"engine-local"`. Lorsqu'un package moteur est demandé, son groupe est copié dans un dossier temporaire, avec taille et SHA-256, puis lu par le core. Les changements ultérieurs du fichier source ne modifient pas cette copie. Le contexte ne fige pas toute l'installation moteur et ne garantit pas qu'une modification concurrente de l'installation soit atomique.

## Catalogue de contenus

`/Game` correspond au dossier `Content` à côté du `.uproject`. Les descripteurs sous `Plugins` et dans `AdditionalPluginDirectories` sont cherchés dans le même commit. Les chemins supplémentaires relatifs peuvent remonter vers un autre dossier **dans le dépôt** ; une recherche extérieure au dépôt est signalée comme non résolue, sans lire le checkout externe.

Les plugins avec `CanContainContent:true` fournissent des racines `/<NomDuPlugin>`. Les désactivations explicites du projet sont appliquées, sans distinction de casse ASCII. Un plugin du projet masque celui du moteur, y compris lorsqu'il ne contient pas de contenu. Les plugins imbriqués sous un plugin déjà identifié ne sont pas montés. Un doublon au même niveau de provenance, un descripteur opaque ou un drapeau invalide est diagnostiqué ; aucun montage ambigu n'est choisi arbitrairement.

Il s'agit d'un **catalogue de racines**, avec `pluginActivationEvaluated:false`. Les règles de plateforme, cible, configuration, chargement explicite, activation par défaut et dépendances transitives ne sont pas évaluées comme dans Unreal. Les propriétés correspondantes restent dans les descripteurs. Un montage de ce catalogue ne prouve donc pas qu'un plugin serait actif dans une exécution du projet. Les sous-modules, les liens symboliques et les plugins extérieurs au dépôt ne sont pas récupérés automatiquement.

Les noms longs ne portent ni extension ni suffixe d'objet : `/Game/Foo`, `/MyPlugin/Foo`, `/Engine/Foo`. La résolution cherche `.uasset` ou `.umap` ; si les deux existent, elle refuse l'ambiguïté. Un chemin relatif complet de package peut aussi être fourni explicitement et contourne le choix d'un montage. Les protections contre les traversées et sorties du moteur sélectionné restent appliquées.

## Contrat C++

```cpp
#include <cyuediff/git.hpp>

auto repo = cyuediff::GitRepository::open("D:/MonJeu");
cyuediff::GitProjectOptions before_options, after_options;
before_options.engine_root = "D:/Unreal/UE_5.3";
after_options.engine_root = "D:/Unreal/UE_5.5";
auto before = repo.project("ancien", "Game/Game.uproject", before_options);
auto after = repo.project("nouveau", "Game/Game.uproject", after_options);
auto comparison = cyuediff::compare_git(repo, before, after, "/Game/Foo");

// Décodage d'un seul package dans le contexte choisi :
auto revision = repo.materialize(before, "/Game/Foo");
auto package = revision.open();
```

Le commit est fixé une fois pendant la construction du contexte. Les descripteurs peuvent eux aussi être des pointeurs LFS locaux. `GitProjectContext::locate` décrit la source d'un nom long, sans extension ; `materialize(context, nom)` cherche le fichier réel et produit un groupe possédé par `GitRevision`. Les règles de durée de vie des payloads temporaires décrites dans [GIT.md](GIT.md) s'appliquent également aux copies de contenus moteur.

`GitProjectContext::to_json()` produit `cyuediff.git-project/1`, les descripteurs et leur provenance, les racines de contenus, l'installation moteur et les diagnostics. `semantic_snapshot()` omet le commit et les empreintes des descripteurs décodés pour comparer leurs propriétés. Les références de plugins du `.uproject` sont indexées par nom ; changer leur ordre ou l'espacement JSON ne crée pas de changement sémantique. Les sources locales des montages restent explicites : changer d'installation peut modifier plusieurs chemins de montage.

Le diff contextuel conserve `comparison` pour le résultat du core et ajoute `projectContexts` et `contextChanges`. Si l'asset est inchangé mais que le contexte change, le statut principal est `changed`, code `1`, tandis que `comparison.status` reste `identical`. Un catalogue incomplet produit `incomplete`, code `2`, tout en conservant les changements observés. Cette égalité porte sur les données inspectées, pas sur le comportement d'un projet compilé.

L'exemple `examples/git_consumer` accepte aussi `REPO BEFORE AFTER PACKAGE BEFORE_PROJECT AFTER_PROJECT [BEFORE_ENGINE AFTER_ENGINE]` et consomme ces API depuis le SDK installé, sans CyUI. L'hôte peut présenter les contextes et leurs différences à côté des vues de packages ; le rendu ne lance aucune opération Git.

## Bornes et JSON Unreal

`GitProjectOptions` plafonne par défaut chaque descripteur à 4 Mio, leur total à 32 Mio, le listing historique à 32 Mio et 200 000 entrées, et le nombre de plugins à 2 048. Ces limites sont configurables par l'hôte. Les limites Git/LFS de matérialisation s'appliquent aussi. La lecture des descripteurs ne copie pas les contenus des plugins.

Le lecteur partagé de descripteurs accepte les commentaires et les virgules finales des **tableaux**, observés dans les `.uplugin` du moteur UE 5.5 local. Il préserve les chaînes littérales et rejette notamment les virgules finales d'objets, doubles virgules, commentaires incomplets et données après la racine JSON. L'imbrication est limitée à 128 niveaux. Le lecteur local `ProjectContext` profite de ce même décodage, sans dépendance Git.

## Validation

La suite `git_project` réalise 56 vérifications dans des dépôts jetables : descripteurs Git/LFS, sous-dossiers de projet, recherche de plugins supplémentaires, masquage projet/moteur, désactivation, doublons, données opaques, changement de contexte seul, moteurs distincts, provenance et copie stable de leurs contenus, `.umap`, budgets, checkout sale, worktree lié et dépôt bare. La suite `descriptor` ajoute 15 vérifications du dialecte JSON et de ses refus.

```powershell
python scripts/validate_git_project.py --asset chemin/TEST.uasset --project chemin/Game.uproject
# Vérification d'un moteur installé et du client SDK indépendant :
python scripts/validate_git_project.py --asset chemin/TEST.uasset --project chemin/Game.uproject --real-engine-root D:/Unreal/UE_5.5 --consumer build-native/git-consumer/Release/git_compare.exe
```

Le script utilise des copies d'un asset réel et d'un descripteur local, puis deux contextes de test et installations minimales. Il vérifie un asset identique avec changement de moteur/activation explicite, dix refus de CLI, la provenance du moteur, les empreintes intactes des originaux et du checkout, et le nettoyage des temporaires. L'option `--real-engine-root` inspecte en plus le catalogue réel de l'installation ; ses diagnostics sont conservés dans le rapport. Le catalogue réel UE 5.5.4 de cette session comprend 666 documents inspectés et 283 racines de contenu, avec `complete:true`. Ces nombres décrivent le catalogue de cette installation, pas les plugins actifs d’une cible Unreal. Le moteur n'est ni exécuté ni modifié. Rapports locaux : `build-native/git-project-validation/`.

Références : [descripteur de projet, Epic](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Projects/FProjectDescriptor), [répertoires de plugins supplémentaires, Epic](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Projects/FProjectDescriptor/GetAdditionalPluginDirectories?application_version=5.5), [structure des plugins, Epic](https://dev.epicgames.com/documentation/en-us/unreal-engine/plugins?application_version=4.27). Les installations locales ont servi à vérifier le dialecte JSON ; aucun code ou asset du moteur n'est redistribué.
