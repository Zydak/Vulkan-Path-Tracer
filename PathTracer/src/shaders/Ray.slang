module Ray;

import rtcommon;
import Payload;

public struct Ray
{
    float3 m_Origin = float3(0.0f);
    float3 m_Direction = float3(0.0f);
    float3 m_Weight = 1.0f;

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

    [mutating]
    public void Trace(RaytracingAccelerationStructure topLevelAS, uint flags, inout RTPayload payload)
    {
        payload.HitValue = float3(0.0f);
        payload.Weight = float3(1.0f);
        payload.RayDirection = m_Direction;
        payload.RayOrigin = m_Origin;

        RayDesc rayDesc = GetRayDesc();

        TraceRay(topLevelAS, flags, 0xff, 0, 0, 0, rayDesc, payload);

        m_Direction = payload.RayDirection;
        m_Origin = payload.RayOrigin;
        m_Weight *= payload.Weight;
    }

    public const inline float3 GetOrigin() { return m_Origin; }
    public const inline float3 GetDirection() { return m_Direction; }
    public const inline float3 GetWeight() { return m_Weight; }
}