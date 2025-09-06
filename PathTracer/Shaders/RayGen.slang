import RTCommon;
import Sampler;
import Volume;
import Defines;

[[vk::image_format("rgba32f")]]
[[vk::binding(0, 0)]] RWTexture2D<float4> uImage;

[[vk::binding(1, 0)]] RaytracingAccelerationStructure uTopLevelAS;

[[vk::binding(2, 0)]] ConstantBuffer<UniformBuffer> uUBO;

[[vk::binding(10, 0)]] SamplerState uSampler;

[[vk::image_format("rgba32f")]]
[[vk::binding(15, 0)]] Texture2D uEnvMapTexture;

// Alias map is used to efficiently select texels from env map based on importance.
[[vk::binding(16, 0)]] StructuredBuffer<AliasMapEntry> uEnvAliasMap;

[[vk::binding(17, 0)]] StructuredBuffer<Volume, ScalarDataLayout> uVolumes;

[shader("raygeneration")]
void Main()
{
    uint3 size = DispatchRaysDimensions();
    uint3 LaunchID = DispatchRaysIndex();

    // // TEST

    // const float2 pixelCenter = float2(LaunchID.xy) + float2(0.5f);
    // const float2 pixelUV = pixelCenter / float2(size.xy);

    // uImage[LaunchID.xy] = uVolumeDensityTexture.SampleLevel(uSampler, float3(pixelUV, uUBO.FrameCount % 154), 0);
    // return;

    Payload payload;
    payload.Sampler = Sampler(LaunchID.y + size.x * LaunchID.x + uUBO.Seed);

    float3 prevColor = uImage[LaunchID.xy].rgb;

    float3 accumulatedLight = 0.0f;
    for (uint i = 0; i < uUBO.SampleCount; i++)
    {
        const float2 pixelCenter = float2(LaunchID.xy) + float2(0.5f) + payload.Sampler.UniformFloat2(-0.75f, 0.75f); // Add small jitter for anti aliasing
        const float2 pixelUV = pixelCenter / float2(size.xy);
        float2 d = pixelUV * 2.0f - 1.0f;

        float3 origin       = mul(uUBO.ViewInverse, float4(0, 0, 0, 1)).xyz;
        float3 target       = mul(uUBO.ProjectionInverse, float4(d.x, d.y, 1.0f, 1.0f)).xyz;
        float3 direction    = mul(uUBO.ViewInverse, float4(normalize(target), 0.0f)).xyz;

        float3 focusPoint = origin + direction * max(uUBO.FocusDistance, 0.001f);
        float2 randomOffset = payload.Sampler.UniformFloat2(-0.5f, 0.5f) * uUBO.DepthOfFieldStrength;

        const float3 cameraRight = float3(uUBO.ViewInverse[0][0], uUBO.ViewInverse[1][0], uUBO.ViewInverse[2][0]);
        const float3 cameraUp = float3(uUBO.ViewInverse[0][1], uUBO.ViewInverse[1][1], uUBO.ViewInverse[2][1]);
        origin += randomOffset.x * cameraRight + randomOffset.y * cameraUp;

        direction = normalize(focusPoint - origin);

        payload.Depth = 0;
        payload.Origin = origin;
        payload.Direction = direction;
        payload.BxDF = float3(1.0f, 1.0f, 1.0f);
        payload.PDF = 1.0f;
        payload.Emitted = float3(0.0f, 0.0f, 0.0f);
        payload.InMedium = false;
        payload.QueryDistance = false;

        float3 pathThroughput = 1.0f;
        float3 pathLight = 0.0f;
        // Depth is incremented in closest hit and miss shaders
        for (;payload.Depth < uUBO.MaxDepth;)
        {
            RayDesc rayDesc;
            rayDesc.Origin = payload.Origin;
            rayDesc.Direction = normalize(payload.Direction);
            rayDesc.TMin = 0.00001f;
            rayDesc.TMax = 1000.0f;

            payload.Emitted = float3(0.0f, 0.0f, 0.0f);

            bool nullCollision = false;
            bool scatteredInVolume = ScatteredInVolume(payload, nullCollision);

            if (nullCollision)
                continue;

            // If ray didn't scatter inside volume trace to the geometry surface
            if (!scatteredInVolume)
                TraceRay(uTopLevelAS, RAY_FLAG_FORCE_OPAQUE, 0xff, 0u, 0u, 0u, rayDesc, payload);

            pathLight += payload.Emitted * pathThroughput;
            pathThroughput *= payload.BxDF / payload.PDF;

            // Russian roulette
            float p = max(pathThroughput.x, max(pathThroughput.y, pathThroughput.z));
            // Because of energy compensation pathThroughput is sometimes greater than 1
            // and p gets compared to random variable that is between 0 and 1
            // so it also has to be between 0 and 1 hence the min()
            p = min(p, 1.0f);
            if (p <= payload.Sampler.UniformFloat())
                break;
            pathThroughput /= p;
        }

        accumulatedLight += pathLight;
    }
    accumulatedLight /= (float)uUBO.SampleCount;
    
    // Eliminate high luminance
    float lum = dot(accumulatedLight, float3(0.212671f, 0.715160f, 0.072169f));
    float scale = uUBO.MaxLuminance / max(lum, uUBO.MaxLuminance);
    accumulatedLight *= scale;

    float3 color;
    if (uUBO.FrameCount > 1)
    {
        float a = 1.0f / float(uUBO.FrameCount);
        color = lerp(prevColor, accumulatedLight, a);
    }
    else
    {
        color = accumulatedLight;
    }

    // I'm not exactly sure where, but a NAN happens from time to time. It's somewhere in the SampleGlass function
    // but I can't pinpoint it. So just get rid of nans if there are any.
    if (!isinf(color.x) && !isinf(color.y) && !isinf(color.z) && !isnan(color.x) && !isnan(color.y) && !isnan(color.z))
    {
        uImage[LaunchID.xy] = float4(color, 1.0f);
    }
}

bool ScatteredInVolume(inout Payload payload, out bool nullCollision)
{
    nullCollision = false;

    #ifdef ENABLE_VOLUMES
    {
        float distanceToGeometry = GetDistanceToGeometry(uTopLevelAS, payload.Origin, payload.Direction);

        float scatterDistance = -1.0f;
        bool isNullCollision = false;
        int scatteredVolumeIndex = -1;
        for (int i = 0; i < uUBO.VolumesCount; i++)
        {
            bool isNullCollisionTemp = false;
            float scatterDistanceTemp = uVolumes[i].DoesRayScatterInVolume(payload.Origin, payload.Direction, payload.Sampler, uSampler, isNullCollisionTemp);

            // Choose the closest scatter distance
            if (scatterDistanceTemp >= 0.0f && (scatterDistanceTemp < scatterDistance || scatterDistance < 0.0f))
            {
                scatterDistance = scatterDistanceTemp;
                scatteredVolumeIndex = i;
                isNullCollision = isNullCollisionTemp;
            }
        }

        if (scatterDistance >= 0.0f && (distanceToGeometry < 0.0f || scatterDistance < distanceToGeometry))
        {
            payload.Origin += payload.Direction * scatterDistance;
            
            if (isNullCollision)
            {
                // Null collision, just continue tracing
                nullCollision = true;
                return true;
            }
            payload.Depth++;

            const float3 newDir = uVolumes[scatteredVolumeIndex].GetScatteringDirection(payload.Direction, payload.Sampler);

            // Importance sampling Env map
            #ifdef ENABLE_ENV_MAP_MIS
            {
                float3 toLightEnv;
                float4 envMapValue;
                payload.Sampler.ImportanceSampleEnvMap(uEnvMapTexture, uSampler, uEnvAliasMap, uUBO.EnvMapRotationAzimuth, uUBO.EnvMapRotationAltitude, toLightEnv, envMapValue);
                float3 envMapColor = envMapValue.rgb * uUBO.EnvironmentIntensity;
                float envMapPDF = envMapValue.a;

                float azimuth = uUBO.EnvMapRotationAzimuth / 180.0f * M_PI; // Convert degrees to radians
                float altitude = uUBO.EnvMapRotationAltitude / 180.0f * M_PI; // Convert degrees to radians
                toLightEnv = Rotate(toLightEnv, float3(1.0f, 0.0f, 0.0f), altitude);
                toLightEnv = Rotate(toLightEnv, float3(0.0f, 1.0f, 0.0f), azimuth);

                bool isObscuredByGeometry = DoesRayIntersectWithAS(uTopLevelAS, payload.Origin, toLightEnv);
                float phaseLightDir = uVolumes[scatteredVolumeIndex].EvaluateHenyeyGreenstein(newDir, toLightEnv) * (float)!isObscuredByGeometry;
                float phaseSampledDir = uVolumes[scatteredVolumeIndex].EvaluateHenyeyGreenstein(payload.Direction, newDir);

                // Transmittance along the ray has to be accounted for
                float transmittance = Volume::CalculateVolumesTransmittance(payload.Sampler, payload.Origin, toLightEnv, uVolumes, uUBO.VolumesCount, uSampler);

                float3 colorSample = uVolumes[scatteredVolumeIndex].GetColor() * phaseSampledDir;
                float3 colorLight = uVolumes[scatteredVolumeIndex].GetColor() * phaseLightDir;

                if (!isObscuredByGeometry && phaseLightDir > 0.0f)
                {
                   payload.Emitted = uVolumes[scatteredVolumeIndex].GetEmissiveColor() + transmittance * colorLight * (envMapColor / envMapPDF) * PowerHeuristics(envMapPDF, phaseLightDir);
                }
                payload.Direction = newDir;
                payload.BxDF = colorSample;
                payload.PDF = phaseSampledDir;
            }
            #else
            {
                payload.Direction = newDir;
                payload.BxDF = uVolumes[scatteredVolumeIndex].GetColor();
                payload.PDF = 1.0f;
            }
            #endif

            return true;
        }
    }
    #endif
    
    return false;
}