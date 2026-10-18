# Asset Flow

Assets pass through three stages: raw files, intermediate definitions, and
cooked resources. Cooking turns definitions into data ready for runtime use.
The goal is to save cooked resources to files and load those at runtime instead
of importing glTF.

## Intended flow

```mermaid
flowchart TD
    A[glTF and geometry buffers] --> B[GltfLoader]
    B --> C[Intermediate definitions: SceneDef]
    D[Manual C++ construction or ShapeDefs] --> C
    C --> E[ResourceBundleBuilder]
    E --> F[Cooked ResourceBundle file]
    F --> G[Runtime scene and GPU resources]
    H[PNG / JPG textures] --> I[Texture cooking]
    I --> J[Separate compressed texture files]
    I --> L[Copies of original PNG / JPG files]
    F -->|Texture file references| K[Parallel texture loading]
    J --> K
    L --> K
    K --> G
```

Cooked textures stay in individual files so they can load in parallel. Each
texture resource stores its original path and flags indicating which additional
encodings are available. The loader derives the compressed filename from the
original path when both the availability flag and GPU support permit that
encoding. Compressed pixel data uploads without image decoding at runtime.

## Intermediate definitions

[`SceneDef`](../../src/SceneTypes.h) holds the data used to build a bundle:

- `MeshDef` holds vertices, indices, and a `MaterialDef`.
- `MaterialDef` describes surface properties and a texture path.
- `ModelDef` groups meshes under a name.
- Nodes describe transforms and hierarchy, refer to models by name, and can
  include physics definitions on roots.

[`GltfLoader`](../../src/GltfLoader.cpp) produces these definitions from glTF.
A glTF mesh becomes a `ModelDef`, and each primitive becomes a `MeshDef`.

Code can also build the definitions directly. For example, create a mesh with
[`ShapeDefs::Box`](../../src/ShapeDefs.h), assign its material, add it to a named
`ModelDef`, and reference that model from a node in `SceneDef`. Hand-authored,
generated, and imported definitions all use the same cooking path.

## Cooking a bundle

[`ResourceBundleBuilder::Build`](../../src/ResourceBundle.cpp) takes a
`SceneDef` and produces a [`ResourceBundle`](../../src/ResourceBundle.h). It
packs geometry and resource records into one buffer, shares duplicate strings,
texture paths, and materials, resolves model names to indices, computes bounds,
and flattens the node hierarchy.

The bundle owns its buffer. Its accessors return read-only views that remain
valid while the bundle exists. Intermediate definitions can be discarded after
building it.

## What exists today

[`Level::CreateTask`](../../src/Level.cpp) accepts scene definitions, imports
glTF, or loads cooked resource bundles. [`Scene`](../../src/Scene.cpp) creates
runtime nodes and physics, and [`View`](../../src/View.cpp) creates GPU resources.

The [Cook tool](../../tools/Cook/Cook.cpp) writes resource bundles and separate
compressed texture files. It also copies the original PNG/JPG files for devices
without support for the available compressed formats. No mipmaps are generated
for these PNG/JPEG files.  Mipmaps are only generated for cooked/compressed textures.

Before generating mipmaps, the cooker resizes the base image to satisfy the
selected compression format’s block dimensions, rounding up where necessary.
It then generates a full mip chain down to 1×1 and compresses each level.
Partial blocks in smaller levels repeat edge pixels instead of rescaling the
image.

Each cooked texture file contains a header with the base dimensions, format,
and mip count, followed by tightly packed mip levels from largest to smallest.
The file does not include GPU upload-row padding.

At runtime, texture-loading workers copy the mip data into mapped staging
memory, adding the required row padding for each level. The main thread creates
the GPU texture and records the copies for all levels.

Currently supported compression formats:

- BC7

The Cook tool marks the generated encodings as available when building the bundle. Bundles built
directly from scene definitions or glTF use the default flags, which indicate no
additional encodings.

[TextureLoader](../../src/TextureLoader.cpp) selects a compressed file only when the
resource advertises its encoding and the device supports it.
If the GPU supports none of the available compression formats, or no compressed
encoding is available, it uses the original PNG/JPEG file. The image is decoded
at runtime and uploaded without mipmaps (only the base level).
[TextureFetcher](../../src/TextureFetcher.cpp) manages the collection of
texture-loading tasks.
