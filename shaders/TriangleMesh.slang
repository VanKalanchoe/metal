struct MeshArguments
{
    uint64_t instanceReference;
    uint2 viewportSize;
};

struct QuadInstance
{
    float2 position;
    float2 size;
    float4 color;
    float angle;
};

struct MeshVertex
{
    float4 position : SV_Position;
    float4 color : COLOR;
    float2 texCoord : TEXCOORD;
};

float2 rotatePoint(float2 p, float c, float s)
{
    return float2(
        p.x * c - p.y * s,
        p.x * s + p.y * c
    );
}

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

    float2 viewport =
        float2(meshArgs.viewportSize);

    float2 halfSize =
        instance.size * 0.5;

    float c = cos(instance.angle);
    float s = sin(instance.angle);

    float2 scale = viewport * 0.5;

    float2 p0 =
        rotatePoint(
            float2(-halfSize.x, -halfSize.y),
            c, s
        ) + instance.position;

    float2 p1 =
        rotatePoint(
            float2( halfSize.x, -halfSize.y),
            c, s
        ) + instance.position;

    float2 p2 =
        rotatePoint(
            float2( halfSize.x,  halfSize.y),
            c, s
        ) + instance.position;

    float2 p3 =
        rotatePoint(
            float2(-halfSize.x,  halfSize.y),
            c, s
        ) + instance.position;

    p0 /= scale;
    p1 /= scale;
    p2 /= scale;
    p3 /= scale;

    vertices[0] = MeshVertex(
        float4(p0, 0, 1),
        instance.color,
        float2(0, 1)
    );

    vertices[1] = MeshVertex(
        float4(p1, 0, 1),
        instance.color,
        float2(1, 1)
    );

    vertices[2] = MeshVertex(
        float4(p2, 0, 1),
        instance.color,
        float2(1, 0)
    );

    vertices[3] = MeshVertex(
        float4(p3, 0, 1),
        instance.color,
        float2(0, 0)
    );

    triangles[0] = uint3(0, 1, 2);
    triangles[1] = uint3(0, 2, 3);
}