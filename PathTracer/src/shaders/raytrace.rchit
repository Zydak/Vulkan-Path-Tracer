#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_ray_query : enable

#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference2 : require

layout(set = 0, binding = 9) uniform sampler2DArray uReflectionEnergyLookupTexture;
layout(set = 0, binding = 10) uniform sampler2DArray uRefractionEnergyLookupTextureEtaGreaterThan1;
layout(set = 0, binding = 11) uniform sampler2DArray uRefractionEnergyLookupTextureEtaLessThan1;

#include "raycommon.glsl"

layout(push_constant) uniform _PushConstantRay { PushConstantRay push; };

layout(set = 1, binding = 2) readonly buffer uEnvMapAccel
{
    EnvAccel[] uAccels;
};

layout(location = 0) rayPayloadInEXT hitPayload payload;

#include "BSDF.glsl"

hitAttributeEXT vec2 attribs;

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
layout(set = 0, binding = 12, scalar) buffer Volumes { AABB uVolumes[]; };

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
    const vec3 worldPos = vec3(gl_ObjectToWorldEXT * vec4(pos, 1.0));
     
    // Computing the normal at hit position
    const vec3 nrm      = v0.Normal.xyz * barycentrics.x + v1.Normal.xyz * barycentrics.y + v2.Normal.xyz * barycentrics.z;

    // Note that for normals gl_WorldToObjectEXT is used instead of gl_ObjectToWorldEXT. That's because normals have to 
    // be transformed using inverse of the transpose
    vec3 worldNrm = normalize(vec3(nrm * gl_WorldToObjectEXT)); 

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
    material.MediumDensity = loadedMaterial.MediumDensity;
    material.MediumColor = loadedMaterial.MediumColor;
    material.Anisotropy = loadedMaterial.Anisotropy;

    material.Ior = max(material.Ior, 1.0001f);
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

    material.Roughness *= texture(uRoghnessTextures[gl_InstanceCustomIndexEXT], texCoord).r;
    material.Metallic *= texture(uMetallnessTextures[gl_InstanceCustomIndexEXT], texCoord).r;

    material.eta = dot(gl_WorldRayDirectionEXT, surface.GeoNormal) < 0.0 ? (1.0 / material.Ior) : material.Ior;
    
    // -------------------------------------------
    // Hit
    // -------------------------------------------
    payload.HitValue = material.EmissiveColor.xyz;

    HitData hitData;
    hitData.HitDistance = length(payload.RayOrigin - worldPos);

    payload.Weight = vec3(1.0f);

    if (payload.InMedium)
    {
        float scatterDistance = -log(Rnd(payload.Seed)) / material.MediumDensity;
        
        if (scatterDistance < hitData.HitDistance)
        {
            payload.HitValue = vec3(0.0f);
            float anisotropy = 1.0f;

            // just use Beer's law directly since there is no need for a random walk if anisotropy == 1
            if (anisotropy == 1.0f)
                payload.Weight = exp(-(1.0f - material.MediumColor.rgb) * material.MediumDensity * hitData.HitDistance);
            else
            {
                vec3 newDir = SampleHenyeyGreenstein(anisotropy, payload.RayDirection, vec2(Rnd(payload.Seed), Rnd(payload.Seed)));
                payload.RayOrigin = payload.RayOrigin + (scatterDistance * payload.RayDirection);
        
                payload.RayDirection = newDir;
        
                payload.Weight = material.MediumColor.rgb;
        
                return;
            }
        }
        else
        {
            if (payload.MediumID == gl_InstanceCustomIndexEXT)
                payload.InMedium = false;
        }
    }

    BSDFSampleData sampleData;
    sampleData.View = -gl_WorldRayDirectionEXT;
    
    bool absorb = !SampleBSDF(payload.Seed, sampleData, material, surface, hitData);
    
    if (absorb)
    {
        payload.Weight = vec3(0.0f);
        payload.Depth = DEPTH_INFINITE;
    }
    else
    {
        payload.Weight *= sampleData.BSDF / sampleData.PDF;
    }
    
    payload.RayDirection = sampleData.RayDir;

    vec3 offsetDir  = dot(payload.RayDirection, surface.Normal) > 0.0f ? surface.Normal : -surface.Normal;
    payload.RayOrigin = OffsetRay(worldPos, offsetDir);

    //payload.Weight = vec3(0.0f);
    //payload.HitValue = (surface.Normal + 1.0f) * 0.5f;
    
}