struct VSQuadOut
{
    float4 position : SV_Position;
    float2 texcoord : UV;
};

float4 main(VSQuadOut input) : SV_TARGET0
{
    return float4(1.0f, 0.0f, 1.0f, 1.0f);
}