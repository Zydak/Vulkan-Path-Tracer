module Surface;

import rtcommon;

public struct Surface
{
    float3 m_WorldPos;
    float2 m_TexCoords;

    float3 m_Normal;
    float3 m_Tangent;
    float3 m_Bitangent;

    [mutating]
    public void Prepare(MeshAddress meshAddress, float2 barycentrics, float anisotropyRotation)
    {
        const float3 barycentricCoords = float3(1.0f - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y);

        uint instanceIndex = InstanceIndex();
        uint primitiveIndex = PrimitiveIndex();

        int i1 = meshAddress.IndexBuffer[(primitiveIndex * 3)];
        int i2 = meshAddress.IndexBuffer[(primitiveIndex * 3) + 1];
        int i3 = meshAddress.IndexBuffer[(primitiveIndex * 3) + 2];

        Vertex v1 = meshAddress.VertexBuffer[i1];
        Vertex v2 = meshAddress.VertexBuffer[i2];
        Vertex v3 = meshAddress.VertexBuffer[i3];

        float3 pos1 = v1.Position;
        float3 pos2 = v2.Position;
        float3 pos3 = v3.Position;

        // Texture Coordinates
        m_TexCoords = v1.TexCoord.xy * barycentricCoords.x + v2.TexCoord.xy * barycentricCoords.y + v3.TexCoord.xy * barycentricCoords.z;

        const float3 pos = pos1 * barycentricCoords.x + pos2 * barycentricCoords.y + pos3 * barycentricCoords.z;
        m_WorldPos = mul(ObjectToWorld3x4(), float4(pos, 1.0)).xyz;

        float3 normal = v1.Normal.xyz * barycentricCoords.x + v2.Normal.xyz * barycentricCoords.y + v3.Normal.xyz * barycentricCoords.z;
        //float3 normal = normalize(cross(pos2 - pos1, pos3 - pos1));

        normal = normalize(mul(normal, WorldToObject()).xyz);

        float3 V = -WorldRayDirection();
        
        if (dot(V, normal) < 0.0f)
        {
            normal = -normal;
        }

        m_Normal = normal;

        CalculateTangents(m_Normal, m_Tangent, m_Bitangent);

        float rotation = anisotropyRotation / 360.0f * M_PI;
        m_Tangent = m_Tangent * cos(rotation) + cross(m_Normal, m_Tangent) * sin(rotation) + m_Normal * dot(m_Normal, m_Tangent) * (1.0f - cos(rotation));
        m_Bitangent = cross(m_Tangent, m_Normal);
    }

    public float3 TangentToWorld(float3 V)
    {
        return normalize(V.x * m_Tangent + V.y * m_Bitangent + V.z * m_Normal);
    }

    public float3 WorldToTangent(float3 V)
    {
        return normalize(float3(dot(V, m_Tangent), dot(V, m_Bitangent), dot(V, m_Normal)));
    }

    public inline float3 GetNormal() { return m_Normal; }
    public inline float3 GetTangent() { return m_Tangent; }
    public inline float3 GetBitangent() { return m_Bitangent; }
    public inline float3 GetWorldPos() { return m_WorldPos; }
    public inline float2 GetTexCoords() { return m_TexCoords; }
}