#include "SlangCompiler.h"

#include <array>
#include <memory>

#include <cassert>
#include <iostream>
#include <filesystem>

namespace NRI
{
    std::unique_ptr<ShaderCompiler> CreateSlangCompiler()
    {
        return std::make_unique<SlangCompiler>();
    }

    SlangCompiler::SlangCompiler()
    {
        slang::createGlobalSession(m_globalSession.writeRef());
    }
    
    std::vector<char> SlangCompiler::compile(const std::string& path)
    {
        if (!std::filesystem::exists(path)) std::cout << "SlangCompiler::compile file not found: " << path << '\n';
        
        Slang::ComPtr<slang::ISession> session;
        
        auto slangTargets{
            std::to_array<slang::TargetDesc>({
                {
                    .format = SLANG_METAL,
                    .profile = m_globalSession->findProfile("metal")
                }
            })
        };
    
        
        std::array<const char*, 4> searchPaths =
        {
            "../NoxCore/src/NoxCore/Renderer",
            "shaders",
            "../shaders",
            "../../shaders"
        };
        
        // Define preprocessor macros to pass into the Slang session
                std::array<slang::PreprocessorMacroDesc, 1> macros =
                {
                    { "__SLANG_TARGET_METAL__", "1" }
                };
        
        /*NOX_CORE_INFO("Slang current working directory: {}", std::filesystem::current_path().string());
        NOX_CORE_INFO("Slang search paths being checked:");
        for (const char* path : searchPaths)
        {
            std::filesystem::path p(path);
            std::filesystem::path absPath = std::filesystem::absolute(p);
            bool exists = std::filesystem::exists(absPath);
    
            NOX_CORE_INFO("  - [{}] {}", exists ? "EXISTS" : "NOT FOUND", absPath.string());
        }*/
        slang::SessionDesc slangSessionDesc
        {
            .targets{slangTargets.data()},
            .targetCount{SlangInt(slangTargets.size())},
            .defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR,
            .searchPaths = searchPaths.data(),
            .searchPathCount = SlangInt(searchPaths.size()),
            .preprocessorMacroCount = static_cast<SlangInt>(macros.size()),
            .preprocessorMacros = macros.data()
        };
        m_globalSession->createSession(slangSessionDesc, session.writeRef());
        
        std::string moduleName = std::filesystem::path(path).stem().string();
        
        Slang::ComPtr<slang::IBlob> diagnostics;
        Slang::ComPtr<slang::IModule> slangModule
        {
            session->loadModuleFromSource(moduleName.c_str(), path.c_str(), nullptr, diagnostics.writeRef())
        };
        
        if (diagnostics)
        {
            std::cout << "Slang compile diagnostics (" << path << "): " << static_cast<const char*>(diagnostics->getBufferPointer()) << '\n';
        }
        
        if (!slangModule)
            throw std::runtime_error("Slang failed to load module: " + path);
        
        Slang::ComPtr<ISlangBlob> spirv;
        Slang::ComPtr<slang::IBlob> targetDiagnostics;
        SlangResult result = slangModule->getTargetCode(0, spirv.writeRef(), targetDiagnostics.writeRef());
        
        if (targetDiagnostics)
        {
            std::cout << "Slang target-code diagnostics (" << path << "): " << static_cast<const char*>(targetDiagnostics->getBufferPointer()) << '\n';
        }

        if (SLANG_FAILED(result) || !spirv)
            throw std::runtime_error("Slang failed to generate target code: " + path);
        
        const char* dataPtr = static_cast<const char*>(spirv->getBufferPointer());
        size_t dataSize = spirv->getBufferSize();
        
        return std::vector<char>(dataPtr, dataPtr + dataSize);
    }
}

