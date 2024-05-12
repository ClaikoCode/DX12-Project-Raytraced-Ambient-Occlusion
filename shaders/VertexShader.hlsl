struct VSOut
{
    float4 pos : SV_POSITION;
    float4 color : COLOR;
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
    matrix mvpMatrix = mul(transpose(transf.transform), transpose(camInfo.viewProjMatrix));
    output.pos = mul(float4(input.pos, 1.0f), mvpMatrix);
    output.color = float4(input.normal, 1.0f);

    return output;
}