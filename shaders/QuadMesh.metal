#include <metal_stdlib>

using namespace metal;

struct QuadInstance
{
    array<packed_float4, 4> modelMatrix;
    packed_float4 color;
    uint textureIndex;
};

struct MeshArguments
{
    array<float4, 4> viewProjection;
    uint64_t instanceReference;
};

struct MeshVertex
{
    float4 position [[position]];
    float4 color;
    float2 texCoord;
    uint textureIndex;
};

struct TextureArgument
{
    texture2d<float, access::sample> texture;
};

struct PrimitiveData
{
};

using QuadMesh = metal::mesh<
    MeshVertex,
    PrimitiveData,
    4,
    2,
    topology::triangle
>;

constant float4 positions[4] =
{
    float4(-0.5f, -0.5f, 0.0f, 1.0f),
    float4( 0.5f, -0.5f, 0.0f, 1.0f),
    float4( 0.5f,  0.5f, 0.0f, 1.0f),
    float4(-0.5f,  0.5f, 0.0f, 1.0f)
};

constant float2 uv[4] =
{
    float2(0.0f, 0.0f),
    float2(1.0f, 0.0f),
    float2(1.0f, 1.0f),
    float2(0.0f, 1.0f)
};

[[mesh]]
void meshMain(
    uint3 threadID [[thread_position_in_threadgroup]],
    uint3 groupID [[threadgroup_position_in_grid]],
    QuadMesh outputMesh,
    constant MeshArguments* meshArgs [[buffer(1)]]
)
{
    uint threadIndex =
        (threadID.z + threadID.y) * 32u +
        threadID.x;

    outputMesh.set_primitive_count(2);

    if (threadIndex != 0)
        return;

    device const QuadInstance* instances =
        (device const QuadInstance*)
        meshArgs->instanceReference;

    QuadInstance quad =
        instances[groupID.x];

    float4x4 modelMatrix =
        float4x4(
            float4(quad.modelMatrix[0]),
            float4(quad.modelMatrix[1]),
            float4(quad.modelMatrix[2]),
            float4(quad.modelMatrix[3])
        );

    float4x4 viewProjection =
        float4x4(
            meshArgs->viewProjection[0],
            meshArgs->viewProjection[1],
            meshArgs->viewProjection[2],
            meshArgs->viewProjection[3]
        );

    for (uint i = 0; i < 4; ++i)
    {
        float4 position =
            viewProjection *
            (modelMatrix * positions[i]);

        outputMesh.set_vertex(
            i,
            MeshVertex{
                position,
                float4(quad.color),
                uv[i],
                quad.textureIndex
            }
        );
    }

    outputMesh.set_index(0, 0);
    outputMesh.set_index(1, 1);
    outputMesh.set_index(2, 2);

    outputMesh.set_index(3, 0);
    outputMesh.set_index(4, 2);
    outputMesh.set_index(5, 3);
}

fragment float4 fragmentMain(
    MeshVertex input [[stage_in]],
    device TextureArgument* textures [[buffer(0)]],
    sampler diffuseSampler [[sampler(0)]]
)
{
    return textures[input.textureIndex]
        .texture
        .sample(
            diffuseSampler,
            input.texCoord
        );
}
