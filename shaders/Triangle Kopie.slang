struct VertexData
{
    float2 position;
    float4 color;
};

struct VertexOut
{
    float4 position : SV_Position;
    float4 color;
};

StructuredBuffer<VertexData> vertexData;
ConstantBuffer<uint2> viewportSize;

[shader("vertex")]
VertexOut vertexMain(uint vertexId : SV_VertexID)
{
    VertexOut output;

    float2 pixelSpacePosition =
        vertexData[vertexId].position;

    float2 viewport =
        float2(viewportSize[0]);

    output.position.xy =
        pixelSpacePosition / (viewport / 2.0);

    output.position.z = 0.0;
    output.position.w = 1.0;

    output.color =
        vertexData[vertexId].color;

    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOut input) : SV_Target
{
    return input.color;
}