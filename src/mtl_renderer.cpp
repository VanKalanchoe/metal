#include "mtl_renderer.h"
#include "ShaderCompiler.h"
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
namespace NRI
{
MTLRenderer::MTLRenderer(SDL_Window& window)
:   _angle(0.0f),
_lastFrameTime(SDL_GetPerformanceCounter()),
_deltaTime(0.0f)
{
    // Initialize Metal here
    // Retrieve the Metal device instance from the view.
    m_metalView = SDL_Metal_CreateView(&window);
    if(!m_metalView)
    {
        SDL_Log("Failed to create Metal view: %s", SDL_GetError());
        
        return;
    }
    
    m_layer = static_cast<CA::MetalLayer*>(SDL_Metal_GetLayer(m_metalView));
    if(!m_layer)
    {
        SDL_Log("Failed to get CAMetalLayer");
        return;
    }
    
    m_Device = MTL::CreateSystemDefaultDevice();
    if(!m_Device)
    {
        SDL_Log("Failed to create Metal 4 device");
        return;
    }
    
    m_layer->setDevice(m_Device);
    
    m_layer->setPixelFormat(MTL::PixelFormat::PixelFormatBGRA8Unorm_sRGB);
    
    // Create a command queue from the device.
    m_CommandQueue = m_Device->newMTL4CommandQueue();
    if(!m_CommandQueue)
    {
        SDL_Log("Failed to create Metal 4 command queue");
        return;
    }
    
    // Create the command buffer from the device.
    m_CommandBuffer = m_Device->newCommandBuffer();
    
    if (!m_CommandBuffer)
    {
        SDL_Log("Failed to create Metal 4 command buffer");
        return;
    }
    
    // Create a default library instance, which contains the project's shaders.
    m_DefaultLibrary = m_Device->newDefaultLibrary();
    
    // Create the essential resources.
    buildCommandAllocators();
    
    buildTextureHeap();
    buildTexture();
    
    
    
    buildSampler();
    buildBuffers();
    
  
    
    // Shaders / Pipelines
    buildShaders();
    buildBindlessTextureBuffer();
    buildArgumentTable();
    buildResidencySet();
    // Frame state
    
    frameNumber = 0;
    
    m_SharedEvent = m_Device->newSharedEvent();
    
    if(!m_SharedEvent)
    {
        SDL_Log("Failed to create shared event");
        return;
    }
    
    m_SharedEvent->setSignaledValue(frameNumber);
    
    // Residency
    for(uint32_t i = 0; i < kMaxFramesInFlight; i++)
    {
        m_ResidencySet->addAllocation(m_TriangleVertexBuffers[i]);
        m_ResidencySet->addAllocation(
                m_VertexArguments[i]
            );
    }
    
    for (MTL::Texture* texture : m_Textures)
    {
        m_ResidencySet->addAllocation(texture);
    }
    if (m_TextureStagingBuffer)
    {
        m_ResidencySet->addAllocation(
            m_TextureStagingBuffer
        );
    }
    // Bindless argument buffer.
        if (m_BindlessTextureBuffer)
        {
            m_ResidencySet->addAllocation(
                m_BindlessTextureBuffer
            );
        }
    
    
    m_ResidencySet->commit();
    
    m_CommandQueue->addResidencySet(m_ResidencySet);
    
    if(m_layer->residencySet())
    {
        m_CommandQueue->addResidencySet(m_layer->residencySet());
    }
    uploadTexture();
    int width = 0;
    int height = 0;
    
    SDL_GetWindowSizeInPixels(&window, &width, &height);
    
    updateViewportSize(
                       static_cast<uint32_t>(width),
                       static_cast<uint32_t>(height)
                       );
}

NRI::MTLRenderer::~MTLRenderer()
{
    // ------------------------------------------------------------
    // Pipeline
    // ------------------------------------------------------------
    
    if (m_RenderPipelineState)
    {
        m_RenderPipelineState->release();
        m_RenderPipelineState = nullptr;
    }
    
    // ------------------------------------------------------------
    // Shader library
    // ------------------------------------------------------------
    
    if (m_DefaultLibrary)
    {
        m_DefaultLibrary->release();
        m_DefaultLibrary = nullptr;
    }
    
    // ------------------------------------------------------------
    // Textures
    // ------------------------------------------------------------
    if (m_BindlessTextureEncoder)
    {
        m_BindlessTextureEncoder->release();
        m_BindlessTextureEncoder = nullptr;
    }

    if (m_BindlessTextureBuffer)
    {
        m_BindlessTextureBuffer->release();
        m_BindlessTextureBuffer = nullptr;
    }
    if (m_TextureStagingBuffer)
    {
        m_TextureStagingBuffer->release();
        m_TextureStagingBuffer = nullptr;
    }
    
    for (MTL::Texture* texture : m_Textures)
    {
        if (texture)
            texture->release();
    }

    m_Textures.clear();
    
    if (m_TextureHeap)
    {
        m_TextureHeap->release();
        m_TextureHeap = nullptr;
    }
    
    // ------------------------------------------------------------
    // Buffers
    // ------------------------------------------------------------
    
    for (uint32_t i = 0;
         i < kMaxFramesInFlight;
         ++i)
    {
        if (m_VertexArguments[i])
          {
              m_VertexArguments[i]->release();
              m_VertexArguments[i] = nullptr;
          }

        
        if (m_TriangleVertexBuffers[i])
        {
            m_TriangleVertexBuffers[i]->release();
            m_TriangleVertexBuffers[i] = nullptr;
        }
    }
    
    // ------------------------------------------------------------
    // Argument table
    // ------------------------------------------------------------
    
    if (m_ArgumentTable)
    {
        m_ArgumentTable->release();
        m_ArgumentTable = nullptr;
    }
    
    // ------------------------------------------------------------
    // Residency
    // ------------------------------------------------------------
    
    if (m_ResidencySet)
    {
        m_ResidencySet->release();
        m_ResidencySet = nullptr;
    }
    
    // ------------------------------------------------------------
    // Command allocators
    // ------------------------------------------------------------
    
    for (uint32_t i = 0;
         i < kMaxFramesInFlight;
         ++i)
    {
        if (m_CommandAllocators[i])
        {
            m_CommandAllocators[i]->release();
            m_CommandAllocators[i] = nullptr;
        }
    }
    
    // ------------------------------------------------------------
    // Shared event
    // ------------------------------------------------------------
    
    if (m_SharedEvent)
    {
        m_SharedEvent->release();
        m_SharedEvent = nullptr;
    }
    
    // ------------------------------------------------------------
    // Command buffer
    // ------------------------------------------------------------
    
    if (m_CommandBuffer)
    {
        m_CommandBuffer->release();
        m_CommandBuffer = nullptr;
    }
    
    // ------------------------------------------------------------
    // Command queue
    // ------------------------------------------------------------
    
    if (m_CommandQueue)
    {
        m_CommandQueue->release();
        m_CommandQueue = nullptr;
    }
    
    // ------------------------------------------------------------
    // Metal device
    // ------------------------------------------------------------
    
    if (m_Device)
    {
        m_Device->release();
        m_Device = nullptr;
    }
    
    // ------------------------------------------------------------
    // SDL Metal view
    // ------------------------------------------------------------
    
    if (m_metalView)
    {
        SDL_Metal_DestroyView(m_metalView);
        m_metalView = nullptr;
    }
}

void MTLRenderer::buildCommandAllocators()
{
    for(uint32_t i = 0; i < kMaxFramesInFlight; i++)
    {
        m_CommandAllocators[i] = m_Device->newCommandAllocator();
        
        if(!m_CommandAllocators[i])
        {
            SDL_Log("Failed to create command allocator %u", i);
            
            return;
        }
    }
}
void MTLRenderer::buildResidencySet()
{
    NS::Error* error = nullptr;
    
    MTL::ResidencySetDescriptor* residencySetDescriptor = MTL::ResidencySetDescriptor::alloc()->init();
    
    m_ResidencySet = m_Device->newResidencySet(residencySetDescriptor, &error);
    
    residencySetDescriptor->release();
    
    if (!m_ResidencySet)
    {
        SDL_Log(
                "Failed to create residency set: %s",
                error
                ? error->localizedDescription()->utf8String()
                : "Unknown error"
                );
        
        if (error)
            error->release();
        
        return;
    }
}

void MTLRenderer::buildArgumentTable()
{
    NS::Error* error = nullptr;
    
    MTL4::ArgumentTableDescriptor* argumentTableDescriptor = MTL4::ArgumentTableDescriptor::alloc()->init();
    // ------------------------------------------------------------
       // buffer 0 = bindless texture argument buffer
       // buffer 1 = VertexArguments BDA
       // ------------------------------------------------------------

    argumentTableDescriptor->setMaxBufferBindCount(2);
    // IMPORTANT:
       //
       // We are NOT putting 1,000,000 textures into
       // MTL4ArgumentTable.
       //
       // The texture references live inside the bindless
       // argument buffer.
       //
    argumentTableDescriptor->setMaxTextureBindCount(0);
    argumentTableDescriptor->setMaxSamplerStateBindCount(1);
    
    m_ArgumentTable = m_Device->newArgumentTable(argumentTableDescriptor, &error);
    
    argumentTableDescriptor->release();
    
    if(!m_ArgumentTable)
    {
        SDL_Log("Failed to create argument table: %s", error ? error->localizedDescription()->utf8String() : "Unknown error");
        
        if(error)
            error->release();
        
        return;
    }
}

void MTLRenderer::buildShaders()
{
    using NS::StringEncoding::UTF8StringEncoding;

    NS::Error* error = nullptr;

    const char* metalSource = R"METAL(
#include <metal_stdlib>

using namespace metal;

struct VertexData
{
    float2 position;
    float4 color;
    float2 texCoord;
};

struct VertexArguments
{
    ulong vertexReference;
    uint2 viewportSize;
};

struct SourceTextureArguments
{
    texture2d<float, access::sample> texture [[id(0)]];
};

struct VertexOut
{
    float4 position [[position]];
    float4 color;
    float2 texCoord;
    uint materialIndex;
};

vertex VertexOut vertexMain(
    uint vertexId [[vertex_id]],
    constant VertexArguments* vertexArgs [[buffer(1)]]
)
{
    device VertexData* vertices =
        (device VertexData*)vertexArgs->vertexReference;

    VertexData v = vertices[vertexId];

    float2 viewport =
        float2(vertexArgs->viewportSize);

    VertexOut output;

    output.position =
        float4(
            v.position / (viewport / 2.0),
            0.0,
            1.0
        );

    output.color = v.color;
    output.texCoord = v.texCoord;

    // Test texture 0 for now.
    output.materialIndex = 0;

    return output;
}

fragment float4 fragmentMain(
    VertexOut input [[stage_in]],
    device SourceTextureArguments* textures [[buffer(0)]],
    sampler diffuseSampler [[sampler(0)]]
)
{
    texture2d<float, access::sample> texture =
        textures[input.materialIndex].texture;

    return texture.sample(
        diffuseSampler,
        input.texCoord
    );
}
)METAL";
    
    auto shaderCompiler = CreateSlangCompiler();

    std::vector<char> msl =
        shaderCompiler->compile(
            "../../shaders/Triangle.slang"
        );

    NS::String* source =
        NS::String::string(
            std::string(msl.begin(), msl.end()).c_str(),
            UTF8StringEncoding
        );
    //printf("\n================ SLANG GENERATED MSL ================\n");
    //printf("%.*s", static_cast<int>(msl.size()), msl.data());
    //printf("\n================ END SLANG MSL ======================\n\n");

    MTL::Library* library =
        m_Device->newLibrary(
                             source,
            nullptr,
            &error
        );

    if (!library)
    {
        SDL_Log(
            "Failed to compile Metal shader: %s",
            error
                ? error->localizedDescription()->utf8String()
                : "Unknown error"
        );

        if (error)
            error->release();

        return;
    }

    // Replace old library.
    if (m_DefaultLibrary)
    {
        m_DefaultLibrary->release();
        m_DefaultLibrary = nullptr;
    }

    m_DefaultLibrary = library;

    // ------------------------------------------------------------
    // Metal 4 compiler
    // ------------------------------------------------------------

    MTL4::CompilerDescriptor* compilerDescriptor =
        MTL4::CompilerDescriptor::alloc()->init();

    MTL4::Compiler* compiler =
        m_Device->newCompiler(
            compilerDescriptor,
            &error
        );

    compilerDescriptor->release();

    if (!compiler)
    {
        SDL_Log(
            "Failed to create Metal 4 compiler: %s",
            error
                ? error->localizedDescription()->utf8String()
                : "Unknown error"
        );

        if (error)
            error->release();

        return;
    }

    // ------------------------------------------------------------
    // Vertex
    // ------------------------------------------------------------

    MTL4::LibraryFunctionDescriptor* vertexDescriptor =
        MTL4::LibraryFunctionDescriptor::alloc()->init();

    vertexDescriptor->setLibrary(library);

    vertexDescriptor->setName(
        NS::String::string(
            "vertexMain",
            UTF8StringEncoding
        )
    );

    // ------------------------------------------------------------
    // Fragment
    // ------------------------------------------------------------

    MTL4::LibraryFunctionDescriptor* fragmentDescriptor =
        MTL4::LibraryFunctionDescriptor::alloc()->init();

    fragmentDescriptor->setLibrary(library);

    fragmentDescriptor->setName(
        NS::String::string(
            "fragmentMain",
            UTF8StringEncoding
        )
    );

    // ------------------------------------------------------------
    // Pipeline
    // ------------------------------------------------------------

    MTL4::RenderPipelineDescriptor* pipelineDescriptor =
        MTL4::RenderPipelineDescriptor::alloc()->init();

    pipelineDescriptor->setLabel(
        NS::String::string(
            "NRI Metal 4 Bindless Pipeline",
            UTF8StringEncoding
        )
    );

    pipelineDescriptor->setVertexFunctionDescriptor(
        vertexDescriptor
    );

    pipelineDescriptor->setFragmentFunctionDescriptor(
        fragmentDescriptor
    );

    auto* colorAttachment =
        pipelineDescriptor
            ->colorAttachments()
            ->object(0);

    colorAttachment->setPixelFormat(
        MTL::PixelFormat::PixelFormatBGRA8Unorm_sRGB
    );

    colorAttachment->setBlendingState(
        MTL4::BlendState::BlendStateEnabled
    );

    colorAttachment->setSourceRGBBlendFactor(
        MTL::BlendFactor::BlendFactorSourceAlpha
    );

    colorAttachment->setDestinationRGBBlendFactor(
        MTL::BlendFactor::BlendFactorOneMinusSourceAlpha
    );

    colorAttachment->setSourceAlphaBlendFactor(
        MTL::BlendFactor::BlendFactorOne
    );

    colorAttachment->setDestinationAlphaBlendFactor(
        MTL::BlendFactor::BlendFactorOneMinusSourceAlpha
    );

    // ------------------------------------------------------------
    // Compile
    // ------------------------------------------------------------

    m_RenderPipelineState =
        compiler->newRenderPipelineState(
            pipelineDescriptor,
            nullptr,
            &error
        );

    if (!m_RenderPipelineState)
    {
        SDL_Log(
            "Failed to create Metal 4 render pipeline: %s",
            error
                ? error->localizedDescription()->utf8String()
                : "Unknown error"
        );

        if (error)
            error->release();

        compiler->release();
        vertexDescriptor->release();
        fragmentDescriptor->release();
        pipelineDescriptor->release();

        return;
    }

    compiler->release();
    vertexDescriptor->release();
    fragmentDescriptor->release();
    pipelineDescriptor->release();

    SDL_Log("Metal 4 bindless pipeline created successfully");
}
void MTLRenderer::buildTextureHeap()
{
    MTL::HeapDescriptor* descriptor =
        MTL::HeapDescriptor::alloc()->init();

    descriptor->setType(
        MTL::HeapType::HeapTypeAutomatic
    );

    descriptor->setStorageMode(
        MTL::StorageMode::StorageModePrivate
    );

    descriptor->setCpuCacheMode(
        MTL::CPUCacheMode::CPUCacheModeDefaultCache
    );

    descriptor->setSize(
        64 * 1024 * 1024
    );

    m_TextureHeap =
        m_Device->newHeap(descriptor);

    descriptor->release();

    if (!m_TextureHeap)
    {
        SDL_Log("Failed to create texture heap");
        return;
    }

    SDL_Log("Metal texture heap created successfully");
}
void MTLRenderer::buildTexture()
{
    int width = 0;
    int height = 0;
    int channels = 0;

    stbi_uc* pixels =
        stbi_load(
            "../../textures/ChernoLogo.png",
            &width,
            &height,
            &channels,
            STBI_rgb_alpha
        );

    if (!pixels)
    {
        SDL_Log(
            "Failed to load texture: %s",
            stbi_failure_reason()
        );

        return;
    }

    const size_t bytesPerPixel = 4;

    const size_t bytesPerRow =
        static_cast<size_t>(width) *
        bytesPerPixel;

    const size_t textureSize =
        bytesPerRow *
        static_cast<size_t>(height);

    // ------------------------------------------------------------
    // Shared CPU-visible staging buffer
    // ------------------------------------------------------------

    m_TextureStagingBuffer =
        m_Device->newBuffer(
            textureSize,
            MTL::ResourceStorageModeShared
        );

    if (!m_TextureStagingBuffer)
    {
        SDL_Log(
            "Failed to create texture staging buffer"
        );

        stbi_image_free(pixels);
        return;
    }

    memcpy(
        m_TextureStagingBuffer->contents(),
        pixels,
        textureSize
    );

    stbi_image_free(pixels);

    // ------------------------------------------------------------
    // Private heap texture
    // ------------------------------------------------------------

    MTL::TextureDescriptor* descriptor =
        MTL::TextureDescriptor::alloc()->init();

    descriptor->setTextureType(
        MTL::TextureType::TextureType2D
    );

    descriptor->setPixelFormat(
        MTL::PixelFormat::PixelFormatRGBA8Unorm_sRGB
    );

    descriptor->setWidth(width);
    descriptor->setHeight(height);
    descriptor->setMipmapLevelCount(1);

    descriptor->setUsage(
        MTL::TextureUsageShaderRead
    );

    descriptor->setStorageMode(
        MTL::StorageMode::StorageModePrivate
    );

    MTL::Texture* texture =
        m_TextureHeap->newTexture(
            descriptor
        );

    descriptor->release();

    if (!texture)
    {
        SDL_Log(
            "Failed to create texture from heap"
        );

        m_TextureStagingBuffer->release();
        m_TextureStagingBuffer = nullptr;

        return;
    }

    m_Textures.push_back(texture);
}
void MTLRenderer::buildBindlessTextureBuffer()
{
    if (m_Textures.empty())
    {
        SDL_Log(
            "No textures available for bindless texture buffer"
        );
        return;
    }

    MTL::Function* fragmentFunction =
        m_DefaultLibrary->newFunction(
            NS::String::string(
                "fragmentMain",
                NS::StringEncoding::UTF8StringEncoding
            )
        );

    if (!fragmentFunction)
    {
        SDL_Log(
            "Failed to get fragmentMain"
        );
        return;
    }

    MTL::ArgumentEncoder* encoder =
        fragmentFunction->newArgumentEncoder(0);

    fragmentFunction->release();

    if (!encoder)
    {
        SDL_Log(
            "Failed to create bindless argument encoder"
        );
        return;
    }

    const NS::UInteger elementStride =
        encoder->encodedLength();

    if (elementStride == 0)
    {
        SDL_Log(
            "Argument encoder returned zero stride"
        );

        encoder->release();
        return;
    }

    const NS::UInteger bufferSize =
        elementStride *
        static_cast<NS::UInteger>(
            m_Textures.size()
        );

    m_BindlessTextureBuffer =
        m_Device->newBuffer(
            bufferSize,
            MTL::ResourceStorageModeShared
        );

    if (!m_BindlessTextureBuffer)
    {
        SDL_Log(
            "Failed to create bindless texture buffer"
        );

        encoder->release();
        return;
    }

    m_BindlessTextureBuffer->setLabel(
        NS::String::string(
            "BindlessTextureBuffer",
            NS::StringEncoding::UTF8StringEncoding
        )
    );

    for (NS::UInteger i = 0;
         i < static_cast<NS::UInteger>(m_Textures.size());
         ++i)
    {
        encoder->setArgumentBuffer(
            m_BindlessTextureBuffer,
            i * elementStride
        );

        encoder->setTexture(
            m_Textures[i],
            0
        );
    }

    SDL_Log(
        "Bindless texture buffer created: %zu textures",
        m_Textures.size()
    );

    encoder->release();
}
void MTLRenderer::uploadTexture()
{
    if (m_Textures.empty())
        return;

    if (!m_TextureStagingBuffer)
        return;

    MTL::Texture* texture =
        m_Textures[0];

    const uint32_t width =
        static_cast<uint32_t>(
            texture->width()
        );

    const uint32_t height =
        static_cast<uint32_t>(
            texture->height()
        );

    const size_t bytesPerRow =
        static_cast<size_t>(width) * 4;

    const size_t textureSize =
        bytesPerRow * height;

    MTL::SharedEvent* uploadEvent =
        m_Device->newSharedEvent();

    if (!uploadEvent)
    {
        SDL_Log(
            "Failed to create texture upload event"
        );
        return;
    }

    uploadEvent->setSignaledValue(0);

    MTL4::CommandAllocator* allocator =
        m_CommandAllocators[0];

    allocator->reset();

    m_CommandBuffer->beginCommandBuffer(
        allocator
    );

    MTL4::ComputeCommandEncoder* encoder =
        m_CommandBuffer->computeCommandEncoder();

    if (!encoder)
    {
        SDL_Log(
            "Failed to create Metal 4 compute encoder"
        );

        m_CommandBuffer->endCommandBuffer();
        uploadEvent->release();
        return;
    }

    encoder->copyFromBuffer(
        m_TextureStagingBuffer,
        0,
        bytesPerRow,
        textureSize,

        MTL::Size::Make(
            width,
            height,
            1
        ),

        texture,
        0,
        0,

        MTL::Origin::Make(
            0,
            0,
            0
        ),

        0
    );

    encoder->endEncoding();

    m_CommandBuffer->endCommandBuffer();

    // Signal is ordered after GPU work already queued on this queue.
    m_CommandQueue->signalEvent(
        uploadEvent,
        1
    );

    MTL4::CommandBuffer* commandBuffers[] =
    {
        m_CommandBuffer
    };

    m_CommandQueue->commit(
        commandBuffers,
        1
    );

    const bool completed =
        uploadEvent->waitUntilSignaledValue(
            1,
            10000
        );

    if (!completed)
    {
        SDL_Log(
            "Texture upload timed out"
        );
    }

    uploadEvent->release();

    // GPU is finished with staging data.
    m_TextureStagingBuffer->release();
    m_TextureStagingBuffer = nullptr;
}
void MTLRenderer::buildSampler()
{
    MTL::SamplerDescriptor* descriptor =
        MTL::SamplerDescriptor::alloc()->init();

    descriptor->setMinFilter(
        MTL::SamplerMinMagFilter::SamplerMinMagFilterLinear
    );

    descriptor->setMagFilter(
        MTL::SamplerMinMagFilter::SamplerMinMagFilterLinear
    );

    descriptor->setMipFilter(
        MTL::SamplerMipFilter::SamplerMipFilterNotMipmapped
    );

    descriptor->setSAddressMode(
        MTL::SamplerAddressMode::SamplerAddressModeRepeat
    );

    descriptor->setTAddressMode(
        MTL::SamplerAddressMode::SamplerAddressModeRepeat
    );

    m_Sampler =
        m_Device->newSamplerState(descriptor);

    descriptor->release();

    if (!m_Sampler)
        SDL_Log("Failed to create sampler");
}

enum InputBufferIndex
{
    InputBufferIndexForVertexArguments = 0
    //InputBufferIndexForVertexData = 0,
    //InputBufferIndexForViewportSize = 1
};

struct VertexData
{
    simd_float2 position;
    simd_float4 color;
    simd_float2 texCoord;
};
struct VertexArguments
{
    uint64_t vertexReference;
    simd_uint2 viewportSize;
};

static_assert(sizeof(VertexArguments) == 16);
void MTLRenderer::buildBuffers()
{
    const size_t vertexDataSize =
    sizeof(VertexData) * 6;
    
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i)
    {
        m_TriangleVertexBuffers[i] =
        m_Device->newBuffer(
                            vertexDataSize,
                            MTL::ResourceStorageModeShared
                            );
        
        if (!m_TriangleVertexBuffers[i])
        {
            SDL_Log(
                    "Failed to create triangle vertex buffer %u",
                    i
                    );
            
            return;
        }
        
        m_VertexArguments[i] =
                    m_Device->newBuffer(
                        sizeof(VertexArguments),
                        MTL::ResourceStorageModeShared
                    );

                if (!m_VertexArguments[i])
                {
                    SDL_Log(
                        "Failed to create vertex argument buffer %u",
                        i
                    );

                    return;
                }
        
        auto* args =
                    reinterpret_cast<VertexArguments*>(
                        m_VertexArguments[i]->contents()
                    );

                args->vertexReference =
                    m_TriangleVertexBuffers[i]->gpuAddress();
    }
}
struct ScopedAutoreleasePool {
    NS::AutoreleasePool* pool;
    ScopedAutoreleasePool()  : pool(NS::AutoreleasePool::alloc()->init()) {}
    ~ScopedAutoreleasePool() { pool->release(); }
};

void MTLRenderer::updateViewportSize(
    uint32_t width,
    uint32_t height
)
{
    m_ViewportSize.x = width;
    m_ViewportSize.y = height;

    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i)
    {
        if (!m_VertexArguments[i])
            continue;

        auto* args =
            reinterpret_cast<VertexArguments*>(
                m_VertexArguments[i]->contents()
            );

        args->viewportSize = m_ViewportSize;
    }
}

bool MTLRenderer::waitOnSharedEvent(uint64_t earlierFrameNumber)
{
    // Increased timeout to prevent false positives during VSync hitches or window resizing.
    constexpr uint64_t timeoutMS = 1000;

    const bool signaled = m_SharedEvent->waitUntilSignaledValue(earlierFrameNumber, timeoutMS);

    if (!signaled)
    {
        SDL_Log("GPU timeout waiting for frame %llu after %llu ms",
                static_cast<unsigned long long>(earlierFrameNumber),
                static_cast<unsigned long long>(timeoutMS));
    }

    return signaled;
}

void MTLRenderer::setViewport(
                              MTL4::RenderCommandEncoder* encoder
                              )
{
    MTL::Viewport viewport;
    
    viewport.originX = 0.0;
    viewport.originY = 0.0;
    
    viewport.width =
    static_cast<double>(m_ViewportSize.x);
    
    viewport.height =
    static_cast<double>(m_ViewportSize.y);
    
    viewport.znear = 0.0;
    viewport.zfar = 1.0;
    
    encoder->setViewport(viewport);
}

void MTLRenderer::setRenderPassArguments(
                                         MTL4::RenderCommandEncoder* encoder,
                                         uint32_t frameIndex
                                         )
{
    MTL::Buffer* vertexBuffer =
    m_TriangleVertexBuffers[frameIndex];
    
    VertexData* vertices =
    reinterpret_cast<VertexData*>(
                                  vertexBuffer->contents()
                                  );
    
    
    // ------------------------------------------------------------
    // Rotation
    // ------------------------------------------------------------
    
    // Apple example effectively rotates 1 degree per frame.
    // At 60 FPS that is 60 degrees per second.
    //
    // Using delta time makes the speed constant regardless
    // of whether the renderer runs at 60, 120, 144, etc. FPS.
    
    const float radius = 350.0f;
    
    constexpr float rotationSpeed =
    60.0f * (3.14159265359f / 180.0f);
    
    _angle += rotationSpeed * _deltaTime;
    
    if (_angle >= 2.0f * 3.14159265359f)
        _angle -= 2.0f * 3.14159265359f;
    
    const float halfSize = 200.0f;

    const float c = cosf(_angle);
    const float s = sinf(_angle);

    auto rotatePoint =
        [c, s](float x, float y)
    {
        return simd_make_float2(
            x * c - y * s,
            x * s + y * c
        );
    };

    vertices[0] = {
        rotatePoint(-halfSize, -halfSize),
        {1.0f, 0.0f, 0.0f, 1.0f},
        {0.0f, 1.0f}
    };

    vertices[1] = {
        rotatePoint( halfSize, -halfSize),
        {1.0f, 1.0f, 0.0f, 1.0f},
        {1.0f, 1.0f}
    };

    vertices[2] = {
        rotatePoint( halfSize,  halfSize),
        {1.0f, 0.0f, 1.0f, 1.0f},
        {1.0f, 0.0f}
    };

    vertices[3] = {
        rotatePoint(-halfSize, -halfSize),
        {1.0f, 0.0f, 0.0f, 1.0f},
        {0.0f, 1.0f}
    };

    vertices[4] = {
        rotatePoint( halfSize,  halfSize),
        {1.0f, 0.0f, 1.0f, 1.0f},
        {1.0f, 0.0f}
    };

    vertices[5] = {
        rotatePoint(-halfSize,  halfSize),
        {0.0f, 1.0f, 1.0f, 1.0f},
        {0.0f, 0.0f}
    };
    
    
    // ------------------------------------------------------------
    // BUFFER 0
    //
    // Bindless argument buffer.
    // ------------------------------------------------------------

    m_ArgumentTable->setAddress(
            m_BindlessTextureBuffer->gpuAddress(),
            0
        );

        // ------------------------------------------------------------
        // BUFFER 1
        //
        // BDA vertex arguments.
        // ------------------------------------------------------------

        m_ArgumentTable->setAddress(
            m_VertexArguments[frameIndex]->gpuAddress(),
            1
        );

        // ------------------------------------------------------------
        // SAMPLER 0
        // ------------------------------------------------------------

        m_ArgumentTable->setSamplerState(
            m_Sampler->gpuResourceID(),
            0
        );

        // ------------------------------------------------------------
        // Same table can be used by both stages.
        // ------------------------------------------------------------

        encoder->setArgumentTable(
            m_ArgumentTable,
            MTL::RenderStageVertex
        );

        encoder->setArgumentTable(
            m_ArgumentTable,
            MTL::RenderStageFragment
        );
}
void MTLRenderer::submitCommandBuffer(
                                      CA::MetalDrawable* drawable
                                      )
{
    // Metal-cpp uses wait(), not waitForDrawable().
    m_CommandQueue->wait(drawable);
    
    MTL4::CommandBuffer* commandBuffers[] =
    {
        m_CommandBuffer
    };
    
    m_CommandQueue->commit(
                           commandBuffers,
                           1
                           );
    
    m_CommandQueue->signalDrawable(
                                   drawable
                                   );
    
    drawable->present();
}

void MTLRenderer::draw()
{
    // Drain autoreleased objects created during the frame when scope ends
        ScopedAutoreleasePool autoreleasePool;

        // ------------------------------------------------------------
        // Delta Time Calculation
        // ------------------------------------------------------------
        const Uint64 currentTime = SDL_GetPerformanceCounter();
        
        if (_lastFrameTime == 0)
        {
            _deltaTime = 0.0f;
        }
        else
        {
            _deltaTime = static_cast<float>(
                static_cast<double>(currentTime - _lastFrameTime) /
                static_cast<double>(SDL_GetPerformanceFrequency())
            );
        }
        _lastFrameTime = currentTime;

        // ------------------------------------------------------------
        // Frame Indexing & GPU Wait
        // ------------------------------------------------------------
        frameNumber++;
        const uint32_t frameIndex = frameNumber % kMaxFramesInFlight;

        if (frameNumber > kMaxFramesInFlight)
        {
            if (!waitOnSharedEvent(frameNumber - kMaxFramesInFlight))
            {
                // Abort frame recording to prevent GPU resource corruption
                return;
            }
        }

        // ------------------------------------------------------------
        // Command Buffer & Drawable Acquisition
        // ------------------------------------------------------------
        MTL4::CommandAllocator* allocator = m_CommandAllocators[frameIndex];
        allocator->reset();

        m_CommandBuffer->beginCommandBuffer(allocator);

        CA::MetalDrawable* drawable = m_layer->nextDrawable();
        if (!drawable)
        {
            m_CommandBuffer->endCommandBuffer();
            return;
        }

        // ------------------------------------------------------------
        // Pass Descriptor Setup
        // ------------------------------------------------------------
        MTL4::RenderPassDescriptor* renderPassDescriptor = MTL4::RenderPassDescriptor::alloc()->init();
        MTL::RenderPassColorAttachmentDescriptor* colorAttachment = renderPassDescriptor->colorAttachments()->object(0);

        colorAttachment->setTexture(drawable->texture());
        colorAttachment->setLoadAction(MTL::LoadAction::LoadActionClear);
        colorAttachment->setStoreAction(MTL::StoreAction::StoreActionStore);
        colorAttachment->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));

        // ------------------------------------------------------------
        // Encoding
        // ------------------------------------------------------------
        MTL4::RenderCommandEncoder* encoder = m_CommandBuffer->renderCommandEncoder(renderPassDescriptor);

        encoder->setRenderPipelineState(m_RenderPipelineState);
        setViewport(encoder);
        setRenderPassArguments(encoder, frameIndex);
        encoder->drawPrimitives(MTL::PrimitiveType::PrimitiveTypeTriangle, 0, 6);

        encoder->endEncoding();
        m_CommandBuffer->endCommandBuffer();

        // ------------------------------------------------------------
        // Submission & Event Signaling
        // ------------------------------------------------------------
        submitCommandBuffer(drawable);
        m_CommandQueue->signalEvent(m_SharedEvent, frameNumber);

        renderPassDescriptor->release();
}
}

