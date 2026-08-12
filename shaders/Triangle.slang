struct VertexData
{
    float2 position;
    float4 color;
    float2 texCoord;
};

struct VertexArguments
{
    uint64_t vertexReference;
    uint2 viewportSize;
};

struct TextureContainer
{
    Texture2D<float4> textures[1024];
};

// [[buffer(0)]] in MSL
[vk::binding(0, 0)]
ParameterBlock<TextureContainer> bindlessTextures : register(b0, space0);

// [[buffer(1)]] in MSL
[vk::binding(1, 0)]
ConstantBuffer<VertexArguments> vertexArgs : register(b1, space0);

// [[sampler(0)]] in MSL
[vk::binding(0, 1)]
SamplerState diffuseSampler : register(s0, space1);

struct VertexOut
{
    float4 position      : SV_Position;
    float4 color         : COLOR0;
    float2 texCoord      : TEXCOORD0;
    uint   materialIndex : BLENDINDICES0;
};

// ── Vertex Shader ─────────────────────────────────────────────────────────────

[shader("vertex")]
VertexOut vertexMain(uint vertexId : SV_VertexID)
{
    VertexData* vertexPTR = (VertexData*)vertexArgs.vertexReference;
    VertexData v          = vertexPTR[vertexId];

    float2 viewport = float2(vertexArgs.viewportSize);

    VertexOut output;
    output.position.xy   = v.position / (viewport / 2.0);
    output.position.z    = 0.0;
    output.position.w    = 1.0;
    output.color         = v.color;
    output.texCoord      = v.texCoord;
    output.materialIndex = 0;

    return output;
}

// ── Fragment Shader ───────────────────────────────────────────────────────────

[shader("fragment")]
float4 fragmentMain(VertexOut input) : SV_Target
{
#if __SLANG_TARGET_METAL__
    return bindlessTextures.textures[input.materialIndex].Sample(diffuseSampler, input.texCoord);
#endif
}