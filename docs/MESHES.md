# Meshes statiques — tranche 0.4

Le core lit les sources `MeshDescription` des meshes statiques éditeur, avec des identifiants d'éléments conservés. Il fournit les positions, triangles, instances de sommets, UV, normales, tangentes, couleurs et noms de groupes de matériaux. Cette géométrie est décodée explicitement ; ouvrir les métadonnées d'un package ne charge pas tous ses sommets.

## Commandes

```powershell
cyuediff mesh Content/SM_Wall.uasset --out mesh-metadata.json
cyuediff geometry Content/SM_Wall.uasset --lod 0 --codec codec.dll --out geometry.json
cyuediff export-mesh Content/SM_Wall.uasset --codec codec.dll --out wall.obj
cyuediff mesh-diff before/SM_Wall.uasset after/SM_Wall.uasset --codec codec.dll --out diff.json
```

`--codec` est optionnel pour un buffer source méthode 0. Les sources Oodle demandent un décodeur explicite, décrit dans [CODECS.md](CODECS.md). Les commandes acceptent les options de contexte projet/moteur ; `mesh-diff` accepte aussi `--before-project` et `--after-project`.

`--object` sélectionne un StaticMesh par nom ou chemin. Il peut également désigner directement le chemin complet d'un sous-objet de bulk data, par exemple une source haute résolution disponible. `--lod` sélectionne un modèle source enregistré dans `SourceModels`. Un LOD généré sans source sérialisée n'est pas reconstruit automatiquement.

`export-mesh` exige `--out`. Il écrit un OBJ avec une position par sommet source, les UV du premier canal, les normales par instance et les groupes `section_<id>`. Les coutures sont conservées grâce aux indices distincts position/UV/normale. Il ne crée pas de MTL, ne charge pas les textures et n'applique pas les réglages de build. Les autres canaux UV et les couleurs restent accessibles dans le JSON et l'API.

Les sorties sont vérifiées avant écriture : elles ne peuvent pas écraser les packages, compagnons, descripteurs de projet ni le module de codec utilisé. Codes de sortie des diffs : 0 identique, 1 changé, 2 erreur ou source indisponible.

## Représentations et diff

- `cyuediff.meshes/1` décrit les StaticMeshes, leurs modèles source, réglages, slots de matériaux et sous-objets de bulk data. `diff` publie `meshChanges` ; `textconv` inclut cette projection dans le snapshot.
- `cyuediff.geometry/1` contient les éléments décodés, indexés par leurs identifiants sérialisés, ainsi que les bornes recalculées et la description des attributs.
- `cyuediff.mesh-diff/1` compare explicitement deux géométries décodées. Exemple de chemin vérifié : `/vertices/0/position/0`. Les UV utilisent `/instances/<id>/uvs/<canal>/<composante>` et les triangles référencent les instances de sommets.

La comparaison est exacte. Elle ne cherche pas à faire correspondre deux maillages après renumérotation, fusion de sommets ou changement de topologie. Les attributs conservent aussi des empreintes par canal ; des données de slots inactifs peuvent donc produire un changement malgré des triangles visibles identiques. Les changements de projection recoupent les données générales : ne pas additionner `changes` et `meshChanges` comme des événements indépendants.

Les groupes de polygones et noms de slots importés sont disponibles. L'affectation finale des sections de rendu peut être modifiée par les réglages de build et `SectionInfoMap`, encore partiellement opaque. Les coordonnées sont celles de la source, sans conversion d'axes, changement d'unité, réduction de LOD ou application de `BuildScale3D`. Les compteurs de cache Unreal peuvent décrire la géométrie reconstruite et différer des compteurs source.

## API et CyUI

```cpp
auto package = cyuediff::Package::open("SM_Wall.uasset");
cyuediff::MeshDecodeOptions options;
options.decoder = cyuediff::load_block_decoder("cyuediff_oodle.dll"); // si nécessaire
auto geometry = cyuediff::decode_mesh(package.mesh_source("", 0), options);
cyuediff::write_obj("wall.obj", geometry);
auto comparison = cyuediff::compare_meshes(geometry, geometry);
```

`ReadOptions::native_meshes` permet de désactiver la lecture native des descripteurs. Les limites par défaut sont 256 Mio de payload stocké ou brut, un million d'indices par conteneur et huit millions de composantes d'attributs. Les conversions JSON et la comparaison consomment de la mémoire supplémentaire. L'hôte peut réduire ces limites et travailler hors de sa boucle de rendu.

`MeshView` prépare un filaire CPU à partir d'un `shared_ptr<const MeshGeometry>`. Il utilise la navigation orbitale de `CyUI::Viewport`, avec Z vertical. `AssetView::set_mesh_geometry(geometry, après)` associe la géométrie décodée à un côté du document ; `set_packages` efface ces associations. La sélection du LOD et son décodage appartiennent à l'hôte. Publier une géométrie immuable après préparation.

La vue propose cadrage, orbite Alt + glisser, panoramique avec le bouton central et zoom à la molette. Elle ne requiert pas de cible de rendu 3D supplémentaire ; l'hôte garde son backend ImGui. L'affichage est limité à 100 000 arêtes avec indication de troncature. Il n'y a ni ombrage, rendu de matériaux, sélection de faces ni suppression des faces cachées. Les arêtes traversant le plan proche ne sont pas affichées.

## Profils et limites

Le lecteur vise le format moderne par conteneurs et attributs : versions personnalisées UE5 Main 51–119, Editor 40 et Release 44. Le descripteur bulk tient compte du drapeau de hash Enterprise ajouté en version 8 ; les versions Enterprise supérieures à 10 sont refusées. Les types d'attributs 0–6 sont pris en charge, y compris les tableaux à étendue variable ; `FTransform` et types futurs demandent une extension. La validation réelle concerne UE 5.3.2, avec des fixtures pour les variantes et les bornes.

Les formats `FRawMesh`/MeshDescription anciens, ressources cooked, géométrie Nanite, squelettes, skinning, animations et collisions ne sont pas décodés dans cette tranche. Un asset avec Nanite activé peut cependant fournir une MeshDescription éditeur lisible. Les payloads virtualisés, référencés ou absents restent explicites. Le hash BLAKE3 Unreal n'est pas recalculé ; les contrôles de header/CRC, tailles, références et SHA-256 ne constituent pas une validation de l'asset dans Unreal.

## Validation du 4 novembre 2026

La suite `mesh` apporte 47 vérifications : indices creux, positions, références, deux canaux UV, normales, groupes UTF16, attributs variables, bornes, troncatures, limites, profils, OBJ, diff et callbacks de décompression. Les 30 frames UI vérifient aussi l'émission des trois arêtes d'un triangle, dans les cinq thèmes et les deux langues. Il s'agit d'un test sans backend graphique, pas d'une revue visuelle de fenêtre native.

| Source locale UE 5.3.2 | Positions | Instances | Triangles |
| --- | ---: | ---: | ---: |
| `SM_wall_9` | 5 381 | 32 886 | 10 962 |
| `SM_rock_9` | 345 | 2 058 | 686 |
| `SM_Plant_01` | 2 210 | 7 800 | 2 600 |

Les trois sources ont été décodées avec l'adaptateur Oodle construit depuis le SDK local 2.9.12 et exportées en OBJ. Aucun lancement d'Unreal, aucune modification des originaux.

```powershell
python scripts/validate_mesh.py --asset chemin/SM_wall_9.uasset --codec build-native/engine-codecs/Release/cyuediff_oodle.dll
```

Sur le mur, le script vérifie l'identité d'une comparaison avec elle-même, le déplacement d'une coordonnée, un réglage de lightmap sans changement de géométrie, les indices de l'OBJ et six erreurs de CLI. La copie de géométrie reçoit un nouveau buffer brut avec tailles et CRC mis à jour ; son identité Unreal n'est pas recalculée. Ce test du lecteur n'est pas un outil de réécriture d'assets validé dans Unreal.

Les rapports sont dans `build-native/mesh-validation/report.json` et `corpus.json`. `wall-wireframe.png` est une projection de validation générée depuis le JSON du core, et non une capture de CyUI. Le SDK installé a aussi décodé et exporté le mur depuis l'exemple client, sans dépendance CyUI. Aucun asset du corpus n'est redistribué.

Référence : [API FMeshDescriptionBulkData d'Epic](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/MeshDescription/FMeshDescriptionBulkData).
