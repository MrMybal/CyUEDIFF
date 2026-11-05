# CyUEDiff

[English](README.md) · **Français**

Cœur C++17 pour inspecter des packages Unreal **sans lancer Unreal Editor**, produire une représentation stable et comparer deux versions. Une bibliothèque de vues CyUI reste séparée du lecteur.

Cette version 0.101 lit les objets et propriétés sérialisées, reconstruit les graphes Blueprint et ajoute les sources de textures et de meshes statiques. Le core fournit des aperçus d'images, la géométrie source, des graphes de matériaux et leurs paramètres, des diffs spécialisés et un export OBJ. Les vues CyUI restent séparées. Un adaptateur Oodle local optionnel débloque les sources comprimées, sans lancer Unreal. Le module Git/LFS matérialise les révisions et leurs compagnons depuis les objets locaux, puis les compare avec le core. Les descripteurs de projet et plugins historiques, les installations moteur par côté et leurs différences de contexte sont accessibles. Un registre C++ permet désormais à l’application hôte de fournir ses décodeurs de structures et de fins natives, sélectionnés par type et versions. Une première application desktop Windows/CyUI ouvre et compare désormais ces fichiers dans une fenêtre native, avec chargement en arrière-plan. Elle compare aussi les révisions Git/LFS locales, les ajouts/suppressions et les changements de contexte projet. Un catalogue de métadonnées et un navigateur de contenus permettent de rechercher et sélectionner les packages du projet ou des révisions Git sans les décoder pendant le parcours.

Les jalons 0.68–0.72 ajoutent la recherche et l’inspection avant/après des nœuds Blueprint/Material, la navigation entre nœuds modifiés, le filtrage des paramètres d’instances, le cadrage de la géométrie modifiée et des pixels modifiés en LDR/HDR. Les sélections et filtres sont conservés dans les workspaces explicites. Voir [le guide de revue visuelle](docs/VISUAL_REVIEW.md).

La version 0.77 ajoute l’inspection des collisions enregistrées : sphères, boîtes, capsules, capsules effilées et données sources convexes. Les commandes collision/collision-diff et l’onglet Collisions comparent les formes nommées et conservent la sélection dans les workspaces. Les données physiques natives restent opaques ; voir [les contrats](docs/COLLISIONS.md).

Le mode **Conteneurs** permet de parcourir les membres Pak/IoStore, de les filtrer et d’inspecter leurs octets décodés. Un profil UE explicite et un TOC global optionnel donnent accès aux objets et références Zen ; les corps natifs restent opaques. Cette interface fonctionne aussi sans Git. Voir [le guide desktop](docs/DESKTOP.md#pakiostore-browser--026).

Le navigateur d’historique Git permet maintenant de choisir les commits avant/après, puis de comparer l’asset courant ou d’ouvrir leur catalogue. L’API et la commande `git-history` fournissent aussi cet historique paginé, sans checkout ni téléchargement.

Les jalons 0.20–0.22 ajoutent les références soft, interface et GUID lazy aux propriétés cooked prises en charge, un inspecteur avec recherche et aperçu de la sélection, puis la sauvegarde du thème, de la langue et des options d’aperçu/contenu moteur. L’anglais reste la langue par défaut ; le français est intégré et le choix est conservé. Les chemins et documents précédents ne sont pas enregistrés. `--no-preferences` désactive cette sauvegarde ; voir [le guide desktop](docs/DESKTOP.md).

La version 0.17 ajoute le chargement explicite de schémas USMAP et le décodage ciblé de propriétés cooked non versionnées. Les associations classe/schéma/décalage doivent correspondre au build ; les types non pris en charge et les données natives restent opaques. Voir les [contrats et limites des schémas](docs/SCHEMAS.md).

Les collisions disposent d’un aperçu 3D avant/après, d’une sélection par clic, d’un filtre des formes modifiées et d’une caméra conservée dans les fichiers de travail. La commande collision-preview exporte les lignes en JSON pour d’autres applications ; les données absentes restent signalées.

Les objets Skeleton et SkeletalMesh disposent d’une lecture des os et poses locales enregistrés, de différences par nom et d’un inspecteur de hiérarchie dans CyUI. Les commandes skeleton, skeleton-diff et skeleton-pose exposent ces données sans Unreal. La commande skeleton-pose-diff compare aussi les déplacements calculés des articulations, avec une tolérance de position. Les bounds et emplacements de matériaux natifs des SkeletalMesh sont conservés. CyUI propose aussi un aperçu 3D avant/après des poses de référence, avec sélection des articulations et caméra conservée dans le workspace. La géométrie source squelettique, le skinning et les animations restent à développer.

Les poses de retargeting enregistrées dans les Skeleton sont aussi lisibles et comparables par nom. Le CLI, le SDK et CyUI permettent de choisir une source différente avant/après ; les choix sont conservés dans le workspace. Une source absente ou sans correspondance vérifiée reste indisponible. Cela ne lance ni le retargeting Unreal ni les animations.

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


## Mise à jour 0.14

Le code original est désormais sous [licence MIT](LICENSE). Les licences tierces restent applicables.

Les diffs visuels surlignent les nœuds Blueprint/matériau, les paramètres enregistrés, les arêtes et les pixels modifiés. L'inspecteur affiche les propriétés typées avant/après. Les deux côtés peuvent utiliser des contextes projet/moteur distincts. Le chargement dispose d'une annulation coopérative, d'une progression et d'un cache vérifié par empreintes des packages et compagnons.

Les sources HDR FP16/FP32/RGBE se comparent en flottants avant conversion d'aperçu. La lecture des textes localisés, Maps/Sets et transformations natives est étendue. Le corpus CyTrace fourni ajoute 150 packages sources vérifiés ; les exports partiels restent signalés. Voir les guides anglais [HDR](docs/HDR.md), [propriétés](docs/PROPERTIES.md), [exécution](docs/RUNTIME.md) et [distribution](docs/DISTRIBUTION.md).

La CI Windows/Linux est préparée et les archives exécutables/SDK se construisent avec CPack. Les fichiers restent locaux tant qu'aucun push n'est demandé. La lecture cooked native complète, les ressources virtualisées, meshes squelettiques, collisions et données Nanite, ainsi que la sauvegarde complète de la disposition des fenêtres restent à réaliser.

## Ajouts 0.15 : conteneurs Pak / IoStore

Le Core lit les conteneurs Pak 10/11 avec index de répertoires complet et IoStore TOC 3–5 avec leurs partitions UCAS. La CLI fournit l’inventaire, la lecture d’un membre, les empreintes des contenus décompressés et leur comparaison. Un codec reste explicite pour les blocs compressés. Le build CyTrace fourni valide 6 219 membres, avec un second lecteur indépendant et des sources inchangées. Le décodage sémantique des objets cooked/Zen reste à implémenter ; les empreintes BLAKE3 stockées dans IoStore et les signatures ne sont pas vérifiées. Contrats et exemples dans [CONTAINERS.md](docs/CONTAINERS.md).

## Ajouts 0.16 : tables d’objets cooked/Zen

Le Core et la CLI lisent les tables de noms/imports/exports Zen, résolvent les classes avec la table globale et comparent les corps opaques par objet. Les profils UE 5.3 et 5.5 sont explicites ; le profil 5.3 est validé sur les 3 507 packages et 7 502 exports du build CyTrace. Les propriétés non versionnées et les payloads natifs cooked restent à décoder avec leurs schémas. Voir [ZEN.md](docs/ZEN.md).

## Ajouts 0.17 : schémas USMAP explicites

Le Core charge les formats de base USMAP 0–4 et décode les types simples, tableaux, tableaux statiques, références, noms, chaînes et structures réfléchies déclarées explicitement. La CLI accepte `--schema` et `--after-schema` pour les commandes Zen. Les diffs nomment les propriétés lisibles modifiées et conservent l’empreinte complète des exports. Les extensions USMAP, les autres types non versionnés et les données natives restent à traiter. Les nouvelles valeurs sont vérifiées sur des fixtures ; le corpus CyTrace ne fournit pas de schéma correspondant à son build. Contrats et exemples dans [SCHEMAS.md](docs/SCHEMAS.md).

## Ajouts 0.18 : extensions et identités des schémas

Le Core lit CEXT et les extensions PPTH, MODL, EATR et ENVP : chemins de modules, attributs des types et valeurs d’énumération sur 64 bits. Les types homonymes restent distincts grâce à leur identité qualifiée. Les noms courts ambigus, versions inconnues et extensions endommagées sont refusés. Ces métadonnées n’autorisent pas automatiquement un décodeur natif ; la validation sur un schéma correspondant au build reste nécessaire. Voir [SCHEMAS.md](docs/SCHEMAS.md).

## Ajouts 0.19 : Map et Set cooked

Les propriétés non versionnées Map et Set exposent les suppressions et ajouts/remplacements sérialisés, avec des clés stables dans les diffs. Les tableaux et structures réfléchies pris en charge peuvent être imbriqués. Les conteneurs vidés par un masque de zéros se distinguent des deltas vides ; le marqueur de remplacement complet des Map est activé pour le profil UE 5.5. Les valeurs héritées ne sont pas reconstituées. Les clés en double et types non pris en charge conservent l’export opaque, tandis que les limites et l’annulation interrompent l’opération. Validation sur fixtures uniquement faute de schéma correspondant au corpus réel. Voir [SCHEMAS.md](docs/SCHEMAS.md).

La version 0.28 compare deux conteneurs Pak/IoStore dans l'interface : appariement par chemin ou ID, ajouts/suppressions, comparaison explicite des octets décodés et inspection des membres avant/après. Les objets Zen sont appariés par chemin avec leurs classes, références et empreintes de corps opaques ; chaque côté conserve son profil et sa table de scripts. Les membres communs non lus restent « À vérifier ». La comparaison globale est limitée à 512 Mio décodés, avec 64 Mio par membre ; les gros conteneurs restent consultables sans lancer ce scan. Voir [le guide desktop](docs/DESKTOP.md).

La version 0.31 ajoute les contextes de schéma USMAP explicites dans le desktop, avec sélection indépendante avant/après, propriétés nommées et typées, recherche, diff et provenance. Le Core expose un lecteur de manifestes réutilisable, également utilisé par la CLI. Les valeurs d'un côté non décodé restent inconnues ; elles ne deviennent pas des suppressions. La validation des nouvelles propriétés desktop utilise des données synthétiques : le corpus CyTrace ne fournit toujours pas de USMAP compatible.


Les jalons 0.32–0.34 ajoutent jusqu'à huit documents en onglets, avec contextes projet/moteur et vues indépendants, une file de décodage commune, duplication, fermeture des autres documents et réouverture des huit derniers onglets fermés pendant la session. Les filtres, sélections et aperçus GPU restent disponibles lors des changements d'onglet. La duplication et la réouverture relisent les sources ; les fichiers d’espace de travail ajoutés en 0.36 permettent de restaurer les contextes après redémarrage. Les commandes sont disponibles en anglais et en français.

La version 0.40 passe 47 suites CTest et 2 251 vérifications, plus 620 frames UI sans renderer. Le Core seul passe 23 suites et le desktop sans Git 39. Dix-neuf nouvelles captures natives vérifient les inspecteurs et la navigation restaurés dans les cinq thèmes et les deux langues, avec une texture, un Blueprint, un matériau et un mesh réels. Les empreintes des sources restent inchangées.

Les jalons 0.35–0.37 ajoutent les fichiers d’espace de travail JSON, les commandes Ouvrir/Enregistrer/Enregistrer sous et la restauration des documents, contextes projet/moteur, révisions Git et vues initiales. L’enregistrement est explicite et atomique. Les fichiers invalides sont préservés et un import invalide conserve les onglets ouverts. Les caches, aperçus GPU et modules de codec ne sont pas enregistrés. Voir [les espaces de travail](docs/WORKSPACES.md), [la validation](docs/VALIDATION.md) et [les commandes desktop](docs/DESKTOP.md).

Les jalons 0.38–0.40 sauvegardent aussi les filtres et sélections par identité, les choix avant/après, les inspecteurs de l’objet courant, le pan/zoom des graphes et textures, ainsi que la caméra du mesh. Les onglets non affichés conservent leur état en attente. Le format v2 lit les anciens espaces de travail v1 ; l’application relit toujours les sources. Les jalons 0.41–0.43 ajoutent le format v3 : largeur latérale, proportions des panneaux/colonnes et sections dépliées. La géométrie des fenêtres et les gestes actifs restent hors du format.

Les jalons 0.44–0.49 ajoutent 17 formats explicites de structures natives mathématiques/couleurs/GUID, les textes cooked localisés, invariants ou issus de tables, les delegates simples/multicast et les références faibles persistantes. Les changements de composantes, sources, fonctions et cibles apparaissent dans les diffs et l’inspecteur, avec restauration des sélections depuis un workspace. Ces lectures exigent des schémas correspondant au build ; la validation des nouvelles valeurs repose sur des fixtures synthétiques. Voir les [contrats des schémas](docs/SCHEMAS.md).

Les jalons 0.50–0.52 ajoutent les propriétés optionnelles du profil UE 5.5 et les chemins de champs modernes (`FieldPath`) des profils pris en charge. Les diffs distinguent une option non définie d’une valeur définie à zéro ou null et montrent les segments/propriétaires modifiés. L’inspecteur, les espaces de travail et le SDK utilisent ces valeurs ; les anciens formats non certifiés conservent leur empreinte opaque.

Les cinq jalons 0.53–0.57 ajoutent les chaînes UTF-8/ANSI prises en charge, les transformations de texte imbriquées, les modèles à arguments nommés/ordonnés et les formats de nombre/pourcentage/monnaie. Les lecteurs classique et cooked partagent ces valeurs structurées. Le diff montre la source, les arguments, la culture et les options d’arrondi enregistrés ; aucun formatage/localisation n’est exécuté. Les nouvelles propriétés sont accessibles depuis le CLI, l’inspecteur et le SDK.

La version 0.62 ajoute les textes de date/heure, les arguments de format versionnés et les structures natives temporelles et de volumes englobants. Les valeurs stockées sont comparables dans le CLI et l’inspecteur, avec conservation des sélections dans les espaces de travail.

La version 0.67 conserve les générateurs de texte inconnus par identité et empreinte, étend les textes des pins Blueprint et ajoute les volumes englobants classiques, les matrices et les plans. Le Core propose 29 dispositions natives explicites.

Les métadonnées d’animation des Skeleton comprennent désormais les conteneurs de courbes natifs pris en charge, leurs indicateurs matériau/morph target, les liens vers les os, les limites de LOD présentes et les noms des marqueurs de synchronisation. Les commandes `skeleton-animation` et `skeleton-animation-diff` et le mode « Métadonnées d’animation » de l’onglet Skeleton proposent une comparaison nommée avec inspection typée. L’ordre des marqueurs est normalisé en conservant leur nombre d’occurrences. Les données illisibles ou retirées du fichier restent signalées comme incomplètes ; aucune animation n’est évaluée.
