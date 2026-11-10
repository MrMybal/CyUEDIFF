# Décodeurs propres aux applications — 0.8

Le header public `<cyuediff/decoders.hpp>` permet à une application de fournir ses décodeurs C++ au core. Ils lisent les structures natives d’une propriété balisée, ou la fin native d’un export après ses propriétés. Le core conserve le parcours des packages, la résolution des noms/références, les bornes et le diff.

## Enregistrer un schéma

```cpp
#include <cyuediff/decoders.hpp>
using namespace cyuediff;

AssetDecoder decoder;
decoder.id = "mygame.weather/1";
decoder.kind = DecoderKind::struct_value;
decoder.target = "WeatherSettings"; // nom exact dans le tag sérialisé
decoder.profile.ue4 = {522, 522};
decoder.profile.ue5 = {1012, 1012}; // versions de fichier, pas numéros marketing UE
// Ajouter ici les GUID/versions réellement nécessaires au format de votre projet :
// decoder.profile.custom_versions["0123456789abcdef0123456789abcdef"] = {3, 5};
decoder.decode = [](DecoderInput &input, const DecoderContext &) -> Json {
    // Exemple de disposition fournie par l’application, pas un format Unreal intégré.
    auto intensity = input.get<float>();
    auto preset = input.name();
    return {{"intensity", intensity}, {"preset", preset}};
};
ReadOptions options;
options.decoders = std::make_shared<const DecoderRegistry>(
    std::vector<AssetDecoder>{decoder});
auto before = Package::open("before/Weather.uasset", options);
auto after = Package::open("after/Weather.uasset", options);
auto diff = compare(before, after);
```

Les intervalles sont inclusifs. Un GUID exigé mais absent ne correspond jamais ; les autres GUID du package restent permis. Les intervalles laissés par défaut acceptent toute valeur prise en charge par le lecteur de packages. L’hôte doit donc déclarer les versions qu’il connaît. Les GUID utilisent les 32 chiffres hexadécimaux minuscules publiés par `PackageSummary::custom_versions`, sans tirets.

Pour `DecoderKind::native_export`, `target` est exactement le chemin de classe résolu de l’export, par exemple `/Script/MyGame.WeatherAsset`. Il n’y a ni recherche de sous-classes ni wildcard. Pour une structure, utiliser exactement le nom sérialisé : les anciens tags n’offrent pas nécessairement un chemin complet de type. `DecoderContext` expose le résumé du package, les métadonnées de l’export et `exact_size`. Lors d’une propriété, le tableau de propriétés de cet export n’est pas encore publié ; le callback doit utiliser ses métadonnées et son entrée.

Chaque enregistrement possède un ID ASCII unique, sans espaces, de 1 à 256 caractères. Il doit identifier le schéma du JSON produit et évoluer si sa signification change. Une cible vide, un callback absent, un GUID mal formé ou un intervalle inversé sont refusés avec `decoder-registration`.

Le registre est immuable après construction, partagé via `ReadOptions`, et indexé par genre/cible. Plusieurs applications ou lectures peuvent avoir des registres distincts ; il n’existe pas de registre global. Les captures des callbacks restent sous la responsabilité de l’hôte, notamment si plusieurs workers partagent le registre. Les entrées et références du contexte sont empruntées pour la durée du callback.

## Bornes et sélection

| Entrée | Fenêtre | Consommation exigée |
| --- | --- | --- |
| Structure directement balisée | Payload exact de la propriété | Tous les octets |
| Structure dans un tableau | Du début de l’élément à la fin du payload du tableau | Au moins un octet par élément ; le tableau entier doit finir exactement à sa borne |
| Fin native d’un export | Après les propriétés jusqu’à la fin de l’export | Tous les octets |

Les éléments de tableaux n’ont pas chacun une taille générique disponible. `exact_size=false` le signale : le schéma du callback doit déterminer la fin de son élément. Le core borne alors le tableau, sans pouvoir certifier les frontières internes d’un schéma fourni par l’hôte.

`DecoderInput` fournit `get<T>()` en little-endian, `read`, `skip`, `count`, `string` (FString), `name` (FName résolu), `reference` et `guid`. `position` est relatif au début de la fenêtre ; `remaining` donne les octets encore disponibles. Il n’y a pas de seek, d’accès au fichier entier, de chargement récursif ou d’API d’écriture. Utiliser des entiers de largeur fixe, `float` ou `double`, et `uint8_t` pour les booléens.

La sélection suit ces règles :

1. Aucun profil correspondant : le chemin intégré habituel reste utilisé.
2. Un profil correspondant : le callback explicite prend priorité sur le lecteur intégré de ce type. Une fin native remplace toute la lecture native intégrée de cet export.
3. Plusieurs profils correspondants : `decoder-ambiguous`, conservation opaque. L’ordre d’enregistrement ne décide jamais.
4. Un callback sélectionné échoue : conservation opaque, sans nouvelle tentative par un autre lecteur.

Une surcharge native de Texture2D ou MeshDescription ne remplit donc pas les collections spécialisées `textures`/`mesh_sources` du lecteur intégré. Les callbacks de cette API retournent du JSON pour le snapshot général ; ils ne créent pas automatiquement de projections spécialisées ni de widgets CyUI.

`max_property_bytes` borne la fenêtre d’entrée, le total des textes retournés et la taille JSON sérialisée. `max_string_bytes` borne chaque chaîne/clé ; `max_properties` borne les comptes lus et le nombre total de nœuds du JSON retourné ; `max_depth` borne sa profondeur. Le résultat doit être du JSON ordinaire, UTF-8 valide, avec des nombres finis. Représenter explicitement NaN/Infinity si le schéma les prévoit. Les callbacks sont du code de confiance exécuté dans le processus hôte : ces contrôles ne limitent pas leurs propres allocations ou leur durée d’exécution.

Une exception, une lecture hors fenêtre, une consommation incorrecte ou un résultat invalide retire la valeur décodée. Un échec de structure rend la propriété opaque et laisse le lecteur poursuivre à la propriété suivante ; un échec natif conserve la fin d’export opaque avec diagnostic. Les empreintes SHA-256 suivent `ReadOptions::fingerprints`. Un callback qui intercepte une erreur d’entrée reste en échec, même s’il retourne ensuite une valeur.

Les lectures de propriétés sont également bornées à leur payload, celles des scripts et des exports à leurs plages. Les formats d’en-tête futurs, licensee et les propriétés non versionnées gardent leurs refus ou leur couverture opaque habituels : le registre ne fournit pas un backend de package ni un schéma complet de réflexion Unreal. `properties=false` désactive aussi ces callbacks. Les interrupteurs `native_graphs`, `native_textures`, `native_meshes`, `native_materials` concernent les lecteurs intégrés ; le registre explicite reste indépendant.

## Snapshot et Git

Une valeur décodée devient :

```json
{"decoder":"mygame.weather/1","data":{"intensity":0.5,"preset":"Rain"}}
```

Elle se trouve sous `properties/<nom>/value` pour une structure, sous `native` pour une fin d’export. La comparaison structurée confronte les membres de `data` lorsque les identités de définition sont compatibles ; une définition explicite incompatible conserve les valeurs lisibles mais marque leur correspondance comme indéterminée. Les instantanés bruts peuvent différer sous deux interprétations sans prouver un changement stocké. Les callbacks de l’hôte gardent une obligation d’identité stable, versionnée et honnête. Les empreintes binaires restent distinctes des valeurs interprétées.

Le même `ReadOptions` est accepté par `GitRevision::open` et les deux surcharges de `compare_git`. L’adaptateur Git/LFS continue de posséder et vérifier les fichiers matérialisés. Aucun module de décodage n’est découvert ou chargé automatiquement ; cette tranche est une API C++, sans commande CLI chargeant du code de projet.

## Exemple installé et validation

`examples/decoder_consumer` consomme uniquement `CyUEDiff::Core` via `find_package`. Son schéma `ExampleStruct` et la fin native de `TestAsset` sont des fixtures contrôlées : ils ne décrivent pas le format natif générique d’un vrai DataAsset. Le test génère une version intégrée et une version `.uexp` séparée :

```powershell
build-native/Release/cyuediff_decoder_tests.exe --emit-fixtures build-native/decoder-fixtures
cmake --install build-native --config Release --prefix build-native/install
cmake -S examples/decoder_consumer -B build-native/decoder-consumer -DCMAKE_PREFIX_PATH="D:/_Project/CyUEDIFF/build-native/install"
cmake --build build-native/decoder-consumer --config Release
python scripts/validate_decoders.py --consumer build-native/decoder-consumer/Release/custom_diff.exe --cli build-native/Release/cyuediff.exe --fixtures build-native/decoder-fixtures
```

Le script vérifie le changement contrôlé `42 → 44`, l’égalité sur un même fichier et les empreintes d’entrée intactes. Ajouter `--sample <asset>` pour vérifier que le client installé avec registre vide produit exactement le même rapport que la CLI. La session locale a vérifié sept assets réels UE 5.3.2/5.5.4, comprenant deux Blueprints, deux matériaux, une instance, une texture et un mesh. Aucun nouveau format natif de plugin réel n’est certifié par ces essais.

## Declarative descriptions

Since 0.173, hosts can create this registry from bounded JSON descriptions with `BinaryLayouts::decoder_registry()`, without a C++ callback for each serialized type. See [binary layouts](BINARY_LAYOUTS.md). Exact target/profile selection and opaque-on-error behavior are shared with this API.

Source development 0.191 adds [per-binding native definition identity](DECODER_IDENTITY.md). Interpretation hashes stay separate from asset members; explicit incompatible definitions preserve readable values and mark native correspondence unknown.

Source development 0.192 retains [stored struct GUID/owner identities](STRUCT_IDENTITY.md), supports exact GUID selectors and separates matched profile applicability from immutable declarative program identity.
