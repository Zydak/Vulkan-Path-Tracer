import RTCommon;
import Sampler;
import Volume;
import Defines;
import Atmosphere;

import Bindings;

[shader("raygeneration")]
void Main()
{
    uint3 size = DispatchRaysDimensions() * uUBO.ScreenSplitCount;
    uint3 LaunchID = DispatchRaysIndex();

    if (LaunchID.x >= size.x || LaunchID.y >= size.y) // Guard against excess threads, this can happen with screen splitting
        return;

    uint splitScreenDispatchIndex = uPushConstants.ChunkIndex;
    LaunchID.x += (splitScreenDispatchIndex % uUBO.ScreenSplitCount) * (size.x / uUBO.ScreenSplitCount);
    LaunchID.y += (splitScreenDispatchIndex / uUBO.ScreenSplitCount) * (size.y / uUBO.ScreenSplitCount);

    Payload payload;
    payload.Sampler = Sampler(LaunchID.y + size.x * LaunchID.x + uPushConstants.Seed);

    float3 prevColor = uImage[LaunchID.xy].rgb;

    float3 accumulatedLight = 0.0f;
    for (uint i = 0; i < uUBO.SampleCount; i++)
    {
        const float2 pixelCenter = float2(LaunchID.xy) + float2(0.5f) + payload.Sampler.UniformFloat2(-0.5f, 0.5f); // Add small jitter for anti aliasing
        const float2 pixelUV = pixelCenter / float2(size.xy);
        float2 d = pixelUV * 2.0f - 1.0f;

        float3 origin       = mul(uUBO.ViewInverse, float4(0, 0, 0, 1)).xyz;
        float3 target       = mul(uUBO.ProjectionInverse, float4(d.x, d.y, 1.0f, 1.0f)).xyz;
        float3 direction    = mul(uUBO.ViewInverse, float4(normalize(target), 0.0f)).xyz;

        float3 focusPoint = origin + direction * max(uUBO.FocusDistance, 0.001f);
        float2 randomOffset = payload.Sampler.RandomCircleVec() * 0.5f * uUBO.DepthOfFieldStrength;

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
        payload.ColorChannel = -1; // Start with all channels being tracked

        float3 pathThroughput = 1.0f;
        float3 pathLight = 0.0f;
        // Depth is incremented in closest hit and miss shaders
        for (;payload.Depth < uUBO.MaxDepth;)
        {
            RayDesc rayDesc;
            rayDesc.Origin = payload.Origin;
            rayDesc.Direction = normalize(payload.Direction);
            rayDesc.TMin = 0.00001f;
            rayDesc.TMax = 100000.0f;

            payload.Emitted = float3(0.0f, 0.0f, 0.0f);

            #ifdef ENABLE_ATMOSPHERE
            {
                if (GetAtmosphereHeight(payload.Origin) < 0.0f)
                {
                    // Below the surface of the planet
                    break;
                }
            }
            #endif

            bool scatteredInVolume = ScatteredInVolume(payload);

            // If ray didn't scatter inside volume, trace to the geometry surface
            if (!scatteredInVolume)
                TraceRay(uTopLevelAS, RAY_FLAG_FORCE_OPAQUE, 0xff, 0u, 0u, 0u, rayDesc, payload);

            if (payload.ColorChannel == -1)
            {
                // Ray hasn't been split, so just sum all color channels
                pathLight += payload.Emitted * pathThroughput;
                pathThroughput *= payload.BxDF / payload.PDF;
            }
            else
            {
                // Ray has been split, so only track the relevant color channel
                pathLight[payload.ColorChannel] += payload.Emitted[payload.ColorChannel] * pathThroughput[payload.ColorChannel];
                pathThroughput[payload.ColorChannel] *= (payload.BxDF[payload.ColorChannel] / payload.PDF);
            }

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
    if (uPushConstants.FrameCount > 0)
    {
        float a = 1.0f / float(uPushConstants.FrameCount);
        color = lerp(prevColor, accumulatedLight, a);
    }
    else
    {
        color = accumulatedLight;
    }

    // I'm not exactly sure where, but a NAN happens from time to time. It's somewhere in the glass equations
    // but I can't pinpoint it. So just get rid of nans if there are any.
    if (!isinf(color.x) && !isinf(color.y) && !isinf(color.z) && !isnan(color.x) && !isnan(color.y) && !isnan(color.z))
    {
        uImage[LaunchID.xy] = float4(color, 1.0f);
    }
}

bool ScatteredInVolume(inout Payload payload)
{
    float distanceToGeometry = GetDistanceToGeometry(uTopLevelAS, payload.Origin, payload.Direction);

    float scatterDistance = -1.0f;
    int scatteredVolumeIndex = -1;
    int tempNullCollisions = 0;
    for (int i = 0; i < uUBO.VolumesCount; i++)
    {
        float scatterDistanceTemp = uVolumes[i].DoesRayScatterInVolume(payload.Origin, payload.Direction, payload.Sampler, tempNullCollisions, payload.Depth);

        // Choose the closest scatter distance
        if (scatterDistanceTemp >= 0.0f && (scatterDistanceTemp < scatterDistance || scatterDistance < 0.0f))
        {
            scatterDistance = scatterDistanceTemp;
            scatteredVolumeIndex = i;
        }
    }

    // payload.Emitted = float(tempNullCollisions) / 1000.0f;
    // payload.Depth = MAX_DEPTH;
    // return true;

    float atmosphereScatterDistance = -1.0f;
    int atmosphereComponentHit = -1;
    int colorChannel = payload.ColorChannel;
    #ifdef ENABLE_ATMOSPHERE
    {
        // stochastically choose a color channel to track if the ray hasn't been split yet
        if (colorChannel == -1)
        {
            float channelPick = payload.Sampler.UniformFloat();
            if (channelPick < 0.33333f)
                colorChannel = 0;
            else if (channelPick < 0.66666f)
                colorChannel = 1;
            else
                colorChannel = 2;
        }

        atmosphereScatterDistance = SampleAtmosphereScatterDistance(payload.Sampler, payload.Origin, payload.Direction, colorChannel, atmosphereComponentHit);
        if (atmosphereScatterDistance >= 0.0f && (atmosphereScatterDistance < scatterDistance || scatterDistance < 0.0f))
        {
            // Closest scatter is in atmosphere
            scatterDistance = atmosphereScatterDistance;
            scatteredVolumeIndex = -2; // Mark that atmosphere was hit
        }
    }
    #endif

    if (scatterDistance >= 0.0f && (distanceToGeometry < 0.0f || scatterDistance < distanceToGeometry))
    {
        // Scatter in the volume
        #ifdef ENABLE_ATMOSPHERE
        {
            if (scatteredVolumeIndex == -2)
            {
                // Scattering in atmosphere
                payload.ColorChannel = colorChannel; // Split the ray to only track this color channel from now on
                EvaluateAtmosphereScatteringEvent(payload, scatterDistance, atmosphereComponentHit);
            }
            else
            {
                // Scattering in regular volume
                EvaluateVolumeScatteringEvent(payload, scatterDistance, scatteredVolumeIndex);
            }
        }
        #else
        {
            EvaluateVolumeScatteringEvent(payload, scatterDistance, scatteredVolumeIndex);
        }
        #endif

        return true;
    }
    
    return false;
}

void EvaluateVolumeScatteringEvent(inout Payload payload, float scatterDistance, int scatteredVolumeIndex)
{
    payload.Origin += payload.Direction * scatterDistance;
    const float3 newDir = uVolumes[scatteredVolumeIndex].GetScatteringDirection(payload.Direction, payload.Sampler, payload.Depth);

    // Importance sampling Sky
    #ifdef ENABLE_SKY_MIS
    {
        float3 toLightEnv;
        float4 skyValue;
        payload.Sampler.ImportanceSampleSky(toLightEnv, skyValue);
        float3 skyColor = skyValue.rgb;
        float skyPDF = skyValue.a;
        bool isObscuredByGeometry = DoesRayIntersectWithAS(uTopLevelAS, payload.Origin, toLightEnv);

        float phaseLightDir = uVolumes[scatteredVolumeIndex].EvaluatePhaseFunction(payload.Direction, toLightEnv, payload.Depth) * (float)!isObscuredByGeometry;
        float phaseSampledDir = uVolumes[scatteredVolumeIndex].EvaluatePhaseFunction(payload.Direction, newDir, payload.Depth);

        // Transmittance along the ray has to be accounted for
        float3 transmittance;
        if (!isObscuredByGeometry)
        {
            int tempCollisionCount = 0;
            transmittance = Volume::CalculateVolumesTransmittance(payload.Sampler, payload.Origin, toLightEnv, payload.Depth, tempCollisionCount);

            // payload.Depth = MAX_DEPTH;
            // payload.Emitted = tempCollisionCount / 1000.0f;
            // return;

            #ifdef ENABLE_ATMOSPHERE
            {
                if (payload.ColorChannel == -1)
                {
                    // Ray hasn't been split yet, so just sum all color channels
                    transmittance.r *= CalculateTransmittanceThroughAtmosphere(payload.Sampler, payload.Origin, toLightEnv, 0).r;
                    transmittance.g *= CalculateTransmittanceThroughAtmosphere(payload.Sampler, payload.Origin, toLightEnv, 1).g;
                    transmittance.b *= CalculateTransmittanceThroughAtmosphere(payload.Sampler, payload.Origin, toLightEnv, 2).b;
                }
                else
                {
                    // Ray has been split, so only calculate the transmittance for the relevant color channel
                    transmittance *= CalculateTransmittanceThroughAtmosphere(payload.Sampler, payload.Origin, toLightEnv, payload.ColorChannel);
                }
            }
            #endif
        }
        else
        {
            transmittance = float3(0.0f);
        }

        float3 colorSample = uVolumes[scatteredVolumeIndex].GetColor() * phaseSampledDir;
        float3 colorLight = uVolumes[scatteredVolumeIndex].GetColor() * phaseLightDir;
        payload.Emitted = uVolumes[scatteredVolumeIndex].GetEmissiveColor() + uVolumes[scatteredVolumeIndex].GetEmissionFromTemperatureAtPoint(payload.Sampler, payload.Origin);

        if (!isObscuredByGeometry && phaseLightDir > 0.0f)
        {
           payload.Emitted += transmittance * colorLight * (skyColor / skyPDF) * PowerHeuristics(skyPDF, phaseLightDir);
        }

        payload.Direction = newDir;
        payload.BxDF = colorSample;
        payload.PDF = phaseSampledDir;
    }
    #else
    {
        payload.Direction = newDir;
        payload.Emitted = (uVolumes[scatteredVolumeIndex].GetEmissiveColor() + uVolumes[scatteredVolumeIndex].GetEmissionFromTemperatureAtPoint(payload.Sampler, payload.Origin));
        payload.BxDF = uVolumes[scatteredVolumeIndex].GetColor();
        payload.PDF = 1.0f;
    }
    #endif

    payload.Depth++;
}

void EvaluateAtmosphereScatteringEvent(inout Payload payload, float scatterDistance, int componentHit)
{
    payload.Origin += scatterDistance * payload.Direction;

    // Stochastically choose between Rayleigh and Mie
    float3 newDir;
    if (componentHit == 0)
    {
        // Rayleigh
        newDir = payload.Sampler.SampleRayleigh(payload.Direction);
    }
    else
    {
        // Mie
        newDir = payload.Sampler.SampleHenyeyGreenstein(payload.Direction, 0.85f); // Sample henyey since it's a pretty close match
    }

    #ifdef ENABLE_SKY_MIS
    {
        float3 skyDirSampled;
        float4 colorPdf;
        payload.Sampler.ImportanceSampleSky(skyDirSampled, colorPdf);
        colorPdf.rgb *= uUBO.EnvironmentIntensity;

        bool isObscuredByGeometry = DoesRayIntersectWithAS(uTopLevelAS, payload.Origin, skyDirSampled);

        float3 transmittanceToSun = float3(1.0f);
        if (!isObscuredByGeometry)
        {
            // Compute transmittance to the sun
            transmittanceToSun = CalculateTransmittanceThroughAtmosphere(payload.Sampler, payload.Origin, skyDirSampled, payload.ColorChannel);

            int tempCollisionCount = 0;
            transmittanceToSun *= Volume::CalculateVolumesTransmittance(payload.Sampler, payload.Origin, skyDirSampled, payload.Depth, tempCollisionCount);
        }
        else
        {
            transmittanceToSun = float3(0.0f);
        }

        // Stochastically choose between Rayleigh and Mie
        if (componentHit == 0)
        {
            // Rayleigh
            payload.Emitted += RayleighPhase(payload.Direction, skyDirSampled) * transmittanceToSun * (colorPdf.rgb / colorPdf.a);

            payload.BxDF = RayleighPhase(payload.Direction, newDir);
            payload.PDF = RayleighPhase(payload.Direction, newDir);
        }
        else
        {
            // Mie
            payload.Emitted += PhaseMie(payload.Direction, skyDirSampled) * transmittanceToSun * (colorPdf.rgb / colorPdf.a);

            payload.BxDF = PhaseMie(payload.Direction, newDir) * (1.0f - C_MIE * uUBO.MieScatteringCoefficientMultiplier.rgb * 100);
            payload.PDF = PhaseHenyeyGreenstein(payload.Direction, newDir, 0.85f);
        }
    }
    #else
    {
        if (componentHit == 0)
        {
            payload.BxDF = RayleighPhase(payload.Direction, newDir);
            payload.PDF = RayleighPhase(payload.Direction, newDir);
        }
        else
        {
            payload.BxDF = PhaseMie(payload.Direction, newDir);
            payload.PDF = PhaseHenyeyGreenstein(payload.Direction, newDir, 0.85f);
        }
    }
    #endif

    payload.Direction = newDir;
    payload.Depth++;
}