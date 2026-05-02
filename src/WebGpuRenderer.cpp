#include "WebGpuRenderer.hpp"

#include <SDL3/SDL.h>

#include <glm/gtc/matrix_transform.hpp>

#include <windows.h>

#include <array>
#include <iostream>

static std::ostream& operator<<(std::ostream& os, wgpu::StringView view) {
    if (view.data == nullptr) {
        return os;
    }

    if (view.length == WGPU_STRLEN) {
        os << view.data;
    } else {
        os.write(view.data, static_cast<std::streamsize>(view.length));
    }

    return os;
}

WebGpuRenderer::WebGpuRenderer(SDL_Window* window)
    : window_(window) {}

bool WebGpuRenderer::initialize() {
    if (!createInstance()) {
        return false;
    }

    if (!createSurface()) {
        return false;
    }

    if (!requestAdapter()) {
        return false;
    }

    if (!requestDevice()) {
        return false;
    }

    if (!configureSurface()) {
        return false;
    }

    if (!createDepthTexture()) {
        return false;
    }

    if (!createPrismBuffers()) {
        return false;
    }

    if (!createPipeline()) {
        return false;
    }

    return true;
}

bool WebGpuRenderer::createInstance() {
    wgpu::InstanceDescriptor instanceDesc{};
    instance_ = wgpu::CreateInstance(&instanceDesc);

    if (!instance_) {
        std::cerr << "Failed to create WebGPU instance.\n";
        return false;
    }

    return true;
}

bool WebGpuRenderer::createSurface() {
    SDL_PropertiesID props = SDL_GetWindowProperties(window_);

    if (props == 0) {
        std::cerr << "SDL_GetWindowProperties failed: "
                  << SDL_GetError()
                  << "\n";
        return false;
    }

    HWND hwnd = static_cast<HWND>(
        SDL_GetPointerProperty(
            props,
            SDL_PROP_WINDOW_WIN32_HWND_POINTER,
            nullptr
        )
    );

    HINSTANCE hinstance = static_cast<HINSTANCE>(
        SDL_GetPointerProperty(
            props,
            SDL_PROP_WINDOW_WIN32_INSTANCE_POINTER,
            nullptr
        )
    );

    if (hwnd == nullptr) {
        std::cerr << "SDL did not provide a Win32 HWND.\n";
        return false;
    }

    if (hinstance == nullptr) {
        hinstance = GetModuleHandle(nullptr);
    }

    wgpu::SurfaceSourceWindowsHWND hwndDesc{};
    hwndDesc.hinstance = hinstance;
    hwndDesc.hwnd = hwnd;

    wgpu::SurfaceDescriptor surfaceDesc{};
    surfaceDesc.nextInChain = &hwndDesc;

    surface_ = instance_.CreateSurface(&surfaceDesc);

    if (!surface_) {
        std::cerr << "Failed to create WebGPU surface.\n";
        return false;
    }

    return true;
}

bool WebGpuRenderer::requestAdapter() {
    wgpu::RequestAdapterOptions adapterOptions{};
    adapterOptions.compatibleSurface = surface_;
    adapterOptions.backendType = wgpu::BackendType::D3D12;

    bool done = false;

    instance_.RequestAdapter(
        &adapterOptions,
        wgpu::CallbackMode::AllowProcessEvents,
        [this, &done](
            wgpu::RequestAdapterStatus status,
            wgpu::Adapter result,
            wgpu::StringView message
        ) {
            done = true;

            if (status == wgpu::RequestAdapterStatus::Success) {
                adapter_ = result;
            } else {
                std::cerr << "RequestAdapter failed: " << message << "\n";
            }
        }
    );

    while (!done) {
        SDL_PumpEvents();
        instance_.ProcessEvents();
    }

    if (!adapter_) {
        std::cerr << "No WebGPU adapter available.\n";
        return false;
    }

    return true;
}

bool WebGpuRenderer::requestDevice() {
    wgpu::DeviceDescriptor deviceDesc{};

    bool done = false;

    adapter_.RequestDevice(
        &deviceDesc,
        wgpu::CallbackMode::AllowProcessEvents,
        [this, &done](
            wgpu::RequestDeviceStatus status,
            wgpu::Device result,
            wgpu::StringView message
        ) {
            done = true;

            if (status == wgpu::RequestDeviceStatus::Success) {
                device_ = result;
            } else {
                std::cerr << "RequestDevice failed: " << message << "\n";
            }
        }
    );

    while (!done) {
        SDL_PumpEvents();
        instance_.ProcessEvents();
    }

    if (!device_) {
        std::cerr << "No WebGPU device available.\n";
        return false;
    }

    queue_ = device_.GetQueue();

    return true;
}

bool WebGpuRenderer::configureSurface() {
    wgpu::SurfaceCapabilities caps{};
    surface_.GetCapabilities(adapter_, &caps);

    if (caps.formatCount == 0) {
        std::cerr << "Surface has no supported formats.\n";
        return false;
    }

    surfaceFormat_ = caps.formats[0];

    uint32_t width = 0;
    uint32_t height = 0;

    if (!getWindowPixelSize(width, height)) {
        width = 1280;
        height = 720;
    }

    surfaceConfig_ = {};
    surfaceConfig_.device = device_;
    surfaceConfig_.format = surfaceFormat_;
    surfaceConfig_.usage = wgpu::TextureUsage::RenderAttachment;
    surfaceConfig_.width = width;
    surfaceConfig_.height = height;
    surfaceConfig_.presentMode = wgpu::PresentMode::Fifo;
    surfaceConfig_.alphaMode = wgpu::CompositeAlphaMode::Opaque;

    surface_.Configure(&surfaceConfig_);

    return true;
}

bool WebGpuRenderer::createDepthTexture() {
    depthTexture_ =
        createDepthTextureObject(
            surfaceConfig_.width,
            surfaceConfig_.height
        );

    return static_cast<bool>(depthTexture_);
}

wgpu::Texture WebGpuRenderer::createDepthTextureObject(
    uint32_t width,
    uint32_t height
) {
    wgpu::TextureDescriptor desc{};
    desc.size = { width, height, 1 };
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;
    desc.dimension = wgpu::TextureDimension::e2D;
    desc.format = wgpu::TextureFormat::Depth24Plus;
    desc.usage = wgpu::TextureUsage::RenderAttachment;

    return device_.CreateTexture(&desc);
}

bool WebGpuRenderer::createPrismBuffers() {
    prismMesh_ = createHexPrismMesh();

    prismVertexBuffer_ = createBuffer(
        prismMesh_.vertices,
        wgpu::BufferUsage::Vertex
    );

    prismIndexBuffer_ = createBuffer(
        prismMesh_.indices,
        wgpu::BufferUsage::Index
    );

    return prismVertexBuffer_ && prismIndexBuffer_;
}

wgpu::ShaderModule WebGpuRenderer::createShaderModule() {
    const char* wgsl = R"(
struct ObjectUniforms {
    mvp : mat4x4f,
    model : mat4x4f,
    color : vec4f,
};

@group(0) @binding(0)
var<uniform> object : ObjectUniforms;

struct VertexIn {
    @location(0) position : vec3f,
    @location(1) normal : vec3f,
};

struct VertexOut {
    @builtin(position) position : vec4f,
    @location(0) normal : vec3f,
};

@vertex
fn vsMain(input : VertexIn) -> VertexOut {
    var out : VertexOut;

    out.position = object.mvp * vec4f(input.position, 1.0);

    // Correct as long as model has no non-uniform scale.
    out.normal = normalize((object.model * vec4f(input.normal, 0.0)).xyz);

    return out;
}

@fragment
fn fsMain(input : VertexOut) -> @location(0) vec4f {
    let lightDir = normalize(vec3f(0.4, 0.7, 1.0));
    let n = normalize(input.normal);

    let diffuse = max(dot(n, lightDir), 0.0);
    let ambient = 0.22;

    let lighting = ambient + diffuse * 0.78;
    let finalColor = object.color.rgb * lighting;

    return vec4f(finalColor, 1.0);
}
)";

    wgpu::ShaderSourceWGSL source{};
    source.code = wgsl;

    wgpu::ShaderModuleDescriptor desc{};
    desc.nextInChain = &source;

    return device_.CreateShaderModule(&desc);
}

bool WebGpuRenderer::createPipeline() {
    wgpu::BindGroupLayoutEntry bindEntry{};
    bindEntry.binding = 0;
    bindEntry.visibility =
        wgpu::ShaderStage::Vertex |
        wgpu::ShaderStage::Fragment;
    bindEntry.buffer.type = wgpu::BufferBindingType::Uniform;
    bindEntry.buffer.minBindingSize = sizeof(ObjectUniforms);

    wgpu::BindGroupLayoutDescriptor bindLayoutDesc{};
    bindLayoutDesc.entryCount = 1;
    bindLayoutDesc.entries = &bindEntry;

    objectBindGroupLayout_ =
        device_.CreateBindGroupLayout(&bindLayoutDesc);

    wgpu::PipelineLayoutDescriptor pipelineLayoutDesc{};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &objectBindGroupLayout_;

    pipelineLayout_ =
        device_.CreatePipelineLayout(&pipelineLayoutDesc);

    wgpu::ShaderModule shader = createShaderModule();

    std::array<wgpu::VertexAttribute, 2> attributes{};

    attributes[0].shaderLocation = 0;
    attributes[0].offset = offsetof(PrismVertex, px);
    attributes[0].format = wgpu::VertexFormat::Float32x3;

    attributes[1].shaderLocation = 1;
    attributes[1].offset = offsetof(PrismVertex, nx);
    attributes[1].format = wgpu::VertexFormat::Float32x3;

    wgpu::VertexBufferLayout vertexLayout{};
    vertexLayout.arrayStride = sizeof(PrismVertex);
    vertexLayout.stepMode = wgpu::VertexStepMode::Vertex;
    vertexLayout.attributeCount =
        static_cast<uint32_t>(attributes.size());
    vertexLayout.attributes = attributes.data();

    wgpu::ColorTargetState colorTarget{};
    colorTarget.format = surfaceFormat_;
    colorTarget.writeMask = wgpu::ColorWriteMask::All;

    wgpu::FragmentState fragment{};
    fragment.module = shader;
    fragment.entryPoint = "fsMain";
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;

    wgpu::DepthStencilState depthStencil{};
    depthStencil.format = wgpu::TextureFormat::Depth24Plus;
    depthStencil.depthWriteEnabled = true;
    depthStencil.depthCompare = wgpu::CompareFunction::Less;

    wgpu::RenderPipelineDescriptor pipelineDesc{};
    pipelineDesc.layout = pipelineLayout_;

    pipelineDesc.vertex.module = shader;
    pipelineDesc.vertex.entryPoint = "vsMain";
    pipelineDesc.vertex.bufferCount = 1;
    pipelineDesc.vertex.buffers = &vertexLayout;

    pipelineDesc.fragment = &fragment;
    pipelineDesc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    pipelineDesc.primitive.cullMode = wgpu::CullMode::None;
    pipelineDesc.depthStencil = &depthStencil;
    pipelineDesc.multisample.count = 1;

    pipeline_ = device_.CreateRenderPipeline(&pipelineDesc);

    if (!pipeline_) {
        std::cerr << "Failed to create render pipeline.\n";
        return false;
    }

    return true;
}

template <typename T>
wgpu::Buffer WebGpuRenderer::createBuffer(
    const std::vector<T>& data,
    wgpu::BufferUsage usage
) {
    wgpu::BufferDescriptor desc{};
    desc.size = data.size() * sizeof(T);
    desc.usage = usage | wgpu::BufferUsage::CopyDst;
    desc.mappedAtCreation = false;

    wgpu::Buffer buffer = device_.CreateBuffer(&desc);

    queue_.WriteBuffer(
        buffer,
        0,
        data.data(),
        desc.size
    );

    return buffer;
}

bool WebGpuRenderer::getWindowPixelSize(
    uint32_t& width,
    uint32_t& height
) const {
    int w = 0;
    int h = 0;

    if (!SDL_GetWindowSizeInPixels(window_, &w, &h)) {
        std::cerr << "SDL_GetWindowSizeInPixels failed: "
                  << SDL_GetError()
                  << "\n";
        return false;
    }

    if (w <= 0 || h <= 0) {
        return false;
    }

    width = static_cast<uint32_t>(w);
    height = static_cast<uint32_t>(h);

    return true;
}

bool WebGpuRenderer::resizeIfNeeded() {
    uint32_t width = 0;
    uint32_t height = 0;

    if (!getWindowPixelSize(width, height)) {
        return false;
    }

    if (
        width == surfaceConfig_.width &&
        height == surfaceConfig_.height
    ) {
        return true;
    }

    surfaceConfig_.width = width;
    surfaceConfig_.height = height;

    surface_.Configure(&surfaceConfig_);

    depthTexture_ =
        createDepthTextureObject(
            surfaceConfig_.width,
            surfaceConfig_.height
        );

    return static_cast<bool>(depthTexture_);
}

void WebGpuRenderer::ensureObjectUniformCapacity(std::size_t count) {
    while (objectUniformBuffers_.size() < count) {
        wgpu::BufferDescriptor uniformDesc{};
        uniformDesc.size = sizeof(ObjectUniforms);
        uniformDesc.usage =
            wgpu::BufferUsage::Uniform |
            wgpu::BufferUsage::CopyDst;
        uniformDesc.mappedAtCreation = false;

        wgpu::Buffer uniformBuffer =
            device_.CreateBuffer(&uniformDesc);

        wgpu::BindGroupEntry bgEntry{};
        bgEntry.binding = 0;
        bgEntry.buffer = uniformBuffer;
        bgEntry.offset = 0;
        bgEntry.size = sizeof(ObjectUniforms);

        wgpu::BindGroupDescriptor bindGroupDesc{};
        bindGroupDesc.layout = objectBindGroupLayout_;
        bindGroupDesc.entryCount = 1;
        bindGroupDesc.entries = &bgEntry;

        wgpu::BindGroup bindGroup =
            device_.CreateBindGroup(&bindGroupDesc);

        objectUniformBuffers_.push_back(uniformBuffer);
        objectBindGroups_.push_back(bindGroup);
    }
}

glm::mat4 WebGpuRenderer::createPrismModelMatrix(
    const Prism& prism
) const {
    glm::mat4 translation =
        glm::translate(
            glm::mat4(1.0f),
            prism.position
        );

    // Mesh is authored along Z. The negative rotation puts the front cap on
    // top after the prism is stood upright, so its normal faces upward.
    glm::mat4 uprightRotation =
        glm::rotate(
            glm::mat4(1.0f),
            glm::radians(-90.0f),
            glm::vec3(1.0f, 0.0f, 0.0f)
        );

    int step = prism.rotationStep % 6;

    if (step < 0) {
        step += 6;
    }

    float angleDegrees = static_cast<float>(step) * 60.0f;

    glm::mat4 hexRotation =
        glm::rotate(
            glm::mat4(1.0f),
            glm::radians(angleDegrees),
            glm::vec3(0.0f, 1.0f, 0.0f)
        );

    return translation * hexRotation * uprightRotation;
}

void WebGpuRenderer::render(
    const std::vector<Prism>& prisms,
    const Camera& camera
) {
    instance_.ProcessEvents();

    if (!resizeIfNeeded()) {
        return;
    }

    ensureObjectUniformCapacity(prisms.size());

    glm::vec3 forward = getCameraForward(camera);

    glm::mat4 view =
        glm::lookAt(
            camera.position,
            camera.position + forward,
            getCameraUp(camera)
        );

    glm::mat4 proj =
        glm::perspective(
            glm::radians(45.0f),
            static_cast<float>(surfaceConfig_.width) /
                static_cast<float>(surfaceConfig_.height),
            0.1f,
            100.0f
        );

    proj[1][1] *= -1.0f;

    for (std::size_t i = 0; i < prisms.size(); ++i) {
        glm::mat4 model = createPrismModelMatrix(prisms[i]);

        ObjectUniforms uniforms{};
        uniforms.model = model;
        uniforms.mvp = proj * view * model;
        uniforms.color = glm::vec4(prisms[i].color, 1.0f);

        queue_.WriteBuffer(
            objectUniformBuffers_[i],
            0,
            &uniforms,
            sizeof(ObjectUniforms)
        );
    }

    wgpu::SurfaceTexture surfaceTexture{};
    surface_.GetCurrentTexture(&surfaceTexture);

    if (
        surfaceTexture.status !=
            wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal &&
        surfaceTexture.status !=
            wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal
    ) {
        std::cerr << "GetCurrentTexture failed. Status = "
                  << static_cast<int>(surfaceTexture.status)
                  << "\n";

        surface_.Configure(&surfaceConfig_);
        return;
    }

    wgpu::TextureView backbuffer =
        surfaceTexture.texture.CreateView();

    wgpu::TextureView depthView =
        depthTexture_.CreateView();

    wgpu::RenderPassColorAttachment colorAttachment{};
    colorAttachment.view = backbuffer;
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    colorAttachment.loadOp = wgpu::LoadOp::Clear;
    colorAttachment.storeOp = wgpu::StoreOp::Store;
    colorAttachment.clearValue = {
        0.05,
        0.05,
        0.08,
        1.0
    };

    wgpu::RenderPassDepthStencilAttachment depthAttachment{};
    depthAttachment.view = depthView;
    depthAttachment.depthLoadOp = wgpu::LoadOp::Clear;
    depthAttachment.depthStoreOp = wgpu::StoreOp::Store;
    depthAttachment.depthClearValue = 1.0f;

    wgpu::RenderPassDescriptor passDesc{};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;
    passDesc.depthStencilAttachment = &depthAttachment;

    wgpu::CommandEncoder encoder =
        device_.CreateCommandEncoder();

    wgpu::RenderPassEncoder pass =
        encoder.BeginRenderPass(&passDesc);

    pass.SetPipeline(pipeline_);
    pass.SetVertexBuffer(0, prismVertexBuffer_);
    pass.SetIndexBuffer(prismIndexBuffer_, wgpu::IndexFormat::Uint16);

    for (std::size_t i = 0; i < prisms.size(); ++i) {
        pass.SetBindGroup(0, objectBindGroups_[i]);

        pass.DrawIndexed(
            static_cast<uint32_t>(prismMesh_.indices.size())
        );
    }

    pass.End();

    wgpu::CommandBuffer commands = encoder.Finish();

    queue_.Submit(1, &commands);

    surface_.Present();
}
