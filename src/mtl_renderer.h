#pragma once

#include <Metal/Metal.hpp>
#include <SDL3/SDL.h>
#import <simd/simd.h>

#include <vector>

namespace NRI
{

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

    float _angle = 0.0f;

    Uint64 _lastFrameTime = 0;

    float _deltaTime = 0.0f;


    // ========================================================
    // Mesh arguments
    // ========================================================

    struct alignas(16) MeshArguments
    {
        simd_uint2 viewportSize;

        float angle;

        float padding;
    };

    static_assert(
        sizeof(MeshArguments) == 16
    );

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
};

}
