#pragma once

#include "Camera.hpp"
#include "HexPrismMesh.hpp"
#include "Prism.hpp"

#include <SDL3/SDL.h>

#include <webgpu/webgpu_cpp.h>

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

class WebGpuRenderer {
public:
    explicit WebGpuRenderer(SDL_Window* window);

    bool initialize();

    void render(
        const std::vector<Prism>& prisms,
        const Camera& camera,
        uint64_t prismRevision = 0
    );

private:
    struct FrameUniforms {
        glm::mat4 viewProjection;
        glm::vec4 cameraRightAndAspect;
        glm::vec4 cameraUpAndTanHalfFov;
        glm::vec4 cameraForwardAndTime;
    };

    struct PrismInstanceData {
        glm::vec4 positionAndCos;
        glm::vec4 colorAndSin;
        float alpha;
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

    wgpu::Buffer frameUniformBuffer_;
    wgpu::BindGroup frameBindGroup_;

    wgpu::Buffer prismInstanceBuffer_;
    std::size_t prismInstanceCapacity_ = 0;
    uint64_t uploadedPrismRevision_ = 0;
    std::size_t uploadedPrismCount_ = 0;
    std::size_t uploadedOpaquePrismCount_ = 0;
    std::vector<PrismInstanceData> prismInstanceData_;

    wgpu::BindGroupLayout frameBindGroupLayout_;
    wgpu::PipelineLayout pipelineLayout_;
    wgpu::RenderPipeline skyPipeline_;
    wgpu::RenderPipeline pipeline_;
    wgpu::RenderPipeline transparentPipeline_;

    bool createInstance();
    bool createSurface();
    bool requestAdapter();
    bool requestDevice();
    bool configureSurface();
    bool createDepthTexture();
    bool createPrismBuffers();
    bool createPipeline();

    bool resizeIfNeeded();

    void ensurePrismInstanceBufferCapacity(std::size_t count);
    void updatePrismInstanceBuffer(
        const std::vector<Prism>& prisms,
        uint64_t prismRevision
    );

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

};
