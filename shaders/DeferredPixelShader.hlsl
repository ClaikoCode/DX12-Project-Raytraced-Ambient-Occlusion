struct VSOut
{
    float4 pos : SV_POSITION;
    float4 color : COLOR;
};

struct PSOut
{
    float diffuse : SV_TARGET0;
    float normal : SV_TARGET1;
    float position : SV_TARGET2;
};

float4 main(VSOut input)
{
    return input.color;
}