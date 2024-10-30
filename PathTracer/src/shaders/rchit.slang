import rtcommon;
import Sampler;
import Surface;

struct Input
{
    [[vk::location(0)]] RtPayload Payload;
}

[[vk::push_constant]] PushConstant push;

[[vk::binding(2, 0)]]
StructuredBuffer<MeshAddress> uMeshAddresses;

[shader("closesthit")]
void main(inout Input input, in float2 attribs)
{
    // Prepare Surface

    Surface surface;
    surface.Prepare(uMeshAddresses[InstanceIndex()], attribs);

    Sampler s = Sampler(input.Payload.Seed);
    float r = s.UniformFloat();

    input.Payload.HitValue = (surface.GetNormal() + float3(1.0f)) * float3(0.5f);
    //input.Payload.HitValue = surface.GetNormal();
}