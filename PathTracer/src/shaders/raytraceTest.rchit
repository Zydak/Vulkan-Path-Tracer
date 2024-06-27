#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_GOOGLE_include_directive : enable

#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference2 : require

#include "raycommon.glsl"

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

vec3 BRDF(Material mat)
{
    return mat.Color.xyz;
}

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

    material.eta = dot(gl_WorldRayDirectionEXT, worldNrm) < 0.0 ? (1.0 / material.Ior) : material.Ior;

    const vec3 V = -gl_WorldRayDirectionEXT;

    Surface surface;
    surface.GeoNormal = worldNrm;
    if (dot(worldNrm, V) < 0)
    {
        worldNrm = -worldNrm;
    }

    surface.Normal = worldNrm;
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

#ifdef USE_ALBEDO
    material.Color *= texture(uAlbedoTextures[gl_InstanceCustomIndexEXT], texCoord);
#else
    material.Color = vec4(0.5f);
#endif

    material.SpecTrans = material.SpecTrans;
    
    // -------------------------------------------
    // Hit
    // -------------------------------------------
    vec3 rayDirCos = CosineSamplingHemisphere(payload.Seed, surface.Tangent, surface.Bitangent, surface.Normal);
    vec3 rayDirUni = UniformSamplingHemisphere(payload.Seed, surface.Tangent, surface.Bitangent, surface.Normal);
    
    const float uniformScatteringPdf = 1;// / (2 * M_PI);

    const vec3 brdf = BRDF(material) * uniformScatteringPdf;

    payload.Weight       = brdf / uniformScatteringPdf;
    payload.RayDirection = rayDirUni;

    payload.RayOrigin    = worldPos;
    payload.HitValue     = BRDF(material) * material.Color.a;
}