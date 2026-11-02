struct ClipSpaceTransform
{
    xform: mat4x4<f32>,
}

struct MeshInstanceParams
{
    transformIndex: u32,
}

struct Material
{
    color: vec4<f32>,
    metalness: f32,
    roughness: f32,
    // Align to 16 bytes due to Vec4.
    pad0: f32,
    pad1: f32,
}

@group(0) @binding(0) var<storage, read> clipSpaceTransforms: array<ClipSpaceTransform>;
@group(0) @binding(1) var<storage, read> meshInstanceParams: array<MeshInstanceParams>;

struct VSInput
{
    @location(0) inPosition: vec3<f32>,
}

struct FSInput
{
    @builtin(position) position: vec4<f32>,
}

@vertex
fn vs_main(input: VSInput, @builtin(instance_index) instance_index: u32) -> FSInput
{
    var output: FSInput;

    let transformIndex = meshInstanceParams[instance_index].transformIndex;
    let clipXform = clipSpaceTransforms[transformIndex].xform;

    output.position = clipXform * vec4<f32>(input.inPosition, 1.0);

    return output;
}