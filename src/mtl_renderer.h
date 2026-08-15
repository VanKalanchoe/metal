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
#include <imgui.h>
namespace NRI
{

struct QuadInstance
{
    glm::mat4 modelMatrix;
    glm::vec4 color;
    uint32_t textureIndex;
};

static_assert(sizeof(QuadInstance) == 84);

class MTLRenderer
{
public:

    static constexpr uint32_t kMaxFramesInFlight = 3;

    MTLRenderer(SDL_Window& window);
    ~MTLRenderer();

    void buildCommandAllocators();
    void buildResidencySet();
    void buildArgumentTable();

    void buildShaders();

    void buildTextureHeap();
    uint32_t buildTexture(const char* path);
    bool uploadTexture(
        uint32_t textureIndex,
        MTL::Buffer* stagingBuffer
    );

    void buildBindlessTextureBuffer();
    void buildSampler();

    void buildBuffers();
    void buildQuadInstances(uint32_t instanceCount);
    void buildMeshArguments();
    void buildIndirectBuffer();

    void updateQuadInstances(uint32_t frameIndex);
    void updateIndirectBuffer(uint32_t frameIndex);
    void updateMeshArguments(uint32_t frameIndex);
    void updateFrameData(uint32_t frameIndex);
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
        MTL4::RenderCommandEncoder* encoder,
        uint32_t frameindex
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
    uint64_t m_LastSubmittedFrame = 0;
    MTL::Library* m_ShaderLibrary = nullptr;

    MTL::RenderPipelineState*
        m_RenderPipelineState = nullptr;
    
    // ========================================================
    // Offscreen scene texture
    // ========================================================
    MTL::Texture* m_SceneMSAATexture = nullptr;
    MTL::Texture* m_SceneTexture = nullptr;

    void createSceneTexture(
        uint32_t width,
        uint32_t height
    );

    void destroySceneTexture();
    bool waitForGPUIdle();
    ImTextureID getImGuiTextureID() const;
    void applyPendingResize();
    simd_uint2 m_ViewportSize = {};
    
    simd_uint2 m_PendingViewportSize = {};
    bool m_ResizePending = false;
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
    };

    static_assert(sizeof(MeshArguments) == 80);

    std::vector<MTL::Buffer*> m_MeshArguments;
    std::vector<void*> m_MeshArgumentsMapped;


    // ========================================================
    // Bindless textures
    // ========================================================

    static constexpr uint32_t kMaxBindlessTextures = 64;

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

    

    std::vector<MTL::Buffer*> m_QuadInstanceBuffers;
    std::vector<void*> m_QuadInstanceBuffersMapped;

    std::vector<MTL::Buffer*> m_IndirectBuffers;
    std::vector<void*> m_IndirectBuffersMapped;
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
