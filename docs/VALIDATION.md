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
- `catalog` : **21 vérifications** des montages, métadonnées, budgets, annulation et union avant/après.
- `desktop_browser` : **25 vérifications** du chargement, sélection, annulation, priorité des demandes et erreurs du catalogue.
- `desktop_git` : **35 vérifications** du chargement de révisions, LFS local/externe, compagnons, ajouts/suppressions, renommage, contexte historique, durée de vie et conservation du document après erreur.
- `desktop_renderer` : fenêtre cachée rendue avec le backend Direct3D 11/WARP réel.
- `ui_smoke` : **30 frames ImGui**, cinq thèmes et langues anglais/français, avec inspecteur, comparaison, graphes Blueprint et matériau, tableau d’instance, image et filaire mesh préparés. Test sans renderer : production des commandes de dessin, sans revue visuelle native.

Les quinze tests passent (625 vérifications, 30 frames UI sans renderer et un test de rendu natif). Les fixtures binaires synthétiques sont générées pendant le test et supprimées après réussite. Aucun asset propriétaire n'est inclus dans les sources.

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

## Catalogue et navigateur 0.11 — 4 novembre 2026

Le build natif passe quinze suites et 625 vérifications. Le desktop sans Git passe douze suites ; le Core seul passe huit suites, sans UI/Desktop/Git/Oodle. Un consommateur externe `examples/catalog_consumer` a été compilé avec `find_package` contre le SDK Core installé et exécuté sur le projet local. Le SDK n’installe ni les vues CyUI ni Git dans cette configuration.

`scripts/validate_browser.py` produit 21 captures natives et vérifie cinq refus de sorties, dont un catalogue CLI partiel qui tente d’écrire dans les sources. Les essais couvrent contenus projet/plugins/moteur, cinq thèmes et deux langues, recherche avec ou sans résultat, présences Git, sélection locale/Git, erreur de projet/révision/sélection et LFS manquant. Les catalogues correspondent à la CLI et les empreintes des entrées, de l’index et du checkout restent inchangées. Rapports dans `build-native/browser-validation/`, ignoré. La présence de fichiers volontairement vides dans les fixtures montre que l’index n’essaie pas de décoder les packages ; leur validité n’est vérifiée qu’à l’ouverture.


## 0.13 runtime and distribution validation

The Windows desktop passes 17 CTest suites (860 checks), 30 UI frames and native DX11/WARP rendering. Core-only passes ten suites. The desktop-without-Git configuration passes fourteen suites. Cancellation is exercised at every reader checkpoint in a representative package, inside texture/mesh decoding and while a direct Git child is active. Cache tests include same-size, restored-timestamp edits and companion creation/removal.

The CyTrace project supplied locally adds 150 original loose packages (UE 5.3 / serializer 1009), 12,396 exports, 12 rejected truncated headers and unchanged original package/companion hashes. Of those exports, 11,575 expose tagged properties, 818 remain partial and three remain opaque; these counts are coverage observations, not a claim of complete native decoding. The supplied build also contains a version-11 Pak and version-5 IoStore TOCs; their presence alone does not establish container/cooked support. No `.usmap` was found in that project tree.

A desktop CPack archive was extracted and validated with its CLI, native WARP smoke and an external Core consumer compiled and run against the extracted SDK. The archive excludes source assets and the optional locally built Oodle module, while preserving selected third-party notices. CI YAML was parsed locally; hosted Windows/Linux jobs await a user-authorized push.


## 0.14 extended-property and HDR validation

The desktop passes 19 suites with 914 checks. Extended-property fixtures cover legacy/full type tags, map/set order independence and deltas, localized text histories, native transforms/ticks, bounded unknown histories and retained experimental-extension metadata. The original 150-package CyTrace corpus remains unchanged by SHA-256; full tagged-property coverage rises to 12,091 exports, with 302 partial and three opaque exports. Twelve corrupt headers are still rejected.

HDR tests preserve finite FP16/FP32 and RGBE samples before display conversion, including negatives, subnormals and values above one. A source-sample change hidden by identical RGBA8 previews remains detectable. Real RGBA16F/32F sources decode using the explicit local codec. Comparing two real 1024-square HDR sources reports 1,048,576 changed pixels with maximum sample delta 1.0; the native French desktop and CLI reports match exactly. Self-comparison, unsupported HDR use through byte-domain image-diff, exposure bounds and unchanged original hashes are checked.


## 0.15 container validation

The Windows desktop passes 21 CTest suites: 1,011 checks, 30 renderer-free UI frames and native DX11/WARP rendering. The new suites add 65 native container checks and 32 CLI checks. Core without Git/UI/Desktop/Oodle passes 14 suites; desktop without Git passes 18 suites. An external container consumer compiles and runs against the extracted Core SDK.

The original CyTrace UE 5.3 packaged build validates Pak 11 and TOC 5. `scripts/validate_containers.py` reconstructs all 1,523 Pak members, 4,695 project IoStore chunks and one global chunk: 231,432,265 decoded bytes. An independent Python index/block parser produces exactly matching SHA-256 values using the same explicit local codec. All three self-diffs are identical, extracted sample hashes match, 19 truncated-input/output-overlap refusals pass and original Pak/TOC/UCAS SHA-256 fingerprints remain unchanged. Reports stay in `build-native/container-validation-015-independent/` and are excluded from Git. Stored IoStore BLAKE3 hashes and RSA authenticity are not checked; this is decoded-byte container validation, not cooked UObject coverage.


## 0.16 cooked Zen validation

The Windows desktop passes 23 suites with 1,109 checks, including 69 native cooked checks and 29 cooked CLI checks. Core without Git/UI/Desktop/Oodle passes 16 suites; desktop without Git passes 20. The installed SDK is exercised by a separate cooked consumer with explicit profile selection and an original synthetic copy-block package.

`scripts/validate_zen.py` independently parses the supplied UE 5.3 corpus: 3,507 packages, 7,502 exports and 31,616 global script objects. Object names/classes/references, import/bulk metadata and body hashes match Core exactly. Every package uses unversioned properties; their values remain opaque. A one-byte edit on a copy of a real Blueprint produces exactly one `/opaque/sha256` change for its generated-class object. Self-comparison is identical, seven protected-output/truncated-input refusals pass and original TOC/UCAS hashes remain unchanged. Reports/copies stay in `build-native/zen-validation-016/`, excluded from Git. The independent parser shares the explicit local codec implementation; it is not an independent codec validation. UE 5.5 header alignment is tested with fixtures only.


## 0.17 explicit USMAP and property validation

The Windows desktop passes 25 CTest suites with 1,227 checks, 30 English/French UI frames and native DX11/WARP rendering. Core without Git/UI/Desktop/Oodle passes 18 suites; desktop without Git passes 22. The new suites add 96 native schema checks and 22 CLI checks. Coverage includes base USMAP versions 0–4, version metadata, enum layouts, inheritance/static arrays, zero masks, typed values, UTF-16 surrogate pairs, exact property changes, explicit reflected structs, opaque fallback, work/text bounds, cancellation, every truncated mapping prefix and named codec dispatch/output-size checks. Identity-callback compression tests are not a validation of a compression codec.

CLI tests exercise schema fingerprints, distinct before/after bindings, wrong profiles/offsets, duplicate/unknown fields, UTF-8 paths, and refusal to overwrite mapping/context inputs or their hard links. Original synthetic input fingerprints remain unchanged. An extracted 0.17 desktop/SDK archive passes its CLI and WARP smoke, dependency notices and four independently built Core consumers, including a minimal USMAP/property buffer generated independently in Python.

The supplied CyTrace corpus is rechecked in `build-native/zen-validation-017/`: 3,507 packages, 7,502 exports and 31,616 global script objects still match the independent metadata/body parser, with seven refusals and unchanged original hashes. No matching USMAP is available in that project, so this real run validates the existing object/opaque path only. Real mapped property values, matching class offsets, native struct serializers and USMAP extensions remain uncertified. `mappingVerified:false` and unresolved defaults are explicit in successful property reports.


## 0.18 USMAP extension validation

Windows Release passes 26 CTest suites and 1,286 checks, with 19 suites for Core without Git/UI/Desktop/Oodle and 23 for desktop without Git. The extension suite adds 48 checks; schema CLI coverage increases from 22 to 33 checks. Tests cover CEXT layout 0, PPTH/EATR/ENVP version 0, legacy and CEXT MODL, the 255/256 module-index boundary, extension reordering, signed 64-bit enum replacements, high property-flag bits, homonymous types, qualified bases, valid homonymous inheritance, self/cross-type cycles and escaped property diff paths.

Malformed tests exercise duplicate/conflicting/unknown extensions, unknown versions, count/name-index mismatches, invalid tags, unsupported type kinds, bounded extension windows, extra payload bytes and every truncated extension prefix with corrected outer sizes. Cancellation is exercised inside extension traversal. Resolved names, identities and static-array type copies are bounded as well as serialized input.

The extracted 0.18 archive and four standalone SDK consumers are validated. Its schema consumer additionally reads independently generated Python fixtures containing CEXT/PPTH/EATR/ENVP, verifies a qualified property value, and checks lossless attribute metadata through the extracted CLI. Proprietary `.usmap` files are rejected by the distribution validator alongside Unreal assets and local codecs. The real CyTrace build still has no matching schema: these extensions and mapped values are certified on synthetic fixtures only, and native serializers/default reconstruction remain outside this coverage.


## 0.19 unversioned collection validation

Windows Release passes 28 suites and 1,350 checks; Core without Git/UI/Desktop/Oodle passes 21 suites, and desktop without Git passes 25. Two new suites add 46 native collection checks and 18 CLI checks. Fixtures cover map/set removal and addition buckets, canonical key escaping, order changes, duplicate identities, re-additions, reflected struct values/elements, enum elements, nested arrays/maps/sets, resolved object keys, explicitly cleared containers and the UE 5.5 map replacement marker. The marker stays unsupported in the UE 5.3 profile.

Every truncated collection input window is rejected. Limits cover declared counts, recursive values, canonical identity work and text before allocation; cancellation is checked during removals, canonical identity traversal and immediately before returning a completed zero-container read. Unsupported or duplicate bound properties retain their opaque export fingerprint, while limits/cancellation propagate. CLI checks verify precise key/value diff paths, iteration-only binary evidence, catalogue coverage counts, distinct before/after profiles/contexts, UTF-8 manifest paths, protected inputs/hard links and unchanged source hashes.

The extracted 0.19 archive is exercised by four external Core consumers. The schema consumer additionally reads an independently encoded Python `Map<int, Set<int16>>` fixture and verifies both removal and addition buckets through the installed SDK. The new cooked values remain validated on synthetic data only: no matching USMAP is available for the supplied CyTrace build. Neither this validation nor successful schema reads certify runtime key equality, native struct serializers, inherited/default reconstruction or a complete cooked asset.


## 0.20 milestone: cooked references

The Core-only configuration passes 22 suites. A new reference suite checks soft package/asset/subpath separation, numbered FNames, UTF-16 subpaths, exact windows and every truncated soft payload, null/interface indices, four-word GUID order, serialized zeros, limits/cancellation, and member-level Zen diffs for the explicit UE 5.3/5.5 profiles. These tests use original synthetic mappings; no matching USMAP has become available for the CyTrace build. Full desktop and distribution validation is performed after the following milestones.

## 0.21 milestone: property inspection

Windows Release passes all 29 CTest suites. Model checks cover unversioned type unwrapping, retained tag metadata and single-pass JSON pointer decoding. UI checks exercise value search, valid/invalid selection and filtered selection across 30 frames in five themes and both languages. Native Direct3D11/WARP captures of the supplied loose NewDataAsset package were inspected in English/mid-grey and French/light-grey; the property table, selected preview and copy actions fit their panel. The source asset was opened read-only.

## 0.22 milestone: preferences and combined release

Windows Release passes 31 CTest suites with 1,439 reported checks, plus 30 renderer-free UI frames. The Core-only configuration passes 22 suites without Git/UI/Desktop/Oodle; desktop without Git passes 28. Preference coverage adds 39 model checks and 24 native CLI checks: Unicode filenames, all built-in themes, missing-file defaults, restart restoration, explicit overrides, malformed/future/duplicate/oversized/deep input, unrelated-file preservation, atomic replacement and input/report hard-link protection. Hidden smoke tests use isolated preference files and leave the real user profile untouched.

Ten additional native DX11/WARP captures check preferences restored in each of the five themes in English and French, including the filtered empty property state for an identical comparison. Representative captures in all five themes were visually inspected. The two 0.21 captures separately check the populated table and selected preview. Original source asset hashes remain unchanged. Soft/interface/lazy reference support remains validated with synthetic mapped fixtures, without a matching real CyTrace USMAP.

## 0.23 milestone: Git history API and CLI

The desktop configuration passes 32 suites before the GUI history integration, and the new CLI integration adds 20 checks. History tests cover NUL record boundaries, truncation, metadata/record limits, cancellation, duplicate IDs, merge parents, Unicode and punctuation, SHA-1/SHA-256 repositories and pages pinned across a moving branch. The CLI tests hash every file in a disposable repository before and after reads and rejected output requests; the hashes remain identical.

## 0.24 milestone: desktop history and combined release

The combined Windows Release configuration passes 36 suites and 1,603 reported checks, plus 60 renderer-free UI frames (30 asset views, 30 history views). Core without Git/UI/Desktop/Oodle passes 22 suites; desktop without Git passes 28. History adds 108 native adapter checks, 20 CLI checks, 15 worker/model checks and 21 native desktop CLI checks. The history worker is checked for superseded requests, bounded pending work, stale publication rejection, retained pages on failure, null results and cancellation during shutdown.

Native tests select two commits and compare generated packages through the same action as the dialog, verify desktop/CLI change parity, open the revision catalogue when no path is selected, compare identical commits, exercise invalid selections/revisions/options and protect Git-storage outputs. Every source and Git-storage file in the disposable repository is hashed before/after; the hashes remain equal. Ten native DX11/WARP captures cover all five themes in English/French; representative captures in each theme were visually inspected. Filtering and disabled loading/error states also have renderer-free coverage. The existing real cooked-schema coverage limitation is unchanged.

Distribution validation additionally compiles and runs the installed Git SDK history consumer alongside the four Core consumers. These local checks do not certify a hosted CI run.

## 0.25 milestone: desktop container model

Windows Release passes 37 suites. New model checks cover metadata-only index reads without codec loading, decoded SHA-256 and bounded hex prefixes, missing/deleted members, payload limits, cancellation, explicit Zen profiles, global script class resolution, source protection and re-reading a modified source. The existing `DocumentSession` owns these requests, preserving its tested cancellation and stale-result behavior. Core checks verify summary/full metadata parity without constructing entry JSON in the summary.

## 0.26 milestone: native container browsing

Windows Release passes 39 suites and 1,662 checks, with 120 renderer-free UI frames (30 asset, 30 history and 60 container frames). Core without Git/UI/Desktop/Oodle passes 22 suites; desktop without Git passes 31. Container additions include 22 model checks, 35 native CLI checks and two Core summary checks. Coverage includes Unicode product paths, search/selection filtering, explicit profile refusal, Pak bytes, partitioned IoStore reads, Zen/CLI parity, script classes, bounded prefixes, missing/deleted members, invalid options and report aliases to a Pak, a secondary UCAS partition and global-script UCAS. Codec-resolution failures now produce stable English errors instead of localized filesystem bytes that could break JSON reports.

Ten native container captures check all five themes in English and French; representative captures in each theme were inspected. The host-owned CyTrace corpus adds native views of its Pak and 4,695-member IoStore (3,626 named members, 3,507 Zen package chunks). The selected `BP_CyTrace_Demo_DirectionalTraces` package has 12 exports and matches the CLI report exactly with the explicit UE 5.3 profile, global script objects and local Oodle adapter. Source Pak/TOC/UCAS and codec hashes remain unchanged. Reports and captures remain in ignored build directories. This does not add real USMAP or native cooked-payload coverage.

The extracted distribution and its four Core SDK consumers plus Git history consumer are checked again; the container consumer exercises the new summary API through the installed library. No source project assets or codec modules are included in the archive.

## 0.27 milestone: container comparison model

The reusable Core pairing API now supplies the desktop comparison model. Tests cover sorted identities, added/removed members, omitted tombstones, metadata-only unchecked pairs, exact aggregate byte boundaries and refusal before payload I/O. Desktop checks cover selected opaque Zen differences, Core diff parity, independent after profiles, one-sided member inspection, both-side source protection and explicit aggregate limits. Full UI/distribution validation follows the desktop integration milestone.

## 0.28 milestone: desktop container comparisons

Windows Release passes 39 suites with 1,710 reported checks and 280 renderer-free UI frames (30 asset, 30 history, 220 container). Core without Git/UI/Desktop/Oodle passes 22 suites; desktop without Git passes 31. Container coverage now includes 72 Core checks, 36 desktop model checks and 64 native CLI checks. New checks exercise pairing without decoding, exact aggregate limits before payload I/O, cancellation during a full scan, explicit after profiles/global scripts, absent peers, unchecked equality, complete/selected diff parity and after-side partition/global hard-link protection. A peer without a Zen decode never implies removed objects. French translation assertions guard the UTF-8 strings checked in the native captures.

Eighteen native DX11/WARP captures cover ten theme/language combinations, verified changed object fingerprints, paired member states, byte inspection, the comparison inspector, unchecked and identical rows, and one-sided/partial cooked coverage. Representative captures in all five themes and the partial/comparison/member views were visually inspected. The real CyTrace selected Blueprint is compared against itself in both explicit UE 5.3 contexts: 12 exports, identical byte/Zen evidence and unchanged Pak/TOC/UCAS/codec hashes. These checks retain the existing limits: real mapped values still lack a matching USMAP, and native cooked payloads remain opaque.

The installed container SDK consumer additionally exercises the reusable pairing API. The extracted 0.28 archive is validated with four independently built Core SDK consumers and the Git SDK history consumer, as well as its CLI and WARP executable. Validation reports/captures remain under ignored build directories; no source project data or proprietary decoder is distributed. Hosted CI has not been run or published.

## 0.29 milestone: reusable context manifests

Core without Git/UI/Desktop/Oodle passes 23 suites. New native context checks cover manifest-only loading, Unicode relative paths, provenance, explicit profiles, resolved named Zen properties, wrong mappings, missing/invalid/duplicate fields, unsigned offsets, missing schemas, reflected binding duplicates, malformed UTF-8, text/depth/entry/byte bounds, cancellation and unchanged mapping hashes. Existing schema CLI suites pass after replacing their manifest parser with the Core API. Combined desktop and distribution validation follows the next milestones.

## 0.30 milestone: desktop mapped-property requests

Desktop model and native CLI checks exercise metadata-only manifest reads without codec loading, named before/after property values, CLI parity, independent/empty after bindings, schema/profile mismatches, absent mappings, invalid offsets with whole-body fallback and manifest/USMAP hard-link protection on both sides. Combined view and distribution checks follow the dedicated inspection milestone.

## 0.31 milestone: named property and provenance views

Windows Release validation covers 40 suites, 1,790 reported checks and 380 renderer-free UI frames (30 asset, 30 history, 320 container). Core without Git/UI/Desktop/Oodle passes 23 suites, and desktop without Git covers 32. Context tests add 41 native checks; container model/native CLI coverage now reports 49/90 checks. The final changed container views and WARP host are rechecked in both desktop configurations after correcting a stale-table reset and adding French translation assertions.

Typed UI tests verify Health 42-to-99 differences, unbound/opaque regions, one-sided property inspection without false removals, property-table clearing on index requests and provenance selection. Native checks retain exact Core/CLI package/diff parity and protect both context JSON files and both USMAP files through hard-link aliases. Synthetic context/mapping/container source hashes remain unchanged.

Eighteen native DX11/WARP captures cover all ten theme/language combinations plus single/paired/partial properties, opaque failure, schema provenance, object details, empty properties, metadata-only mapping selection and context-only differences. Representative captures in all five themes and the partial/provenance/failure views are visually inspected. These mapped values are synthetic; the supplied real CyTrace corpus still has no matching USMAP. Native cooked payloads and defaults remain outside semantic coverage.

The extracted 0.31 distribution is checked with its CLI/WARP host, four separately built Core consumers and the installed Git history consumer. The independent Python SDK fixture additionally exercises manifest loading, relative mapping resolution and a bound integer through the installed schema consumer. Proprietary mappings/assets/codecs, captures and reports remain outside Git and distribution archives. Hosted CI has not been run or published.

## 0.32 milestone: document workspace lifecycle

The desktop configuration without Git passes 33 suites. New model checks cover stable/non-reused IDs, per-document snapshots/errors, sequential decoding, queued request replacement, external progress, cancellation/close during work, discarded closed requests, tab limits, adjacent selection, empty workspaces, snapshot release and absence of retained request callback/cache closures. Combined native tab/distribution validation follows the next milestones.

## 0.33 milestone: native tabs and state isolation

New native workspace checks load loose/Pak/mapped-Zen documents in one process, settle the shared queue, select each source group, isolate failed tabs, enforce the eight-document limit and reject output hard links to inactive loose packages, mapping/context files and secondary/global UCAS partitions. Source hashes and the real user profile remain unchanged. Renderer-free tab checks cover programmatic selection, duplicate labels, literal ImGui delimiters in filenames, queued/failure cues, empty/full tab bars and all five themes in English/French. Lifecycle command and combined distribution validation follows the next milestone.


## 0.34 milestone: duplicate, close, reopen and validate

Windows Release passes all 43 CTest suites with 1,885 reported checks and 500 renderer-free UI frames (30 asset, 30 history, 320 container and 120 document-tab frames). Core without Git/UI/Desktop/Oodle passes 23 suites; desktop without Git passes 35. The workspace model reports 47 checks and its native CLI harness reports 48. Checks exercise independent duplication, fresh reads on reopening, adjacent/last-tab closure, closing other tabs, bounded closed history, preserved history on a tab-limit failure, cycling, explicit reload and retained container filters/selection after switching. Closure releases snapshots; saved/closed contexts do not pin callback/cache ownership.

Seventeen native DX11/WARP captures cover all ten theme/language combinations, duplication, reopening, the eight-tab limit, failed/empty tabs and a real loose Texture2D preview in English/French. Representative captures in all five themes and the duplicate/failure/real-texture views were visually inspected. Opening another document and switching back keeps the real 512-by-512 JPEG preview uploaded with the original document revision unchanged. Fixture and original texture/companion SHA-256 hashes remain unchanged, and smoke validation leaves the real user profile untouched. Reports and captures are retained locally under the ignored `build-native/documents-034` directory.

The extracted 0.34 archive is checked with its CLI and WARP host, four separately compiled installed Core SDK consumers and the Git history consumer. No proprietary assets, USMAPs, engine codecs, development symbols or validation captures are distributed. The SDK and Core remain independent of the desktop workspace. Session-local reopening does not provide disk workspace persistence; matching-build real mapped values and cooked native visual payloads remain outside this milestone. All development commits and distribution outputs remain local; hosted CI has not run.


## 0.35 milestone: bounded workspace file model

A dedicated desktop model suite passes 84 checks for loose/Git/container context round trips, independent after contexts, mounted names, Unicode filenames, relative path resolution, stable atomic saves, data-only codec exclusion, format/type/version validation, duplicate fields, UTF-8/control characters, read/write/depth/document/string budgets and preservation of unrelated or invalid files. The other 35 desktop-without-Git suites pass after extracting the shared settings type; a Windows separator mismatch in one new assertion was corrected and the complete new suite rerun. Native workspace commands and restart validation follow in the next milestones.


## 0.36 milestone: native workspace commands

Desktop without Git passes 37 suites, including 48 native persistence CLI checks. Three-source workspaces round-trip across separate processes and a different working directory with matching loose/Pak/mapped-Zen reports. Tests verify active tab/view restoration, per-document preview options and explicit overrides, fresh reads after fixture edits, isolated missing-package failures, empty workspaces, action-based save/import and retention of existing views after failed imports.

Workspace output guards reject hard-link aliases to packages, schema contexts, mappings, global UCAS and secondary partitions. Reports/preferences cannot overwrite a workspace input; workspace saves can replace that valid file explicitly. A clear operation releases snapshots/closed history, preserves unique IDs and rejects pre-import worker results. The full Git build and native capture/distribution checks follow the next milestone.


## 0.37 milestone: restart, Git and distribution validation

The final Windows Release configuration validates 45 suites and 2,040 reported checks, with 500 renderer-free UI frames. Desktop without Git validates 37 suites; Core without Git/UI/Desktop/Oodle passes 23. Workspace file coverage now has 96 checks, lifecycle coverage 52 and native persistence coverage 54 with Git / 52 without Git. The full suite runs passed all unaffected tests; the persistence suite was rerun successfully after correcting its interactive-error test invocation and excluding process-specific materialization directories from the Git comparison assertion. Logical package contents, diffs and immutable commit IDs remain compared exactly.

Sixteen native DX11/WARP captures validate restored loose/Pak/mapped-Zen workspaces in all ten theme/language combinations, separate asset/container views, a failed import retaining all previous documents, an empty workspace and a real 512-by-512 Texture2D in English/French. Representative captures in all five themes plus failure/texture views were visually inspected. A new process reads and uploads the texture afresh, then keeps that preview while switching tabs. Original texture/companion and fixture SHA-256 hashes remain unchanged. Reports/captures are retained under ignored `build-native/workspaces-037`; the real preference profile is untouched.

The extracted 0.37 archive is checked with its CLI, blank desktop workspace save/reopen, WARP rendering, four installed Core consumers and the Git history consumer. Assets, mappings, codecs, captures and development symbols remain excluded. Workspace save/open is explicit; complete view-state persistence and automatic last-session restoration remain unimplemented. No real matching-build USMAP or cooked native visual coverage is added by this milestone. Changes remain in local commits and hosted CI has not run.


## 0.38 milestone: bounded presentation state and workspace migration

The new header-only presentation contract validates identities, filters, graph/image transforms, orbit camera data and inspector state without depending on CyUI/ImGui. Workspace version two records this data and reads version-one files with empty presentation state. Model checks cover migration, invalid/unknown fields, finite numeric bounds, control characters, malformed UTF-8 and bounded object maps. The existing workspace file suite and the new state model suite pass; native application of this state follows the next milestones.


## 0.39 milestone: reusable view snapshot and restore APIs

Property, asset, Blueprint, material, texture, mesh and desktop container views now capture/apply presentation data outside drawing. Tests validate property identity after row insertion, object identity after export reordering, missing/hidden selections, unavailable comparison sides, graph identities, texture difference bindings and orbit camera state. Invalid nested state is rejected before changing existing views. A new renderer-free suite draws six restored view types across 120 frames in all five themes and English/French, confirms that auto-fit does not replace restored navigation and verifies immutable package data. Existing container, asset UI and state/file model suites pass alongside these tests. Native workspace integration follows the next milestone.


## 0.40 milestone: native presentation persistence

The final Windows Release configuration passes all 47 suites with 2,251 reported checks and 620 renderer-free UI frames (30 asset, 30 history, 320 container, 120 document-tab and 120 restored-view frames). Desktop without Git passes 39 suites, with the expanded native persistence suite rerun after adding state-specific checks; Core without Git/UI/Desktop/Oodle passes 23. State/file/UI suites report 34/107/143 checks, and persistence reports 77 with Git / 75 without Git. Native checks cover selected property paths, filters, unvisited-tab preservation, one-time activation, stale identities, version-one migration, invalid imports/runtime state rejected before mutation and retained pending state after missing-source failures. A restored highlight never changes which container member is decoded.

Nineteen hidden DX11/WARP captures validate restored inspector state in all ten theme/language combinations, filtered Pak rows, mapped Zen properties, failed imports and modified camera/canvas poses on real loose texture, Blueprint, material and mesh sources. Representative images in all five themes and all four real visual types were inspected. Reopened reports match the saved graph/image/camera transforms exactly. Original assets/companions, fixture files and the explicitly selected local codec keep their SHA-256 hashes; the user profile is untouched. The mesh validation explicitly supplies the host codec on launch; workspace data does not select it. These captures/reports remain ignored under `build-native/view-state-040`.

The extracted 0.40 archive is checked with its CLI, WARP workspace save/reopen and pending-state preservation, four installed Core consumers and the Git history consumer. All source assets, schemas, codecs, images and development symbols remain excluded. Semantic reader coverage is unchanged: no matching real USMAP or cooked native visual reader is added here. Automatic session restore and general window/layout persistence remain unimplemented. All commits remain local; hosted CI has not run.


## 0.41-0.42 milestones: layout contract and reusable views

The 47 existing suites pass for the version-three contract, including 49 state-model and 110 workspace-file checks. Version-one/version-two migration, exact column counts, finite positive weights, sidebar/preview bounds and invalid nested state are checked. The UI integration adds a dedicated layout suite with 725 checks across 72 frames: five themes, English/French, 1280x800 and 640x480, actual column proportions, exact no-drift snapshots, retained section expansion, narrow-screen sidebar bounds and a real mouse resize captured/restored on an already resized table. The 143-check restored-view suite passes after adding explicit diagnostic layout to its saved graph states; all other existing suites also pass.


## 0.43 milestone: native layout persistence

Windows Release passes all 48 suites with 3,008 reported checks and 724 renderer-free UI frames. Desktop without Git passes 40 suites; Core without Git/UI/Desktop/Oodle passes 23. The layout suite now reports 730 checks across 104 frames, additionally exercising real mouse drags on the sidebar edge and preview divider, lower-bound clamping, closing a restored expanded section and a 2x DPI transition without changing saved logical width. Native persistence reports 86 checks with Git / 84 without Git: independent per-document layouts, inactive/pending preservation, exact inspector proportions, blank-document save/restart, version-two migration, mixed invalid state rejected before mutation and failure-state preservation join the existing restart/input-protection cases.

Twenty-one hidden Direct3D11/WARP captures exercise restored inspector layouts in all five themes and both languages, Pak member columns, mapped Zen properties, failed imports and the previous real texture/Blueprint/material/static-mesh fixtures. Representative captures of all five themes and each real view were inspected. The validation script checks saved/restored camera poses, nonempty draws, source/companion/codec SHA-256 and an untouched preference profile. Captures and private corpus files remain in ignored build folders. The new input scroll region and sidebar resize edge share per-document identity scopes.

The Core SDK excludes the UI layout adapter. Layout capture's read-only ImGui internal dependency is isolated in `ui/view_layout.cpp`; serialized state contains no ImGui IDs or INI text. General scroll, native window geometry, active gestures and dialog layouts remain outside this contract. Reader/cooked native coverage is unchanged.


The extracted 0.43 archive passes CLI startup, WARP workspace save/open with pending inspector state and shell layout, notice/input exclusion checks, four installed Core consumers and the local Git history consumer. Native executables and SDK validation use the extracted files. Private corpus data, codecs, mappings, captures and debug symbols are excluded from the archive. The three milestone commits remain local; no hosted CI run or push is claimed.


## 0.44-0.46 milestones: explicitly bound native struct properties

Seventeen wire layout tokens cover 2/3/4-component vectors, quaternion, rotator and transform at explicit float/double widths, GUID, float linear color, byte BGRA color and signed integer point/vector. Component order was checked against the local UE 5.5 Core serializers (Math/Vector.h, Vector2D.h, Vector4.h, Quat.h, Rotator.h, TransformNonVectorized.h and Color.h); no engine implementation or private schema bytes are bundled. The native suite passes 570 checks for exact component/byte order, every truncated prefix, finite values, zero masks (including zero scale/quaternion), trailing-byte preservation, alias/reflected conflicts, shared work/text/depth/cancellation limits, nested arrays and canonical native map keys.

The 54-check manifest suite covers version-two fields/tokens, combined entry budgets, native/reflected conflicts and legacy fingerprint stability. Zen tests check precise translation-component diffs in UE 5.3/5.5, context identity changes when a layout changes, full opaque fallback without partial values and fatal enclosing-budget exhaustion. The CLI and desktop add 32 and 21 checks for component precision, context/byte provenance, independent before/after contexts, schema inspection, workspace restarts, Unicode paths, output guards and unchanged input hashes. Large float64 components retain precision beyond float32.

The complete Windows Release configuration passes 51 suites. After replacing a repeated reflected-name linear search with a bounded set lookup and adding two native map-key checks, all 13 affected schema/container suites pass again. The final desktop-without-Git configuration passes 43 suites, and Core without Git/UI/Desktop/Oodle passes 25. Existing renderer-free UI coverage remains 724 frames.

Eighteen hidden WARP captures exercise native comparisons in five themes and both languages, single-sided values, schema provenance, unsupported/truncated fallback, workspace save/reopen and a selected changed component. Draws and before/after values are checked, generated fixture hashes remain unchanged and no preference profile is created. Representative captures in all five themes, provenance/fallback and the selected French preview were inspected. These fixtures validate the declared layouts, not matching-build provenance: the supplied CyTrace corpus still has no matching USMAP, so real mapped/native property values remain unverified. General native export payloads and external bulk semantics remain outside this milestone.


The extracted 0.46 desktop archive passes CLI/WARP startup, workspace restart/pending-state checks, notices and exclusion of host assets, USMAPs, codecs and debug symbols. Four Core consumers and the Git history consumer compile and run against its installed SDK. The schema consumer additionally decodes independently encoded version-two native transform and zero-mask inputs with float64 precision. All milestone commits remain local; no hosted CI execution is claimed.

## 0.47-0.49 milestones: cooked text and persistent references

The text suite passes 167 checks for flags/history shape, localized namespace/key/source, empty and modern invariant strings, unresolved string-table identities, UTF-16/surrogates, FName bounds and suffixes, every truncated prefix, zero masks, nested arrays, trailing bytes, work/text/depth limits and cancellation. Zen tests cover both explicit profiles, source-level diffs, opaque fallback and an explicit old EditorObjectVersion that disables the modern text layout. Serializers were reviewed in the local UE 5.5 Text.cpp, TextHistory.cpp and TextKey.cpp; no engine source is compiled or bundled.

The persistent delegate suite passes 92 checks for signed object indices, function names/suffixes, ordered multicast entries, duplicate/null targets, zero values, all truncated prefixes, nested values, precise function diffs and opaque fallback. It also exercises bounded host-resolver JSON, including object-key text accounting, nesting/work limits, invalid UTF-8, non-finite values and cancellation. Local ScriptDelegates.h, property serializers and ArchiveUObject.cpp establish the persistent package reference layout; process pointer memory and runtime execution are outside the reader.

The final Windows Release build passes all 55 CTest suites. Desktop without Git passes 47; Core without Git/UI/Desktop/Oodle passes 28. New integration suites report 33 CLI checks and 22 desktop checks for typed values, snapshots, byte/context provenance, both profiles, complete opaque fallback, zero masks, output guards, workspace restart and unchanged fixture hashes. Existing renderer-free UI coverage remains 724 frames.

Twenty hidden Direct3D11/WARP captures cover all five themes in English/French, selected text/function/weak-path previews, invariant and string-table values, unsupported/invalid-target states, zero masks and workspace save/reopen. Representative images in every theme and each new value/fallback view were inspected. Original fixture/schema/container hashes remain identical and no preference profile is created. Captures and generated files remain ignored under `build-native/values-049`.

These checks validate explicit synthetic wire layouts and their application integration. The supplied CyTrace corpus still has no matching USMAP: real mapped text/delegate/native property semantics remain unverified. Formatted text histories, further unversioned types and general cooked native export payload readers remain future work.

The extracted 0.49 archive passes CLI startup, hidden WARP workspace save/reopen, notice and private-input exclusions, four installed Core consumers and the Git history consumer. The schema consumer additionally reads independently encoded localized text, UTF-16 invariant text, an empty multicast list and a null weak reference using the extracted headers/library. These raw examples have no name/object resolver and do not claim nonempty package reference resolution. No engine, mapping or private asset is required or distributed. All three milestone commits remain local; no push or hosted CI run is claimed.

## 0.50-0.52 milestones: optional values and persistent field paths

The optional suite passes 44 checks covering binary uint32 presence, set/unset/absent distinctions, set zero and null references, zero masks, unsupported absent/present inner types, nested optionals/arrays/reflected structs, all truncated prefixes, trailing native bytes, invalid markers, limits, cancellation and shared enclosing work. UE 5.5 Zen reads and precise inner/presence diffs pass; unchecked UE 5.3 nonzero optionals retain the opaque state. PropertyOptional.cpp and BinaryArchiveFormatter.h in the local UE 5.5 source establish the selected binary wire contract.

The field-path suite passes 58 checks covering stored segment order, numeric FName suffixes, signed owner references, retained paths with null owners, preserved None segments, empty/zero values, every truncated prefix, invalid counts/names/owners, nested arrays/optionals and canonical map keys. Work/text/depth limits, cancellation between segments, enclosing-budget exhaustion, complete opaque fallback and precise owner/segment diffs are checked. Both profiles pass modern reads. Recorded Release/Fortnite custom versions are checked at and below their owner-layout thresholds. Local FieldPath.cpp/FieldPathProperty.cpp establish the shape and version rules; no runtime field resolution or loader cleanup is reproduced.

The complete Windows Release configuration passes 59 CTest suites. Desktop without Git passes 51; Core without Git/UI/Desktop/Oodle passes 31. New integration suites report 47 CLI checks and 36 desktop checks for typed snapshots, catalogues, binary/context provenance, set-zero/unset distinctions, supported profile behavior, legacy and damaged input fallback, guarded output paths, filtered selections, workspace save/reopen and unchanged fixture hashes. Existing renderer-free UI coverage remains 724 frames.

Twenty-one hidden Direct3D11/WARP captures cover all five themes and English/French, optional field segments, ordinary field segments, optional integer and presence previews, unset/set-null/zero values, invalid marker and legacy-version fallback, and workspace save/reopen. Representative images in every theme and every new preview/fallback state were inspected. All generated input/container/schema hashes remain unchanged and no preference profile is created. Captures and reports remain ignored under `build-native/paths-052`.

New mapped-value validation uses synthetic fixtures. The supplied real CyTrace corpus still has no matching USMAP, so real optional/field semantics remain unverified. Newer string types, formatted text histories and general cooked native visual payloads remain outside these milestones. All engine sources are read only and excluded from distribution.

The extracted 0.52 archive passes CLI startup, hidden WARP workspace save/reopen, notice/private-input exclusions, four installed Core consumers and the Git history consumer. The schema consumer additionally reads independently encoded set/unset optional integers, empty/null field paths and a set optional field path using the installed headers/library. Nonempty package paths are checked through the Zen CLI/desktop fixtures, whose name and owner tables are explicit. All three milestone commits remain local; no push or hosted CI run is claimed.

## 0.53-0.57 milestones: byte strings and structured text formats

Four reader suites report 76 byte-string checks, 279 transform checks, 473 named/ordered argument checks and 324 numeric-format checks. Coverage includes exact byte counts without terminators, embedded NUL, UTF-8 scalar boundaries and invalid/overlong/surrogate encodings, ANSI ASCII scope, zero masks, every truncated prefix, nested text histories, empty argument containers, named-order normalization and duplicate identities, signed/unsigned 64-bit boundaries, float width/precision, recursive text and uint64 gender arguments. Number/percent/currency tests retain source, currency, culture and individual format options; the recorded EditorObjectVersion 21 alwaysSign threshold is checked. No localization or formatting execution occurs. Local UE 5.5 string/text serializers establish the wire layouts; their implementations are not bundled.

The final Windows Release configuration passes 65 CTest suites. Desktop without Git passes 57; Core without Git/UI/Desktop/Oodle passes 36. Integration adds 75 CLI checks and 47 desktop checks for equivalent classic/cooked text JSON, precise argument/source/operation/currency/option diffs, binary evidence for a named reorder, complete mapped fallback, tagged continuation after a failed property, Unicode strings, selected previews, workspace restart and protected outputs. Existing renderer-free UI coverage remains 724 frames.

Visual inspection found that the default 16-bit ImGui host displayed different supplementary scalars using the same replacement glyph. The typed preview now uses JSON surrogate escapes in that host, retaining readable accents and exact round-trip values. The Core option defaults off. Six new model checks (32 total) verify distinct before/after displays, literal escape identity, valid truncation boundaries, default UTF-8 behavior and immutable source data; the UI smoke suite checks host selection of the option. No glyph/font dependency enters the Core.

Twenty-six hidden Direct3D11/WARP captures were regenerated after that correction. They cover all five themes and English/French, selected named/nested/ordered/numeric/currency values, visible distinct Unicode escapes, empty/invalid/unsupported states, loose-package comparisons and workspace save/reopen. Representative images in each theme and each new value/fallback view were inspected. Fixture/schema/container hashes remain unchanged and no preference profile is created. Captures/reports remain ignored under build-native/text-057.

The read-only corpus validator additionally examines the first 1,000 sorted loose packages under the supplied local CyTrace project tree. No package-level read fails. Export coverage is 72,946 properties, 1,939 partial-properties and 12 opaque; 12 independently truncated headers are refused. The report counts 12,969 encountered TextProperty tags, without certifying which advanced histories occur in the real files. Source and companion SHA-256 values remain unchanged. These results are not a claim of complete native decoding; mapped cooked properties still need the build's matching USMAP, which has not been supplied. Reports and private inputs remain outside Git under build-native/corpus-057.

The extracted 0.57 archive passes CLI startup, hidden WARP workspace save/reopen, notice/private-input exclusions, four installed Core consumers and the Git history consumer. Its schema consumer additionally reads independently encoded named arguments, a nested transform, precise currency/rounding/culture fields and counted UTF-8/ASCII strings using the installed headers/library. The original Core remains independent of CyUI, ImGui, Git and engine sources. All five milestone commits remain local; no push or hosted CI run is claimed.

## 0.58-0.62 milestones: calendar text, versioned arguments and native bounds

The new suites report 409 calendar checks, 402 argument-data checks and 326 native bounds checks. The extended native struct suite reports 618 checks, including explicit DateTime/Timespan layouts, exact signed extrema, zero masks and one-tick diffs beyond double integer precision. Calendar fixtures cover date/time/custom patterns, empty timezone/culture, every truncated prefix of representative histories, recursive values, invalid styles, limits and cancellation. Argument-data fixtures distinguish recorded 32/64-bit integer layouts, pre-variant text entries, byte-sized gender, float/double/text values, duplicate and empty names, unknown types, truncation and resource limits. Both explicit cooked profiles are checked; these mapped values remain original synthetic fixtures.

The Windows Release configuration passes 68 CTest suites; desktop without Git passes 60, and Core without Git/UI/Desktop/Oodle passes 39. The four expanded application suites report 120 text CLI checks, 52 native CLI checks, 75 text desktop checks and 41 native desktop checks. They verify equivalent classic/cooked values, named component diffs, opaque fallback, immutable source hashes, explicit schema provenance, selected previews, workspace save/reopen and protected outputs. A final review additionally verifies that an invalid argument-data layout cannot silently select a width and that the desktop property table retains exact before/after tick and large-radius strings. The affected suites were rerun successfully in all three configurations. Existing renderer-free UI coverage remains 724 frames.

Twenty-six hidden Direct3D11/WARP captures cover all five themes and English/French, calendar/custom pattern/argument selections, empty/invalid data, native tick/validity/radius previews, loose-package comparisons and workspace restart. Representative images in every theme and every new kind of preview/fallback were inspected. Selected ticks and large radii remain visibly distinct; filters and selected paths survive reopening. All generated fixture/schema/container hashes remain unchanged, and no preference profile is created. Reports and captures remain ignored under build-native/advanced-062.

The serializers were checked against read-only local UE 5.5 Text.cpp, TextHistory.cpp, DateTime.h, Timespan.cpp, Box.h, Sphere.h and BoxSphereBounds.h. No engine source, private corpus, mapping or codec is included in Git or the archive. Text-generator histories, other wire versions, mixed-precision bounds and general cooked native visual payloads remain outside these milestones.

The read-only 1,000-package CyTrace-tree corpus rerun has no package-level failures. Export coverage remains 72,946 properties, 1,939 partial-properties and 12 opaque, with 12 independently truncated headers refused and all source/companion hashes unchanged. The 12,969 encountered TextProperty tags do not establish that the newly supported advanced histories occur in this corpus. Real mapped cooked values still require the build's matching USMAP, which has not been supplied. The new corpus summary remains ignored under build-native/corpus-062.

The extracted 0.62 archive passes CLI startup, hidden WARP workspace save/reopen, dependency/private-input exclusions, four installed Core consumers and the installed Git history consumer. New independently encoded SDK probes retain recursive argument-data/calendar metadata, duplicate names, byte gender, exact ticks and five temporal/bounding layout values. The archive remains local. All five milestone commits are local; no push, hosted CI execution or release publication is claimed.

## 0.63-0.67 milestones: generators, Blueprint text and native geometry

Reader suites report 170 opaque text-generator checks, 388 Blueprint checks, 1,764 tagged geometry checks and 288 matrix/plane checks. Generator cases include unknown identities, None versus present empty buffers, numbered names, binary content/hash changes, every truncated prefix of representative values, budgets, recursion and cancellation. Blueprint cases use the shared bounded archive adapter for friendly/default texts and retain exact pin alignment and GUID-based graph paths. The UI smoke test also checks that an opaque generator hash change marks the owning node as modified without changing the source package.

A review of UE 5.5 registration and struct flags establishes the tagged selection: Box, Matrix and Plane have native serializers; Sphere uses immutable binary reflected fields; BoxSphereBounds uses ordinary reflected fields. The final reader follows those declarations. Explicit Vector3f/Vector3d tags retain child precision. Tests cover generic/precision aliases, LWC versions, reflected combined bounds, rejected raw buffers for nonbinary bounds, finite components, exact binary sizes, following-tag recovery and matrix row/column paths. Pre-LWC explicit Sphere3d remains uncertified. Explicit USMAP wire bindings remain host certifications and do not infer serializers from type names.

The final Windows Release configuration has 71 CTest suites; desktop without Git has 63; standalone Core without Git/UI/Desktop/Oodle has 42. The expanded application suites report 141 text CLI checks, 88 native CLI checks, 83 text desktop checks and 63 native desktop checks. They compare appropriate classic/cooked values, preserve reflected field shape, distinguish same-size opaque payload changes, restore selected hashes/cells through workspaces and protect all input hashes/output paths. Unsupported-history fixtures now use an unimplemented tag rather than the implemented generator tag. Existing renderer-free UI coverage remains 724 frames.

Twenty-two hidden Direct3D11/WARP captures cover every theme and English/French, complete selected generator hashes, matrix cells, plane distances, loose binary geometry, reflected combined bounds, absent/empty/invalid generators and workspace restart. Representative captures in each theme and each new view/fallback state were inspected. All fixture/schema/container hashes remain unchanged and no preference profile is created. Captures and reports remain ignored under build-native/generator-067.

The final read-only 1,000-package CyTrace-tree corpus has no package-level failures. Coverage remains 72,946 properties, 1,939 partial-properties and 12 opaque, and all 12 truncated-header probes are refused. Source and companion hashes remain unchanged. These counts do not certify the occurrence of newly supported advanced histories in the corpus. New mapped values still use original synthetic fixtures; the matching build USMAP has not been supplied. Reports remain ignored under build-native/corpus-067-final.

The extracted 0.67 archive validates CLI startup, hidden WARP workspace save/reopen, notice/private-input exclusions, four installed Core consumers and the installed Git consumer. Its raw schema example additionally accepts an explicitly bounded package-name JSON table; independent probes validate generator identity/binary hash, reject invalid tables and retain row-major matrices and plane coefficients. No engine/plugin generator code executes, and no engine source, host asset, mapping or codec is bundled. Five milestone commits remain local, with no push or hosted CI/release publication.

## 0.68-0.72 milestones: visual change navigation

The Windows Release configuration passes 74 CTest suites, desktop without Git passes 66, and standalone Core without Git/UI/Desktop/Oodle passes 43. After the final graph interaction/localization/layout adjustments, the eight relevant Core/UI/native suites pass again with and without Git. The reader format decoders are unchanged; the prior 1,000-package corpus result remains the last reader-corpus validation, rather than a new run.

New focused coverage includes 98 LDR/HDR changed-bounds checks, 57 graph-inspector checks with 40 renderer-free frames, 212 view-state UI checks with 160 restored-view frames, and 54 data-only view-state checks. Graph cases cover stable identities after canvas-ID/export reordering, added/removed values, filtered navigation and wraparound, explicit canvas selection of hidden nodes, missing references and rejection of invalid nested state before mutation. Mesh cases isolate changed edges from distant unchanged geometry, retain preset orientation while fitting, and restore display/camera state independently.

Image bounds are tested for single alpha changes at every position of a nonsquare image, threshold equality, disjoint changes, one-pixel images, identical/incomparable images, HDR source samples and operation without heatmap allocation. Host-provided image bounds are checked before navigation. Core JSON gains only the additive changedBounds region; native format support is unchanged.

The native visual navigation harness passes 516 checks across 240 hidden Direct3D11/WARP frames. Its 80 captures cover five themes, English/French, selected Blueprint/material comparisons, filtered instance overrides, changed mesh framing, changed texture framing, empty navigation/mesh views and a narrow stacked inspector. Both theme contact sheets and representative full-size captures were inspected. The review caught and corrected French source encoding and viewport-center retention when switching to a narrow layout; explicit regressions cover French accents and visibility of the selected node. Synthetic package snapshots remain unchanged and the harness does not load/write appearance preferences. Captures/reports remain ignored in build-native/visual-navigation-072-final.

The extracted 0.72 archive passes CLI startup, hidden WARP workspace save/reopen, notices/private-input exclusions, five installed Core consumers (catalogue, container, cooked Zen, schema and images) and the installed Git consumer. The new image consumer compiles against the installed public headers/library and verifies independent LDR/HDR changed regions without Unreal, CyUI, Git or a renderer. The distribution validator rejects UI-dependent headers in the Core SDK. UI views remain source-embedded targets rather than installed Core dependencies.

The five milestone commits and the Windows archive remain local. No push, hosted CI run or release publication is claimed.

## 0.73-0.77 milestones: stored collision inspection

Windows Release passes 78 CTest suites, desktop without Git passes 70, and standalone Core without Git/UI/Desktop/Oodle passes 45. After the final metadata-budget refinements, the 12 related Core/CLI/UI/native suites pass again with and without Git, and the standalone Core collision suite passes again.

The collision suites report 64 Core checks, 62 CLI checks, 18 desktop restart checks and 110 UI checks across 100 renderer-free frames. Independent classic UE 5.3/5.5-style package fixtures pass through the actual tagged-property reader with spheres, boxes, capsules, tapered capsules and convex vertices/triangle indices/transforms. Tests cover missing/opaque fields, invalid indices, node/text/depth/body/shape/vertex/index/reference budgets, cancellation, local/absent/external references, exact export containment, duplicate names, stable named reorderings, index-based ambiguity and escaped diff paths. Generic package diffs retain serialized ordering changes; collisionChanges separates the named geometry correspondence.

Output protection tests found and corrected a Windows/MSVC failure when weakly_canonical received missing companion paths under an 8.3 temporary directory. The CLI now canonicalizes an existing ancestor before appending missing components. Collision, ordinary diff and material-diff tests reject overwriting inputs, companions and hard-link aliases without changing input hashes.

The final read-only collision corpus scans 1,000 packages, with no process/package failures and unchanged source/companion hashes. Eleven packages contain 66 reflected convex bodies. Their stored transforms are absent from the decoded properties, so all eleven stored projections remain incomplete. No identity transform or cooked physics geometry is inferred. Other primitive kinds currently have synthetic tagged-package coverage rather than occurrence certification in this corpus. Reports remain ignored under build-native/collision-corpus-077-final.

The shared native harness passes 577 checks through 270 hidden WARP frames and retains 90 captures across five themes, English/French and nine view scenes, now including selected collision geometry. Thirteen additional desktop captures exercise the actual Collisions tab in every theme/language, an incomplete shape, an empty package and the supplied real source mesh. Both collision theme contact sheets and representative full-size complete/incomplete/empty/real captures were inspected. Inputs and companions retain their hashes and no preference profile is created. Captures/reports remain ignored under build-native/visual-collisions-077.

The extracted 0.77 archive passes CLI startup, hidden WARP workspace save/reopen, private-input/notice checks, six installed Core consumers (catalogue, containers, cooked Zen, schema, images and collisions) and the installed Git consumer. The collision consumer validates the public projection/diff API on a host-populated model, and a separate installed-reader probe reads the six-shape tagged package and detects only the changed radius. Core remains independent of CyUI, Unreal and Git; UI-dependent headers are excluded from the SDK.

All five milestone commits and the Windows archive remain local, with no push or hosted release publication. Physics simulation, native Chaos/level-set payload decoding, skeleton pose evaluation and a 3D collision renderer remain outside this stored-property milestone.

## 0.78-0.82 milestones: reusable collision wireframes and 3D review

Windows Release passes all 79 CTest suites, desktop without Git passes 71, and standalone Core without Git/UI/Desktop/Oodle passes 46. Focused coverage reports 29 wireframe checks, 84 collision CLI checks and 205 collision UI checks across 168 renderer-free frames, alongside the existing 64 stored-collision checks. Tests cover analytic primitive bounds, full box dimensions and Unreal rotations, tapered end orientation/containing spheres, deduplicated convex edges, signed scales and explicit quaternions, missing transforms/topology, exact edge budgets, cancellation and source preservation. Preview coordinate overflow retains the desktop property inspector.

Real ImGui mouse-event tests select a shape in the after pane, exclude Alt-orbit from picking and verify that a ten-pixel orbit delta applies once across the two panes. Validated nested workspace state retains the camera/filter/body/shape/property preview; invalid camera values are rejected before mutation. Actual desktop save/reopen tests retain the camera too.

The native harness passes 757 checks through 360 hidden Direct3D11/WARP frames and produces 120 captures across five themes, English/French and twelve scenes. Collision cases include one selected changed sphere, all five shape kinds, a narrow 640-pixel view and incomplete geometry. Thirteen additional full desktop captures cover each theme/language plus incomplete, empty and real source packages. Theme contact sheets and representative full-size complete/narrow/incomplete captures were inspected. Input/companion hashes remain unchanged and no appearance preference profile is created. Captures/reports remain ignored in build-native/visual-collisions-082.

The updated read-only corpus validator scans 1,000 CyTrace-tree packages with no failures and unchanged source/companion hashes. Eleven packages contain 66 reflected convex bodies; all 66 previews explicitly remain unavailable because stored Transform values are absent. This run certifies that missing data is reported without inventing identity transforms; it does not certify real curved primitive previews, cooked physics or skeletal placement. Reports remain ignored in build-native/collision-corpus-082.

The extracted 0.82 archive passes executable startup, hidden WARP workspace save/reopen, notices/private-input exclusions, six installed Core consumers and the installed Git consumer. The collision consumer links the new public wireframe API with no CyUI/Unreal/Git dependency. An installed-reader probe reads the independent six-shape tagged package; installed CLI collision-preview output exactly matches the SDK result. Source bytes remain unchanged.

Gitleaks 8.30.1 reports zero findings across 273 publishable source files and local Git history. All five milestone commits and the Windows archive remain local. No push, hosted CI execution or release publication is claimed. Native cooked physics, virtualized assets and skeletal/world placement remain outside this wireframe preview scope.

## 0.83-0.87 milestones: raw reference skeletons and hierarchy review

Windows Release passes 85 CTest suites, desktop without Git passes 77, and standalone Core without Git/UI/Desktop/Oodle passes 50. After the final host-index and narrow-window framing refinements, the 13 related Core/CLI/UI/native suites pass again with and without Git, and all four skeleton suites pass again in standalone Core. Focused suites report 370 native skeleton checks, 19 projection checks, 13 affine-pose checks, 28 CLI checks and 211 UI checks across 200 renderer-free frames. Native cases include every truncated prefix, optional/invalid UObject GUID markers, duplicate names, forward parents, inconsistent pose/map counts, finite values, explicit profiles, cache-profile isolation, budgets and cancellation. Malformed unsigned host parent indices cannot wrap into roots. A comparison without hashes for nonempty opaque native tails remains incomplete rather than certifying equality.

The first synthetic native fixtures omitted UObject's GUID-presence marker; checking real engine packages exposed that omission. Both the reader and independent fixtures now include it, with regressions for absent/present GUIDs and invalid marker values. Source inspection and read-only real inputs establish support for UE4 file version 522 with UE5 file versions 1004 or 1013. Modern 1013 has independent fixture coverage; three actual engine-owned assets saved by UE 5.0 exercise 1004. Filtered export-name omission has bounded native-prefix coverage only.

The three real packages expose 1, 68 and 2 stored bones, for 71 raw bones and 71 ready reference-affine joint positions, with no reader/pose failures and unchanged source/companion hashes. The existing CyTrace tree additionally passes inspection of 1,000 profile-1009 packages without failures or input changes; it contains no Skeleton objects, so it provides regression coverage rather than native skeleton certification. Reports stay ignored in build-native/skeleton-engine-corpus-087 and build-native/skeleton-cytrace-corpus-087.

Named structural diffs separate sibling source reordering from changed local transforms. Generic package diffs still retain serialized array changes. Stored parent/local transforms, UObject properties, exported names and undecoded native tail fingerprints remain visible. Pose export composes raw affine matrices iteratively; signed scale, invalid quaternion propagation, depth/joint budgets and nonfinite derived values are checked. This is not animated/skinned runtime-pose reconstruction or Unreal FTransform shear decomposition.

The shared native harness passes 888 checks through 420 hidden Direct3D11/WARP frames and retains 140 captures across five themes, English/French and fourteen scenes. Skeleton scenes include normal and 640-pixel layouts; the selected restored bone remains visible when switching layouts. Fifteen full desktop captures cover all theme/language pairs, incomplete/empty input and all three real engine packages. Component/application theme sheets and representative full-size real/narrow views were inspected. Sources retain their hashes and no appearance preference profile is created. Captures stay ignored in build-native/visual-skeletons-087.

The extracted 0.87 archive passes executable startup, hidden WARP workspace save/reopen, notices/private-input exclusions, seven installed Core consumers (catalogue, containers, cooked Zen, schemas, images, collisions and skeletons) and the installed Git consumer. The skeleton consumer validates projection/diff/affine pose and incomplete comparison without opaque fingerprints. Independent installed CLI/SDK probes then read all three actual engine packages and produce matching snapshots and all 71 ready joint positions, retaining original input hashes. Core remains independent of Unreal, CyUI and Git.

Gitleaks 8.30.1 reports zero findings in 293 publishable source files and local Git history. The five milestone commits and Windows archive remain local. No push, hosted CI execution or release publication is claimed. Native SkeletalMesh geometry/skin, animation, virtual-bone rebuilding and native cooked skeleton layouts remain outside this milestone.

## 0.88-0.92 milestones: SkeletalMesh references and joint displacement review

Windows Release passes all 88 CTest suites; desktop without Git passes 80 and standalone Core without Git/UI/Desktop/Oodle passes 53. The native mesh reader passes 488 checks, including every truncated prefix, absent/present imported material labels, custom-version bounds, UObject GUIDs, invalid strip flags/booleans/references, nonfinite bounds/UV values, shared work budgets, cache separation and cancellation. Projection tests pass 29 checks and derived displacement comparison passes 21. The actual CLI passes 59 skeleton/mesh/pose checks, including tolerance, explicit independent object selection, incomplete status and output protection. Source and engine data are not redistributed.

Read-only verification of three engine-owned SkeletalMesh assets saved by UE5.0 adds 71 raw bones and 71 ready affine joints. Together with the three previously verified Skeleton assets this gives six checked reference prefixes, 142 stored bones and 142 ready positions. All six reference-only self-comparisons report identical. Full stored-property projections remain incomplete for the three meshes because unrelated reflected properties are still partial; a complete reference pose never certifies the rest of the asset. Original source and companion hashes remain unchanged. The CyTrace tree again passes inspection of 1,000 profile-1009 packages, with no failures or input changes and no reference skeleton objects in that sample. Reports remain ignored under build-native/skeletal-engine-corpus-092 and build-native/skeletal-cytrace-corpus-092.

The Skeleton tab adds prepared 3D reference positions with a shared before/after camera, narrow stacked panes, moved-joint filtering, exact joint picking and typed stored/derived inspection. Its colors describe derived position/parent changes, independently of stored local-transform graph outlines. The renderer-free view suite passes 301 checks over 284 frames, including actual mouse selection, unavailable rotations, display limits and atomic camera/property state restoration. Explicit desktop workspace restart also retains pose mode and selection. The shared native harness passes 1,158 checks through 540 hidden Direct3D11/WARP frames and writes 180 component captures across eighteen scenes, five themes and English/French. Fifteen full desktop captures include all themes/languages and the three actual engine meshes. Both theme contact sheets and full-size real/narrow views were inspected. No preference profile is created. Captures remain ignored under build-native/visual-skeletons-092.

Full Git-enabled desktop tests exposed MSVC weakly_canonical failures on missing temporary revision companions. Desktop output guards now canonicalize only the nearest existing ancestor before appending missing components. An independent regression covers existing files, missing companions, nested outputs and available Windows 8.3 parent aliases. Appending an empty path component no longer introduces a spurious trailing separator in desktop or CLI guards. Existing source/companion/hard-link protections and Git workspace/history tests pass.

The extracted 0.92 archive passes CLI startup, hidden WARP workspace save/reopen, notices/private-input exclusion checks, all seven installed Core consumers and the installed Git consumer. The independent skeleton consumer now exercises reference-pose displacement comparison as well as stored structural comparison. Extracted CLI and independently compiled SDK then read all six actual assets and produce matching snapshots and all 142 positions; reference-only self-comparisons are identical and the three incomplete structural projections remain incomplete. Core still has no Unreal, CyUI or Git dependency.

Gitleaks 8.30.1 reports zero findings in 302 publishable source files and local Git history. Five milestone commits and the Windows archive remain local, without a push, hosted CI run or published release. Native SkeletalMesh source/render geometry, skin weights, animation, cooked native skeleton layouts, virtualized source resolution and Nanite remain outside this batch.

## 0.93-0.97 milestones: stored Skeleton retarget sources

Windows Release passes all 92 CTest suites; desktop without Git passes 84 and standalone Core without Git/UI/Desktop/Oodle passes 57. After the final unknown-correspondence refinement, all 19 related native desktop suites, 19 desktop-without-Git suites and 10 standalone Core suites pass again. The retarget-specific suites cover 618 native checks, 16 normalized projection checks, 15 pose composition checks and 25 public CLI checks. Existing raw-pose and displacement tests remain successful.

The native continuation uses source-verified serialization for retarget map keys, separate pose labels, bounded local transforms, the explicit hard/soft mesh-reference transition and the distinct Skeleton GUID. It checks every truncated continuation, supported AnimPhys versions, duplicate keys, invalid references, absent filtered references, aggregate work budgets, decoder/cache separation and cancellation. A failed continuation retains the checked raw reference prefix and fingerprints the entire remaining suffix. Valid count mismatches stay as unbound ordered values. Named projections normalize source-map and matching raw-bone reorderings without rebuilding virtual bones or loading referenced meshes.

The real six-asset engine corpus retains all 142 ready raw-reference joints. The three Skeleton assets now also expose their stored GUIDs and verified empty retarget tables. Their source/companion hashes remain unchanged. No real nonempty retarget table is claimed: independently encoded packages provide eight source records, six bound records, two deliberately unbound records and eighteen ready named-source joints across the fixture corpus. All eight source self-comparisons return the expected complete/incomplete status. Inspection of the supplied CyTrace tree again passes 1,000 profile-1009 packages with zero failures and unchanged input hashes; that sample contains no supported skeleton objects. Reports remain ignored under build-native/retarget-engine-corpus-097, retarget-fixture-corpus-097 and retarget-cytrace-corpus-097.

The pose APIs and commands retain the selected exact source-map identities before/after. An omitted after selection inherits before in the CLI/API; an explicitly empty after source selects raw. Missing keys fail explicitly. Unknown tables and unbound records never substitute raw values. Unknown opposite poses leave unmatched joints unavailable instead of claiming their removal; a known absent asset side can still show additions/removals. Raw hierarchy graph outlines remain independent of selected pose positions.

The UI suite passes 430 checks through 404 renderer-free frames, including independent named/raw selections, deleted saved sources, unbound source counts, typed state rejection and the existing point-picking checks. Explicit desktop save/reopen retains source keys, selected bone and pose camera. The native harness passes 1,368 checks through 630 hidden Direct3D11/WARP frames across twenty-one scenes and retains 210 component captures. Seventeen additional full desktop captures cover all five themes in English/French, unbound/opaque sources, raw-after selection, empty input and the three actual Skeleton assets. Theme contact sheets and representative full-size changed/narrow/unavailable views were inspected. No preference profile is written. Captures remain ignored under build-native/visual-retargets-097.

The extracted 0.97 archive passes executable startup, hidden WARP workspace persistence, notices/private-input exclusion checks, seven independently compiled installed Core consumers and the installed Git consumer. The skeleton consumer verifies explicit named retarget composition and independent raw-after comparison. Installed CLI/SDK additionally produce identical snapshots and raw poses on the six actual assets and two independent native retarget fixtures, without changing their inputs. Core remains independent of Unreal, CyUI and Git.

Gitleaks 8.30.1 reports zero findings in 307 publishable source files and local Git history. All five milestone commits and the Windows archive remain local, with no push, hosted CI run or published release. Runtime retargeting, animation, skinning, native SkeletalMesh source/render geometry, virtual-bone reconstruction and further cooked layouts remain outside this batch.

## 0.98-0.102 milestones: stored Skeleton curves and synchronization markers

Windows Release passes all 97 CTest suites; desktop without Git passes 89 and standalone Core without Git/UI/Desktop/Oodle passes 61. After aligning the no-Git executables to the final 0.102 version string, all 19 related native/CLI/UI/renderer suites pass again. The new suites pass 123 native curve checks, 45 native marker checks, 28 bounded projection checks and 23 public CLI checks. Existing retarget, raw-pose, displacement and package-reader regressions remain successful.

The source-verified native continuation reads modern SmartName container/curve maps, stored material/morph-target booleans, ordered bone-name links, present byte LOD limits, marker strip flags and marker-name arrays. Framework 15-37, AnimPhys 10-17 and Anim 0-15 are explicitly bounded within the previously supported loose-package file profiles. Empty containers need no unused entry-layout versions. Unknown old UID/GUID layouts, future versions and malformed suffixes remain opaque. Each continuation rolls back only its own stage, retaining previously checked references, poses and metadata. Fixtures exercise every truncated continuation byte, duplicate map keys, invalid booleans/names/counts, omitted LODs, stripped/pre-marker layouts, shared budgets, decoder/cache separation and cancellation.

The named Core projection keys curves by exact container/name and markers by name/occurrence count; native marker source order remains visible in the generic serialized snapshot. The separate scoped API/CLI comparison is independent of unrelated property coverage, animation samples and opaque native suffixes. An unread or stripped table cannot establish equality, even against itself. Host-populated models receive typed metadata, coverage, node/depth/text/container/curve/link/marker budget checks. Stored None or unresolved bone links remain explicit without inferred correspondence or runtime defaults.

Read-only validation of the six existing engine assets retains 142 ready raw-reference joints. All three Skeleton native bodies are now fully consumed: each has one verified empty curve container and an empty marker list. Their scoped metadata self-comparisons are identical. No real nonempty curve or marker table is claimed. Eleven independently encoded fixture packages provide twenty decoded curve entries, forty bone links, six distinct per-object marker names and nine occurrences; all eleven metadata self-comparisons return their expected complete/incomplete status. A fresh 1,000-package CyTrace-tree run has zero failures and unchanged source/companion hashes, with no Skeleton objects in that sample. Reports remain ignored under build-native/animation-engine-corpus-102, animation-fixture-corpus-102 and animation-cytrace-corpus-102. Engine source and asset bytes are not redistributed.

The Skeleton tab's animation-metadata mode passes 255 checks over 240 renderer-free UI frames across five themes, English/French and wide/narrow layouts. Exact curve/marker selection, search, confirmed-change filtering/navigation, typed details, unknown opposite tables, display bounds and atomic state restoration are checked. Seventeen desktop automation checks include explicit save/reopen of curve and marker selections without writing preferences or changing source files. The native harness passes 1,608 checks through 750 hidden Direct3D11/WARP frames and retains 250 component captures across twenty-five scenes. Sixteen additional full desktop captures cover all themes/locales, curve inspection, stripped/corrupt tables and the three real Skeleton assets. Both theme sheets and representative full-size narrow/unavailable views were inspected. Captures remain ignored under build-native/visual-animation-102.

The extracted 0.102 archive passes executable startup, hidden WARP workspace persistence, notices/private-input exclusion, seven independently compiled installed Core consumers and the separate Git consumer. The skeleton consumer demonstrates a changed curve LOD with reordered marker names and incomplete stripped metadata. Installed CLI and independently compiled SDK additionally produce matching reference snapshots, raw poses, animation metadata snapshots and scoped comparisons for nine real/fixture package pairs. Input hashes remain unchanged. Core has no Unreal, CyUI or Git dependency.

Gitleaks 8.30.1 reports zero findings in 317 publishable source files and local Git history. The five milestone commits and Windows archive remain local; no push, hosted CI execution or release publication is claimed. Animation samples, runtime curve evaluation/timelines, native SkeletalMesh source/render LODs and skinning, virtual-bone rebuilding, further cooked native layouts and Nanite remain separate work.

## 0.103-0.107 milestones: skeletal sockets and virtual definitions

Windows Release passes all 103 CTest suites; desktop without Git passes 95 and standalone Core without Git/UI/Desktop/Oodle passes 66. The new Core suites cover 25 socket projection checks, 17 virtual-definition checks, 16 independently encoded package checks and 18 socket-origin checks. The public CLI passes 72 attachment/pose checks across file profiles 1004 and 1013. Existing hierarchy, retarget, animation metadata, reference-pose and package-reader regressions remain successful.

The attachment projection retains exact socket object paths, stored typed name/bone/location/rotation/scale/force-animation fields, actual outers, explicit ordered membership and separate containment. It does not infer membership from child exports or load external objects. Duplicate socket labels preserve distinct identities and list-order diagnostics. Virtual definitions use exact unique names, source/target reference states, index fallbacks for ambiguous identities and iterative cycle blocking without rebuilding runtime bones. Counts, raw-name/reference work, JSON depth/nodes/text, cancellation and malformed typed values are checked. Both file profiles have independently encoded tagged properties, object/import/export tables, raw references and retarget sources; no production asset writer or engine-owned bytes are used. Export-index remapping and unique virtual-array reorder preserve named equality while socket membership order remains visible.

Socket origins are transformed from explicitly stored local locations by the selected raw-bone affine matrices. Tests cover signed scales, rotated parents, independent raw/retarget choices, tolerance, omitted local locations, unresolved and virtual parents, invalid ancestors, point overflow, explicit owner selection and cancellation through comparison. Unrelated invalid virtual metadata does not erase usable raw-bone socket origin data. These derived points do not certify socket orientation, runtime lookup, animation, skinning or Unreal shear decomposition. Missing values are not supplied from class defaults.

Read-only inspection of six actual Engine assets finds six owners and seven socket objects. Only the Tutorial Skeleton has an explicit stored socket list; it contains all seven references and two explicitly stored local locations. Those two origins are ready; the other five remain unavailable. Missing owner/virtual lists are not treated as empty defaults, and all six full metadata/pose self-comparisons return their expected incomplete status. No real nonempty virtual-definition table is claimed. Twelve independent fixture packages contain twenty-four socket records, twenty references, twenty virtual definitions, four cycle-blocked definitions and eighteen ready origin points. Ten metadata projections and ten origin poses are complete; missing-location/cycle cases return the expected incomplete result in their relevant scope. All twelve metadata and twelve origin self-comparisons pass. A fresh 1,000-package CyTrace-tree run has zero failures and unchanged hashes, with no attachment owners or sockets in that sample. Source/companion fingerprints remain unchanged throughout. Reports stay ignored under build-native/attachments-engine-corpus-107, attachments-fixture-corpus-107 and attachments-cytrace-corpus-107.

The desktop Attachments mode passes 254 checks over 240 renderer-free UI frames. Exact socket/virtual selection, search, changed navigation, independent source choices, unknown opposite membership, source deletion without fallback, absent locations, saved camera/typed properties and atomic nested-state validation are covered. Twenty-one desktop automation checks include explicit save/reopen of both socket and virtual selections with source choices and preview state, without writing preferences or changing inputs. The native harness passes 1,908 checks through 900 hidden Direct3D11/WARP frames across thirty scenes and retains 300 component captures. Seventeen full desktop captures cover all themes and English/French, virtual cycles, omitted local locations, missing sources, explicit empty membership and actual Engine assets. Theme contact sheets and representative full-size narrow/virtual views were inspected. Captures remain ignored under build-native/visual-attachments-107.

The extracted 0.107 archive passes executable startup, hidden WARP workspace persistence, dependency notices/private-input exclusion, seven independently compiled installed Core consumers and the separate Git consumer. The skeleton consumer demonstrates attachment projection, a changed socket location, affine displacement and an unavailable omitted location. The installed independent SDK and CLI additionally produce identical attachment snapshots/comparisons and socket-origin snapshots/comparisons for ten real/fixture package pairs, with original input hashes unchanged. Core remains independent of Unreal, CyUI and Git.

Gitleaks 8.30.1 reports zero findings in 333 publishable source files and local Git history. The five milestone commits and Windows archive remain local; no push, hosted CI run or release publication is claimed. Runtime virtual-bone reconstruction, external/default resolution, animation samples, skinning, native SkeletalMesh source/render LODs and further cooked/Nanite formats remain separate work.

## 0.108-0.112 milestones: stored animation source tracks

Windows Release passes 109 CTest suites with desktop/Git, 101 with desktop but no Git, and 70 with standalone Core without Git/UI/Desktop/Oodle. Final focused checks additionally pass 25 raw-key cases, 23 named-model cases, 18 source-frame/package cases, 46 public CLI cases, 294 source-view checks across 280 renderer-free UI frames, four native process/workspace scenarios, and 2,268 navigation checks across 1,080 hidden WARP frames. Existing Skeleton animation-metadata, attachment, reference/retarget, property-reader and desktop persistence regressions remain successful.

The new reader checks Release custom versions 39–51 and verified file profiles 1004/1013 before decoding element-size/count-prefixed float arrays. Wrong strides, invalid counts, non-finite components, truncation, extra bytes, resource limits and cancellation are covered. A legacy Quat4f test confirms four float components even in LWC packages. Both binary fixture profiles independently encode model classes/imports, tagged nested tracks and raw key arrays. Duplicate names remain indexed/incomplete; track storage order does not create named changes. Explicit model selectors pair different object paths without erasing per-bone/key differences.

Integer source frames support constant one-key channels and full-count channels. Empty/missing/count-mismatched channels, non-unit quaternions, unavailable models and frames outside the stored range never fall back to default transforms or another frame. Tests cover rational timing, timing omitted independently of otherwise usable local keys, typed changed values, source guards and cancellation. This API does not evaluate hierarchy, interpolation, root motion, compressed animation, retargeting or skinning.

Read-only inspection of Tutorial_Walk_Fwd and Tutorial_Idle finds 136 named tracks and 49,980 stored position/rotation/scale key records. Sampling first, middle and final frames produces 408 ready local track samples across six frame reports. All input and companion hashes remain unchanged. Both model metadata comparisons correctly remain incomplete because the stored frame-rate denominator is omitted; no engine default is assumed. Twelve independent fixture packages produce 24 tracks, 124 key records, 36 sampled frames and 48 ready track samples, with eight complete metadata projections. Missing timing and duplicate identities produce expected incomplete metadata results; mismatched channel cardinalities stay unavailable during sampling. A fresh 1,000-package CyTrace-tree run, including maps, has zero failures and unchanged input hashes; it contains no AnimDataModel objects and therefore provides regression evidence rather than real animation-format coverage. Reports remain ignored under build-native/animation-engine-corpus-112, animation-fixture-corpus-112 and animation-cytrace-corpus-112.

The Animations tab supports independent model choices, named track filters, confirmed-change navigation, synchronized frame indices, source-key channel plots, and typed frame comparison. It preserves object/track/frame/component/filter/inspector state through explicit workspace files and native process restarts. Missing selections, unknown opposite tables and shorter sequences remain explicit. English/French, five themes and narrow layouts have native capture coverage. Display plots use at most 2,048 points per side and do not certify interpolation between stored keys. No user preferences or input assets are changed.

The refreshed native run retains 360 component PNG captures and 14 complete application captures, including the two actual engine animations, five themes, both locales, unknown timing and mismatched channels. Theme sheets and representative complete and narrow views were inspected. Inputs remain byte-identical and no preferences directory is created. Captures stay ignored under build-native/visual-animation-112.

The extracted 0.112 Windows archive passes startup, hidden WARP workspace persistence, notices/private-input exclusion, eight independently compiled installed Core consumers and the separate Git consumer. The new consumer constructs its own complete source model, checks a changed position key and validates rational frame timing. Additional installed SDK/CLI reports match for thirteen real/fixture package pairs: source snapshots, frame-zero values and named differences all agree, including incomplete timing and duplicate-name cases. Input hashes remain unchanged. The prepared CI additionally links the standalone animation consumer; hosted CI has not been run.

Gitleaks 8.30.1 reports zero findings across 350 publishable source files and local Git history. Five milestone commits and the Windows archive remain local, with no push or release publication. Broader animation data-model classes, source curves/attributes/notifies, compressed playback, hierarchy evaluation, blending/root motion, skinning and further native/cooked mesh payloads remain future work.

## 0.113-0.117 milestones: stored rich curves

Windows Release passes all 115 desktop/Git CTest suites and all 74 standalone Core suites without Git/UI/Desktop/Oodle. New checks cover fifteen packed native-key cases, twenty-three named/time-key projection cases, twenty-four evaluation/file cases and seventy-five public CLI cases. The tests verify three independently encoded file profiles (UE4 522 / UE5 0, 1004 and 1013), exact packed sizes, atomic opaque rollback, raw unknown mode retention, finite values, aggregate budgets and cancellation. Exact key-time identities use fixed IEEE754 bit digits rather than implementation-dependent stream formatting. Stored order, duplicate/missing times and modern animation float-curve names have explicit coverage.

Supported evaluation is limited to exact stored points and in-range constant, linear and unweighted cubic segments, using stored slopes in double precision. Tests cover active/inactive tangent-weight sides, unknown interpolation, missing input, non-finite input coordinates, arithmetic overflow and unavailable extrapolation. Actual engine load-time repair, weighted evaluation and exact float-runtime equivalence are not claimed. CurveFloat/Vector/LinearColor lanes and AnimDataModel float-source lanes remain distinct from full asset behavior, color postprocessing, defaults and compressed animation.

Read-only inspection of ContentIntroCurve plus the two tutorial animations finds three supported owners, one stored curve lane and three complete native keys. The actual curve has four ready stored/interior evaluation probes. The two AnimDataModel owners omit CurveData and remain incomplete without inferring empty curve lists. Twenty-seven independent fixtures expose twenty-seven lanes and sixty-six keys, with eighteen complete owners, eighty-one evaluation probes and fifty-seven ready values; duplicate/unsorted/missing data and weighted segments produce their expected gaps. A fresh 1,000-package CyTrace-tree run, including maps, has no errors and no curve owners in that sample. Every input and existing companion hash remains unchanged. These reports are ignored under build-native/curves-engine-corpus-117, curves-fixture-corpus-117 and curves-cytrace-corpus-117.

The Curves tab passes 291 renderer-free checks across 280 UI frames and four native process/workspace scenarios. It retains selected owners, lane, source key, evaluation coordinate, filter and typed-property state. Unknown opposite tables, missing selections, explicit empty data and weighted gaps remain visible. Preparing a selected lane caches at most 512 evaluation samples and 4,096 stored markers per side; drawing does not project packages or scan unbounded key arrays. The expanded native harness passes 2,628 checks across 1,260 hidden WARP frames and forty-two scenes. It retains 420 component captures plus fifteen complete application captures across all themes and English/French, including weighted/missing/empty/cubic cases and the actual curve asset. Theme sheets and representative complete/narrow views were inspected. Captures remain ignored under build-native/visual-curves-117; no preferences or source files are changed.

The desktop configuration without Git also passes all 107 CTest suites. All three CLI builds report version 0.117.0. Hosted Linux/Windows CI is prepared but has not been executed.

The extracted 0.117 archive passes CLI startup, hidden desktop workspace persistence, notices/private-input exclusion, nine independently compiled installed Core consumers and the separate Git consumer. The curve consumer constructs its own source model, verifies a changed key and obtains the expected linear value through Core alone. Additional installed SDK/CLI snapshots, value evaluations and comparisons agree for twenty-two real/fixture package pairs, including weighted, incomplete and changed-key cases. Original input hashes remain unchanged. The prepared CI includes linking the new standalone consumer; no hosted run or publication is claimed.

Gitleaks 8.30.1 reports zero findings across 367 publishable source files and local Git history. Five milestone commits and the archive remain local, without push, tag or release publication. Weighted segment evaluation, extrapolation/default resolution, legacy animation smart-name identities, transform curves, notifications/attributes, compressed playback, animation hierarchy evaluation, skinning and further cooked/native mesh formats remain future work.
