struct MeshArguments
{
    uint2 viewportSize;
    float angle;
    float padding;
};

struct MeshVertex
{
    float4 position : SV_Position;
    float4 color    : COLOR;
    float2 texCoord : TEXCOORD;
};

float2 rotatePoint(
    float2 p,
    float c,
    float s
)
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
    out vertices MeshVertex vertices[4],
    out indices uint3 triangles[2]
)
{
    SetMeshOutputCounts(4, 2);

    if (threadID != 0)
        return;

    float2 viewport = float2(meshArgs.viewportSize);

    const float halfSize = 200.0f;

    float c = cos(meshArgs.angle);
    float s = sin(meshArgs.angle);

    float2 scale = viewport / 2.0f;

    float2 p0 =
        rotatePoint(
            float2(-halfSize, -halfSize),
            c, s
        ) / scale;

    float2 p1 =
        rotatePoint(
            float2( halfSize, -halfSize),
            c, s
        ) / scale;

    float2 p2 =
        rotatePoint(
            float2( halfSize,  halfSize),
            c, s
        ) / scale;

    float2 p3 =
        rotatePoint(
            float2(-halfSize,  halfSize),
            c, s
        ) / scale;

    vertices[0] = MeshVertex(
        float4(p0, 0.0f, 1.0f),
        float4(1.0f, 0.0f, 0.0f, 1.0f),
        float2(0.0f, 1.0f)
    );

    vertices[1] = MeshVertex(
        float4(p1, 0.0f, 1.0f),
        float4(1.0f, 1.0f, 0.0f, 1.0f),
        float2(1.0f, 1.0f)
    );

    vertices[2] = MeshVertex(
        float4(p2, 0.0f, 1.0f),
        float4(1.0f, 0.0f, 1.0f, 1.0f),
        float2(1.0f, 0.0f)
    );

    vertices[3] = MeshVertex(
        float4(p3, 0.0f, 1.0f),
        float4(0.0f, 1.0f, 1.0f, 1.0f),
        float2(0.0f, 0.0f)
    );

    triangles[0] = uint3(0, 1, 2);
    triangles[1] = uint3(0, 2, 3);
}