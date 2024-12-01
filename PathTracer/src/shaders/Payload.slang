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

    public bool InMedium = false;
    public float MediumDensity = 0.0;
    public float MediumAnisotropy = 1.0;
    public float3 MediumColor = float3(1.0);

    public float3 GBufferColor = float3(0.0f);
    public float3 GBufferNormal = float3(0.0f);
    public bool GBufferPass = false;

    public __init() {}
};