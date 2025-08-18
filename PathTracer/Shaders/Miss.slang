import RTCommon;
import Defines;

[shader("miss")]
void Main(inout Payload payload)
{
    payload.LightColor += float3(0.0f);
    payload.Depth = MAX_DEPTH; // Set depth to max value to indicate no hit
}