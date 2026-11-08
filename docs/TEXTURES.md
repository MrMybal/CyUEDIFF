# Textures — tranches 0.3 / 0.4

Le core peut lire les métadonnées de sources `Texture2D`, retrouver leur bulk data et produire des aperçus RGBA8 sans charger Unreal. La lecture native vise le profil moderne `FEditorBulkData` (version personnalisée UE5 Main 51 à 119). La validation réelle porte sur UE 5.3.2 ; les autres profils de cette plage restent à vérifier sur un corpus.

## Commandes

```powershell
cyuediff texture Content/T_Water.uasset --out texture.json
cyuediff preview Content/T_Water.uasset --out water.png
cyuediff preview Content/T_Raw.uasset --mip 1 --out mip1.png
cyuediff image-diff before/T_Water.uasset after/T_Water.uasset --threshold 2 --heatmap mask.png --out pixels.json
```

Les commandes acceptent `--project` et `--engine-root`. La comparaison accepte aussi `--before-project` et `--after-project`. `--object NOM` sélectionne un objet par son nom final ou son chemin complet lorsque plusieurs textures sont présentes. Sans sélection explicite, une seule source doit correspondre. `preview` exige une destination PNG ; son résumé JSON est envoyé sur stdout. Les sorties ne peuvent pas écraser les packages d'entrée, leurs compagnons ou le descripteur de projet.

Le JSON `cyuediff.textures/1` contient dimensions, format source, compression source, nombre de mips/slices/couches, offsets de blocs, réglages et description du payload. Les chemins de fichiers et offsets physiques sont omis du snapshot sémantique ; l'API `TextureSource::to_json(true)` les expose pour le diagnostic.

## Formats et stockage

| Donnée | Couverture actuelle |
| --- | --- |
| Index de fin de package | Versions 0, 1 et 2 ; entrées locales, référencées et virtualisées distinguées |
| Recherche du payload | Identité depuis l'objet natif et index validé du `.uasset`, `.uexp` ou `.upayload` ; aucune recherche de signatures d'image dans le package |
| `FCompressedBuffer` | En-tête, tailles, identité et CRC d'en-tête ; copie brute de la méthode 0 |
| JPEG / PNG source | Décodage d'un mip source unique dans un buffer méthode 0 |
| Source brute | G8, G16, BGRA8, RGBA8 et RGBA16 ; sélection de mip dans la chaîne source |
| Structure d'image | Un bloc 2D, une couche et une slice |
| UI | Vue `CyUI::Images` avec zoom/panoramique, cadrage et sélection avant/après ; ressource GPU fournie par l'hôte |

Depuis la tranche 0.4, la méthode de buffer 3 (Oodle) peut être décodée via un adaptateur local explicitement fourni avec `--codec` ou `TextureDecodeOptions::decoder`. La méthode 4 (LZ4) est identifiée et empreintée mais aucun adaptateur LZ4 n’est fourni. Voir [CODECS.md](CODECS.md). Cooked platforms, legacy bulk data, referenced resources, UDIM and volume/cube/array textures require other decoders. Virtualized editor resources can use an explicitly selected local filesystem store; see [VIRTUALIZED.md](VIRTUALIZED.md). Les sources HDR/flottantes 2D brutes sont prises en charge depuis 0.14 : voir [HDR.md](HDR.md). Choisir une installation moteur ne charge aucune DLL de codec automatiquement.

Pour les sources LDR, les aperçus sont des **échantillons RGBA8 encodés**. Aucune transformation sRGB/linéaire, profil ICC ou reconstruction de normal map n'est appliquée. Les sources 16 bits sont réduites à 8 bits pour l'aperçu : ce chemin ne permet pas d'établir une égalité en pleine précision. Les mips sont ceux présents dans la source, sans génération de mips de rendu. Le décodage JPEG peut différer légèrement d'un autre décodeur.

## Trois niveaux de comparaison

- `diff` compare objets/propriétés et publie `textureChanges`, une projection des métadonnées et empreintes du payload. Une modification de réglage et une modification du SHA-256 de la source sont identifiables par des chemins distincts. Cette projection recoupe les données de `changes` ; elle n'est pas une liste indépendante à additionner.
- `textconv` inclut les sources et leurs empreintes dans le snapshot, même lorsque les pixels ne sont pas décodables.
- `image-diff` décode explicitement les deux sources et compare leurs RGBA8. Pour chaque pixel, la différence maximale des quatre canaux doit être **strictement supérieure** au seuil pour être comptée. Le seuil est compris entre 0 et 255. L'erreur moyenne absolue et la différence maximale sont calculées avant application du seuil.

Le masque est rouge opaque pour les pixels changés et transparent ailleurs. Lorsque les dimensions diffèrent, le rapport signale `sameSize: false`, `pixelsComparable: false` et des métriques nulles. Un masque exige des dimensions identiques. Codes de sortie : 0 pour pixels identiques au seuil choisi, 1 pour changement, 2 pour erreur ou source non prise en charge. Le statut pixel `identical` ne signifie pas que les packages sont binaires identiques.

## API C++

```cpp
auto package = cyuediff::Package::open("T_Water.uasset");
if (package.textures.size() != 1) throw std::runtime_error("Choose a texture");
cyuediff::TextureDecodeOptions limits;
limits.max_pixels = 16ull * 1024 * 1024;
auto rgba = cyuediff::decode_texture(package.textures.front(), 0, limits);
cyuediff::write_png("water.png", rgba);
auto result = cyuediff::compare_images(rgba, rgba, 0, true);
```

`Package::open` ne décode pas les pixels. Il garde un descripteur vers le fichier source, qui doit rester disponible jusqu'à `decode_texture`. Par défaut, SHA-256 vérifie que le payload n'a pas changé entre ces appels. `ReadOptions::native_textures` désactive la lecture native et `fingerprints` désactive les empreintes. Les limites par défaut sont 256 Mio pour le payload, 64 millions de pixels et 512 Mio d'allocations internes au décodeur PNG/JPEG. Les buffers du payload, du résultat RGBA et du masque ont leur propre coût mémoire en plus de ce budget interne. L'hôte peut réduire les limites et exécuter les décodages sur ses workers.

The CRC checks the buffer header. The content ID is checked against object/index/header metadata; virtualized reads additionally require a full raw BLAKE3 match after decompression. Local embedded reads can opt in with `verify_raw_hash`. Le SHA-256 décrit les octets observés et permet de les comparer, sans certifier leur validité pour Unreal. Les lectures sont bornées par les exports/index et les formats inconnus restent explicites.

## Validation

La suite `texture` vérifie les formats bruts et encodés, mips, canaux/alpha, seuils, tailles, budgets mémoire, profils, trois versions d'index, stockage non local, CRC, bornes et fichiers modifiés après inspection.

```powershell
python scripts/validate_texture.py --asset chemin/vers/T_caustic.uasset
```

Ce script lit l'original puis réalise les mutations sur des copies dans le dossier de build. Sur le fichier local UE 5.3.2 de 512 × 512 : un réglage modifié conserve des pixels identiques ; une modification ciblée d'une table de quantification JPEG change 259 129 pixels. Cette copie sert à tester le lecteur et ne constitue pas un asset réécrit et validé dans Unreal : son identité de contenu n'est pas recalculée. Sept erreurs de CLI sont vérifiées, dont la protection des entrées et la collision des destinations. L'empreinte de l'original reste identique.

Deux aperçus réels ont été comparés à Pillow : JPEG `T_caustic` 512 × 512 (écart maximal 2/255) et PNG `T_Voronoi` 4096 × 4096 (égalité exacte). `T_Noise` 2048 × 2048 fournit le cas Oodle refusé sans codec, puis décodé en 0.4 avec l’adaptateur local. Aucun asset de ce corpus n'est redistribué. Rapports locaux : `build-native/texture-validation/report.json` et `corpus.json`. Les commandes de dessin CyUI sont testées sans backend graphique ; il n'y a pas encore d'application desktop autonome ni de revue visuelle de la vue native.

La commande `hdr-diff` compare les échantillons flottants avant la conversion d’affichage. Les aperçus HDR appliquent explicitement exposition, Reinhard et transfert sRGB ; voir [HDR.md](HDR.md).

## Explicit local virtualized payloads

Version 0.179 adds a local filesystem store for virtualized editor source payloads, including mandatory full raw BLAKE3 verification and independent before/after roots in CLI/Desktop. See [the backend contract and limits](VIRTUALIZED.md). Local embedded reads can opt into BLAKE3 verification; virtualized reads always require it.

## Explicit GPU pixel API

The reusable Core can also decode explicitly identified raw/BC1–BC7 platform mip bytes, including unsigned BC6H and floating RGBA. See [GPU_TEXTURES.md](GPU_TEXTURES.md) for exact footprints, stored-channel semantics and independent platform validation. Native cooked payload resolution remains a separate reader.
