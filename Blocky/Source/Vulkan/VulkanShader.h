#pragma once

struct shader
{
    VkShaderModule VertexShaderModule;
    VkShaderModule FragmentShaderModule;
};

inline void ShaderCreate(shader* Shader, VkDevice Device, buffer VertexShader, buffer FragmentShader)
{
    // Vertex
    {
        VkShaderModuleCreateInfo ModuleCreateInfo = {};
        ModuleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ModuleCreateInfo.pNext = nullptr;
        ModuleCreateInfo.pCode = static_cast<u32*>(VertexShader.Data);
        ModuleCreateInfo.codeSize = VertexShader.Size;
        VkAssert(vkCreateShaderModule(Device, &ModuleCreateInfo, nullptr, &Shader->VertexShaderModule));
    }

    // Fragment
    {
        VkShaderModuleCreateInfo ModuleCreateInfo = {};
        ModuleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ModuleCreateInfo.pNext = nullptr;
        ModuleCreateInfo.pCode = static_cast<u32*>(FragmentShader.Data);
        ModuleCreateInfo.codeSize = FragmentShader.Size;
        VkAssert(vkCreateShaderModule(Device, &ModuleCreateInfo, nullptr, &Shader->FragmentShaderModule));
    }
}

inline void ShaderDestroy(shader* Shader, VkDevice Device)
{
    vkDestroyShaderModule(Device, Shader->VertexShaderModule, nullptr);
    vkDestroyShaderModule(Device, Shader->FragmentShaderModule, nullptr);

    Shader->VertexShaderModule = nullptr;
    Shader->FragmentShaderModule = nullptr;
}
