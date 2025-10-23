import RTCommon;
import Defines;
import Bindings;

// This class holds geometric data of the hit surface, things like world position, normals, tangents and texture coordinates
public struct Surface
{
    float3 m_WorldPos;
    float2 m_TextureCoord;

    float3 m_Normal;
    float3 m_Tangent;
    float3 m_Bitangent;

    Vertex m_V1;
    Vertex m_V2;
    Vertex m_V3;

    bool m_HitFromInside;

    [mutating]
    public void Initialize(
        in StructuredBuffer<Vertex, ScalarDataLayout> vertices,
        in StructuredBuffer<uint> indices,
        in float3 barycentrics,
        in Texture2D normalTexture
    )
    {
        uint primitiveIndex = PrimitiveIndex();

        const uint i1 = indices[primitiveIndex * 3 + 0];
        const uint i2 = indices[primitiveIndex * 3 + 1];
        const uint i3 = indices[primitiveIndex * 3 + 2];

        m_V1 = vertices[i1];
        m_V2 = vertices[i2];
        m_V3 = vertices[i3];

        m_WorldPos = m_V1.Position * barycentrics.x + m_V2.Position * barycentrics.y + m_V3.Position * barycentrics.z;
        m_WorldPos = mul(ObjectToWorld3x4(), float4(m_WorldPos, 1.0f)).xyz;

        #ifdef USE_ONLY_GEOMETRY_NORMALS
        {
            const float3 v1World = mul(ObjectToWorld3x4(), float4(m_V1.Position, 1.0f)).xyz;
            const float3 v2World = mul(ObjectToWorld3x4(), float4(m_V2.Position, 1.0f)).xyz;
            const float3 v3World = mul(ObjectToWorld3x4(), float4(m_V3.Position, 1.0f)).xyz;
            m_Normal = normalize(cross(v3World - v1World, v2World - v1World)); // Face normal
        }
        #else
        {
            // Transformed normals often cause rays to go under the geometry, the same goes for normal textures.
            // They aren't implemented properly yet so there is no compensation for that
            m_Normal = normalize(m_V1.Normal * barycentrics.x + m_V2.Normal * barycentrics.y + m_V3.Normal * barycentrics.z);
            m_Normal = mul(m_Normal, WorldToObject()).xyz;
        }
        #endif

        if (dot(m_Normal, WorldRayDirection()) > 0.0f)
        {
            // If geometry is hit from inside then invert the normal
            m_Normal = -m_Normal;
            m_HitFromInside = true;
        }
        else
        {
            m_HitFromInside = false;
        }

        m_TextureCoord = m_V1.TexCoord * barycentrics.x + m_V2.TexCoord * barycentrics.y + m_V3.TexCoord * barycentrics.z;

        float3 up = abs(m_Normal.z) < 0.9999999 ? float3(0, 0, 1) : float3(1, 0, 0);
        m_Tangent = normalize(cross(up, m_Normal));
        m_Bitangent = cross(m_Normal, m_Tangent);

        #ifndef USE_ONLY_GEOMETRY_NORMALS
        {
            float3 normalMapValue = normalTexture.SampleLevel(uTextureSampler, m_TextureCoord, 0).xyz * 2.0f - 1.0f;

            normalMapValue = TangentToWorld(normalMapValue);
            m_Normal = normalize(normalMapValue);
        }
        #endif
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
    public inline Vertex GetV1() { return m_V1; }
    public inline Vertex GetV2() { return m_V2; }
    public inline Vertex GetV3() { return m_V3; }
}