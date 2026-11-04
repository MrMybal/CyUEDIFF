# CyUEDiff

[English](README.md) · **Français**

Cœur C++17 pour inspecter des packages Unreal **sans lancer Unreal Editor**, produire une représentation stable et comparer deux versions. Une bibliothèque de vues CyUI reste séparée du lecteur.

Cette version 0.10 lit les objets et propriétés sérialisées, reconstruit les graphes Blueprint et ajoute les sources de textures et de meshes statiques. Le core fournit des aperçus d'images, la géométrie source, des graphes de matériaux et leurs paramètres, des diffs spécialisés et un export OBJ. Les vues CyUI restent séparées. Un adaptateur Oodle local optionnel débloque les sources comprimées, sans lancer Unreal. Le module Git/LFS matérialise les révisions et leurs compagnons depuis les objets locaux, puis les compare avec le core. Les descripteurs de projet et plugins historiques, les installations moteur par côté et leurs différences de contexte sont accessibles. Un registre C++ permet désormais à l’application hôte de fournir ses décodeurs de structures et de fins natives, sélectionnés par type et versions. Une première application desktop Windows/CyUI ouvre et compare désormais ces fichiers dans une fenêtre native, avec chargement en arrière-plan. Elle compare aussi les révisions Git/LFS locales, les ajouts/suppressions et les changements de contexte projet.

## Modules

| Cible | Rôle |
| --- | --- |
| `CyUEDiff::Core` | Lecture binaire, contexte projet/moteur, snapshots et diff ; aucune dépendance Unreal, ImGui ou CyUI |
| `CyUEDiff::Git` | Adaptateur optionnel : révisions Git et objets LFS locaux vérifiés, fichiers compagnons, durée de vie des copies temporaires |
| `cyuediff` | CLI JSON : `inspect`, `diff`, `textconv`, `git-inspect`, `git-diff`, `git-project`, `graph`, `material`, `material-diff`, `texture`, `preview`, `image-diff`, `mesh`, `geometry`, `mesh-diff`, `export-mesh`, `project`, `resolve` |
| `cyuediff_desktop` | Application Windows optionnelle : ouverture locale, contexte projet/moteur, inspection, comparaison, graphes, images et filaire |
| `CyUEDiff::UI` | Vues embarquables dans une application ImGui/CyUI : objets, propriétés, graphes Blueprint et matériaux, paramètres d’instances, images, filaire 3D et comparaison côte à côte |

Le cœur utilise la bibliothèque MIT nlohmann/json 3.12.0, incluse dans `third_party`. Le décodage PNG/JPEG et l’écriture PNG utilisent stb, également inclus. La compilation du cœur ne télécharge rien. L'interface utilise un checkout de [CyUI](https://github.com/MrMybal/CyUI) fourni par l'application hôte.

## Compiler

CMake 3.20+, compilateur C++17. Windows/MSVC 2022 est la plateforme vérifiée. Le cœur est conçu pour les autres plateformes, sans validation Linux/macOS à ce stade.

```powershell
python scripts/build.py
# Core/CLI sans le module Git :
python scripts/build.py --no-git --build-dir build-core
# Avec l'adaptateur CyUI :
python scripts/build.py --cyui D:/path/to/CyUI
# Avec la fenêtre desktop Windows :
python scripts/build.py --desktop --cyui D:/path/to/CyUI
build-native/Release/cyuediff_desktop.exe
```

Le script compile en Release et exécute CTest. Le module Git est activé par défaut dans un build autonome ; ses commandes demandent Git 2.45+ à l’exécution. Le lecteur Core ne dépend ni de Git ni de Git LFS. Il retrouve le CMake de Visual Studio s'il n'est pas dans PATH. La sortie Windows est dans `build-native/Release/`.

Équivalent avec CMake :

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Pour construire l'adaptateur Oodle optionnel depuis un SDK local, ajouter `--oodle-sdk <chemin du SDK>` au script de build. Le module n'est pas installé avec le SDK core et son chargement reste explicite avec `--codec`. Voir [CODECS.md](docs/CODECS.md).

Pour l'interface, ajouter `-DCYUEDIFF_BUILD_UI=ON -DCYUEDIFF_CYUI_SOURCE_DIR=/chemin/vers/CyUI`. Aucun code du moteur Unreal n'est compilé ni redistribué.

## Utiliser le lecteur

```powershell
build-native/Release/cyuediff.exe inspect Content/BP_Player.uasset
build-native/Release/cyuediff.exe inspect Content/BP_Player.uasset --metadata
build-native/Release/cyuediff.exe git-diff D:/MonJeu HEAD~1 HEAD Content/M_Water.uasset
build-native/Release/cyuediff.exe git-diff D:/MonJeu HEAD~1 HEAD /Game/M_Water --project Game.uproject --no-engine
build-native/Release/cyuediff.exe material Content/M_Water.uasset
build-native/Release/cyuediff.exe material-diff before/MI_Water.uasset after/MI_Water.uasset
build-native/Release/cyuediff.exe preview Content/T_Water.uasset --out water.png
build-native/Release/cyuediff.exe image-diff before/T_Water.uasset after/T_Water.uasset --heatmap mask.png
build-native/Release/cyuediff.exe diff revisions/before/BP_Player.uasset revisions/after/BP_Player.uasset
build-native/Release/cyuediff.exe project MyGame.uproject --engine-root D:/Unreal/UE_5.5
build-native/Release/cyuediff.exe inspect /Game/BP_Player --project MyGame.uproject --engine-root D:/Unreal/UE_5.5
```

`--metadata` lit seulement les tables : aucune lecture des propriétés ni calcul d'empreinte. Sans cette option, les empreintes SHA-256 sont calculées par blocs de 64 Kio, sans charger les textures ou les meshes en mémoire. `--out resultat.json` écrit le résultat ; la CLI refuse d'écraser un fichier d'entrée ou un compagnon.

Le contexte lit `EngineAssociation` du `.uproject`, accepte un moteur explicite, ou cherche `CYUEDIFF_ENGINE_ROOT`, l'association des moteurs personnalisés dans le registre Windows, puis l'installation Epic standard. Il vérifie `Engine/Build/Build.version` et prépare les chemins `/Game`, `/Engine` et les contenus de plugins disponibles. Les plugins explicitement désactivés sont exclus ; la résolution complète des règles de plugins par plateforme n'est pas encore implémentée.

Un moteur cible fournit le contexte et les contenus. Il n'est jamais exécuté. Le format réellement lu reste celui enregistré dans chaque fichier : sélectionner UE 5.5 ne force pas un asset UE 5.3 à être interprété comme un asset UE 5.5. Pour comparer deux projets, utiliser `--before-project` et `--after-project` ; une racine moteur distincte pour chaque côté reste accessible via l'API C++.

## Intégrer le cœur

```cmake
add_subdirectory(path/to/CyUEDiff)
target_link_libraries(my_app PRIVATE CyUEDiff::Core)
```

```cpp
#include <cyuediff/core.hpp>

auto before = cyuediff::Package::open("before/BP_Player.uasset");
auto after  = cyuediff::Package::open("after/BP_Player.uasset");
auto diff   = cyuediff::compare(before, after);
std::cout << diff.to_json().dump(2);
```

Le module optionnel `CyUEDiff::Git` expose `GitRepository`, `GitRevision` et `compare_git`, sans dépendance CyUI. Il conserve chaque révision et ses compagnons dans un dossier temporaire, avec vérification des contenus LFS locaux. Les contextes `.uproject` et plugins sont lus dans leur commit, avec un moteur local sélectionnable par côté. Voir [GIT.md](docs/GIT.md), [PROJECTS.md](docs/PROJECTS.md) et `examples/git_consumer`.

Les classes et structures propres à une application peuvent être décodées via `<cyuediff/decoders.hpp>`, `SerializationProfile`, `AssetDecoder` et `DecoderRegistry`. Les callbacks reçoivent une entrée bornée ; les échecs conservent les empreintes opaques. Le registre passe aussi dans les lectures Git/LFS. Voir [DECODERS.md](docs/DECODERS.md) et `examples/decoder_consumer`.

L'installation CMake du cœur permet aussi `find_package(CyUEDiff CONFIG REQUIRED)`. Voir `examples/consumer`. Les options de lecture plafonnent les tables, chaînes, tailles de propriétés, nombres de propriétés et profondeurs.

## Couverture actuelle

- Packages classiques `.uasset` / `.umap`, tables de noms, imports, exports, versions personnalisées et références d'objets.
- Formats de fichier UE4 518–522 et UE5 1000–1018 : branches de lecture implémentées, **pas une certification de tous les assets de toutes ces versions**. Validation réelle initiale UE 5.3.2 et 5.5.4 ; fixtures synthétiques pour les changements de format.
- Exports intégrés ou séparés dans `.uexp`. Empreintes des compagnons présents `.uexp`, `.ubulk`, `.uptnl`, `.m.ubulk`, `.o.ubulk`, `.upayload`.
- Propriétés primitives, noms, chaînes, références, chemins souples, GUID, vecteurs/couleurs, structures balisées et tableaux dont les éléments sont décodables.
- Blueprint : objets, positions, propriétés, pins natifs et connexions ; modèle identifié par GUID et vue CyUI en lecture seule. Profils vérifiés UE 5.3/5.5, bytecode encore opaque. Voir [BLUEPRINTS.md](docs/BLUEPRINTS.md).
- Texture2D : métadonnées source, index de bulk data, aperçus JPEG/PNG et formats bruts pris en charge, sélection de mip brut, diff RGBA8 avec seuil et masque. Oodle est accessible via un adaptateur local explicite ; LZ4, cooked et ressources virtualisées demandent d’autres décodeurs. Voir [TEXTURES.md](docs/TEXTURES.md).
- StaticMesh : sources MeshDescription modernes, positions, instances de sommets, UV, normales, tangentes, couleurs, triangles et groupes de matériaux ; diff de géométrie, OBJ et vue filaire CyUI. Réglages et slots de matériaux disponibles dans les métadonnées. Voir [MESHES.md](docs/MESHES.md).
- Matériau : graphes d’expressions par GUID, entrées natives, masques, constantes, références de fonctions, reroutages nommés et paramètres d’instances identifiés par nom/association/index. Diff spécialisé et vue CyUI ; les parents, fonctions externes et shaders ne sont pas évalués. Voir [MATERIALS.md](docs/MATERIALS.md).
- Data assets : propriétés balisées et décodeurs C++ de projet enregistrables ; les types natifs sans schéma restent partiellement opaques. Leur catégorie ne peut pas toujours être déduite sans le schéma de la classe du projet.

Les tags non décodés et les données natives sont conservés comme régions opaques avec taille et SHA-256. Les empreintes opaques peuvent changer si les indices internes ou la sérialisation changent, même lorsque le contenu fonctionnel est équivalent. Les propriétés décodées utilisent des références résolues et des identités d'objets stables pour réduire ce bruit.

Les formats non versionnés, sérialisations licensee, packages compressés au niveau conteneur, pak, IoStore, ressources virtualisées et chargement des classes C++ de plugins nécessitent d'autres backends. Les fichiers sources et ressources externes ne sont pas chargés automatiquement.

## Lire un diff

Exemple vérifié en modifiant uniquement une copie d'un Blueprint UE 5.5 :

```json
{
  "op": "replace",
  "path": "/objects/TEST.EventGraph.K2Node_CallFunction_0/properties/NodePosX/value",
  "before": 256,
  "after": 320
}
```

| Statut | Interprétation |
| --- | --- |
| `identical` | Empreintes identiques du package et des compagnons présents, aucun changement observé |
| `changed` | Propriété, objet, dépendance ou région opaque différente ; le chemin précise laquelle |
| `binary-only` | Octets différents sans changement dans la représentation inspectée, par exemple une réorganisation des noms |
| `incomplete` | Export absent ou tronqué, notamment `.uexp` manquant ; aucune conclusion d'égalité |
| `unverified` | Empreintes désactivées : égalité binaire non vérifiable |

`completeInput` couvre les plages d'exports nécessaires à la lecture. Il ne certifie pas la présence des bulk data externes ou virtualisées : leur découverte exige les décodeurs natifs correspondants. Le périmètre binaire est le fichier principal et les compagnons présents à côté de lui.

Les niveaux `properties` et `partial-properties` décrivent uniquement la couverture du **flux de propriétés** ; une section `opaque` peut encore contenir des données natives. Chaque erreur de décodage d'export produit un diagnostic. Une erreur d'en-tête ou de référence structurelle interrompt la lecture.

`meshChanges`, `textureChanges` et `graphChanges` offrent des projections spécialisées du diff. `image-diff` compare explicitement les pixels décodés, avec ses propres métriques ; `mesh-diff` compare explicitement la géométrie source. Le diff de package ne décode pas ces données lourdes automatiquement.

Codes de sortie de `diff` : `0` identique, `1` changement, `2` erreur/incomplet/non vérifié. Les chemins utilisent JSON Pointer (`~0` et `~1` pour les caractères spéciaux).

## CyUI et Git

L'adaptateur UI se construit séparément et reste embarquable. L’exécutable optionnel `cyuediff_desktop` fournit un premier hôte Windows avec sélection de fichiers, chargement asynchrone, inspection, comparaison et aperçus source. Il utilise Direct3D 11 et CyUI, sans dépendance inverse du core. Voir [l’application desktop](docs/DESKTOP.md) et [l’intégration UI](docs/UI.md).

`textconv` exporte le snapshot pour une intégration Git optionnelle. Git LFS stocke les versions binaires ; le diff lisible vient de ce lecteur. Rien n'est configuré automatiquement dans un dépôt. Voir [l'intégration Git](docs/GIT.md).

La suite synthétique et les tests CyUI sont reproductibles avec le script de build. Le compte rendu initial des essais sur des fichiers locaux est dans [VALIDATION.md](docs/VALIDATION.md). La suite du développement est décrite dans [ARCHITECTURE.md](docs/ARCHITECTURE.md).
