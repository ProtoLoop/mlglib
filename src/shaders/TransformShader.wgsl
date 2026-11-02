struct WorldTransform
{
    xform : mat4x4<f32>,
};

struct ClipSpaceTransform
{
    xform : mat4x4<f32>,
};

struct ViewProj
{
    view      : mat4x4<f32>,
    proj      : mat4x4<f32>,
    viewProj  : mat4x4<f32>,
};

@group(0) @binding(0) var<storage, read> inMats : array<WorldTransform>;
@group(0) @binding(1) var<storage, read_write> outMats : array<ClipSpaceTransform>;

@group(1) @binding(0) var<uniform> viewProj : ViewProj;

override WorkgroupSizeOverride: u32;

@compute @workgroup_size(WorkgroupSizeOverride)
fn cs_main(@builtin(global_invocation_id) gid: vec3<u32>)
{
    let i = gid.x;
    let count = arrayLength(&inMats);

    if (i >= count)
    {
        return;
    }

    outMats[i].xform = viewProj.viewProj * inMats[i].xform;
}