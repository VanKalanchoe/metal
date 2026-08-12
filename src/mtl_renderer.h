#pragma once
#include <Metal/Metal.hpp>
#include <SDL3/SDL.h>
#import <simd/simd.h>
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
    void updateViewportSize(uint32_t width, uint32_t height);
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
        uint32_t frameIndex
    );

    void submitCommandBuffer(
        CA::MetalDrawable* drawable
    );
    
private:
    SDL_MetalView m_metalView = nullptr;
    CA::MetalLayer* m_layer = nullptr;
    
    /// The Metal device the renderer draws with by sending commands to it.
    ///
    /// The device instance also creates various resources the renderer needs to
    /// encode and submit its commands.
    MTL::Device* m_Device = nullptr;
    
    /// A command queue the app uses to send command buffers to the Metal device.
    MTL4::CommandQueue* m_CommandQueue = nullptr;
    
    /// A command buffer the app reuses to render each frame.
    MTL4::CommandBuffer* m_CommandBuffer = nullptr;
    
    /// An array of allocators that store commands for each frame
    /// while the app encodes them and the GPU runs them.
    MTL4::CommandAllocator* m_CommandAllocators[kMaxFramesInFlight] = {};
    
    /// An argument table that stores the resource bindings for a render encoder.
    MTL4::ArgumentTable* m_ArgumentTable = nullptr;
    
    /// A residency set that keeps resources in memory for the app's lifetime.
    MTL::ResidencySet* m_ResidencySet = nullptr;
    
    /// A shared event that synchronizes work that runs on the CPU and GPU.
    ///
    /// The app instructs the GPU to signal the main code on the CPU when it
    /// finishes rendering a frame.
    MTL::SharedEvent* m_SharedEvent = nullptr;
    
    /// An integer that tracks the current frame number.
    uint64_t frameNumber;
    
    /// The app's default Metal library.
    ///
    /// The default library stores all of the project's shaders that Xcode
    /// compiles at build time.
    MTL::Library* m_DefaultLibrary;
    
    /// A render pipeline the app creates at runtime.
    ///
    /// The app creates the pipeline with the vertex and fragment shaders in the
    /// `Shaders.metal` source code file.
    MTL::RenderPipelineState* m_RenderPipelineState;

    /// The current size of the view.
    simd_uint2 m_ViewportSize;
    
    /// An array of buffers, each of which stores the geometric position and color
    /// data of a triangle's three vertices for one frame.
    ///
    /// The renderer sends one of these buffers, per frame, as an input to the vertex shader.
    MTL::Buffer* m_TriangleVertexBuffers[kMaxFramesInFlight] = {};
    
    float _angle = 0.0f;
    Uint64 _lastFrameTime = 0;
    float _deltaTime = 0.0f;
    
    MTL::Buffer* m_VertexArguments[kMaxFramesInFlight]{};
    static constexpr uint32_t kMaxBindlessTextures = 10000;//currently only128
    std::vector<MTL::Texture*> m_Textures;
    MTL::SamplerState* m_Sampler = nullptr;
    MTL::Heap* m_TextureHeap = nullptr;
    MTL::Buffer* m_TextureStagingBuffer = nullptr;

       MTL::Buffer* m_BindlessTextureBuffer = nullptr;

       MTL::ArgumentEncoder* m_BindlessTextureEncoder = nullptr;
};
}
