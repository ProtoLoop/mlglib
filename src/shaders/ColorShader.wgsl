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

struct Camera
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
@group(0) @binding(5) var<uniform> camera: Camera;

@group(1) @binding(0) var texture0: texture_2d<f32>;
@group(1) @binding(1) var textureSampler: sampler;
@group(1) @binding(2) var<uniform> material: Material;

struct VSInput
{
    @location(0) inPosition: vec3<f32>,
    @location(1) inNormal: vec3<f32>,
    @location(2) inTexCoord: vec2<f32>,
}

struct FSInput
{
    @builtin(position) position: vec4<f32>,
    @location(0) fragNormal: vec3<f32>,
    @location(1) texCoord: vec2<f32>,
    @location(2) @interpolate(flat) instanceIndex: u32,
}

@vertex
fn vs_main(input: VSInput, @builtin(instance_index) instance_index: u32) -> FSInput
{
    var output: FSInput;

    let transformIndex = meshInstanceParams[instance_index].transformIndex;
    let clipXform = clipSpaceTransforms[transformIndex].xform;
    let worldTransform = worldTransforms[transformIndex].xform;

    output.position = clipXform * vec4<f32>(input.inPosition, 1.0);
    output.fragNormal = normalize((worldTransform * vec4<f32>(input.inNormal, 0.0)).xyz);
    output.texCoord = input.inTexCoord;
    output.instanceIndex = instance_index;

    return output;
}

@fragment
fn fs_main(input: FSInput) -> @location(0) vec4<f32>
{
    var diffuse = vec3<f32>(0.0);

    for(var i = 0u; i < directionalLights.count; i++)
    {
        let light = directionalLights.lights[i];
        let diff = max(-dot(input.fragNormal, light.direction), 0.0);
        diffuse += diff * material.color.rgb * light.color;
    }

    let ambientFactor = 0.1;
    let ambient = ambientFactor * material.color.rgb;
    let color = clamp(diffuse + ambient, vec3<f32>(0.0), vec3<f32>(1.0));
    let litColor = vec4<f32>(color, material.color.a);
    return litColor * textureSample(texture0, textureSampler, input.texCoord);
}

/*struct VSSphereOut
{
    @builtin(position) pos : vec4<f32>,
    @location(0) uv        : vec2<f32>,   // [-1,1]
    @location(1) centerVS  : vec3<f32>,
};

@vertex
fn vs_sphere_main(@builtin(vertex_index) vid : u32) -> VSSphereOut
{
    var out : VSSphereOut;

    let centerVS = (camera.view * vec4<f32>(sphere.centerWorld, 1.0)).xyz;

    let uv = quad(vid);

    // billboard quad in view space
    let posVS = centerVS + vec3<f32>(uv * sphere.radius, 0.0);

    out.pos = camera.proj * vec4<f32>(posVS, 1.0);
    out.uv = uv;
    out.centerVS = centerVS;

    return out;
}
*/