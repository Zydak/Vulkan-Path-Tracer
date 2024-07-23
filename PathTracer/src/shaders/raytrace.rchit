#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_GOOGLE_include_directive : enable

#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference2 : require

#include "raycommon.glsl"
#include "BSDF.glsl"

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

    // -------------------------------------------
    // Calculate Material Properties
    // -------------------------------------------
    material.Ior = min(material.Ior, 3.0f);
    material.Roughness = max(material.Roughness, 0.0001f);
    material.Roughness *= material.Roughness;

    material.EmissiveColor.rgb *= material.EmissiveColor.a;

    material.Color *= texture(uAlbedoTextures[gl_InstanceCustomIndexEXT], texCoord);

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
    payload.Weight = sampleData.BSDF / sampleData.PDF;
    
    payload.RayDirection = sampleData.RayDir;

    vec3 offsetDir  = dot(payload.RayDirection, surface.Normal) > 0.0f ? surface.Normal : -surface.Normal;
    payload.RayOrigin = OffsetRay(worldPos, offsetDir);
    payload.HitValue = material.EmissiveColor.xyz;

    //payload.Weight = vec3(0.0f);
    //payload.HitValue = (surface.Normal + 1.0f) / 2.0f;
    
}