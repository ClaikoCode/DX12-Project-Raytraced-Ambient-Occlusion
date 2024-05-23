RaytracingAccelerationStructure gRtScene : register(t0);

Texture2D<float4> gDiffuse : register(t1);
Texture2D<float4> gNorm : register(t2);
Texture2D<float4> gPos : register(t3);

RWTexture2D<float4> gOutput : register(u0);

struct RayPayload
{
	float aoVal;
};

// Value to signify if a pixel should be illuminated.
#define AO_IS_ILLUMINATED_VAL 1.0f
#define AO_MIN_T 0.01f
#define AO_RADIUS 100000.0f

[shader("raygeneration")]
void raygen()
{
	uint3 launchIndex = DispatchRaysIndex();
	uint3 launchDim = DispatchRaysDimensions();
	
	uint randSeed = 21030124;
	
	uint2 pixelIndex = launchIndex.xy;
	float4 worldPos = gPos[pixelIndex];
	float4 worldNormal = gNorm[pixelIndex];
	
    float aoVal = AO_IS_ILLUMINATED_VAL; // Assume that ray did not hit anything.
	if (worldPos.w != 0)
	{
        float3 worldDir = normalize(float3(0.0f, 1.0f, 0.0f)); // TODO: Add semisphere sampling.
		
        RayPayload rayPayload = { 0.0f };
        RayDesc rayAO = { worldPos.xyz, worldDir, AO_MIN_T, AO_RADIUS };
        uint rayFlags = RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER;

        TraceRay(gRtScene, rayFlags, 0xFF, 0, 1, 0, rayAO, rayPayload);

        aoVal = rayPayload.aoVal;
    }
	
    aoVal = 0.0f;
	float3 diffuseColor = gDiffuse.Load(int3(pixelIndex, 0)).rgb; // Read from MIP 0.
	float3 sceneColor = gOutput[pixelIndex].rgb;
	//float3 finalColor = aoVal ? sceneColor : diffuseColor;
    float3 finalColor = aoVal ? float3(1.0f, 0.0f, 0.0f) : float3(0.0f, 1.0f, 0.0f);
	gOutput[pixelIndex] = float4(finalColor, 1.0f);
}

[shader("miss")]
void miss(inout RayPayload payload)
{
    payload.aoVal = AO_IS_ILLUMINATED_VAL;
}

[shader("anyhit")]
void anyhit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
	// TODO: Add alpha testing. (if I have time).
    if (true)
    {
        IgnoreHit();
    }
}
