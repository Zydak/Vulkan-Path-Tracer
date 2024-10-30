module Ray;

import rtcommon;

public struct Ray
{
    float3 m_Origin = float3(0.0f);
    float3 m_Direction = float3(0.0f);
    float3 m_Weight = 0.0f;

    public __init(float3 position, float3 direction)
    {
        m_Origin = position;
        m_Direction = direction;
    }

    public const inline RayDesc GetRayDesc()
    {
        RayDesc rayDesc;
        rayDesc.TMin = 0.001;
        rayDesc.TMax = 10000.0;
        rayDesc.Origin = m_Origin;
        rayDesc.Direction = m_Direction;

        return rayDesc;
    }

    public void Trace(RaytracingAccelerationStructure topLevelAS, uint flags, inout RtPayload payload)
    {
        payload.HitValue = float3(0.0f);
        payload.Weight = float3(0.0f);
        payload.RayDirection = m_Direction;

        RayDesc rayDesc = GetRayDesc();

        TraceRay(topLevelAS, flags, 0xff, 0, 0, 0, rayDesc, payload);
    }

    public const inline float3 GetOrigin() { return m_Origin; }
    public const inline float3 GetDirection() { return m_Direction; }
    public const inline float3 GetWeight() { return m_Weight; }
}