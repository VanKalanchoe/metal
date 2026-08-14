#include <metal_stdlib>

using namespace metal;

struct QuadInstance
{
    array<packed_float4, 4> modelMatrix;
    packed_float4 color;
    uint textureIndex;
    packed_uint3 _padding;
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
    float3 texCoord;
};

struct TextureContainer
{
    array<texture2d<float, access::sample>, 1024> textures;
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

// ============================================================
// MESH SHADER
// ============================================================

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

    // ========================================================
    // GPU ADDRESS -> INSTANCE ARRAY
    // ========================================================

    device const QuadInstance* instances =
        (device const QuadInstance*)
        meshArgs->instanceReference;

    // EXACT same indexing as Slang
    const QuadInstance instance =
        instances[groupID.x];

    // ========================================================
    // RECONSTRUCT THE MATRICES
    // ========================================================

    float4x4 modelMatrix = float4x4(
        float4(instance.modelMatrix[0]),
        float4(instance.modelMatrix[1]),
        float4(instance.modelMatrix[2]),
        float4(instance.modelMatrix[3])
    );

    float4x4 viewProjection = float4x4(
        meshArgs->viewProjection[0],
        meshArgs->viewProjection[1],
        meshArgs->viewProjection[2],
        meshArgs->viewProjection[3]
    );

    // ========================================================
    // QUAD
    // ========================================================

    const float4 positions[4] =
    {
        float4(-0.5f, -0.5f, 0.0f, 1.0f),
        float4( 0.5f, -0.5f, 0.0f, 1.0f),
        float4( 0.5f,  0.5f, 0.0f, 1.0f),
        float4(-0.5f,  0.5f, 0.0f, 1.0f)
    };

    const float2 uvs[4] =
    {
        float2(0.0f, 0.0f),
        float2(1.0f, 0.0f),
        float2(1.0f, 1.0f),
        float2(0.0f, 1.0f)
    };

    // ========================================================
    // SAME SEMANTIC OPERATION AS ORIGINAL SLANG:
    //
    // mul(viewProjection, mul(modelMatrix, position))
    //
    // ========================================================

    float4 p0 =
        viewProjection *
        (modelMatrix * positions[0]);

    float4 p1 =
        viewProjection *
        (modelMatrix * positions[1]);

    float4 p2 =
        viewProjection *
        (modelMatrix * positions[2]);

    float4 p3 =
        viewProjection *
        (modelMatrix * positions[3]);

    // ========================================================
    // OUTPUT
    // ========================================================

    outputMesh.set_vertex(
        0,
        MeshVertex{
            p0,
            float4(instance.color),
            float3(
                uvs[0],
                float(instance.textureIndex)
            )
        }
    );

    outputMesh.set_vertex(
        1,
        MeshVertex{
            p1,
            float4(instance.color),
            float3(
                uvs[1],
                float(instance.textureIndex)
            )
        }
    );

    outputMesh.set_vertex(
        2,
        MeshVertex{
            p2,
            float4(instance.color),
            float3(
                uvs[2],
                float(instance.textureIndex)
            )
        }
    );

    outputMesh.set_vertex(
        3,
        MeshVertex{
            p3,
            float4(instance.color),
            float3(
                uvs[3],
                float(instance.textureIndex)
            )
        }
    );

    // ========================================================
    // TRIANGLES
    // ========================================================

    outputMesh.set_index(0, 0);
    outputMesh.set_index(1, 1);
    outputMesh.set_index(2, 2);

    outputMesh.set_index(3, 0);
    outputMesh.set_index(4, 2);
    outputMesh.set_index(5, 3);
}

// ============================================================
// FRAGMENT
// ============================================================

fragment float4 fragmentMain(
    MeshVertex input [[stage_in]],
    constant TextureContainer* bindlessTextures [[buffer(0)]],
    sampler diffuseSampler [[sampler(0)]]
)
{
    uint textureIndex =
        uint(input.texCoord.z);

    return bindlessTextures
        ->textures[textureIndex]
        .sample(
            diffuseSampler,
            input.texCoord.xy
        );
}
