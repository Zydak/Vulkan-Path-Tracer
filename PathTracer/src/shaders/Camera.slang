module Camera;

import Ray;
import Sampler;

public struct Camera
{
    matrix<float, 4, 4> m_ViewInverse;
    matrix<float, 4, 4> m_ProjInverse;

    public __init(matrix<float, 4, 4> viewInverse, matrix<float, 4, 4> projInverse)
    {
        m_ViewInverse = viewInverse;
        m_ProjInverse = projInverse;
    }

    public Ray GetRay(float focalLength, float dofStrength, inout Sampler sampler, float2 pixelUV)
    {
        float4 target = mul(float4(pixelUV.x, pixelUV.y, 1, 1), m_ProjInverse);

        float3 Origin = mul(float4(0, 0, 0, 1), m_ViewInverse).xyz;
        float3 Direction = mul(float4(normalize(target.xyz), 0), m_ViewInverse).xyz;

        const float3 camRight = float3(m_ViewInverse[0][0], m_ViewInverse[1][0], m_ViewInverse[2][0]);
        const float3 camUp = float3(m_ViewInverse[0][1], m_ViewInverse[1][1], m_ViewInverse[2][1]);

        float3 focusPoint = Origin + Direction * max(focalLength, 0.0001);
        float2 randomOffset = (sampler.UniformFloat2() - 0.5) * dofStrength;
        Origin = Origin + randomOffset.x * camRight + randomOffset.y * camUp;

        Direction = normalize(focusPoint - Origin);

        return Ray(Origin, Direction);
    }
}