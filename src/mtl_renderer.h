#pragma once

#include <Metal/Metal.hpp>
#include <SDL3/SDL.h>
#import <simd/simd.h>

#include <vector>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace NRI
{

struct QuadInstance
{
    glm::mat4 modelMatrix;
    glm::vec4 color;
};

static_assert(sizeof(QuadInstance) == 80);

class MTLRenderer
{
public:

    static constexpr uint32_t kMaxFramesInFlight = 2;

    MTLRenderer(SDL_Window& window);
    ~MTLRenderer();

    void buildCommandAllocators();
    void buildResidencySet();
    void buildArgumentTable();

    void buildShaders();

    void buildTextureHeap();
    void buildTexture();
    void uploadTexture();

    void buildBindlessTextureBuffer();
    void buildSampler();

    void buildBuffers();
    void buildQuadInstances(uint32_t instanceCount);
    void buildMeshArguments();
    void buildIndirectBuffer();

    void updateViewportSize(
        uint32_t width,
        uint32_t height
    );

    void draw();

private:

    bool waitOnSharedEvent(
        uint64_t earlierFrameNumber
    );

    void setViewport(
        MTL4::RenderCommandEncoder* encoder
    );

    void setRenderPassArguments(
        MTL4::RenderCommandEncoder* encoder
    );

    void submitCommandBuffer(
        CA::MetalDrawable* drawable
    );

private:

    SDL_MetalView m_metalView = nullptr;
    CA::MetalLayer* m_layer = nullptr;

    MTL::Device* m_Device = nullptr;

    MTL4::CommandQueue* m_CommandQueue = nullptr;
    MTL4::CommandBuffer* m_CommandBuffer = nullptr;

    MTL4::CommandAllocator*
        m_CommandAllocators[kMaxFramesInFlight] = {};

    MTL4::ArgumentTable* m_ArgumentTable = nullptr;
    MTL::ResidencySet* m_ResidencySet = nullptr;
    MTL::SharedEvent* m_SharedEvent = nullptr;

    uint64_t frameNumber = 0;

    MTL::Library* m_MeshLibrary = nullptr;
    MTL::Library* m_FragmentLibrary = nullptr;

    MTL::RenderPipelineState*
        m_RenderPipelineState = nullptr;

    simd_uint2 m_ViewportSize = {};
    glm::mat4 m_View = glm::mat4(1.0f);
    glm::mat4 m_Projection = glm::mat4(1.0f);

    float _angle = 0.0f;
    Uint64 _lastFrameTime = 0;
    float _deltaTime = 0.0f;


    // ========================================================
    // Mesh arguments
    // ========================================================

    struct alignas(16) MeshArguments
    {
        glm::mat4 viewProjection;
        uint64_t instanceReference;
        uint32_t padding0;
        uint32_t padding1;
    };

    static_assert(sizeof(MeshArguments) == 80);

    MTL::Buffer* m_MeshArguments = nullptr;


    // ========================================================
    // Bindless textures
    // ========================================================

    static constexpr uint32_t kMaxBindlessTextures = 10000;

    std::vector<MTL::Texture*> m_Textures;

    MTL::SamplerState* m_Sampler = nullptr;
    MTL::Heap* m_TextureHeap = nullptr;

    MTL::Buffer* m_TextureStagingBuffer = nullptr;
    MTL::Buffer* m_BindlessTextureBuffer = nullptr;

    MTL::ArgumentEncoder*
        m_BindlessTextureEncoder = nullptr;

    // ========================================================
    // Quads
    // ========================================================

    MTL::Buffer* m_QuadInstanceBuffer = nullptr;
    MTL::Buffer* m_IndirectBuffer = nullptr;
    uint32_t m_QuadInstanceCount = 0;
    struct DrawMeshTasksIndirectCommand
    {
        uint32_t groupCountX;
        uint32_t groupCountY;
        uint32_t groupCountZ;
    };

    static_assert(sizeof(DrawMeshTasksIndirectCommand) == 12);
};

}
