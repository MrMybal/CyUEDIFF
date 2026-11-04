# Visual review - 0.72

The embeddable CyUI views and the Windows desktop share the same read-only inspection components. English is the default interface language; French is built in. Asset names, GUIDs and serialized values retain their original text.

## Blueprint and material nodes

Open the Blueprint graphs or Material tab, choose a graph, then select a node on its canvas or in the Node list. The inspector shows typed stored properties and pins/inputs/outputs with Before, After and Change columns. Select a property row for its value preview. Its own filter can target a nested field or text value.

The graph search matches node titles and stable identities. Changed only filters the navigation list; the complete canvas retains connections for context. An explicit canvas click reveals a node hidden by these filters. Previous/next changed-node actions wrap within matching nodes. Focus selected node centers that node. Clearing a selection returns the full space to the canvas. A narrow panel places the inspector below the graph while retaining the viewed world center.

Navigation lists nodes present on the active side. Switch Before/After to examine added/removed nodes on that side. Connection changes also mark their endpoints. Inspection does not execute Blueprints, shader code or plugin factories.

Material instances have a separate stored-override table. Its search includes name, kind, association/index identity and displayed values; Changed only omits identical overrides. Parent values are not evaluated.

## Mesh changes

Changed edges only hides unchanged wireframe edges. Fit changed geometry frames the retained changed edges, excluding distant unchanged geometry. Perspective, Front, Right and near-Top presets complement orbit/pan/zoom; fitting retains the chosen orientation.

The viewer uses serialized mesh element identities and the prepared edge cache. It is a source wireframe inspection, with an explicit 100,000-edge display cap. It does not imply skeletal, collision, Nanite or surface-rendering support.

## Texture changes

Pixel difference mask switches between the source preview and thresholded changes. Fit changed pixels frames their bounding region. Actual pixels uses one screen pixel per source pixel; Fit image restores a whole-image view. The display reports changed count/percentage, maximum delta and mean absolute error.

Core comparisons return changed_bounds, also serialized as changedBounds with x, y, width and height. The region is half-open: [x,x+width) by [y,y+height). Identical or incomparable images have null JSON bounds. Bounds are computed even when no heatmap is requested. All four channels count, including alpha; threshold equality is unchanged.

HDR regions come from floating-point source samples before display tone mapping. LDR comparisons use encoded RGBA8 samples. Different dimensions and incompatible sample spaces remain explicit.

The standalone [image consumer](../examples/image_consumer/main.cpp) demonstrates these Core APIs without CyUI, Git, Unreal or a renderer.

## Workspaces and embedding

Explicit workspace files retain graph/node selection by snapshot identity, node/property/instance filters, camera transforms, image pan/zoom and difference-mask selection, independently for both sides. Node canvas IDs and GPU handles are not saved. Invalid state is rejected before applying it; missing identities do not select an unrelated node. Restoring node selection preserves an independently saved graph camera.

Embedded hosts can use GraphInspector directly, or call BlueprintView::select_node / MaterialView::select_node and next_changed_node. Bind packages and texture resources before restoring view state. Call AssetView::register_translations during setup, or register the shared inspector/property translations when embedding only those components.

## Local visual validation

The Windows desktop build includes cyuediff_visual_navigation_tests. It renders synthetic Blueprint, material, instance, mesh and texture comparisons, empty filtered views and a narrow layout through a hidden Direct3D11/WARP window, across all five themes and both languages. It protects the package snapshots and checks changed-region navigation. It never opens Unreal or writes appearance preferences.

To retain captures outside Git:

    build-native/Release/cyuediff_visual_navigation_tests.exe --capture-dir build-native/visual-navigation-072
