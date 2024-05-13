struct VSOut
{
    float4 pos : SV_POSITION;
    float4 worldPos : WORLD_POS;
    float4 color : COLOR;
    float4 normal : NORMAL;
    float4 worldNormal : WORLD_NORMAL;
};

float4 main(VSOut input) : SV_TARGET0
{
    return input.color;
}