#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_GOOGLE_include_directive : enable

#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference2 : require

#include "raycommon.glsl"
#include "BRDF.glsl"

layout(location = 0) rayPayloadInEXT hitPayload payload;

hitAttributeEXT vec2 attribs;

layout(push_constant) uniform _PushConstantRay { PushConstantRay push; };

layout(buffer_reference, scalar) buffer Vertices { Vertex v[]; };
layout(buffer_reference, scalar) buffer Indices { int i[]; };

layout(set = 0, binding = 0) uniform accelerationStructureEXT uTopLevelAS;
layout(set = 0, binding = 2, scalar) buffer MeshAdressesUbo { MeshAdresses uMeshAdresses[]; };
layout(set = 0, binding = 3, scalar) buffer MaterialsUbo { Material uMaterials[]; };
layout(set = 0, binding = 4) uniform sampler2D uAlbedoTextures[];
layout(set = 0, binding = 5) uniform sampler2D uNormalTextures[];
layout(set = 0, binding = 6) uniform sampler2D uRoghnessTextures[];
layout(set = 0, binding = 7) uniform sampler2D uMetallnessTextures[];
layout(set = 1, binding = 1) uniform sampler2D uEnvMap;

layout(set = 1, binding = 2) readonly buffer uEnvMapAccel
{
    EnvAccel[] uAccels;
};

void main() 
{
    // -------------------------------------------
    // Get Data From Buffers
    // -------------------------------------------
    MeshAdresses meshAdresses = uMeshAdresses[gl_InstanceCustomIndexEXT];
    Indices indices = Indices(meshAdresses.IndexBuffer);
    Vertices vertices = Vertices(meshAdresses.VertexBuffer);
    Material material = uMaterials[gl_InstanceCustomIndexEXT];
    
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

    const vec3 tang     = v0.Tangent.xyz * barycentrics.x + v1.Tangent.xyz * barycentrics.y + v2.Tangent.xyz * barycentrics.z;
    const vec3 bitang     = v0.Bitangent.xyz * barycentrics.x + v1.Bitangent.xyz * barycentrics.y + v2.Bitangent.xyz * barycentrics.z;

    material.eta = dot(gl_WorldRayDirectionEXT, worldNrm) < 0.0 ? (1.0 / material.Ior) : material.Ior;

    const vec3 V = -gl_WorldRayDirectionEXT;

    Surface surface;
    surface.GeoNormal = worldNrm;
    if (dot(worldNrm, V) < 0)
    {
        worldNrm = -worldNrm;
    }

    surface.Normal = worldNrm;
    //surface.Tangent = tang;
    //surface.Bitangent = bitang;
    CalculateTangents(worldNrm, surface.Tangent, surface.Bitangent);

#ifdef USE_NORMAL_MAPS
    vec3 normalMapVal = texture(uNormalTextures[gl_InstanceCustomIndexEXT], texCoord).xyz;
    normalMapVal = normalMapVal * 2.0f - 1.0f;
    
    normalMapVal = TangentToWorld(surface.Tangent, surface.Bitangent, worldNrm, normalMapVal);
    surface.Normal = normalize(normalMapVal);
    
    CalculateTangents(worldNrm, surface.Tangent, surface.Bitangent);
#endif

    // -------------------------------------------
    // Calculate Material Properties
    // -------------------------------------------
    // TODO: anisotropic
    const float anisotropic = 0.0f;
    const float aspect = sqrt(1.0 - anisotropic * 0.99);
    material.ax = max(0.001, material.Roughness / aspect);
    material.ay = max(0.001, material.Roughness * aspect);

    material.Roughness = max(material.Roughness, 0.001f);
    material.Metallic  = max(material.Metallic, 0.001f);

    material.Color *= texture(uAlbedoTextures[gl_InstanceCustomIndexEXT], texCoord);

    material.SpecTrans = material.SpecTrans;
    
    // -------------------------------------------
    // Hit
    // -------------------------------------------

    BRDFSampleData sampleData;
    sampleData.View = -gl_WorldRayDirectionEXT;
    sampleData.Random = vec4(Rnd(payload.Seed), Rnd(payload.Seed), Rnd(payload.Seed), Rnd(payload.Seed));

    SampleBRDF(sampleData, material, surface);

    payload.Weight = sampleData.BRDF / sampleData.PDF;

    // TEST

    
    vec3 halfVector = normalize(-gl_WorldRayDirectionEXT + sampleData.RayDir);
    float NdotV = abs(dot(surface.Normal, -gl_WorldRayDirectionEXT));
    float NdotL = abs(dot(surface.Normal, sampleData.RayDir));
    float VdotH = abs(dot(-gl_WorldRayDirectionEXT, halfVector));
    float NdotH = abs(dot(surface.Normal, halfVector));
    float LdotH = abs(dot(sampleData.RayDir, halfVector));
    
    vec3 tint = material.Color.xyz / max(GetLuminance(material.Color.xyz), 0.001f);
    vec3 f0 = mix(vec3(material.SpecularStrength) * mix(vec3(1.0f), tint, material.SpecularTint), material.Color.xyz, material.Metallic);
    vec3 f90 = vec3(1.0F);
    float a = material.Roughness * material.Roughness;
    
    //payload.Weight = BrdfSpecular(a, f0, f90, NdotH, NdotL, NdotV, LdotH);
    //payload.Weight = BrdfSpecularGGX(f0, f90, a, VdotH, NdotL, NdotV, NdotH);
    
    // TEST


    payload.RayDirection = sampleData.RayDir;
    
    vec3 offsetDir  = dot(payload.RayDirection, surface.Normal) > 0.0f ? surface.Normal : -surface.Normal;
    payload.RayOrigin = OffsetRay(worldPos, offsetDir);
    payload.HitValue     = material.Color.xyz * material.Color.a;
}