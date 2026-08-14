struct MeshArguments
{
    float4x4 viewProjection;
    uint64_t instanceReference;
};

struct QuadInstance
{
    float4x4 modelMatrix;
    float4 color;
    uint32_t textureIndex;
    uint3 _padding;
};

struct MeshVertex
{
    float4 position : SV_Position;
    float4 color    : COLOR;
    float3 texCoord : TEXCOORD;
};

[vk::binding(1, 0)]
ConstantBuffer<MeshArguments> meshArgs
    : register(b1, space0);

[shader("mesh")]
[numthreads(32, 1, 1)]
[outputtopology("triangle")]
void meshMain(
    uint threadID : SV_GroupIndex,
    uint3 groupID : SV_GroupID,
    out vertices MeshVertex vertices[4],
    out indices uint3 triangles[2]
)
{
    SetMeshOutputCounts(4, 2);

    if (threadID != 0)
        return;

    QuadInstance* instances =
        (QuadInstance*)meshArgs.instanceReference;

    QuadInstance instance =
        instances[groupID.x];

    static const float4 positions[4] =
    {
        float4(-0.5, -0.5, 0.0, 1.0),
        float4( 0.5, -0.5, 0.0, 1.0),
        float4( 0.5,  0.5, 0.0, 1.0),
        float4(-0.5,  0.5, 0.0, 1.0)
    };

    vertices[0] = MeshVertex(
        mul(
            meshArgs.viewProjection,
            mul(instance.modelMatrix, positions[0])
    ),
        instance.color,
        float3(0.0, 0.0, float(instance.textureIndex))
);

    vertices[1] = MeshVertex(
        mul(
            meshArgs.viewProjection,
            mul(instance.modelMatrix, positions[1])
    ),
        instance.color,
        float3(1.0, 0.0, float(instance.textureIndex))
);

    vertices[2] = MeshVertex(
        mul(
            meshArgs.viewProjection,
            mul(instance.modelMatrix, positions[2])
    ),
        instance.color,
        float3(1.0, 1.0, float(instance.textureIndex))
);

    vertices[3] = MeshVertex(
        mul(
            meshArgs.viewProjection,
            mul(instance.modelMatrix, positions[3])
    ),
        instance.color,
        float3(0.0, 1.0, float(instance.textureIndex))
);

    triangles[0] = uint3(0, 1, 2);
    triangles[1] = uint3(0, 2, 3);
}