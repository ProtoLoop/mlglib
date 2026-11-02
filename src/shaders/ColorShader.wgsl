struct WorldTransform
{
    xform: mat4x4<f32>,
}

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

struct ViewProj
{
    view: mat4x4<f32>,
    proj: mat4x4<f32>,
    viewProj: mat4x4<f32>,
}

struct PointLight
{
    location: vec3<f32>,

    // Align to 16 bytes due to Vec3.
    pad0: f32,

    color: vec3<f32>,

    // Align to 16 bytes due to Vec3.
    pad1: f32,
}

struct PointLightBuffer
{
    count: u32,
    lights: array<PointLight>,
}

struct DirectionalLight
{
    direction: vec3<f32>,

    // Align to 16 bytes due to Vec3.
    pad0: f32,

    color: vec3<f32>,

    // Align to 16 bytes due to Vec3.
    pad1: f32,
}

struct DirectionalLightBuffer
{
    count: u32,
    lights: array<DirectionalLight>,
}

@group(0) @binding(0) var<storage, read> worldTransforms: array<WorldTransform>;
@group(0) @binding(1) var<storage, read> clipSpaceTransforms: array<ClipSpaceTransform>;
@group(0) @binding(2) var<storage, read> meshInstanceParams: array<MeshInstanceParams>;
@group(0) @binding(3) var<storage, read> pointLights: PointLightBuffer;
@group(0) @binding(4) var<storage, read> directionalLights: DirectionalLightBuffer;
@group(0) @binding(5) var<uniform> shadowViewProj: ViewProj;
@group(0) @binding(6) var shadowMap: texture_depth_2d;

@group(1) @binding(0) var shadowSampler: sampler_comparison;

@group(2) @binding(0) var texture0: texture_2d<f32>;
@group(2) @binding(1) var textureSampler: sampler;
@group(2) @binding(2) var<uniform> material: Material;

struct VSInput
{
    @location(0) inPosition: vec3<f32>,
    @location(1) inNormal: vec3<f32>,
    @location(2) inTexCoord: vec2<f32>,
}

struct FSInput
{
    @builtin(position) position: vec4<f32>,
    @location(0) shadowPos: vec4<f32>,
    @location(1) fragNormal: vec3<f32>,
    @location(2) texCoord: vec2<f32>,
    @location(3) @interpolate(flat) instanceIndex: u32,
}

@vertex
fn vs_main(input: VSInput, @builtin(instance_index) instance_index: u32) -> FSInput
{
    var output: FSInput;

    let transformIndex = meshInstanceParams[instance_index].transformIndex;
    let clipXform = clipSpaceTransforms[transformIndex].xform;
    let worldTransform = worldTransforms[transformIndex].xform;
    let localPos = vec4<f32>(input.inPosition, 1.0);

    output.position = clipXform * localPos;
    output.shadowPos = shadowViewProj.viewProj * worldTransform * localPos;
    output.fragNormal = normalize((worldTransform * vec4<f32>(input.inNormal, 0.0)).xyz);
    output.texCoord = input.inTexCoord;
    output.instanceIndex = instance_index;

    return output;
}

@fragment
fn fs_main(input: FSInput) -> @location(0) vec4<f32>
{
    var diffuse = vec3<f32>(0.0);

    let shadowNdc = input.shadowPos.xyz / input.shadowPos.w;
    let shadowUv = vec2<f32>(shadowNdc.x * 0.5 + 0.5, 0.5 - shadowNdc.y * 0.5);

    var visibility = 1.0;

    if all(shadowUv >= vec2<f32>(0.0)) && all(shadowUv <= vec2<f32>(1.0)) && shadowNdc.z >= 0.0
        && shadowNdc.z <= 1.0
    {
        visibility = textureSampleCompareLevel(shadowMap, shadowSampler, shadowUv, shadowNdc.z);
    }

    for(var i = 0u; i < directionalLights.count; i++)
    {
        let light = directionalLights.lights[i];
        let diff = max(-dot(input.fragNormal, light.direction), 0.0);
        diffuse += diff * material.color.rgb * light.color;

        // Only consider the first directional light for now.
        break;
    }

    diffuse *= visibility;

    let ambientFactor = 0.1;
    let ambient = ambientFactor * material.color.rgb;
    let color = clamp(diffuse + ambient, vec3<f32>(0.0), vec3<f32>(1.0));
    let litColor = vec4<f32>(color, material.color.a);

    return litColor * textureSample(texture0, textureSampler, input.texCoord);
}