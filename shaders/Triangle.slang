struct MeshArguments
{
    uint2 viewportSize;
    float angle;
    float padding;
};


struct TextureContainer
{
    Texture2D<float4> textures[1024];
};


// ============================================================
// MESH -> FRAGMENT
// ============================================================

struct MeshVertex
{
    float4 position      : SV_Position;
    float4 color         : COLOR0;
    float2 texCoord      : TEXCOORD0;
    uint   materialIndex : TEXCOORD1;
};


// ============================================================
// Rotation
// ============================================================

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


// ============================================================
// MESH SHADER
// ============================================================
//
// IMPORTANT:
//     [vk::binding(1, 0)]
//
// gives this entry-point parameter the same binding that
// your old VertexArguments used.
//
// Metal should therefore generate:
//
//     [[buffer(1)]]
// ============================================================

[shader("mesh")]
[numthreads(32, 1, 1)]
[outputtopology("triangle")]
void meshMain(
    uint threadID : SV_GroupIndex,

    [vk::binding(1, 0)]
    ConstantBuffer<MeshArguments> meshArgs,

    out vertices MeshVertex vertices[4],

    out indices uint3 triangles[2]
)
{
    SetMeshOutputCounts(4, 2);

    if (threadID != 0)
        return;


    // ========================================================
    // Viewport
    // ========================================================

    float2 viewport =
        float2(meshArgs.viewportSize);


    // ========================================================
    // Quad
    // ========================================================

    const float halfSize = 200.0;


    // ========================================================
    // Rotation
    // ========================================================

    float c =
        cos(meshArgs.angle);

    float s =
        sin(meshArgs.angle);


    // ========================================================
    // Pixel -> NDC
    // ========================================================

    float2 scale =
        viewport / 2.0;


    float2 p0 =
        rotatePoint(
            float2(-halfSize, -halfSize),
            c,
            s
        ) / scale;


    float2 p1 =
        rotatePoint(
            float2( halfSize, -halfSize),
            c,
            s
        ) / scale;


    float2 p2 =
        rotatePoint(
            float2( halfSize,  halfSize),
            c,
            s
        ) / scale;


    float2 p3 =
        rotatePoint(
            float2(-halfSize,  halfSize),
            c,
            s
        ) / scale;


    // ========================================================
    // COMPLETE STRUCT ASSIGNMENT
    // ========================================================

    vertices[0] = MeshVertex(
        float4(p0, 0.0, 1.0),
        float4(1.0, 0.0, 0.0, 1.0),
        float2(0.0, 1.0),
        0
    );


    vertices[1] = MeshVertex(
        float4(p1, 0.0, 1.0),
        float4(1.0, 1.0, 0.0, 1.0),
        float2(1.0, 1.0),
        0
    );


    vertices[2] = MeshVertex(
        float4(p2, 0.0, 1.0),
        float4(1.0, 0.0, 1.0, 1.0),
        float2(1.0, 0.0),
        0
    );


    vertices[3] = MeshVertex(
        float4(p3, 0.0, 1.0),
        float4(0.0, 1.0, 1.0, 1.0),
        float2(0.0, 0.0),
        0
    );


    // ========================================================
    // TWO TRIANGLES
    // ========================================================

    triangles[0] =
        uint3(0, 1, 2);

    triangles[1] =
        uint3(0, 2, 3);
}


// ============================================================
// FRAGMENT SHADER
// ============================================================
//
// Explicitly force:
//
//     bindlessTextures -> buffer(0)
//     diffuseSampler   -> sampler(0)
//
// This restores the exact binding layout from your old
// working shader.
// ============================================================

[shader("fragment")]
float4 fragmentMain(
    MeshVertex input,

    [vk::binding(0, 0)]
    ParameterBlock<TextureContainer> bindlessTextures,

    [vk::binding(0, 1)]
    SamplerState diffuseSampler
) : SV_Target
{
    return bindlessTextures
        .textures[input.materialIndex]
        .Sample(
            diffuseSampler,
            input.texCoord
        );
}