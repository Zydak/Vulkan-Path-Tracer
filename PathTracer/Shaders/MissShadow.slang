import RTCommon;
import Defines;

[shader("miss")]
void Main(inout Payload payload)
{
    payload.Depth = MAX_DEPTH;
    payload.DistanceToSurface = -1.0f;
}