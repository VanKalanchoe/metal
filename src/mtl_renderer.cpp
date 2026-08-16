#include "mtl_renderer.h"
#include "ShaderCompiler.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include <imgui_impl_sdl3.h>
#include <imgui_impl_metal4.h>
#include <imgui_internal.h>// For Docking

namespace NRI
{
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
uint32_t m_ChernoTexture = UINT32_MAX;
uint32_t m_CheckerboardTexture = UINT32_MAX;
uint32_t m_Image1Texture = UINT32_MAX;
SDL_Window* windows;
MTLRenderer::MTLRenderer(SDL_Window& window)
    : _angle(0.0f),
      _lastFrameTime(SDL_GetPerformanceCounter()),
      _deltaTime(0.0f)
{windows = &window;
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
    // Command allocators
    // --------------------------------------------------------

    buildCommandAllocators();


    // --------------------------------------------------------
    // Texture heap
    // --------------------------------------------------------

    buildTextureHeap();


    // --------------------------------------------------------
    // Residency set
    //
    // IMPORTANT:
    // Create ONE residency set and keep using it.
    // --------------------------------------------------------

    buildResidencySet();

    if (!m_ResidencySet)
    {
        SDL_Log(
            "Failed to create residency set"
        );

        return;
    }

    // Make the residency set available to the command queue
    // BEFORE any texture upload happens.
    m_CommandQueue->addResidencySet(
        m_ResidencySet
    );


    // --------------------------------------------------------
    // Sampler
    // --------------------------------------------------------

    buildSampler();

    m_ChernoTexture =
        buildTexture("../../textures/ChernoLogo.png");

    if (m_ChernoTexture == UINT32_MAX)
        return;

    m_CheckerboardTexture =
        buildTexture("../../textures/Checkerboard.png");

    if (m_CheckerboardTexture == UINT32_MAX)
        return;

    m_Image1Texture =
        buildTexture("../../textures/image1.jpg");

    if (m_Image1Texture == UINT32_MAX)
        return;
   

    // At this point:
    //
    // m_ChernoTexture      = 0
    // m_CheckerboardTexture = 1
    // m_Image1Texture      = 2
    //
    // m_Textures[0] = ChernoLogo.png
    // m_Textures[1] = Checkerboard.png
    // m_Textures[2] = image1.jpg


    // --------------------------------------------------------
    // Window size
    // --------------------------------------------------------

    int width = 0;
    int height = 0;

    SDL_GetWindowSizeInPixels(
        &window,
        &width,
        &height
    );

    m_ViewportSize =
        simd_make_uint2(
            static_cast<uint32_t>(width),
            static_cast<uint32_t>(height)
        );

    // --------------------------------------------------------
    // Per-frame buffers
    // --------------------------------------------------------

    constexpr uint32_t quadCount = 4;

    buildQuadInstances(quadCount);
    buildMeshArguments();
    buildIndirectBuffer();


    // --------------------------------------------------------
    // Shaders + pipeline
    // --------------------------------------------------------

    buildShaders();
    
    buildDepthStencilState();


    // --------------------------------------------------------
    // Bindless texture buffer
    //
    // All 3 textures already exist and are uploaded here.
    // --------------------------------------------------------

    buildBindlessTextureBuffer();


    // --------------------------------------------------------
    // Argument table
    // --------------------------------------------------------

    buildArgumentTable();


    // --------------------------------------------------------
    // Frame state
    // --------------------------------------------------------

    frameNumber = 0;
    m_LastSubmittedFrame = 0;

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


    // ========================================================
    // FINAL RESIDENCY
    //
    // Textures were already added above.
    // Now add the resources used during rendering.
    // ========================================================

    if (m_BindlessTextureBuffer)
    {
        m_ResidencySet->addAllocation(
            m_BindlessTextureBuffer
        );
    }


    for (uint32_t frame = 0;
         frame < kMaxFramesInFlight;
         ++frame)
    {
        if (m_MeshArguments[frame])
        {
            m_ResidencySet->addAllocation(
                m_MeshArguments[frame]
            );
        }

        if (m_QuadInstanceBuffers[frame])
        {
            m_ResidencySet->addAllocation(
                m_QuadInstanceBuffers[frame]
            );
        }

        if (m_IndirectBuffers[frame])
        {
            m_ResidencySet->addAllocation(
                m_IndirectBuffers[frame]
            );
        }
    }


    // Commit all resources needed for normal rendering.
    m_ResidencySet->commit();


    // --------------------------------------------------------
    // SDL layer residency
    // --------------------------------------------------------

    if (m_layer->residencySet())
    {
        m_CommandQueue->addResidencySet(
            m_layer->residencySet()
        );
    }


    // --------------------------------------------------------
    // Initial viewport
    // --------------------------------------------------------

    createSceneTexture(
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height)
    );
    
    // ========================================================
    // UPDATE VIEWPORT
    // ========================================================

    m_ViewportSize =
        simd_make_uint2(
            width,
            height
        );

    // ========================================================
    // UPDATE PROJECTION
    // ========================================================

    const float aspect =
        static_cast<float>(width) /
        static_cast<float>(height);

    m_Projection =
        glm::perspective(
            glm::radians(60.0f),
            aspect,
            0.1f,
            100.0f
        );

    m_View =
        glm::translate(
            glm::mat4(1.0f),
            glm::vec3(
                0.0f,
                0.0f,
                -5.0f
            )
        );
    
    // Setup Dear ImGui context
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
      //  io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;       // Enable Multi-Viewport / Platform Windows


        // Setup Dear ImGui style
        ImGui::StyleColorsDark();
        //ImGui::StyleColorsLight();

        // When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
        ImGuiStyle& style = ImGui::GetStyle();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            style.WindowRounding = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }

  
        // Setup Platform/Renderer backends
        ImGui_ImplMetal4_Init(m_Device, m_CommandQueue, kMaxFramesInFlight);
        ImGui_ImplSDL3_InitForMetal(&window);

        // Load Fonts
        // - If fonts are not explicitly loaded, Dear ImGui will select an embedded font: either AddFontDefaultVector() or AddFontDefaultBitmap().
        //   This selection is based on (style.FontSizeBase * style.FontScaleMain * style.FontScaleDpi) reaching a small threshold.
        // - You can load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
        // - If a file cannot be loaded, AddFont functions will return a nullptr. Please handle those errors in your code (e.g. use an assertion, display an error and quit).
        // - Read 'docs/FONTS.md' for more instructions and details.
        // - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use FreeType for higher quality font rendering.
        // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
        //style.FontSizeBase = 20.0f;
        //io.Fonts->AddFontDefaultVector();
        //io.Fonts->AddFontDefaultBitmap();
        //io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf");
        //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf");
        //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf");
        //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf");
        //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf");
        //IM_ASSERT(font != nullptr);
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

    if (m_ShaderLibrary)
    {
        m_ShaderLibrary->release();
        m_ShaderLibrary = nullptr;
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

    
    for (uint32_t frame = 0;
         frame < kMaxFramesInFlight;
         ++frame)
    {
        if (m_MeshArguments[frame])
            m_MeshArguments[frame]->release();

        if (m_QuadInstanceBuffers[frame])
            m_QuadInstanceBuffers[frame]->release();

        if (m_IndirectBuffers[frame])
            m_IndirectBuffers[frame]->release();
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
{
    using NS::StringEncoding::UTF8StringEncoding;

    NS::Error* error = nullptr;

    auto shaderCompiler =
        CreateMetalCompiler();

    std::vector<char> shaderMSL =
        shaderCompiler->compile(
            "../../shaders/QuadMesh.metal"
        );

    if (shaderMSL.empty())
    {
        SDL_Log(
            "QuadMesh.metal produced no MSL"
        );
        return;
    }

    NS::String* source =
        NS::String::string(
            std::string(
                shaderMSL.begin(),
                shaderMSL.end()
            ).c_str(),
            UTF8StringEncoding
        );

    m_ShaderLibrary =
        m_Device->newLibrary(
            source,
            nullptr,
            &error
        );

    if (!m_ShaderLibrary)
    {
        SDL_Log(
            "Failed to create Metal shader library: %s",
            error
                ? error->localizedDescription()->utf8String()
                : "Unknown error"
        );

        if (error)
            error->release();

        return;
    }

    // ========================================================
    // MTL4 COMPILER
    // ========================================================

    MTL4::CompilerDescriptor*
        compilerDescriptor =
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

    // ========================================================
    // MESH FUNCTION
    // ========================================================

    MTL4::LibraryFunctionDescriptor*
        meshFunction =
            MTL4::LibraryFunctionDescriptor::alloc()->init();

    meshFunction->setLibrary(
        m_ShaderLibrary
    );

    meshFunction->setName(
        NS::String::string(
            "meshMain",
            UTF8StringEncoding
        )
    );

    // ========================================================
    // FRAGMENT FUNCTION
    // ========================================================

    MTL4::LibraryFunctionDescriptor*
        fragmentFunction =
            MTL4::LibraryFunctionDescriptor::alloc()->init();

    fragmentFunction->setLibrary(
        m_ShaderLibrary
    );

    fragmentFunction->setName(
        NS::String::string(
            "fragmentMain",
            UTF8StringEncoding
        )
    );

    // ========================================================
    // PIPELINE
    // ========================================================

    MTL4::MeshRenderPipelineDescriptor*
        pipelineDescriptor =
            MTL4::MeshRenderPipelineDescriptor::alloc()->init();

    pipelineDescriptor->setLabel(
        NS::String::string(
            "NRI Metal 4 Mesh Pipeline",
            UTF8StringEncoding
        )
    );

    pipelineDescriptor->setMeshFunctionDescriptor(
        meshFunction
    );

    pipelineDescriptor->setFragmentFunctionDescriptor(
        fragmentFunction
    );

    pipelineDescriptor->setMaxTotalThreadsPerMeshThreadgroup(
        32
    );

    pipelineDescriptor
        ->setMeshThreadgroupSizeIsMultipleOfThreadExecutionWidth(
            true
        );
    
    pipelineDescriptor->setRasterSampleCount(4);


    // ========================================================
    // COLOR ATTACHMENT
    // ========================================================

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

    // ========================================================
    // CREATE PIPELINE
    // ========================================================

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

    compiler->release();

    meshFunction->release();
    fragmentFunction->release();
    pipelineDescriptor->release();

    SDL_Log(
        "MTL4 mesh pipeline created successfully"
    );
}

void MTLRenderer::buildDepthStencilState()
{
    MTL::DepthStencilDescriptor* descriptor =
        MTL::DepthStencilDescriptor::alloc()->init();

    descriptor->setDepthCompareFunction(
        MTL::CompareFunction::CompareFunctionLess
    );

    descriptor->setDepthWriteEnabled(true);

    m_DepthStencilState =
        m_Device->newDepthStencilState(descriptor);

    descriptor->release();

    if (!m_DepthStencilState)
    {
        SDL_Log("Failed to create depth stencil state");
    }
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
uint32_t MTLRenderer::buildTexture(const char* path)
{
    int width = 0;
    int height = 0;
    int channels = 0;

    stbi_uc* pixels =
        stbi_load(
            path,
            &width,
            &height,
            &channels,
            STBI_rgb_alpha
        );

    if (!pixels)
    {
        SDL_Log(
            "Failed to load texture %s: %s",
            path,
            stbi_failure_reason()
        );

        return UINT32_MAX;
    }

    const size_t bytesPerRow =
        static_cast<size_t>(width) * 4;

    const size_t imageSize =
        bytesPerRow *
        static_cast<size_t>(height);

    SDL_Log(
        "Loading %s: %dx%d channels=%d row=%zu image=%zu",
        path,
        width,
        height,
        channels,
        bytesPerRow,
        imageSize
    );

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
    m_TextureHeap->newTexture(descriptor);

    descriptor->release();

    if (!texture)
    {
        stbi_image_free(pixels);

        SDL_Log(
            "Failed to create texture %s",
            path
        );

        return UINT32_MAX;
    }

    MTL::Buffer* staging =
        m_Device->newBuffer(
            imageSize,
            MTL::ResourceStorageModeShared
        );

    if (!staging)
    {
        texture->release();
        stbi_image_free(pixels);

        return UINT32_MAX;
    }

    memcpy(
        staging->contents(),
        pixels,
        imageSize
    );

    stbi_image_free(pixels);

    const uint32_t index =
        static_cast<uint32_t>(
            m_Textures.size()
        );

    m_Textures.push_back(texture);

    m_ResidencySet->addAllocation(
        texture
    );

    m_ResidencySet->addAllocation(
        staging
    );

    m_ResidencySet->commit();

    if (!uploadTexture(index, staging))
    {
        staging->release();
        texture->release();

        m_Textures.pop_back();

        return UINT32_MAX;
    }

    // Safe now because uploadTexture() waited
    // for the committed GPU work to finish.
    staging->release();

    SDL_Log(
        "Texture %u created: %s %dx%d resourceID=%llu",
        index,
        path,
        width,
        height,
        static_cast<unsigned long long>(
            texture->gpuResourceID()._impl
        )
    );

    return index;
}

// ============================================================
// Bindless texture argument buffer
// ============================================================

void MTLRenderer::buildBindlessTextureBuffer()
{
    const size_t textureCount =
        m_Textures.size();

    const size_t entrySize =
        sizeof(uint64_t);

    const size_t bufferSize =
        textureCount * entrySize;

    m_BindlessTextureBuffer =
        m_Device->newBuffer(
            bufferSize,
            MTL::ResourceStorageModeShared
        );

    if (!m_BindlessTextureBuffer)
    {
        SDL_Log(
            "Failed to create bindless texture argument buffer"
        );

        return;
    }

    auto* arguments =
        static_cast<uint64_t*>(
            m_BindlessTextureBuffer->contents()
        );

    for (size_t i = 0;
         i < textureCount;
         ++i)
    {
        arguments[i] =
            m_Textures[i]->gpuResourceID()._impl;
    }

    SDL_Log(
        "Tier-2 texture argument buffer created: %zu textures",
        textureCount
    );
}
// ============================================================
// Texture upload
// ============================================================

bool MTLRenderer::uploadTexture(
    uint32_t textureIndex,
    MTL::Buffer* stagingBuffer
)
{
    if (textureIndex >= m_Textures.size())
        return false;

    if (!stagingBuffer)
        return false;

    MTL::Texture* texture =
        m_Textures[textureIndex];

    const NS::UInteger width =
        texture->width();

    const NS::UInteger height =
        texture->height();

    constexpr size_t bytesPerPixel = 4;

    const size_t bytesPerRow =
        static_cast<size_t>(width) *
        bytesPerPixel;

    SDL_Log(
        "Uploading texture %u: %zux%zu row=%zu image=%zu",
        textureIndex,
        static_cast<size_t>(width),
        static_cast<size_t>(height),
        bytesPerRow,
        bytesPerRow * static_cast<size_t>(height)
    );

    MTL::SharedEvent* event =
        m_Device->newSharedEvent();

    if (!event)
    {
        SDL_Log(
            "Failed to create upload event"
        );

        return false;
    }

    event->setSignaledValue(0);

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
        m_CommandBuffer->endCommandBuffer();
        event->release();

        return false;
    }

    encoder->copyFromBuffer(
        stagingBuffer,

        0,                  // sourceOffset
        bytesPerRow,        // sourceBytesPerRow
        0,                  // sourceBytesPerImage
        MTL::Size::Make(
            width,
            height,
            1
        ),

        texture,

        0,                  // destinationSlice
        0,                  // destinationLevel

        MTL::Origin::Make(
            0,
            0,
            0
        ),

        0
    );

    encoder->endEncoding();

    m_CommandBuffer->endCommandBuffer();

    MTL4::CommandBuffer* commandBuffers[] =
    {
        m_CommandBuffer
    };

    // ========================================================
    // FIRST: submit the actual upload
    // ========================================================

    m_CommandQueue->commit(
        commandBuffers,
        1
    );

    // ========================================================
    // SECOND: signal AFTER the upload has been committed
    // ========================================================

    m_CommandQueue->signalEvent(
        event,
        1
    );

    // ========================================================
    // NOW waiting for 1 actually waits for the upload
    // ========================================================

    const bool completed =
        event->waitUntilSignaledValue(
            1,
            10000
        );

    event->release();

    if (!completed)
    {
        SDL_Log(
            "Upload of texture %u timed out",
            textureIndex
        );

        return false;
    }

    SDL_Log(
        "Upload of texture %u completed",
        textureIndex
    );

    return true;
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

    descriptor->setSupportArgumentBuffers(true);
    
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

void MTLRenderer::buildQuadInstances(uint32_t count)
{
    m_QuadInstanceCount = count;

    m_QuadInstanceBuffers.resize(kMaxFramesInFlight);
    m_QuadInstanceBuffersMapped.resize(kMaxFramesInFlight);

    for (uint32_t frame = 0;
         frame < kMaxFramesInFlight;
         ++frame)
    {
        m_QuadInstanceBuffers[frame] =
            m_Device->newBuffer(
                sizeof(QuadInstance) * count,
                MTL::ResourceStorageModeShared
            );

        if (!m_QuadInstanceBuffers[frame])
        {
            SDL_Log(
                "Failed to create quad instance buffer %u",
                frame
            );

            return;
        }

        m_QuadInstanceBuffersMapped[frame] =
            m_QuadInstanceBuffers[frame]->contents();
    }
}

void MTLRenderer::buildMeshArguments()
{
    m_MeshArguments.resize(kMaxFramesInFlight);
    m_MeshArgumentsMapped.resize(kMaxFramesInFlight);

    for (uint32_t frame = 0;
         frame < kMaxFramesInFlight;
         ++frame)
    {
        m_MeshArguments[frame] =
            m_Device->newBuffer(
                sizeof(MeshArguments),
                MTL::ResourceStorageModeShared
            );

        if (!m_MeshArguments[frame])
        {
            SDL_Log(
                "Failed to create mesh arguments buffer %u",
                frame
            );

            return;
        }

        m_MeshArgumentsMapped[frame] =
            m_MeshArguments[frame]->contents();
    }
}

void MTLRenderer::buildIndirectBuffer()
{
    m_IndirectBuffers.resize(kMaxFramesInFlight);
    m_IndirectBuffersMapped.resize(kMaxFramesInFlight);

    for (uint32_t frame = 0;
         frame < kMaxFramesInFlight;
         ++frame)
    {
        m_IndirectBuffers[frame] =
            m_Device->newBuffer(
                sizeof(DrawMeshTasksIndirectCommand),
                MTL::ResourceStorageModeShared
            );

        if (!m_IndirectBuffers[frame])
        {
            SDL_Log(
                "Failed to create indirect buffer %u",
                frame
            );

            return;
        }

        m_IndirectBuffersMapped[frame] =
            m_IndirectBuffers[frame]->contents();
    }
}
// ============================================================
// Scene texture
// ============================================================

void MTLRenderer::createSceneTexture(
    uint32_t width,
    uint32_t height)
{
    if (width == 0 || height == 0)
        return;

    constexpr uint32_t sampleCount = 4;

    if (!m_Device->supportsTextureSampleCount(sampleCount))
    {
        SDL_Log("4x MSAA is not supported");
        return;
    }

    // ========================================================
    // DESTROY OLD SCENE TEXTURES FIRST
    //
    // Caller must have waited for the GPU before calling this.
    // ========================================================

    destroySceneTexture();

    // ========================================================
    // CREATE RESOLVE TEXTURE
    // ========================================================

    MTL::TextureDescriptor* resolveDescriptor =
        MTL::TextureDescriptor::alloc()->init();

    resolveDescriptor->setTextureType(
        MTL::TextureType::TextureType2D
    );

    resolveDescriptor->setPixelFormat(
        MTL::PixelFormat::PixelFormatBGRA8Unorm_sRGB
    );

    resolveDescriptor->setWidth(width);
    resolveDescriptor->setHeight(height);

    resolveDescriptor->setMipmapLevelCount(1);
    resolveDescriptor->setSampleCount(1);

    resolveDescriptor->setUsage(
        MTL::TextureUsageRenderTarget |
        MTL::TextureUsageShaderRead
    );

    resolveDescriptor->setStorageMode(
        MTL::StorageMode::StorageModePrivate
    );

    MTL::Texture* resolveTexture =
        m_Device->newTexture(resolveDescriptor);

    resolveDescriptor->release();

    if (!resolveTexture)
    {
        SDL_Log(
            "Failed to create resolve scene texture %ux%u",
            width,
            height
        );

        return;
    }

    // ========================================================
    // CREATE MSAA TEXTURE
    // ========================================================

    MTL::TextureDescriptor* msaaDescriptor =
        MTL::TextureDescriptor::alloc()->init();

    msaaDescriptor->setTextureType(
        MTL::TextureType::TextureType2DMultisample
    );

    msaaDescriptor->setPixelFormat(
        MTL::PixelFormat::PixelFormatBGRA8Unorm_sRGB
    );

    msaaDescriptor->setWidth(width);
    msaaDescriptor->setHeight(height);

    msaaDescriptor->setMipmapLevelCount(1);
    msaaDescriptor->setSampleCount(sampleCount);

    msaaDescriptor->setUsage(
        MTL::TextureUsageRenderTarget
    );

    msaaDescriptor->setStorageMode(
        MTL::StorageMode::StorageModePrivate
    );

    MTL::Texture* msaaTexture =
        m_Device->newTexture(msaaDescriptor);

    msaaDescriptor->release();

    if (!msaaTexture)
    {
        SDL_Log(
            "Failed to create MSAA scene texture %ux%u",
            width,
            height
        );

        resolveTexture->release();
        return;
    }
    
    MTL::TextureDescriptor* depthDescriptor =
        MTL::TextureDescriptor::alloc()->init();

    depthDescriptor->setTextureType(
        MTL::TextureType::TextureType2DMultisample
    );

    depthDescriptor->setPixelFormat(
        MTL::PixelFormat::PixelFormatDepth32Float
    );

    depthDescriptor->setWidth(width);
    depthDescriptor->setHeight(height);

    depthDescriptor->setMipmapLevelCount(1);
    depthDescriptor->setSampleCount(sampleCount);

    depthDescriptor->setUsage(
        MTL::TextureUsageRenderTarget
    );

    depthDescriptor->setStorageMode(
        MTL::StorageMode::StorageModePrivate
    );

    MTL::Texture* depthTexture =
        m_Device->newTexture(depthDescriptor);

    depthDescriptor->release();

    if (!depthTexture)
    {
        SDL_Log(
            "Failed to create depth texture %ux%u",
            width,
            height
        );

        msaaTexture->release();
        resolveTexture->release();
        return;
    }

    // ========================================================
    // RESIDENCY
    // ========================================================

    m_ResidencySet->addAllocation(
        msaaTexture
    );

    m_ResidencySet->addAllocation(
        resolveTexture
    );
    
    m_ResidencySet->addAllocation(depthTexture);

    m_ResidencySet->commit();

    // ========================================================
    // PUBLISH
    // ========================================================

    m_SceneMSAATexture = msaaTexture;
    m_SceneTexture = resolveTexture;
    m_SceneDepthTexture = depthTexture;

    SDL_Log(
        "Created scene textures: %ux%u",
        width,
        height
    );
}
void MTLRenderer::destroySceneTexture()
{
    // ========================================================
    // REMOVE FROM RESIDENCY
    // ========================================================

    if (m_ResidencySet)
    {
        if (m_SceneMSAATexture)
        {
            m_ResidencySet->removeAllocation(
                m_SceneMSAATexture
            );
        }

        if (m_SceneTexture)
        {
            m_ResidencySet->removeAllocation(
                m_SceneTexture
            );
        }
        
        if (m_SceneDepthTexture)
        {
            m_ResidencySet->removeAllocation(
                m_SceneDepthTexture
            );
        }

        m_ResidencySet->commit();
    }

    // ========================================================
    // RELEASE
    // ========================================================

    if (m_SceneMSAATexture)
    {
        m_SceneMSAATexture->release();
        m_SceneMSAATexture = nullptr;
    }

    if (m_SceneTexture)
    {
        m_SceneTexture->release();
        m_SceneTexture = nullptr;
    }
    
    if (m_SceneDepthTexture)
    {
        m_SceneDepthTexture->release();
        m_SceneDepthTexture = nullptr;
    }
}

ImTextureID MTLRenderer::getImGuiTextureID() const
{
    return static_cast<ImTextureID>(
        reinterpret_cast<uintptr_t>(m_SceneTexture)
    );
}

// ============================================================
// Viewport
// ============================================================

void MTLRenderer::updateViewportSize(
    uint32_t width,
    uint32_t height)
{
    if (width == 0 || height == 0)
        return;

    // Already rendering at this size.
    if (m_ViewportSize.x == width &&
        m_ViewportSize.y == height)
    {
        return;
    }

    // Resize already waiting for this exact size.
    if (m_ResizePending &&
        m_PendingViewportSize.x == width &&
        m_PendingViewportSize.y == height)
    {
        return;
    }

    m_PendingViewportSize =
        simd_make_uint2(
            width,
            height
        );

    m_ResizePending = true;
}
bool MTLRenderer::waitForGPUIdle()
{
    // Nothing has been submitted yet.
    if (m_LastSubmittedFrame == 0)
        return true;

    constexpr uint64_t timeoutMS = 10000;

    const uint64_t frameToWaitFor =
        m_LastSubmittedFrame;

    const bool completed =
        m_SharedEvent->waitUntilSignaledValue(
            frameToWaitFor,
            timeoutMS
        );

    if (!completed)
    {
        SDL_Log(
            "GPU idle wait timed out. "
            "lastSubmitted=%llu eventValue=%llu",
            static_cast<unsigned long long>(
                frameToWaitFor
            ),
            static_cast<unsigned long long>(
                m_SharedEvent->signaledValue()
            )
        );
    }

    return completed;
}
void MTLRenderer::applyPendingResize()
{
    if (!m_ResizePending)
        return;

    const uint32_t width  =
        m_PendingViewportSize.x;

    const uint32_t height =
        m_PendingViewportSize.y;

    if (width == 0 || height == 0)
        return;

    // ========================================================
    // WAIT FOR GPU
    // ========================================================

    if (!waitForGPUIdle())
    {
        SDL_Log(
            "Resize postponed: GPU still using previous frame"
        );

        return;
    }

    // ========================================================
    // RECREATE SCENE TEXTURES
    // ========================================================
    SDL_Log(
        "BEFORE RESIZE: Metal allocated = %llu MB",
        static_cast<unsigned long long>(
            m_Device->currentAllocatedSize() / (1024 * 1024)
        )
    );
    createSceneTexture(
        width,
        height
    );

    SDL_Log(
        "AFTER RESIZE: Metal allocated = %llu MB",
        static_cast<unsigned long long>(
            m_Device->currentAllocatedSize() / (1024 * 1024)
        )
    );
    // ========================================================
    // DONE
    // ========================================================

    m_ResizePending = false;

    SDL_Log(
        "Scene resized successfully: %ux%u",
        width,
        height
    );
    
    // ========================================================
    // UPDATE VIEWPORT
    // ========================================================

    m_ViewportSize =
        simd_make_uint2(
            width,
            height
        );

    // ========================================================
    // UPDATE PROJECTION
    // ========================================================

    const float aspect =
        static_cast<float>(width) /
        static_cast<float>(height);

    m_Projection =
        glm::perspective(
            glm::radians(60.0f),
            aspect,
            0.1f,
            100.0f
        );

    m_View =
        glm::translate(
            glm::mat4(1.0f),
            glm::vec3(
                0.0f,
                0.0f,
                -5.0f
            )
        );
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

void MTLRenderer::updateFrameData(uint32_t frameIndex)
{
    // ========================================================
    // Mesh arguments
    // ========================================================

    MeshArguments meshArgs{};

    meshArgs.viewProjection =
        m_Projection * m_View;

    meshArgs.instanceReference =
        m_QuadInstanceBuffers[frameIndex]->gpuAddress();

    memcpy(
        m_MeshArgumentsMapped[frameIndex],
        &meshArgs,
        sizeof(meshArgs)
    );


    // ========================================================
    // Indirect command
    // ========================================================

    DrawMeshTasksIndirectCommand indirectCommand{};

    indirectCommand.groupCountX =
        m_QuadInstanceCount;

    indirectCommand.groupCountY = 1;
    indirectCommand.groupCountZ = 1;

    memcpy(
        m_IndirectBuffersMapped[frameIndex],
        &indirectCommand,
        sizeof(indirectCommand)
    );


    // ========================================================
    // Instance data
    // ========================================================

    QuadInstance instances[4]{};

    const glm::vec3 positions[4] =
    {
        {-1.0f, -1.0f, 0.0f},
        { 1.0f, -1.0f, 0.0f},
        {-1.0f,  1.0f, 0.0f},
        { 1.0f,  1.0f, 0.0f}
    };

    const glm::mat4 identity(1.0f);

    for (uint32_t i = 0;
         i < m_QuadInstanceCount;
         ++i)
    {
        instances[i].modelMatrix =
            glm::translate(
                identity,
                positions[i]
            );

        instances[i].color =
            glm::vec4(
                i == 0 ? 1.0f : 0.2f,
                i == 1 ? 1.0f : 0.2f,
                i == 2 ? 1.0f : 0.2f,
                1.0f
            );
    }
    instances[0].textureIndex = m_ChernoTexture;
    instances[1].textureIndex = m_CheckerboardTexture;
    instances[2].textureIndex = m_ChernoTexture;
    instances[3].textureIndex = m_Image1Texture;
 
    memcpy(
        m_QuadInstanceBuffersMapped[frameIndex],
        instances,
        sizeof(QuadInstance) * m_QuadInstanceCount
    );
}
// ============================================================
// Render arguments
// ============================================================

void MTLRenderer::setRenderPassArguments(
    MTL4::RenderCommandEncoder* encoder,
    uint32_t frameIndex
)
{
    m_ArgumentTable->setAddress(
        m_BindlessTextureBuffer->gpuAddress(),
        0
    );

    m_ArgumentTable->setAddress(
        m_MeshArguments[frameIndex]->gpuAddress(),
        1
    );

    m_ArgumentTable->setSamplerState(
        m_Sampler->gpuResourceID(),
        0
    );

    encoder->setArgumentTable(
        m_ArgumentTable,
        MTL::RenderStageMesh
    );

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

    // Submit this frame.
    m_CommandQueue->commit(
        commandBuffers,
        1
    );

    // This event value represents THIS submitted frame.
    m_CommandQueue->signalEvent(
        m_SharedEvent,
        frameNumber
    );

    // This is now officially the last submitted frame.
    m_LastSubmittedFrame =
        frameNumber;

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

    // ========================================================
    // DELTA TIME
    // ========================================================

    const Uint64 currentTime = SDL_GetPerformanceCounter();

    if (_lastFrameTime == 0)
    {
        _deltaTime = 0.0f;
    }
    else
    {
        _deltaTime =
            static_cast<float>(
                static_cast<double>(currentTime - _lastFrameTime) /
                static_cast<double>(SDL_GetPerformanceFrequency())
            );
    }

    _lastFrameTime = currentTime;


    // ========================================================
    // FRAME INDEX
    // ========================================================

    ++frameNumber;

    const uint32_t frameIndex =
        frameNumber % kMaxFramesInFlight;


    // ========================================================
    // WAIT FOR THIS FRAME SLOT
    // ========================================================

    if (frameNumber > kMaxFramesInFlight)
    {
        if (!waitOnSharedEvent(
                frameNumber - kMaxFramesInFlight))
        {
            return;
        }
    }


    // ========================================================
    // WINDOW SIZE
    // ========================================================

    int width = 0;
    int height = 0;

    SDL_GetWindowSizeInPixels(
        windows,
        &width,
        &height
    );

    if (width <= 0 || height <= 0)
        return;


    m_layer->setDrawableSize(
        CGSizeMake(
            static_cast<CGFloat>(width),
            static_cast<CGFloat>(height)
        )
    );


    // ========================================================
    // RESIZE SCENE TEXTURE
    // ========================================================

    updateViewportSize(
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height)
    );

    applyPendingResize();

    if (!m_SceneTexture)
        return;


    // ========================================================
    // UPDATE SCENE DATA
    // ========================================================

    updateFrameData(frameIndex);


    // ========================================================
    // COMMAND ALLOCATOR
    // ========================================================

    MTL4::CommandAllocator* allocator =
        m_CommandAllocators[frameIndex];

    if (!allocator)
        return;

    allocator->reset();


    // ========================================================
    // BEGIN COMMAND BUFFER
    // ========================================================

    m_CommandBuffer->beginCommandBuffer(
        allocator
    );


    // ========================================================
    // GET SWAPCHAIN DRAWABLE
    // ========================================================

    CA::MetalDrawable* drawable =
        m_layer->nextDrawable();

    if (!drawable)
    {
        m_CommandBuffer->endCommandBuffer();
        return;
    }


    // ========================================================
    // ========================================================
    //
    // PASS 1
    //
    // SCENE
    //
    // mesh shader quads
    //        ↓
    // m_SceneTexture
    //
    // ========================================================
    // ========================================================

    MTL4::RenderPassDescriptor*
        scenePass =
            MTL4::RenderPassDescriptor::alloc()->init();

    auto* sceneColor =
        scenePass
            ->colorAttachments()
            ->object(0);

    sceneColor->setTexture(
                           m_SceneMSAATexture
    );
    sceneColor->setResolveTexture(
        m_SceneTexture
    );

    sceneColor->setLoadAction(
        MTL::LoadAction::LoadActionClear
    );

    sceneColor->setStoreAction(
        MTL::StoreAction::StoreActionMultisampleResolve
    );

    sceneColor->setClearColor(
        MTL::ClearColor::Make(
            0.00,
            0.00,
            0.00,
            1.0
        )
    );
    
    auto* sceneDepth =
        scenePass->depthAttachment();

    sceneDepth->setTexture(
        m_SceneDepthTexture
    );

    sceneDepth->setLoadAction(
        MTL::LoadAction::LoadActionClear
    );

    sceneDepth->setStoreAction(
        MTL::StoreAction::StoreActionStore
    );

    sceneDepth->setClearDepth(
        1.0
    );


    MTL4::RenderCommandEncoder*
        sceneEncoder =
            m_CommandBuffer->renderCommandEncoder(
                scenePass
            );

    if (!sceneEncoder)
    {
        scenePass->release();

        m_CommandBuffer->endCommandBuffer();

        return;
    }


    // --------------------------------------------------------
    // Scene pipeline
    // --------------------------------------------------------

    sceneEncoder->setRenderPipelineState(
        m_RenderPipelineState
    );

    sceneEncoder->setDepthStencilState(
        m_DepthStencilState
    );

    // --------------------------------------------------------
    // Scene viewport
    // --------------------------------------------------------

    setViewport(
        sceneEncoder
    );


    // --------------------------------------------------------
    // Scene arguments
    // --------------------------------------------------------

    setRenderPassArguments(
        sceneEncoder,
        frameIndex
    );


    // --------------------------------------------------------
    // Mesh shader indirect draw
    // --------------------------------------------------------

    sceneEncoder->drawMeshThreadgroups(
        m_IndirectBuffers[frameIndex]->gpuAddress(),
        MTL::Size::Make(
            1,
            1,
            1
        ),
        MTL::Size::Make(
            32,
            1,
            1
        )
    );


    // --------------------------------------------------------
    // END SCENE PASS
    // --------------------------------------------------------

    sceneEncoder->endEncoding();

    scenePass->release();


    // ========================================================
    // ========================================================
    //
    // PASS 2
    //
    // IMGUI
    //
    // m_SceneTexture
    //       ↓
    //   ImGui::Image
    //       ↓
    // drawable / swapchain
    //
    // ========================================================
    // ========================================================

    MTL4::RenderPassDescriptor*
        imguiPass =
            MTL4::RenderPassDescriptor::alloc()->init();

    auto* imguiColor =
        imguiPass
            ->colorAttachments()
            ->object(0);

    imguiColor->setTexture(
        drawable->texture()
    );

    imguiColor->setLoadAction(
        MTL::LoadAction::LoadActionClear
    );

    imguiColor->setStoreAction(
        MTL::StoreAction::StoreActionStore
    );

    imguiColor->setClearColor(
        MTL::ClearColor::Make(
            0.08,
            0.08,
            0.08,
            1.0
        )
    );


    // ========================================================
    // IMGUI NEW FRAME
    // ========================================================

    ImGui_ImplMetal4_NewFrame();

    ImGui_ImplSDL3_NewFrame();

    ImGui::NewFrame();

           /*--
           * IMGUI Docking
           * Create a dockspace and dock the viewport and settings window.
           * The central node is named "Viewport", which can be used later with Begin("Viewport")
           * to render the final image.
           -*/

           const ImGuiDockNodeFlags dockFlags = ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_NoDockingInCentralNode;

           // 1. Grab the style and save the default minimum size
           ImGuiStyle& style = ImGui::GetStyle();
           float minWinSizeX = style.WindowMinSize.x;
           float minWinSizeY = style.WindowMinSize.y;

           // 2. Enforce the new minimum size globally for the DockSpace
           style.WindowMinSize.x = 370.0f;

           // 3. Submit the DockSpace (It will inherit the 370x350 constraint)
           ImGuiID dockID = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), dockFlags);

           // 4. Restore the original minimum size for standard floating windows
           style.WindowMinSize.x = minWinSizeX;
           style.WindowMinSize.y = minWinSizeY;

           // Docking layout, must be done only if it doesn't exist
           if (!ImGui::DockBuilderGetNode(dockID)->IsSplitNode() && !ImGui::FindWindowByName("Viewport"))
           {
               ImGui::DockBuilderDockWindow("Viewport", dockID); // Dock "Viewport" to  central node
               ImGui::DockBuilderGetCentralNode(dockID)->LocalFlags |= ImGuiDockNodeFlags_NoTabBar; // Remove "Tab" from the central node
               ImGuiID leftID = ImGui::DockBuilderSplitNode(dockID, ImGuiDir_Left, 0.2f, nullptr, &dockID); // Split the central node
               ImGui::DockBuilderDockWindow("Settings", leftID); // Dock "Settings" to the left node
           }

           // [optional] Show the menu bar
           if (ImGui::BeginMainMenuBar())
           {
               // Adding overlay text on the upper left corner
               ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);

               ImGui::EndMainMenuBar();
           }

           /* END Docking */

           // We define "viewport" with no padding an retrieve the rendering area
           // Using the dock "Viewport", this sets the window to cover the entire central viewport
           ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
           if (ImGui::Begin("Viewport"))
           {
               
               
               ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
               
               
               ImGui::Image(
                            getImGuiTextureID(),
                            viewportPanelSize
                            );
           }

    ImGui::End();ImGui::PopStyleVar();
    // ========================================================
    // BUILD IMGUI DRAW DATA
    // ========================================================

    ImGui::Render();

    ImDrawData* drawData =
        ImGui::GetDrawData();


    // ========================================================
    // CREATE SWAPCHAIN RENDER ENCODER
    // ========================================================

    MTL4::RenderCommandEncoder*
        imguiEncoder =
            m_CommandBuffer->renderCommandEncoder(
                imguiPass
            );

    if (!imguiEncoder)
    {
        SDL_Log("Failed to create ImGui render encoder");

        imguiPass->release();

        m_CommandBuffer->endCommandBuffer();

        return;
    }
    imguiEncoder->barrierAfterQueueStages(
        MTL::StageFragment,
        MTL::StageFragment,
        MTL4::VisibilityOptionDevice
    );
    
    // ========================================================
    // RENDER IMGUI
    // ========================================================

    ImGui_ImplMetal4_RenderDrawData(
        drawData, m_CommandBuffer,imguiEncoder, imguiPass, frameIndex);


    // ========================================================
    // END IMGUI PASS
    // ========================================================

    imguiEncoder->endEncoding();

    imguiPass->release();


    // ========================================================
    // END COMMAND BUFFER
    // ========================================================

    m_CommandBuffer->endCommandBuffer();


    // ========================================================
    // SUBMIT
    // ========================================================

    submitCommandBuffer(
        drawable
    );
}

}
