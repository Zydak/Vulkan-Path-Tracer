import RTCommon;
import Defines;

// This class holds geometric data of the hit surface, things like world position, normals, tangents and texture coordinates
public struct Surface
{
    float3 m_WorldPos;
    float2 m_TextureCoord;

    float3 m_Normal;
    float3 m_Tangent;
    float3 m_Bitangent;

    bool m_HitFromInside;

    [mutating]
    public void Initialize(in StructuredBuffer<Vertex, ScalarDataLayout> vertices, in StructuredBuffer<uint> indices, in float3 barycentrics)
    {
        uint primitiveIndex = PrimitiveIndex();

        const uint i1 = indices[primitiveIndex * 3 + 0];
        const uint i2 = indices[primitiveIndex * 3 + 1];
        const uint i3 = indices[primitiveIndex * 3 + 2];

        const Vertex v1 = vertices[i1];
        const Vertex v2 = vertices[i2];
        const Vertex v3 = vertices[i3];

        m_WorldPos = v1.Position * barycentrics.x + v2.Position * barycentrics.y + v3.Position * barycentrics.z;
        m_WorldPos = mul(ObjectToWorld3x4(), float4(m_WorldPos, 1.0f)).xyz;

        // Don't use model normals, they're often transformed which causes rays to go under the geometry
        // The same goes for normal textures, they are not supported for now since it's quite hard to get them working properly
        //m_Normal = normalize(v1.Normal * barycentrics.x + v2.Normal * barycentrics.y + v3.Normal * barycentrics.z);

        // Instead compute geometric normals
        m_Normal = normalize(cross(v2.Position - v1.Position, v3.Position - v1.Position));
        m_Normal = mul(m_Normal, WorldToObject()).xyz;

        if (dot(m_Normal, WorldRayDirection()) > 0.0f)
        {
            // Sometimes the geometry is flat (e.g. a 2D plane), it has only one face (and one normal direction).
            // If that's the case then flip the normal and act as if it has two faces.
            m_Normal = -m_Normal;
            m_HitFromInside = true;
        }
        else
        {
            m_HitFromInside = false;
        }

        m_TextureCoord = v1.TexCoord * barycentrics.x + v2.TexCoord * barycentrics.y + v3.TexCoord * barycentrics.z;

        float3 up = abs(m_Normal.z) < 0.9999999 ? float3(0, 0, 1) : float3(1, 0, 0);
        m_Tangent = normalize(cross(up, m_Normal));
        m_Bitangent = cross(m_Normal, m_Tangent);
    }

    public float3 TangentToWorld(in float3 vec)
    {
        return normalize(vec.x * m_Tangent + vec.y * m_Bitangent + vec.z * m_Normal);
    }

    public float3 WorldToTangent(in float3 vec)
    {
        return normalize(float3(dot(vec, m_Tangent), dot(vec, m_Bitangent), dot(vec, m_Normal)));
    }

    [mutating]
    public void RotateTangents(in float rotationDegrees)
    {
        // Convert to radians
        float rotation = rotationDegrees * (M_PI / 180.0f);

        m_Tangent = m_Tangent * cos(rotation) + cross(m_Normal, m_Tangent) * sin(rotation) + m_Normal * dot(m_Normal, m_Tangent) * (1.0f - cos(rotation));
        m_Bitangent = cross(m_Tangent, m_Normal);
    }

    public inline float3 GetNormal() { return m_Normal; }
    public inline float3 GetTangent() { return m_Tangent; }
    public inline float3 GetBitangent() { return m_Bitangent; }
    public inline float3 GetWorldPos() { return m_WorldPos; }
    public inline float2 GetTextureCoordinates() { return m_TextureCoord; }
    public inline bool WasHitFromInside() { return m_HitFromInside; }
}