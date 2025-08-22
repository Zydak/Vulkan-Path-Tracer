import RTCommon;
import Defines;

struct HitQueryPayload
{
    bool Hit;
}

[shader("miss")]
void Main(inout HitQueryPayload hitPayload)
{
    hitPayload.Hit = false;
}