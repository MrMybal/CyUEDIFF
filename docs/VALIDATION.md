# Validation initiale — 3 novembre 2026

Environnement vérifié : Windows x64, MSVC 19.40, CMake 3.31.6, configuration Release. Aucun lancement d'Unreal Editor.

## Vérifications reproductibles

`python scripts/build.py --desktop --cyui <checkout CyUI>` compile le cœur, la CLI, la bibliothèque UI et l’application Windows puis lance :

- `core` : **64 vérifications**, dont six transitions de format, tags anciens/récents, tableaux de structures, valeurs entières/booléennes/chaînes, SHA-256, exports séparés, changement de propriété, changement opaque, réorganisation des noms, références cycliques, version future, compte négatif, pointeur LFS, troncatures, montages de projet et moteur explicite.
- `decoders` : **110 vérifications** des profils, entrées bornées, structures natives, tableaux, fins d’exports, priorité explicite, erreurs et JSON de résultat.
- `descriptor` : **15 vérifications** du JSON de descripteurs Unreal, commentaires, virgules finales de tableaux, chaînes, refus de syntaxe et limites de profondeur.
- `blueprint` : **30 vérifications** des pins natifs, connexions, identités, profils et bornes de lecture.
- `mesh` : **47 vérifications** de géométrie, références, profils, bornes et callbacks de décompression.
- `texture` : **56 vérifications** des sources, index de payload, formats, mips, pixels, limites et diagnostics.
- `material` : **81 vérifications** des entrées natives, constantes, profils, identités de graphes, connexions, paramètres d’instances et valeurs opaques.
- `git` : **57 vérifications** de révisions, compagnons, LFS local, durée de vie, chemins, contextes Git et limites ; nécessite Git 2.45+ pour être exécuté.
- `git_project` : **56 vérifications** des contextes historiques, plugins et moteurs sélectionnés par côté.
- `desktop_model` : **28 vérifications** du chargement, contexte, erreurs, reprise et priorité des demandes asynchrones.
- `desktop_git` : **27 vérifications** du chargement de révisions, LFS local/externe, compagnons, ajouts/suppressions, renommage, contexte historique, durée de vie et conservation du document après erreur.
- `desktop_renderer` : fenêtre cachée rendue avec le backend Direct3D 11/WARP réel.
- `ui_smoke` : **30 frames ImGui**, cinq thèmes et langues anglais/français, avec inspecteur, comparaison, graphes Blueprint et matériau, tableau d’instance, image et filaire mesh préparés. Test sans renderer : production des commandes de dessin, sans revue visuelle native.

Les treize tests passent (571 vérifications, 30 frames UI sans renderer et un test de rendu natif). Les fixtures binaires synthétiques sont générées pendant le test et supprimées après réussite. Aucun asset propriétaire n'est inclus dans les sources.

## Essais sur six fichiers réels locaux

| Fichier | Version enregistrée | Objets | Flux de propriétés décodé | Flux partiel | Objet opaque |
| --- | --- | ---: | ---: | ---: | ---: |
| `TEST.uasset` — Blueprint | 5.5.4 | 24 | 21 | 3 | 0 |
| `MI_Water.uasset` — material instance | 5.3.2 | 4 | 3 | 1 | 0 |
| `T_caustic.uasset` — texture | 5.3.2 | 3 | 1 | 1 | 1 |
| `SM_wall_9.uasset` — static mesh | 5.3.2 | 7 | 4 | 2 | 1 |
| `BP_Volume_Fog.uasset` — Blueprint | 5.3.2 | 104 | 100 | 4 | 0 |
| `MF_Water_Waves.uasset` — material function | 5.3.2 | 10 | 7 | 3 | 0 |
| **Total** | | **152** | **136** | **14** | **2** |

« Flux décodé » ne signifie pas « asset intégralement décodé » : les régions natives restent opaques. Les deux objets entièrement opaques sont des sous-objets de données d'import de texture/mesh, dont le format ajoute une sérialisation spécifique. Les paramètres du matériau comprennent 15 valeurs scalaires, 2 références de textures et 9 valeurs vectorielles décodées.

Les rapports complets de cette session sont dans `build-native/sample-0.json` à `sample-5.json` et `build-native/corpus-validation.json`. Les petits fichiers de ce corpus ne constituent pas un benchmark représentatif de production. Aucun test réel de DataAsset, de map, d'UE4 ou d'UE5 5.6+ n'a été effectué.

## Diff contrôlé d'un Blueprint réel

Une copie dans le dossier de build a reçu une modification binaire ciblée de la valeur entière de `NodePosX`. L'original est resté intact. La CLI a renvoyé exactement un changement :

```json
{
  "op": "replace",
  "path": "/objects/TEST.EventGraph.K2Node_CallFunction_0/properties/NodePosX/value",
  "before": 256,
  "after": 320
}
```

Code de sortie : `1`, statut `changed`. Le résultat est conservé dans `build-native/real-diff-validation.json`. L'empreinte du fichier calculée par le cœur a été comparée avec Python `hashlib.sha256`.

Le même fichier comparé à lui-même renvoie `identical`, code `0`. Une tentative d'utiliser ce fichier d'entrée comme sortie JSON est refusée, code `2`, et son empreinte reste inchangée.

## Réutilisation du SDK

Le cœur a été installé dans `build-native/install`, puis l'exemple `examples/consumer` a été configuré dans un autre build avec `find_package(CyUEDiff CONFIG REQUIRED)`. Cette application cliente, sans CyUI, a lu les 24 exports du Blueprint UE 5.5.

Le contexte du projet local a aussi été ouvert avec son installation source UE 5.5.4 explicite : lecture de `Build.version`, résolution de `/Game/TEST`, préparation des contenus moteur/plugins.

Ces essais établissent le fonctionnement de cette tranche sur les entrées indiquées. Ils ne valident pas une compatibilité universelle des formats Unreal ni le décodage universel des pixels, de la géométrie, de Nanite, d’IoStore ou des ressources virtualisées. La validation des pins ajoutée en 0.2 est décrite ci-dessous.

## Suite Blueprint 0.2

La suite ajoute 30 vérifications Blueprint (94 avec les 64 vérifications du cœur) et inclut la vue `CyUI::Nodes` dans les 30 frames UI. Les graphes réels et essais de pins sont détaillés dans [BLUEPRINTS.md](BLUEPRINTS.md). Les décodeurs de pins de cette tranche sont implémentés et testés sur les deux fichiers indiqués ; le bytecode reste opaque.

## Suite Texture 0.3

La lecture des tableaux d'enums anciens décode désormais `Source.LayerFormat` de la texture réelle. La table du corpus ci-dessus conserve les mesures de la tranche initiale ; les rapports actualisés sont dans les mêmes chemins de build.

La suite Texture apporte 56 vérifications et la vue `CyUI::Images` est exercée dans les 30 frames UI. Le script `scripts/validate_texture.py` vérifie un réglage sans changement de pixels, une modification de source JPEG avec masque, sept refus de CLI et la préservation de l'original. Les aperçus réels JPEG 512 × 512 et PNG 4096 × 4096 et le refus explicite Oodle sont détaillés dans [TEXTURES.md](TEXTURES.md).

La consommation du cœur installé est aussi vérifiée en décodant et écrivant une texture PNG depuis l'exemple client sans CyUI.

## Suite Mesh / codecs 0.4 — 4 novembre 2026

La géométrie source de trois meshes statiques UE 5.3.2 a été lue et exportée : mur (10 962 triangles), rocher (686) et plante (2 600). L'adaptateur Oodle a été construit séparément depuis le SDK 2.9.12 du moteur local. Aucun éditeur n'a été lancé.

Le script `scripts/validate_mesh.py` vérifie une coordonnée source modifiée sur copie, un réglage de lightmap sans changement de géométrie, l'export OBJ, six refus de CLI et les empreintes intactes de l'original. Le contenu BLAKE3 de cette copie de test n'est pas recalculé. Le lecteur n'est pas un outil d'écriture Unreal. Rapports et limites : [MESHES.md](MESHES.md).

La texture Oodle `T_Noise` a aussi produit un aperçu 2048 × 2048 avec cet adaptateur. Le client CMake séparé a consommé le core installé, décodé un mesh et exporté ses 10 962 triangles, sans CyUI ; le module de codec reste fourni explicitement et n'est pas installé avec le SDK.

Les tests UI incluent un filaire, avec trois arêtes projetées vérifiées dans les 30 frames. Ils ne valident pas un backend graphique ni une application desktop autonome.

## Suite Matériaux 0.5 — 4 novembre 2026

La suite ajoute 81 vérifications du décodage des huit types d’entrées, profils FName/FString, masques et constantes, identités GUID, collections, ports de fonctions, reroutages nommés, connexions invalides, ordre des paramètres, associations/indices, surcharges statiques et conservation des valeurs opaques. Les tests UI exercent un graphe de matériau connecté et le tableau d’une instance dans les 30 frames sans renderer.

Six graphes locaux UE 5.3.2 ont été inspectés, de 2 à 58 nœuds, ainsi que les 29 paramètres enregistrés de `MI_Water`. `M_Master` reste partiel à cause de quatre noms de canaux en FText opaques. Les ports de sortie provenant de valeurs de classe omises sont explicitement marqués inférés.

`scripts/validate_material.py` vérifie un recâblage sur copie, une valeur scalaire de `100` à `101`, la cohérence avec `diff.materialChanges`, les connexions non résolues conservées, le refus d’égalité d’un graphe incomplet, un profil futur opaque et trois refus de CLI. Les empreintes des originaux restent intactes. Le core seul est construit sans CyUI ni codec et le client CMake séparé consomme également les projections de matériaux du SDK installé. Détails et limites : [MATERIALS.md](MATERIALS.md).

## Suite Git/LFS 0.6 — 4 novembre 2026

Le module Git séparé est vérifié avec Git for Windows 2.45.1, sans besoin d’exécuter Git LFS. La suite ajoute 56 vérifications : commits et tags, noms Unicode, `.uexp` LFS distinct par révision, compagnon retiré, ajout/suppression/renommage, checkout sale intact, références de remplacement ignorées, dépôt bare, worktree lié, Git SHA-256, stockage relatif/explicite, contenu manquant/corrompu, budgets, timeout et nettoyage des révisions partagées.

Le script `scripts/validate_git.py` place deux versions réelles de `MI_Water` dans un dépôt jetable avec cache LFS externe. Le diff via Git reproduit le diff direct, dont un unique changement spécialisé de paramètre de `100` à `101`. Sept refus de CLI sont vérifiés sous Windows, dont les chemins de métadonnées avec casse différente. Les originaux, le checkout modifié, l’index et la configuration restent intacts ; les dossiers temporaires sont libérés.

Le SDK `CyUEDiff::Git` est aussi installé et consommé via `find_package` dans `examples/git_consumer`, sans CyUI, avec le même résultat réel. Un build distinct avec `CYUEDIFF_BUILD_GIT=OFF`, sans CyUI ni Oodle, conserve les cinq suites du core. Les limites de cette tranche sont décrites dans [GIT.md](GIT.md).

## Contextes historiques 0.7 — 4 novembre 2026

Les 56 vérifications `git_project` couvrent les descripteurs du bon commit, leur stockage LFS, `/Game`, les plugins et contenus moteur, les associations/racines distinctes, la désactivation explicite, le masquage, les doublons, les moteurs locaux copiés et empreintés, les cartes, catalogues incomplets, limites, checkout sale, worktree et dépôt bare. La suite `descriptor` apporte 15 vérifications supplémentaires du dialecte JSON réellement observé dans les plugins moteur.

Le Blueprint réel `TEST.uasset` et des copies du `.uproject` local ont servi à vérifier un changement de contexte sans changement de package. Dix refus de CLI, la préservation des originaux, de l’index et du checkout, et le nettoyage des copies sont contrôlés par `scripts/validate_git_project.py`. Le catalogue de l’installation source UE 5.5.4 locale est également inspecté sans lancer Unreal : 666 documents, 283 racines de contenu, `complete:true`. Ces racines ne constituent pas une évaluation des plugins actifs d’une cible. Le SDK installé est consommé depuis `examples/git_consumer` pour reproduire ce diff contextuel. Le build sans Git/CyUI/Oodle reste vérifié séparément. Détails : [PROJECTS.md](PROJECTS.md).

## Registre de décodeurs 0.8 — 4 novembre 2026

La suite `decoders` apporte 110 vérifications : contrats invalides, intervalles UE4/UE5 et GUID requis, lecture des noms/FString/références/GUID, structures et tableaux dans six versions de fichier, exports intégrés/séparés, consommation exacte, exceptions, limites, résultats JSON invalides, priorité sur un lecteur intégré et refus d’un en-tête futur/licensee. Le flux conserve les propriétés suivantes et le SHA-256 original lors d’un échec. La suite Git/LFS vérifie également que `ReadOptions::decoders` atteint les deux packages d’un diff historique.

Les dix suites passent en Release : 516 vérifications et 30 frames UI sans renderer. Les sept suites du build Core sans Git/CyUI/Oodle passent aussi, soit 403 vérifications. Le registre n’ajoute aucune dépendance à Unreal.

Le SDK installé est consommé depuis `examples/decoder_consumer`, compilé séparément avec `find_package`. Le changement de structure contrôlé `42 → 44`, entre un export intégré et un `.uexp`, produit exactement un remplacement de champ. Le script `scripts/validate_decoders.py` confirme aussi l’égalité sur soi-même, les entrées intactes et la sortie inchangée avec registre vide sur sept assets réels locaux : les six du corpus initial, ainsi que `M_Water`. Le rapport est conservé dans `build-native/decoder-validation.json`. Ces essais n’étendent pas la certification à des schémas natifs réels de plugins ; le schéma personnalisé est synthétique. Contrat et limites : [DECODERS.md](DECODERS.md).

## Application desktop 0.9 — 4 novembre 2026

Les douze suites passent en Release, dont le worker asynchrone (28 vérifications), la fenêtre DX11/WARP et les composants CyUI. Les sept suites du core seul passent également, avec Desktop/Git/UI/Oodle désactivés. La fenêtre native reste une cible optionnelle et n’ajoute pas de dépendance au SDK installé.

`scripts/validate_desktop.py` produit 21 captures PNG depuis le back buffer réel : cinq thèmes et deux langues sur le Blueprint, puis inspecteur, chemin Unicode, états vide/erreur, comparaison, texture source 512 × 512, mesh de 686 triangles et graphe de matériau. Les captures ont été inspectées visuellement. Le diff d’une copie de Blueprint (`NodePosX` de 256 à 320) correspond au résultat de la CLI, et la vue se positionne sur ce premier changement. Sept refus de paramètres/sorties et les empreintes inchangées des entrées sont vérifiés. Rapports conservés dans `build-native/desktop-validation/validation.json` et captures à côté.

Cette validation concerne le renderer WARP et les fichiers indiqués. L’interaction manuelle avec les dialogues Windows, les transitions DPI et le backend matériel ne sont pas certifiés par ces captures. Les aperçus de cette tranche ciblent la première source de chaque côté ; l’annulation en cours de décodage et la navigation Git dans la fenêtre restent à ajouter. Détails : [DESKTOP.md](DESKTOP.md).

## Desktop Git/LFS 0.10 — 4 novembre 2026

`scripts/validate_desktop_git.py` crée un dépôt jetable à partir de copies d’assets explicitement fournis, sans remote. Les 22 captures natives couvrent les cinq thèmes et deux langues, les propriétés et comparaisons d’ajouts/suppressions, l’égalité, un changement de contexte seul, un cache LFS externe, les aperçus texture/mesh et une erreur LFS. Six refus couvrent les options invalides et les sorties vers les métadonnées Git, le cache LFS ou des liens physiques vers les entrées. Le résultat Git correspond à la CLI ; l’index, le checkout et les assets sources restent inchangés. Rapports/captures ignorés dans `build-native/desktop-git-validation/`.

La configuration desktop sans Git passe dix suites, dont le rejet explicite d’une demande Git non prise en charge. Les vues embarquées restent utilisables indépendamment de l’adaptateur Git et de l’hôte Windows.
