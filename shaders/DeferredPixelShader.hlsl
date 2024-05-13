struct VSOut
{
    float4 pos : SV_POSITION;
    float4 color : COLOR;
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
    OUT.normal = float4(0.0f, 0.0f, 1.0f, 1.0f);
    OUT.position = input.pos;

    return OUT;
}