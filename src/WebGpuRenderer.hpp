#pragma once

#include "Camera.hpp"
#include "HexPrismMesh.hpp"
#include "Prism.hpp"

#include <SDL3/SDL.h>

#include <webgpu/webgpu_cpp.h>

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

class WebGpuRenderer {
public:
    explicit WebGpuRenderer(SDL_Window* window);

    bool initialize();

    void render(
        const std::vector<Prism>& prisms,
        const Camera& camera
    );

private:
    struct ObjectUniforms {
        glm::mat4 mvp;
        glm::mat4 model;
        glm::vec4 color;
    };

    SDL_Window* window_ = nullptr;

    wgpu::Instance instance_;
    wgpu::Surface surface_;
    wgpu::Adapter adapter_;
    wgpu::Device device_;
    wgpu::Queue queue_;

    wgpu::TextureFormat surfaceFormat_{};
    wgpu::SurfaceConfiguration surfaceConfig_{};

    wgpu::Texture depthTexture_;

    MeshData prismMesh_;
    wgpu::Buffer prismVertexBuffer_;
    wgpu::Buffer prismIndexBuffer_;

    wgpu::BindGroupLayout objectBindGroupLayout_;
    wgpu::PipelineLayout pipelineLayout_;
    wgpu::RenderPipeline pipeline_;

    std::vector<wgpu::Buffer> objectUniformBuffers_;
    std::vector<wgpu::BindGroup> objectBindGroups_;

    bool createInstance();
    bool createSurface();
    bool requestAdapter();
    bool requestDevice();
    bool configureSurface();
    bool createDepthTexture();
    bool createPrismBuffers();
    bool createPipeline();

    bool resizeIfNeeded();

    void ensureObjectUniformCapacity(std::size_t count);

    wgpu::Texture createDepthTextureObject(
        uint32_t width,
        uint32_t height
    );

    wgpu::ShaderModule createShaderModule();

    template <typename T>
    wgpu::Buffer createBuffer(
        const std::vector<T>& data,
        wgpu::BufferUsage usage
    );

    bool getWindowPixelSize(
        uint32_t& width,
        uint32_t& height
    ) const;

    glm::mat4 createPrismModelMatrix(const Prism& prism) const;
};
