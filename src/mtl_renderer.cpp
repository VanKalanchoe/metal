#include "mtl_renderer.h"
#include "ShaderCompiler.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <cmath>
#include <cstring>
#include <string>
#include <vector>


namespace NRI
{

// ============================================================
// CPU-side mesh arguments
// ============================================================

struct alignas(16) MeshArguments
{
    simd_uint2 viewportSize;

    float angle;

    float padding;
};

static_assert(sizeof(MeshArguments) == 16);


// ============================================================
// Autorelease pool
// ============================================================

struct ScopedAutoreleasePool
{
    NS::AutoreleasePool* pool;

    ScopedAutoreleasePool()
        : pool(NS::AutoreleasePool::alloc()->init())
    {
    }

    ~ScopedAutoreleasePool()
    {
        pool->release();
    }
};


// ============================================================
// Constructor
// ============================================================

MTLRenderer::MTLRenderer(SDL_Window& window)
    : _angle(0.0f),
      _lastFrameTime(SDL_GetPerformanceCounter()),
      _deltaTime(0.0f)
{
    // --------------------------------------------------------
    // SDL Metal view
    // --------------------------------------------------------

    m_metalView =
        SDL_Metal_CreateView(&window);

    if (!m_metalView)
    {
        SDL_Log(
            "Failed to create Metal view: %s",
            SDL_GetError()
        );

        return;
    }


    // --------------------------------------------------------
    // Metal layer
    // --------------------------------------------------------

    m_layer =
        static_cast<CA::MetalLayer*>(
            SDL_Metal_GetLayer(m_metalView)
        );

    if (!m_layer)
    {
        SDL_Log(
            "Failed to get CAMetalLayer"
        );

        return;
    }


    // --------------------------------------------------------
    // Device
    // --------------------------------------------------------

    m_Device =
        MTL::CreateSystemDefaultDevice();

    if (!m_Device)
    {
        SDL_Log(
            "Failed to create Metal device"
        );

        return;
    }


    m_layer->setDevice(m_Device);

    m_layer->setPixelFormat(
        MTL::PixelFormat::PixelFormatBGRA8Unorm_sRGB
    );


    // --------------------------------------------------------
    // Metal 4 command queue
    // --------------------------------------------------------

    m_CommandQueue =
        m_Device->newMTL4CommandQueue();

    if (!m_CommandQueue)
    {
        SDL_Log(
            "Failed to create Metal 4 command queue"
        );

        return;
    }


    // --------------------------------------------------------
    // Metal 4 command buffer
    // --------------------------------------------------------

    m_CommandBuffer =
        m_Device->newCommandBuffer();

    if (!m_CommandBuffer)
    {
        SDL_Log(
            "Failed to create Metal 4 command buffer"
        );

        return;
    }

    // --------------------------------------------------------
    // Resources
    // --------------------------------------------------------

    buildCommandAllocators();

    buildTextureHeap();

    buildTexture();

    buildSampler();

    buildBuffers();


    // --------------------------------------------------------
    // Shader + pipeline
    // --------------------------------------------------------

    buildShaders();


    // --------------------------------------------------------
    // Bindless resources
    // --------------------------------------------------------

    buildBindlessTextureBuffer();

    buildArgumentTable();

    buildResidencySet();


    // --------------------------------------------------------
    // Frame state
    // --------------------------------------------------------

    frameNumber = 0;


    // --------------------------------------------------------
    // Shared event
    // --------------------------------------------------------

    m_SharedEvent =
        m_Device->newSharedEvent();

    if (!m_SharedEvent)
    {
        SDL_Log(
            "Failed to create shared event"
        );

        return;
    }

    m_SharedEvent->setSignaledValue(
        frameNumber
    );


    // --------------------------------------------------------
    // Residency
    // --------------------------------------------------------

    if (m_MeshArguments)
    {
        m_ResidencySet->addAllocation(
            m_MeshArguments
        );
    }


    for (MTL::Texture* texture : m_Textures)
    {
        if (texture)
        {
            m_ResidencySet->addAllocation(
                texture
            );
        }
    }


    if (m_TextureStagingBuffer)
    {
        m_ResidencySet->addAllocation(
            m_TextureStagingBuffer
        );
    }


    if (m_BindlessTextureBuffer)
    {
        m_ResidencySet->addAllocation(
            m_BindlessTextureBuffer
        );
    }


    m_ResidencySet->commit();


    m_CommandQueue->addResidencySet(
        m_ResidencySet
    );


    if (m_layer->residencySet())
    {
        m_CommandQueue->addResidencySet(
            m_layer->residencySet()
        );
    }


    // --------------------------------------------------------
    // Upload texture
    // --------------------------------------------------------

    uploadTexture();


    // --------------------------------------------------------
    // Initial viewport
    // --------------------------------------------------------

    int width = 0;
    int height = 0;

    SDL_GetWindowSizeInPixels(
        &window,
        &width,
        &height
    );


    updateViewportSize(
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height)
    );
}


// ============================================================
// Destructor
// ============================================================

MTLRenderer::~MTLRenderer()
{
    // --------------------------------------------------------
    // Pipeline
    // --------------------------------------------------------

    if (m_RenderPipelineState)
    {
        m_RenderPipelineState->release();
        m_RenderPipelineState = nullptr;
    }


    // --------------------------------------------------------
    // Shader library
    // --------------------------------------------------------

    if (m_MeshLibrary)
    {
        m_MeshLibrary->release();
        m_MeshLibrary = nullptr;
    }

    if (m_FragmentLibrary)
    {
        m_FragmentLibrary->release();
        m_FragmentLibrary = nullptr;
    }

    // --------------------------------------------------------
    // Bindless encoder
    // --------------------------------------------------------

    if (m_BindlessTextureEncoder)
    {
        m_BindlessTextureEncoder->release();
        m_BindlessTextureEncoder = nullptr;
    }


    // --------------------------------------------------------
    // Bindless buffer
    // --------------------------------------------------------

    if (m_BindlessTextureBuffer)
    {
        m_BindlessTextureBuffer->release();
        m_BindlessTextureBuffer = nullptr;
    }


    // --------------------------------------------------------
    // Texture staging buffer
    // --------------------------------------------------------

    if (m_TextureStagingBuffer)
    {
        m_TextureStagingBuffer->release();
        m_TextureStagingBuffer = nullptr;
    }


    // --------------------------------------------------------
    // Textures
    // --------------------------------------------------------

    for (MTL::Texture* texture : m_Textures)
    {
        if (texture)
        {
            texture->release();
        }
    }

    m_Textures.clear();


    // --------------------------------------------------------
    // Texture heap
    // --------------------------------------------------------

    if (m_TextureHeap)
    {
        m_TextureHeap->release();
        m_TextureHeap = nullptr;
    }


    // --------------------------------------------------------
    // Mesh arguments
    // --------------------------------------------------------

    if (m_MeshArguments)
    {
        m_MeshArguments->release();
        m_MeshArguments = nullptr;
    }


    // --------------------------------------------------------
    // Argument table
    // --------------------------------------------------------

    if (m_ArgumentTable)
    {
        m_ArgumentTable->release();
        m_ArgumentTable = nullptr;
    }


    // --------------------------------------------------------
    // Residency
    // --------------------------------------------------------

    if (m_ResidencySet)
    {
        m_ResidencySet->release();
        m_ResidencySet = nullptr;
    }


    // --------------------------------------------------------
    // Command allocators
    // --------------------------------------------------------

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


    // --------------------------------------------------------
    // Shared event
    // --------------------------------------------------------

    if (m_SharedEvent)
    {
        m_SharedEvent->release();
        m_SharedEvent = nullptr;
    }


    // --------------------------------------------------------
    // Command buffer
    // --------------------------------------------------------

    if (m_CommandBuffer)
    {
        m_CommandBuffer->release();
        m_CommandBuffer = nullptr;
    }


    // --------------------------------------------------------
    // Command queue
    // --------------------------------------------------------

    if (m_CommandQueue)
    {
        m_CommandQueue->release();
        m_CommandQueue = nullptr;
    }


    // --------------------------------------------------------
    // Device
    // --------------------------------------------------------

    if (m_Device)
    {
        m_Device->release();
        m_Device = nullptr;
    }


    // --------------------------------------------------------
    // SDL view
    // --------------------------------------------------------

    if (m_metalView)
    {
        SDL_Metal_DestroyView(
            m_metalView
        );

        m_metalView = nullptr;
    }
}


// ============================================================
// Command allocators
// ============================================================

void MTLRenderer::buildCommandAllocators()
{
    for (uint32_t i = 0;
         i < kMaxFramesInFlight;
         ++i)
    {
        m_CommandAllocators[i] =
            m_Device->newCommandAllocator();

        if (!m_CommandAllocators[i])
        {
            SDL_Log(
                "Failed to create command allocator %u",
                i
            );

            return;
        }
    }
}


// ============================================================
// Residency set
// ============================================================

void MTLRenderer::buildResidencySet()
{
    NS::Error* error = nullptr;

    MTL::ResidencySetDescriptor*
        descriptor =
            MTL::ResidencySetDescriptor
                ::alloc()
                ->init();


    m_ResidencySet =
        m_Device->newResidencySet(
            descriptor,
            &error
        );


    descriptor->release();


    if (!m_ResidencySet)
    {
        SDL_Log(
            "Failed to create residency set: %s",
            error
                ? error->localizedDescription()
                    ->utf8String()
                : "Unknown error"
        );


        if (error)
        {
            error->release();
        }

        return;
    }
}


// ============================================================
// Argument table
// ============================================================

void MTLRenderer::buildArgumentTable()
{
    NS::Error* error = nullptr;


    MTL4::ArgumentTableDescriptor*
        descriptor =
            MTL4::ArgumentTableDescriptor
                ::alloc()
                ->init();


    // buffer(0) = bindless texture argument buffer
    // buffer(1) = MeshArguments

    descriptor->setMaxBufferBindCount(2);

    // Textures themselves are NOT directly in the
    // MTL4 argument table.

    descriptor->setMaxTextureBindCount(0);

    // sampler(0)

    descriptor->setMaxSamplerStateBindCount(1);


    m_ArgumentTable =
        m_Device->newArgumentTable(
            descriptor,
            &error
        );


    descriptor->release();


    if (!m_ArgumentTable)
    {
        SDL_Log(
            "Failed to create argument table: %s",
            error
                ? error->localizedDescription()
                    ->utf8String()
                : "Unknown error"
        );


        if (error)
        {
            error->release();
        }

        return;
    }
}


// ============================================================
// Shaders
// ============================================================

void MTLRenderer::buildShaders()
{SDL_Log(
         "Apple7: %s",
         m_Device->supportsFamily(
             MTL::GPUFamilyApple7
         ) ? "YES" : "NO"
     );

     SDL_Log(
         "Mac2: %s",
         m_Device->supportsFamily(
             MTL::GPUFamilyMac2
         ) ? "YES" : "NO"
     );
    using NS::StringEncoding::UTF8StringEncoding;

    NS::Error* error = nullptr;

    auto shaderCompiler =
        CreateSlangCompiler();


    // ============================================================
    // SLANG -> MSL
    // ============================================================

    std::vector<char> meshMSL =
        shaderCompiler->compile(
            "../../shaders/TriangleMesh.slang"
        );

    if (meshMSL.empty())
    {
        SDL_Log("TriangleMesh.slang produced no MSL");
        return;
    }


    std::vector<char> fragmentMSL =
        shaderCompiler->compile(
            "../../shaders/TriangleFragment.slang"
        );

    if (fragmentMSL.empty())
    {
        SDL_Log("TriangleFragment.slang produced no MSL");
        return;
    }


    // ============================================================
    // METAL 4 COMPILER
    // ============================================================

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
            "Failed to create MTL4 compiler: %s",
            error
                ? error->localizedDescription()->utf8String()
                : "Unknown error"
        );

        if (error)
            error->release();

        return;
    }


    // ============================================================
    // MESH LIBRARY
    // ============================================================

    MTL4::LibraryDescriptor* meshLibraryDescriptor =
        MTL4::LibraryDescriptor::alloc()->init();

    meshLibraryDescriptor->setSource(
        NS::String::string(
            std::string(
                meshMSL.begin(),
                meshMSL.end()
            ).c_str(),
            UTF8StringEncoding
        )
    );

    m_MeshLibrary =
        compiler->newLibrary(
            meshLibraryDescriptor,
            
            &error
        );

    meshLibraryDescriptor->release();

    if (!m_MeshLibrary)
    {
        SDL_Log(
            "Failed to compile MTL4 mesh library: %s",
            error
                ? error->localizedDescription()->utf8String()
                : "Unknown error"
        );

        if (error)
            error->release();

        compiler->release();
        return;
    }


    // ============================================================
    // FRAGMENT LIBRARY
    // ============================================================

    MTL4::LibraryDescriptor* fragmentLibraryDescriptor =
        MTL4::LibraryDescriptor::alloc()->init();

    fragmentLibraryDescriptor->setSource(
        NS::String::string(
            std::string(
                fragmentMSL.begin(),
                fragmentMSL.end()
            ).c_str(),
            UTF8StringEncoding
        )
    );

    m_FragmentLibrary =
        compiler->newLibrary(
            fragmentLibraryDescriptor,
         
            &error
        );

    fragmentLibraryDescriptor->release();

    if (!m_FragmentLibrary)
    {
        SDL_Log(
            "Failed to compile MTL4 fragment library: %s",
            error
                ? error->localizedDescription()->utf8String()
                : "Unknown error"
        );

        if (error)
            error->release();

        compiler->release();
        return;
    }


    // ============================================================
    // MESH FUNCTION
    // ============================================================

    MTL4::LibraryFunctionDescriptor* meshFunction =
        MTL4::LibraryFunctionDescriptor::alloc()->init();

    meshFunction->setLibrary(
        m_MeshLibrary
    );

    meshFunction->setName(
        NS::String::string(
            "meshMain",
            UTF8StringEncoding
        )
    );


    // ============================================================
    // FRAGMENT FUNCTION
    // ============================================================

    MTL4::LibraryFunctionDescriptor* fragmentFunction =
        MTL4::LibraryFunctionDescriptor::alloc()->init();

    fragmentFunction->setLibrary(
        m_FragmentLibrary
    );

    fragmentFunction->setName(
        NS::String::string(
            "fragmentMain",
            UTF8StringEncoding
        )
    );


    // ============================================================
    // MESH PIPELINE
    // ============================================================

    MTL4::MeshRenderPipelineDescriptor* pipelineDescriptor =
        MTL4::MeshRenderPipelineDescriptor::alloc()->init();

    pipelineDescriptor->setLabel(
        NS::String::string(
            "NRI Mesh Pipeline",
            UTF8StringEncoding
        )
    );


    pipelineDescriptor->setMeshFunctionDescriptor(
        meshFunction
    );

    pipelineDescriptor->setFragmentFunctionDescriptor(
        fragmentFunction
    );


    // Shader:
    //
    // [numthreads(32, 1, 1)]
    //

    pipelineDescriptor->setMaxTotalThreadsPerMeshThreadgroup(
        32
    );

    pipelineDescriptor->setMeshThreadgroupSizeIsMultipleOfThreadExecutionWidth(
        true
    );


    // ============================================================
    // COLOR
    // ============================================================

    auto* colorAttachment =
        pipelineDescriptor
            ->colorAttachments()
            ->object(0);

    colorAttachment->setPixelFormat(
        MTL::PixelFormat::PixelFormatBGRA8Unorm_sRGB
    );


    // ============================================================
    // BLENDING
    // ============================================================

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


    // ============================================================
    // PIPELINE
    // ============================================================

    m_RenderPipelineState =
        compiler->newRenderPipelineState(
            pipelineDescriptor,
            nullptr,
            &error
        );


    if (!m_RenderPipelineState)
    {
        SDL_Log(
            "MTL4 mesh pipeline failed: %s",
            error
                ? error->localizedDescription()->utf8String()
                : "Unknown error"
        );

        if (error)
            error->release();

        compiler->release();

        meshFunction->release();
        fragmentFunction->release();
        pipelineDescriptor->release();

        return;
    }


    // ============================================================
    // CLEANUP
    // ============================================================

    compiler->release();

    meshFunction->release();
    fragmentFunction->release();
    pipelineDescriptor->release();


    SDL_Log(
        "MTL4 mesh pipeline created successfully"
    );
}
// ============================================================
// Texture heap
// ============================================================

void MTLRenderer::buildTextureHeap()
{
    MTL::HeapDescriptor*
        descriptor =
            MTL::HeapDescriptor
                ::alloc()
                ->init();


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
        m_Device->newHeap(
            descriptor
        );


    descriptor->release();


    if (!m_TextureHeap)
    {
        SDL_Log(
            "Failed to create texture heap"
        );

        return;
    }


    SDL_Log(
        "Metal texture heap created successfully"
    );
}


// ============================================================
// Texture
// ============================================================

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


    // --------------------------------------------------------
    // Staging buffer
    // --------------------------------------------------------

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


    // --------------------------------------------------------
    // Private texture
    // --------------------------------------------------------

    MTL::TextureDescriptor*
        descriptor =
            MTL::TextureDescriptor
                ::alloc()
                ->init();


    descriptor->setTextureType(
        MTL::TextureType::TextureType2D
    );


    descriptor->setPixelFormat(
        MTL::PixelFormat::PixelFormatRGBA8Unorm_sRGB
    );


    descriptor->setWidth(
        width
    );


    descriptor->setHeight(
        height
    );


    descriptor->setMipmapLevelCount(
        1
    );


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


    m_Textures.push_back(
        texture
    );
}


// ============================================================
// Bindless texture argument buffer
// ============================================================

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
    m_FragmentLibrary->newFunction(
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
        fragmentFunction->newArgumentEncoder(
            0
        );


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
         i <
         static_cast<NS::UInteger>(
             m_Textures.size()
         );
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


// ============================================================
// Texture upload
// ============================================================

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


    m_TextureStagingBuffer->release();

    m_TextureStagingBuffer = nullptr;
}


// ============================================================
// Sampler
// ============================================================

void MTLRenderer::buildSampler()
{
    MTL::SamplerDescriptor*
        descriptor =
            MTL::SamplerDescriptor
                ::alloc()
                ->init();


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
        m_Device->newSamplerState(
            descriptor
        );


    descriptor->release();


    if (!m_Sampler)
    {
        SDL_Log(
            "Failed to create sampler"
        );
    }
}


// ============================================================
// Buffers
// ============================================================

void MTLRenderer::buildBuffers()
{
    m_MeshArguments =
        m_Device->newBuffer(
            sizeof(MeshArguments),
            MTL::ResourceStorageModeShared
        );


    if (!m_MeshArguments)
    {
        SDL_Log(
            "Failed to create mesh arguments buffer"
        );

        return;
    }


    m_MeshArguments->setLabel(
        NS::String::string(
            "MeshArguments",
            NS::StringEncoding::UTF8StringEncoding
        )
    );


    auto* args =
        reinterpret_cast<MeshArguments*>(
            m_MeshArguments->contents()
        );


    args->viewportSize =
        m_ViewportSize;


    args->angle =
        0.0f;


    args->padding =
        0.0f;
}


// ============================================================
// Viewport
// ============================================================

void MTLRenderer::updateViewportSize(
    uint32_t width,
    uint32_t height
)
{
    m_ViewportSize =
        simd_make_uint2(
            width,
            height
        );


    if (!m_MeshArguments)
        return;


    auto* args =
        reinterpret_cast<MeshArguments*>(
            m_MeshArguments->contents()
        );


    args->viewportSize =
        m_ViewportSize;
}


// ============================================================
// Shared event
// ============================================================

bool MTLRenderer::waitOnSharedEvent(
    uint64_t earlierFrameNumber
)
{
    constexpr uint64_t timeoutMS = 1000;


    const bool signaled =
        m_SharedEvent
            ->waitUntilSignaledValue(
                earlierFrameNumber,
                timeoutMS
            );


    if (!signaled)
    {
        SDL_Log(
            "GPU timeout waiting for frame %llu after %llu ms",
            static_cast<unsigned long long>(
                earlierFrameNumber
            ),
            static_cast<unsigned long long>(
                timeoutMS
            )
        );
    }


    return signaled;
}


// ============================================================
// Viewport
// ============================================================

void MTLRenderer::setViewport(
    MTL4::RenderCommandEncoder* encoder
)
{
    MTL::Viewport viewport;


    viewport.originX = 0.0;

    viewport.originY = 0.0;


    viewport.width =
        static_cast<double>(
            m_ViewportSize.x
        );


    viewport.height =
        static_cast<double>(
            m_ViewportSize.y
        );


    viewport.znear = 0.0;

    viewport.zfar = 1.0;


    encoder->setViewport(
        viewport
    );
}


// ============================================================
// Render arguments
// ============================================================

void MTLRenderer::setRenderPassArguments(
    MTL4::RenderCommandEncoder* encoder
)
{
    // ========================================================
    // UPDATE ANGLE
    // ========================================================

    constexpr float PI =
        3.14159265358979323846f;

    constexpr float rotationSpeed =
        60.0f * (PI / 180.0f);

    _angle +=
        rotationSpeed * _deltaTime;

    if (_angle >= 2.0f * PI)
    {
        _angle -= 2.0f * PI;
    }


    // ========================================================
    // UPDATE MESH ARGUMENTS
    // ========================================================

    auto* args =
        reinterpret_cast<MeshArguments*>(
            m_MeshArguments->contents()
        );

    args->viewportSize =
        m_ViewportSize;

    args->angle =
        _angle;


    // ========================================================
    // BUFFER 0
    //
    // Exactly the same bindless buffer you already had working.
    // ========================================================

    m_ArgumentTable->setAddress(
        m_BindlessTextureBuffer->gpuAddress(),
        0
    );


    // ========================================================
    // BUFFER 1
    // ========================================================

    m_ArgumentTable->setAddress(
        m_MeshArguments->gpuAddress(),
        1
    );


    // ========================================================
    // SAMPLER 0
    // ========================================================

    m_ArgumentTable->setSamplerState(
        m_Sampler->gpuResourceID(),
        0
    );


    // ========================================================
    // MESH
    // ========================================================

    encoder->setArgumentTable(
        m_ArgumentTable,
        MTL::RenderStageMesh
    );


    // ========================================================
    // FRAGMENT
    // ========================================================

    encoder->setArgumentTable(
        m_ArgumentTable,
        MTL::RenderStageFragment
    );
}


// ============================================================
// Submit
// ============================================================

void MTLRenderer::submitCommandBuffer(
    CA::MetalDrawable* drawable
)
{
    m_CommandQueue->wait(
        drawable
    );


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


// ============================================================
// DRAW
// ============================================================

void MTLRenderer::draw()
{
    ScopedAutoreleasePool autoreleasePool;


    // --------------------------------------------------------
    // Delta time
    // --------------------------------------------------------

    const Uint64 currentTime =
        SDL_GetPerformanceCounter();


    if (_lastFrameTime == 0)
    {
        _deltaTime = 0.0f;
    }
    else
    {
        _deltaTime =
            static_cast<float>(
                static_cast<double>(
                    currentTime -
                    _lastFrameTime
                ) /
                static_cast<double>(
                    SDL_GetPerformanceFrequency()
                )
            );
    }


    _lastFrameTime =
        currentTime;


    // --------------------------------------------------------
    // Frame index
    // --------------------------------------------------------

    frameNumber++;


    const uint32_t frameIndex =
        frameNumber %
        kMaxFramesInFlight;


    // --------------------------------------------------------
    // Wait for old GPU work
    // --------------------------------------------------------

    if (frameNumber > kMaxFramesInFlight)
    {
        if (!waitOnSharedEvent(
                frameNumber -
                kMaxFramesInFlight
            ))
        {
            return;
        }
    }


    // --------------------------------------------------------
    // Command allocator
    // --------------------------------------------------------

    MTL4::CommandAllocator*
        allocator =
            m_CommandAllocators[
                frameIndex
            ];


    allocator->reset();


    // --------------------------------------------------------
    // Begin command buffer
    // --------------------------------------------------------

    m_CommandBuffer->beginCommandBuffer(
        allocator
    );


    // --------------------------------------------------------
    // Drawable
    // --------------------------------------------------------

    CA::MetalDrawable* drawable =
        m_layer->nextDrawable();


    if (!drawable)
    {
        m_CommandBuffer->endCommandBuffer();

        return;
    }


    // --------------------------------------------------------
    // Render pass
    // --------------------------------------------------------

    MTL4::RenderPassDescriptor*
        renderPassDescriptor =
            MTL4::RenderPassDescriptor
                ::alloc()
                ->init();


    MTL::RenderPassColorAttachmentDescriptor*
        colorAttachment =
            renderPassDescriptor
                ->colorAttachments()
                ->object(0);


    colorAttachment->setTexture(
        drawable->texture()
    );


    colorAttachment->setLoadAction(
        MTL::LoadAction::LoadActionClear
    );


    colorAttachment->setStoreAction(
        MTL::StoreAction::StoreActionStore
    );


    colorAttachment->setClearColor(
        MTL::ClearColor::Make(
            0.0,
            0.0,
            0.0,
            1.0
        )
    );


    // --------------------------------------------------------
    // Encoder
    // --------------------------------------------------------

    MTL4::RenderCommandEncoder* encoder =
        m_CommandBuffer->renderCommandEncoder(
            renderPassDescriptor
        );


    if (!encoder)
    {
        SDL_Log(
            "Failed to create Metal 4 render encoder"
        );


        m_CommandBuffer->endCommandBuffer();

        renderPassDescriptor->release();

        return;
    }


    // --------------------------------------------------------
    // Pipeline
    // --------------------------------------------------------

    encoder->setRenderPipelineState(
        m_RenderPipelineState
    );


    // --------------------------------------------------------
    // Viewport
    // --------------------------------------------------------

    setViewport(
        encoder
    );


    // --------------------------------------------------------
    // Arguments
    // --------------------------------------------------------

    setRenderPassArguments(
        encoder
    );


    // --------------------------------------------------------
    // MESH DRAW
    // --------------------------------------------------------
    //
    // One mesh threadgroup.
    //
    // Mesh shader:
    //
    //     [numthreads(1,1,1)]
    //
    // creates:
    //
    //     4 vertices
    //     2 triangles
    //
    // --------------------------------------------------------

    encoder->drawMeshThreadgroups(
        MTL::Size::Make(
            1,
            1,
            1
        ),

        // No object shader.

        MTL::Size::Make(
            1,
            1,
            1
        ),

        // Mesh threadgroup.

        MTL::Size::Make(
            32,
            1,
            1
        )
    );


    // --------------------------------------------------------
    // End encoding
    // --------------------------------------------------------

    encoder->endEncoding();


    m_CommandBuffer->endCommandBuffer();


    // --------------------------------------------------------
    // Submit
    // --------------------------------------------------------

    submitCommandBuffer(
        drawable
    );


    // --------------------------------------------------------
    // Signal frame completion
    // --------------------------------------------------------

    m_CommandQueue->signalEvent(
        m_SharedEvent,
        frameNumber
    );


    renderPassDescriptor->release();
}

}
