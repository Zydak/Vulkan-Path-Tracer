module Camera;

import Ray;

public struct Camera
{
    matrix<float, 4, 4> m_ViewInverse;
    matrix<float, 4, 4> m_ProjInverse;

    public __init(matrix<float, 4, 4> viewInverse, matrix<float, 4, 4> projInverse)
    {
        m_ViewInverse = viewInverse;
        m_ProjInverse = projInverse;
    }

    public Ray GetRay(float2 pixelUV)
    {
        float4 target = mul(float4(pixelUV.x, pixelUV.y, 1, 1), m_ProjInverse);

        float3 Origin = mul(float4(0, 0, 0, 1), m_ViewInverse).xyz;
        float3 Direction = mul(float4(normalize(target.xyz), 0), m_ViewInverse).xyz;

        return Ray(Origin, Direction);
    }
}