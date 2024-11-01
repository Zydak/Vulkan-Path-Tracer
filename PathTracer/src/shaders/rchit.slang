import rtcommon;
import Sampler;
import Surface;
import Payload;
import Material;

struct Input
{
    [[vk::location(0)]] RTPayload Payload;
}

[[vk::push_constant]] PushConstant push;

[[vk::binding(2, 0)]]
StructuredBuffer<MeshAddress> uMeshAddresses;

[[vk::binding(3, 0)]]
StructuredBuffer<Material> uMaterials;

[shader("closesthit")]
void main(inout Input input, in float2 attribs)
{
    // Prepare Material
    Material material = uMaterials[InstanceIndex()];

    // Prepare Surface
    Surface surface;
    surface.Prepare(uMeshAddresses[InstanceIndex()], attribs, material.AnisotropyRotation);

    float3 V = -WorldRayDirection();
    V = surface.WorldToTangent(V);

    BSDFSample sample = material.SampleMetallic(input.Payload.Sampler, V);

    if (sample.L.z <= 0.0f)
        input.Payload.Depth = DEPTH_INFINITE;

    input.Payload.RayDirection = surface.TangentToWorld(sample.L);
    input.Payload.RayOrigin = surface.GetWorldPos();
    input.Payload.Weight = sample.BSDF / sample.PDF;
    input.Payload.HitValue = material.EmissiveColor.rgb * material.EmissiveColor.a;

    //input.Payload.HitValue = (surface.GetNormal() + float3(1.0f)) * float3(0.5f);
}