struct MeshVertex
{
    float4 position : SV_Position;
    float4 color    : COLOR;
    float2 texCoord : TEXCOORD;
};

struct TextureContainer
{
    Texture2D<float4> textures[1024];
};

[vk::binding(0, 0)]
ParameterBlock<TextureContainer> bindlessTextures
    : register(b0, space0);

[vk::binding(0, 1)]
SamplerState diffuseSampler
    : register(s0, space1);

[shader("fragment")]
float4 fragmentMain(
    MeshVertex input
) : SV_Target
{
    return bindlessTextures
        .textures[0]
        .Sample(
            diffuseSampler,
            input.texCoord
        );
}