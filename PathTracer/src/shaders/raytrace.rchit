#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_GOOGLE_include_directive : enable

#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference2 : require

layout(set = 0, binding = 9) uniform sampler2D uEnergyLookupTexture;

#include "raycommon.glsl"
#include "BSDF.glsl"

layout(location = 0) rayPayloadInEXT hitPayload payload;

hitAttributeEXT vec2 attribs;

layout(push_constant) uniform _PushConstantRay { PushConstantRay push; };

layout(buffer_reference, scalar) buffer Vertices { Vertex v[]; };
layout(buffer_reference, scalar) buffer Indices { int i[]; };

layout(set = 0, binding = 0) uniform accelerationStructureEXT uTopLevelAS;
layout(set = 0, binding = 2, scalar) buffer MeshAdressesUbo { MeshAdresses uMeshAdresses[]; };
layout(set = 0, binding = 3, scalar) buffer MaterialsUbo { MaterialLoad uMaterials[]; };
layout(set = 0, binding = 4) uniform sampler2D uAlbedoTextures[];
layout(set = 0, binding = 5) uniform sampler2D uNormalTextures[];
layout(set = 0, binding = 6) uniform sampler2D uRoghnessTextures[];
layout(set = 0, binding = 7) uniform sampler2D uMetallnessTextures[];
layout(set = 1, binding = 1) uniform sampler2D uEnvMap;

layout(set = 1, binding = 2) readonly buffer uEnvMapAccel
{
    EnvAccel[] uAccels;
};

vec3 GetViewReflectedNormal(vec3 N, vec3 V)
{
    float NdotV = dot(N, V);
    N += (2.0 * clamp(-NdotV, 0.0f, 1.0f)) * V;
    return normalize(N);
}

void main() 
{
    // -------------------------------------------
    // Get Data From Buffers
    // -------------------------------------------
    MeshAdresses meshAdresses = uMeshAdresses[gl_InstanceCustomIndexEXT];
    Indices indices = Indices(meshAdresses.IndexBuffer);
    Vertices vertices = Vertices(meshAdresses.VertexBuffer);
    MaterialLoad loadedMaterial = uMaterials[gl_InstanceCustomIndexEXT];
    
    int i1 = indices.i[(gl_PrimitiveID * 3)];
    int i2 = indices.i[(gl_PrimitiveID * 3)+1];
    int i3 = indices.i[(gl_PrimitiveID * 3)+2];
    
    Vertex v0 = vertices.v[i1];
    Vertex v1 = vertices.v[i2];
    Vertex v2 = vertices.v[i3];

    // -------------------------------------------
    // Calculate Surface Properties
    // -------------------------------------------

    const vec3 barycentrics = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);
    vec2 texCoord = v0.TexCoord.xy * barycentrics.x + v1.TexCoord.xy * barycentrics.y + v2.TexCoord.xy * barycentrics.z;

    const vec3 pos      = v0.Position.xyz * barycentrics.x + v1.Position.xyz * barycentrics.y + v2.Position.xyz * barycentrics.z;
    const vec3 worldPos = vec3(gl_ObjectToWorldEXT * vec4(pos, 1.0));  // Transforming the position to world space
     
    // Computing the normal at hit position
    const vec3 nrm      = v0.Normal.xyz * barycentrics.x + v1.Normal.xyz * barycentrics.y + v2.Normal.xyz * barycentrics.z;
    vec3 worldNrm = normalize(vec3(nrm * gl_ObjectToWorldEXT));  // Transforming the normal to world space

    const vec3 V = -gl_WorldRayDirectionEXT;

    Surface surface;
    surface.GeoNormal = worldNrm;
    if (dot(worldNrm, V) < 0)
    {
        worldNrm = -worldNrm;
    }
    surface.NormalNoTex = worldNrm;

    surface.Normal = worldNrm;
    CalculateTangents(worldNrm, surface.Tangent, surface.Bitangent);

    // Normal Maps

    vec3 normalMapVal = texture(uNormalTextures[gl_InstanceCustomIndexEXT], texCoord).xyz;
    normalMapVal = normalMapVal * 2.0f - 1.0f;
    
    normalMapVal = TangentToWorld(surface.Tangent, surface.Bitangent, worldNrm, normalMapVal);
    surface.Normal = normalize(normalMapVal);

    if (dot(surface.Normal, -gl_WorldRayDirectionEXT) < 0.0f)
    {
        surface.Normal = GetViewReflectedNormal(surface.Normal, -gl_WorldRayDirectionEXT);
    }

    // -------------------------------------------
    // Calculate Material Properties
    // -------------------------------------------
    Material material;
    material.Color = loadedMaterial.Color;
    material.EmissiveColor = loadedMaterial.EmissiveColor;
    material.Metallic = loadedMaterial.Metallic;
    material.Roughness = loadedMaterial.Roughness;
    material.SpecularTint = loadedMaterial.SpecularTint;
    material.Ior = loadedMaterial.Ior;
    material.Transparency = loadedMaterial.Transparency;
    material.Anisotropy = loadedMaterial.Anisotropy;

    material.Ior = min(material.Ior, 3.0f);
    material.Roughness = max(material.Roughness, 0.0001f);
    material.Roughness *= material.Roughness;

    material.EmissiveColor.rgb *= material.EmissiveColor.a;

    const float anisotropy = material.Anisotropy;
    if (anisotropy >= 0)
    {
        const float aspect = sqrt(1.0 - sqrt(anisotropy) * 0.9);
        material.ax = max(0.001, material.Roughness / aspect);
        material.ay = max(0.001, material.Roughness * aspect);
    }
    else
    {
        const float aspect = sqrt(1.0 - sqrt(abs(anisotropy)) * 0.9);
        material.ax = max(0.001, material.Roughness * aspect);
        material.ay = max(0.001, material.Roughness / aspect);
    }

#ifdef FURNACE_TEST_MODE
    material.Color = vec4(1.0f);
    material.EmissiveColor = vec4(0.0f);
#else
    material.Color *= texture(uAlbedoTextures[gl_InstanceCustomIndexEXT], texCoord);
#endif

    material.eta = dot(gl_WorldRayDirectionEXT, surface.GeoNormal) < 0.0 ? (1.0 / material.Ior) : material.Ior;
    
    // -------------------------------------------
    // Hit
    // -------------------------------------------

    BSDFSampleData sampleData;
    sampleData.View = -gl_WorldRayDirectionEXT;
    
    bool absorb = !SampleBSDF(payload.Seed, sampleData, material, surface);
    
    if (absorb)
    {
        payload.Weight = vec3(0.0f);
        payload.Depth = DEPTH_INFINITE;
    }
    else
    {
        payload.Weight = sampleData.BSDF / sampleData.PDF;
    }
    
    payload.RayDirection = sampleData.RayDir;

    vec3 offsetDir  = dot(payload.RayDirection, surface.Normal) > 0.0f ? surface.Normal : -surface.Normal;
    payload.RayOrigin = OffsetRay(worldPos, offsetDir);
    payload.HitValue = material.EmissiveColor.xyz;

    //payload.Weight = vec3(0.0f);
    //payload.HitValue = (surface.Normal + 1.0f) * 0.5f;
    
}