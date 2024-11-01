module Payload;

import rtcommon;
import Sampler;

public struct RTPayload
{
    public float3 HitValue = 0.0f;
    public float3 RayOrigin = float3(0.0f);
    public float3 RayDirection = float3(0.0f);
    public float3 Weight = 1.0f;

    public float3 SurfaceAlbedo = 0.0f;
    public float3 SurfaceNormal = 0.0f;

    public Sampler Sampler;
    public uint Depth = 0;

    public __init() {}
};