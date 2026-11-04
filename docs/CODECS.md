# Décodeurs de blocs — tranche 0.4

Textures et MeshDescriptions partagent maintenant la lecture de `FCompressedBuffer` et un contrat `BlockDecoder`. Le core valide le buffer et son index de blocs, alloue une sortie bornée, puis appelle le décodeur uniquement pour les blocs comprimés. Les blocs stockés bruts sont copiés directement. La méthode 0 est autonome ; les méthodes 3 et 4 nécessitent actuellement un callback fourni explicitement.

```cpp
cyuediff::BulkDecodeOptions options;
options.decoder = [](std::uint32_t method, const std::uint8_t* input,
                     std::size_t input_bytes, std::uint8_t* output,
                     std::size_t output_bytes) -> std::size_t {
    // Appeler ici le codec de l'application en respectant les tailles des buffers.
    return 0; // codec absent ou échec
};
auto bytes = cyuediff::decode_bulk_payload(payload, options);
```

Le callback renvoie le nombre exact d'octets écrits ; toute autre valeur fait échouer la lecture. Les limites par défaut portent sur le payload stocké et la sortie décompressée (256 Mio chacun). La mémoire de travail interne au codec appartient au fournisseur du callback et n'est pas bornée par ce compteur. L'hôte doit choisir un décodeur adapté et gérer son usage concurrent. Le core n'active aucun codec à partir des noms contenus dans un asset.

## Adaptateur Oodle local optionnel

Un adaptateur Windows x64/MSVC peut être construit avec un SDK Oodle Data fourni par l'hôte. L'installation source UE locale contient ce SDK sous `Engine/Source/Runtime/OodleDataCompression/Sdks/<version>`. Le chemin doit contenir `include/oodle2.h` et `lib/Win64/oo2core_win64.lib`.

```powershell
python scripts/build.py --cyui chemin/CyUI --oodle-sdk chemin/OodleDataCompression/Sdks/2.9.12
cyuediff geometry SM_Wall.uasset --codec build-native/engine-codecs/Release/cyuediff_oodle.dll --out wall.json
cyuediff preview T_Noise.uasset --codec build-native/engine-codecs/Release/cyuediff_oodle.dll --out noise.png
```

Équivalent CMake : `-DCYUEDIFF_OODLE_SDK_DIR=<sdk>`. Le module est construit séparément et n'est pas installé avec `CyUEDiff::Core`. Son binaire local intègre la bibliothèque Oodle fournie. Aucun header ou binaire de ce SDK n'est copié dans les sources du projet. Une configuration CMake existante conserve son chemin SDK en cache ; utiliser un nouveau dossier de build ou une valeur vide pour le désactiver.

`--engine-root` continue de décrire le moteur et les contenus du projet ; il ne charge pas de DLL et ne compile pas de codec automatiquement. Le module utilisé doit être indiqué avec `--codec` ou fourni par l'application via l'API. Aucun moteur ou éditeur n'est lancé. L'adaptateur appelle l'API Oodle avec le mode de vérification des entrées et vérifie la taille effectivement produite. Il ne fournit pas de décodeur LZ4.

## Contrat du module dynamique

`load_block_decoder(chemin)` charge explicitement un module Windows et retourne un callback qui maintient ce module chargé tant qu'une copie du callback existe. Les autres plateformes peuvent fournir directement leur callback C++ ; le chargeur dynamique n'y est pas encore implémenté.

Le module doit exporter cette ABI C, avec tailles `size_t` de la plateforme :

```cpp
extern "C" std::size_t cyuediff_decode_block_v1(
    std::uint32_t method, const std::uint8_t* input, std::size_t input_bytes,
    std::uint8_t* output, std::size_t output_bytes);
```

Le module et l'hôte doivent avoir la même architecture. Le callback ne doit ni écrire au-delà de la sortie, ni conserver les pointeurs après l'appel, ni laisser traverser une exception son ABI C. Il renvoie 0 pour une méthode inconnue ou un échec. Le chargeur utilise un chemin résolu explicitement et les chemins système/dépendances du module, sans recherche dans le répertoire courant.

Le CRC couvre l'en-tête et la table de blocs ; le SHA-256 optionnel compare le payload aux octets inspectés. Le BLAKE3 contenu dans Unreal sert ici à vérifier la cohérence d'identité entre les descripteurs et le buffer ; il n'est pas recalculé sur la sortie.

Validation locale : SDK 2.9.12, trois meshes UE 5.3.2, ainsi que la texture PNG Oodle `T_Noise` 2048 × 2048. Le core installé a consommé cet adaptateur depuis une application cliente indépendante de CyUI. Sans adaptateur, les mêmes métadonnées restent inspectables et la demande de géométrie/image comprimée produit une erreur explicite.
