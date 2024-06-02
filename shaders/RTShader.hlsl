RaytracingAccelerationStructure gRtScene : register(t0);

Texture2D<float4> gDiffuse : register(t1);
Texture2D<float4> gNorm : register(t2);
Texture2D<float4> gPos : register(t3);

RWTexture2D<float4> gOutput : register(u0);

struct RayPayload
{
	float aoVal;
};

struct GlobalData
{
    uint frameCount;
};

ConstantBuffer<GlobalData> globalData : register(b0);

// Value to signify if a pixel should be illuminated.
#define AO_IS_ILLUMINATED_VAL 1.0f
#define AO_MIN_T 0.0001f
#define AO_RADIUS 100000.0f
#define NUM_SAMPLES 1u

// The four functions below (initRand, nextRand, and getPerpendicularVector, getCosHemisphereSample) were taken from the codebase 
// of a tutorial on simple raytracing techniques.
// The tutorial can be found here: https://cwyman.org/code/dxrTutors/dxr_tutors.md.html

// Generates a seed for a random number generator from 2 inputs plus a backoff
uint initRand(uint val0, uint val1, uint backoff = 16)
{
    uint v0 = val0, v1 = val1, s0 = 0;

	[unroll]
    for (uint n = 0; n < backoff; n++)
    {
        s0 += 0x9e3779b9;
        v0 += ((v1 << 4) + 0xa341316c) ^ (v1 + s0) ^ ((v1 >> 5) + 0xc8013ea4);
        v1 += ((v0 << 4) + 0xad90777d) ^ (v0 + s0) ^ ((v0 >> 5) + 0x7e95761e);
    }
    return v0;
}

// Takes our seed, updates it, and returns a pseudorandom float in [0..1]
float nextRand(inout uint s)
{
    s = (1664525u * s + 1013904223u);
    return float(s & 0x00FFFFFF) / float(0x01000000);
}

// Utility function to get a vector perpendicular to an input vector 
float3 getPerpendicularVector(float3 u)
{
    float3 a = abs(u);
    uint xm = ((a.x - a.y) < 0 && (a.x - a.z) < 0) ? 1 : 0;
    uint ym = (a.y - a.z) < 0 ? (1 ^ xm) : 0;
    uint zm = 1 ^ (xm | ym);
    return cross(u, float3(xm, ym, zm));
}

// Get a cosine-weighted random vector centered around a specified normal direction.
float3 getCosHemisphereSample(inout uint randSeed, float3 hitNorm)
{
	// Get 2 random numbers to select our sample with
    float2 randVal = float2(nextRand(randSeed), nextRand(randSeed));

	// Cosine weighted hemisphere sample from RNG
    float3 bitangent = getPerpendicularVector(hitNorm);
    float3 tangent = cross(bitangent, hitNorm);
    float r = sqrt(randVal.x);
    float phi = 2.0f * 3.14159265f * randVal.y;

	// Get our cosine-weighted hemisphere lobe sample direction
    return tangent * (r * cos(phi).x) + bitangent * (r * sin(phi)) + hitNorm.xyz * sqrt(1 - randVal.x);
}

[shader("raygeneration")]
void raygen()
{
	uint3 launchIndex = DispatchRaysIndex();
	uint3 launchDim = DispatchRaysDimensions();
	
    uint randSeed = initRand(launchIndex.x + launchIndex.y * launchDim.x, globalData.frameCount);
    //uint randSeed = 30125012;
	
	uint2 pixelIndex = launchIndex.xy;
	float4 worldPos = gPos[pixelIndex];
	float3 worldNormal = gNorm[pixelIndex].xyz;
    float diffuseAlpha = gDiffuse[pixelIndex].a;
	
    float aoVal = 1.0;
    if (worldPos.w != 0.0f)
	{
        float accumulatedAOVal = 0.0f;
        for (uint i = 0; i < NUM_SAMPLES; i++)
        {
            float3 worldDir = getCosHemisphereSample(randSeed, worldNormal);
		
            RayPayload rayPayload = { 0.0f };
            RayDesc rayAO = { worldPos.xyz + mul(worldNormal, 0.0000001f), AO_MIN_T, worldDir, AO_RADIUS };
            uint rayFlags = RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER;

            TraceRay(gRtScene, rayFlags, 0xFF, 0, 1, 0, rayAO, rayPayload);

            accumulatedAOVal += rayPayload.aoVal;
        }
        
        
        aoVal = (accumulatedAOVal / (float)NUM_SAMPLES);
    }
    
    gOutput[pixelIndex] = gOutput[pixelIndex] * aoVal;
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
