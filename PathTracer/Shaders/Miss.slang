import RTCommon;
import Defines;

[shader("miss")]
void Main(inout Payload payload)
{
    payload.LightColor += float4(1.0f);
    payload.Depth = MAX_DEPTH; // Set depth to max value to indicate no hit
}