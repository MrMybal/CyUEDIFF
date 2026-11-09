# Paged structured property inspection

The reusable Core `property_page` API and EN/FR `PropertyBrowserView` provide a bounded way to inspect large stored JSON projections. The desktop uses them in cooked object properties and loose source-package inspection. A large expanded table automatically opens the browser; smaller objects can switch with **Browse by level**. The expanded presentation and raw JSON remain available.

## Desktop workflow

Select an object, enable **Browse by level**, select a container-valued row and choose **Open selected value**. The page shows its root summary and at most 128 direct children. **Parent value** and **Object root** return to broader scopes. Previous/next/last-page controls and an explicit child/array index reach values beyond the expanded table's 20,000-row ceiling. An index jump selects and scrolls to its exact row, including unchanged values.

Typed property values are unwrapped for child enumeration. **Serialization fields** exposes the stored wrapper fields, including type descriptors, mapping/layout provenance and serialized-zero information. Qualified Unreal field names use a short display label; their full paths remain available at hover and copy. Paths are actual escaped JSON pointers; their `/value` segments are retained. Stored fields named `@tag`, empty field names and literal escape sequences do not become synthetic groups or aliases.

Loose packages offer separate **Stored properties**, **Native fields** and **Opaque metadata** roots backed by the immutable loaded package. Cooked objects use their decoded property projections. Neither path reopens files while drawing or retains borrowed JSON pointers. The browser owns only its bounded prepared strings; its owner supplies the current snapshots for navigation.

Filters and **Changed only** apply to the current page. Root summaries can indicate a deeper difference, but filtering does not search unseen pages. Open the changed container to continue. Pagination intentionally collapses descendants and is reported separately from safety truncation. A replacement between incompatible container types is shown as a whole-value change; inspect the individual snapshot separately to explore that side.

Workspaces retain object identity, root choice, scope, page offset, filters, selected value and panel layout. Restoration binds the object/scope before restoring the selected path. Missing scopes return to the object root; stale source identities do not apply a child selection to another export. Older workspace states without these optional fields still open.

## Core contract

```cpp
#include <cyuediff/property_page.hpp>

cyuediff::PropertyPageOptions options;
options.first = 39936;
auto page = cyuediff::property_page(&before, &after, "/Items", options);
```

The result contains the root summary plus a page of children, their actual raw paths, exact change kinds, before/after presence and expansion flags. Missing scopes return `available=false`. A present JSON `null` remains available. No source pointers survive the call, and inputs are not modified. The [installed Core-only example](../examples/property_page_consumer/) reads host-supplied bounded JSON snapshots without Unreal, Git, CyUI, codecs or file writes.

A null input pointer means **known absence**, allowing additions/removals. Unknown or undecoded peer data must be handled by the host. The desktop inspects one known snapshot when cooked properties or selected loose property/native coverage are unavailable, so an unread subtree does not turn into removed values.

Defaults are 128 children (maximum 512), 2,048 input bytes per scalar string preview, 4,096-byte scope/child paths, 128 scope/comparison levels and four million comparison nodes. Direct-child inventory is limited to two million. The UI uses a sixteen-million-node comparison budget. Exact deep classification is bounded and cancellable even when only one page is materialized. Limits fail explicitly; they do not silently declare equality or publish a partly classified page. The UI retains the current page after a failed navigation and displays the diagnostic.

This is stored-value navigation. It neither decodes additional Unreal serializers nor reconstructs missing defaults, evaluates simulations or replaces dedicated geometry/animation inspectors. Private actual corpus captures remain outside the source distribution.


## Large source presentation — 0.187

The asset host starts large source objects in this scoped browser instead of preparing oversized expanded/raw/text views. UI JSON is bounded to 4 MiB and 50,000 lines per input; it is either complete or explicitly unavailable, never truncated asset data. Core values, nested paths and CLI JSON export retain the full decoded content. See [SOURCE_READ_LIMITS.md](SOURCE_READ_LIMITS.md).
