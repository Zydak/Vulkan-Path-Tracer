public struct AliasMapEntry
{
    public uint Alias;
    public float Importance;
};

public struct Vertex
{
    public float3 Position;
    public float3 Normal;
    public float2 TexCoord;
};

public struct PushConstantData
{
    public uint FrameCount;
    public uint Seed;
    public uint ChunkIndex;
};

public struct UniformBuffer
{
    public float4x4 ViewInverse;
    public float4x4 ProjectionInverse;

    // Atmosphere parameters float4 to ensure alighment
    public float4 PlanetPosition;
    public float4 RayleighScatteringCoefficientMultiplier;
    public float4 MieScatteringCoefficientMultiplier;
    public float4 OzoneAbsorptionCoefficientMultiplier;
    public float4 SunColor;
    public float PlanetRadius;
    public float AtmosphereHeight;
    public float RayleighDensityFalloff;
    public float MieDensityFalloff;
    public float OzoneDensityFalloff;
    public float OzonePeak;

    public uint SampleCount;
    public uint MaxDepth;
    public float MaxLuminance;
    public float FocusDistance;
    public float DepthOfFieldStrength;
    public float SkyRotationAzimuth;
    public float SkyRotationAltitude;
    public uint VolumesCount;
    public float EnvironmentIntensity;
    public uint ScreenSplitCount;
    public uint EmissiveMeshCount;
    public uint TotalEmissiveTriangleCount;
    public float EmissiveMeshSamplingPDFBias;
};

// Material data that's passed in from the CPU
public struct CPUMaterial
{
    public float3 BaseColor;
    public float3 EmissiveColor;
    public float3 SpecularColor;
    public float3 MediumColor;
    public float3 MediumEmissiveColor;
    public float Metallic;
    public float Roughness;
    public float IOR;
    public float Transmission;
    public float Anisotropy;
    public float AnisotropyRotation;

    public float MediumDensity;
    public float MediumAnisotropy;

    public uint BaseColorTextureIndex;
    public uint NormalTextureIndex;
    public uint RoughnessTextureIndex;
    public uint MetallicTextureIndex;
    public uint EmissiveTextureIndex;
}

public struct EmissiveMeshEntry
{
    public uint MeshIndex;
    public uint MaterialIndex;
    public uint TriangleCount;
    public uint InstanceIndex;
    public float4x4 Transform;
}

[[vk::image_format("rgba32f")]]
[[vk::binding(0, 0)]] public RWTexture2D<float4> uImage;

[[vk::binding(1, 0)]] public RaytracingAccelerationStructure uTopLevelAS;

[[vk::binding(2, 0)]] public ConstantBuffer<UniformBuffer> uUBO;

[[vk::binding(3, 0)]] public StructuredBuffer<Vertex, ScalarDataLayout> uVertices[];
[[vk::binding(4, 0)]] public StructuredBuffer<uint> uIndices[];

[[vk::image_format("rgba8")]]
[[vk::binding(5, 0)]] public Texture2D uTextures[];

[[vk::binding(6, 0)]] public SamplerState uTextureSampler;
[[vk::binding(7, 0)]] public StructuredBuffer<CPUMaterial, ScalarDataLayout> uMaterials;

[[vk::image_format("r32f")]]
[[vk::binding(8, 0)]] public Texture2DArray uReflectionLookupTable;
[[vk::image_format("r32f")]]
[[vk::binding(9, 0)]] public Texture2DArray uRefractionLookupTableHitFromOutside;
[[vk::image_format("r32f")]]
[[vk::binding(10, 0)]] public Texture2DArray uRefractionLookupTableHitFromInside;

[[vk::image_format("rgba32f")]]
[[vk::binding(11, 0)]] public Texture2D uEnvMapTexture;

// Alias map is used to efficiently select texels from env map based on importance.
[[vk::binding(12, 0)]] public StructuredBuffer<AliasMapEntry> uEnvAliasMap;

[[vk::binding(14, 0)]] public SamplerState uLookupTableSampler;

[[vk::push_constant]] public PushConstantData uPushConstants;

[[vk::image_format("r32f")]]
[[vk::binding(15, 0)]] public StructuredBuffer<uint> uNanoVDBBuffersDensity[];

[[vk::image_format("r32f")]]
[[vk::binding(16, 0)]] public StructuredBuffer<uint> uNanoVDBBuffersTemperature[];

// 32x32x32 grid of max densities for each heterogeneous volume texture to speed up ray tracing by skipping empty space
[[vk::binding(17, 0)]] public StructuredBuffer<float> uVolumeMaxDensities[];

// Buffer of material and mesh indices for each instance in TLAS
[[vk::binding(18, 0)]] public StructuredBuffer<uint> uMaterialAndMeshIndices;

// Buffer of emissive mesh info
[[vk::binding(19, 0)]] public StructuredBuffer<EmissiveMeshEntry> uEmissiveMeshes;