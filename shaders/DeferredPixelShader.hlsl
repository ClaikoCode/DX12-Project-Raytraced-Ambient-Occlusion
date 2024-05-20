struct VSOut
{
    float4 pos : SV_POSITION;
    float4 worldPos : WORLD_POS;
    float4 color : COLOR;
    float4 normal : NORMAL;
    float4 worldNormal : WORLD_NORMAL;
};

struct PSOut
{
    float4 diffuse : SV_TARGET0;
    float4 normal : SV_TARGET1;
    float4 position : SV_TARGET2;
};

PSOut main(VSOut input)
{
    PSOut OUT;

    OUT.diffuse = input.color;
    OUT.normal = input.worldNormal;
    OUT.position = input.worldPos;

    return OUT;
}