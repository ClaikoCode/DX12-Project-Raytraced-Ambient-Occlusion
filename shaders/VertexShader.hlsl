struct VSOut
{
    float4 pos : SV_POSITION;
    float4 worldPos : WORLD_POS;
    float4 color : COLOR;
    float4 normal : NORMAL;
    float4 worldNormal : WORLD_NORMAL;
};

struct VSIn
{
    float3 pos : POSITION;
    float3 normal : NORMAL;
    float3 color : COLOR;
};

struct ModelTransform
{
    matrix transform;
};

struct CameraInfo
{
    matrix viewProjMatrix;
};

ConstantBuffer<CameraInfo> camInfo : register(b0);
ConstantBuffer<ModelTransform> transf : register(b1);

VSOut main(VSIn input)
{
    VSOut output = (VSOut) 0;
    
    // TODO: Remove the need to do transpose in shader.
    matrix transposedTransform = transpose(transf.transform);
    matrix mvpMatrix = mul(transposedTransform, transpose(camInfo.viewProjMatrix));

    output.pos = mul(float4(input.pos, 1.0f), mvpMatrix);
    output.worldPos = mul(float4(input.pos, 1.0f), transposedTransform);
    output.color = float4(input.color, 1.0f);

    output.normal = float4(input.normal, 0.0f);
    output.worldNormal = mul(float4(normalize(input.normal), 0.0f), transposedTransform);

    return output;
}