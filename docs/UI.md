# Vues CyUI

Un hôte Windows autonome est disponible depuis la version 0.9 avec `--desktop` : voir [DESKTOP.md](DESKTOP.md). La bibliothèque ci-dessous reste indépendante de cet hôte.

Construire avec `CYUEDIFF_BUILD_UI=ON`, puis lier `CyUEDiff::UI`. Le checkout de CyUI est fourni par `CYUEDIFF_CYUI_SOURCE_DIR`, ou par l'hôte qui a déjà défini les cibles `CyUI::Diff`, `CyUI::Nodes`, `CyUI::Images` et `CyUI::Viewport` et son ImGui commun.

```cpp
#include <cyuediff/asset_view.hpp>

// Après ImGui::CreateContext(), une fois :
cyui::Config config;
config.themeKey = "mid-grey";
cyui::apply(config);
cyui::loadFonts();
cyuediff::AssetView::register_translations();

// État persistant de la vue, par document :
cyuediff::AssetView view;
// Charger les packages hors de la boucle de rendu :
view.set_packages(std::make_shared<cyuediff::Package>(
    cyuediff::Package::open("before.uasset")),
    std::make_shared<cyuediff::Package>(
    cyuediff::Package::open("after.uasset")));

// Dans chaque frame ImGui, dans une fenêtre de l'hôte :
view.draw("document-1");
cyui::renderNotifications();
```

La vue offre une liste d’objets filtrable, les propriétés JSON et les régions opaques, les données natives décodées et la sélection avant/après de l’inspecteur, une vue de graphes Blueprint avec sélection avant/après, puis une comparaison CyUI unifiée ou côte à côte. `BlueprintView` peut aussi être utilisée seule après `set_package(package)`. Les cinq thèmes et les langues anglais/français sont gérés par CyUI. Les noms d'assets et propriétés restent des données utilisateur non traduites.

Le moteur d'interface est volontairement un composant embarquable. L'application hôte garde la fenêtre, le backend graphique, les chargements, les erreurs et les ressources GPU. La vue `MeshView` affiche désormais la géométrie source en filaire navigable. Les aperçus de textures passent par `TextureView` et une ressource GPU appartenant à l’hôte.

Le test `ui_smoke` crée des frames ImGui sans renderer sur les cinq thèmes, en anglais et français. Il vérifie l'intégration et la production de commandes de dessin ; il ne remplace pas une revue visuelle dans une application native.

## Images

`AssetView` inclut un onglet Textures avec sélection avant/après. Après chargement du package, l'hôte appelle `decode_texture`, charge le résultat RGBA8 sur son backend, puis fournit l'identifiant et les dimensions :

```cpp
// upload_rgba8 est une fonction du backend de l'application hôte.
auto rgba = cyuediff::decode_texture(package->textures.at(0));
auto texture_id = upload_rgba8(rgba.width, rgba.height, rgba.pixels.data());
view.set_texture_preview(package->textures.at(0).object_path,
                         {texture_id, int(rgba.width), int(rgba.height)});
// Passer true comme troisième argument pour la version après.
```

`clear_texture_preview(chemin, après)` retire une association GPU avant de libérer sa ressource. `set_packages` efface toutes les anciennes associations GPU. Les ressources doivent rester valides tant que les vues les utilisent ; l'hôte les libère après avoir retiré ces associations. La vue seule fonctionne avec `TextureView::set_source`, `set_preview`, `clear_preview` et `draw`. `set_preview` refuse les dimensions invalides ou un identifiant nul. Le zoom, le panoramique et le cadrage utilisent `CyUI::Images`. Le masque de comparaison est une autre `ImageRgba8`, que l'hôte peut afficher dans une seconde vue.

L'appel de rendu n'effectue aucun décodage ni transfert GPU. Les sources non prises en charge montrent leurs diagnostics. Les tests sans renderer utilisent un identifiant GPU factice : ils valident la production des commandes, pas l'affichage effectif de l'image par un backend.

## Meshes

Décoder avec `decode_mesh` hors du rendu, puis publier un `shared_ptr<const MeshGeometry>` immuable. `AssetView::set_mesh_geometry(geometry, après)` associe un côté du document à cette géométrie. `set_packages` retire les anciennes associations. L'hôte choisit le mesh et le LOD avant ce chargement.

La vue indépendante `MeshView` accepte `set_mesh(geometry)` et `draw`. Elle prépare les arêtes avant affichage et utilise `CyUI::Viewport` pour orbite, panoramique et zoom. Elle trace le filaire dans les commandes ImGui, sans cible GPU 3D supplémentaire ni rendu de matériaux. La limite d'affichage est 100 000 arêtes. Les détails sont dans [MESHES.md](MESHES.md).

## Matériaux

`MaterialView` affiche les graphes d’expressions via `CyUI::Nodes` et les paramètres d’instances dans un tableau. `set_package` prépare les données, `select_object(path)` choisit le matériau ou l’instance, `draw` affiche sans lecture de fichier. `AssetView` inclut un onglet Matériaux avec sélection avant/après. Les sorties inférées et données incomplètes restent signalées. Il s’agit des graphes et valeurs sérialisés, sans rendu ni compilation du matériau. Voir [MATERIALS.md](MATERIALS.md).

## Révisions Git

L’hôte peut charger un `GitComparison` via `CyUEDiff::Git`, puis fournir ses packages aux vues. Il conserve ce résultat pendant toute la durée d’utilisation des payloads temporaires et effectue les appels Git hors du rendu. Un côté absent lors d’un ajout ou d’une suppression est représenté par un pointeur nul ; passer `true` en troisième argument de `set_packages(before, after, true)` active alors la comparaison avec le côté absent. Les vues inspectent par défaut le côté présent et affichent un badge Ajouté/Supprimé. Voir [GIT.md](GIT.md).

## Navigation depuis l’hôte

`AssetView::select_tab(AssetTab::comparison)` demande la sélection de l’onglet à la prochaine frame. Les valeurs sont `properties`, `blueprint`, `materials`, `textures`, `meshes` et `comparison` ; ce dernier exige deux packages ou `compare_missing_side=true` avec un seul côté présent. À chaque nouveau document comparé, le diff est positionné sur le premier changement. `draw("document-1", false)` masque le sélecteur de langue de la vue si l’hôte le fournit déjà dans son menu. Les appels existants à `draw()` gardent leur comportement. Les textes d’inspection, y compris les données natives des extensions, sont préparés dans `set_packages`.

## Catalogue de contenus — 0.11

`CatalogView` dans `<cyuediff/catalog_view.hpp>` reste indépendant de l’hôte Windows et de Git. L’hôte fournit un `shared_ptr<const vector<CatalogItem>>`, construit avec `merge_catalogs`, via `set_items(items, revisions)`. La vue conserve la recherche, le filtre asset/carte et la sélection. `draw` renvoie une intention `CatalogAction` (`open`, `use_before`, `use_after`) ; l’hôte charge les fichiers hors du rendu. Aucun parcours de dossiers ou appel Git ne se produit dans `draw`.

`set_filter` prépare une recherche et `select` choisit un indice du catalogue non filtré. `selected` indique cet indice, ou `numeric_limits<size_t>::max()` en l’absence de sélection. Une recherche qui masque l’élément sélectionné efface la sélection. Les correspondances sont mises en cache et seules les lignes visibles sont rendues. `register_translations` fournit les contrôles en français avec anglais par défaut. Le desktop l’utilise dans son navigateur asynchrone. Voir [CATALOG.md](CATALOG.md).
