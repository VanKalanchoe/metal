#pragma once

#include <slang.h>
#include <slang-com-ptr.h>

#include "ShaderCompiler.h"

namespace NRI
{
    class SlangCompiler : public ShaderCompiler
    {
    public:
        SlangCompiler();
        ~SlangCompiler() override = default;
        
        std::vector<char> compile(const std::string& path) override;
        
    private:
        Slang::ComPtr<slang::IGlobalSession> m_globalSession;
    };
}

