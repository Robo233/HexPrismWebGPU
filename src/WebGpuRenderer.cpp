#include "WebGpuRenderer.hpp"

#include <SDL3/SDL.h>

#include <glm/gtc/matrix_transform.hpp>

#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
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
struct FrameUniforms {
    viewProjection : mat4x4f,
    cameraRightAndAspect : vec4f,
    cameraUpAndTanHalfFov : vec4f,
    cameraForwardAndTime : vec4f,
};

@group(0) @binding(0)
var<uniform> frame : FrameUniforms;

struct VertexIn {
    @location(0) position : vec3f,
    @location(1) normal : vec3f,
    @location(2) positionAndCos : vec4f,
    @location(3) colorAndSin : vec4f,
};

struct VertexOut {
    @builtin(position) position : vec4f,
    @location(0) normal : vec3f,
    @location(1) color : vec4f,
};

struct SkyVertexOut {
    @builtin(position) position : vec4f,
    @location(0) ndc : vec2f,
};

@vertex
fn vsSky(@builtin(vertex_index) vertexIndex : u32) -> SkyVertexOut {
    var out : SkyVertexOut;
    let positions = array<vec2f, 3>(
        vec2f(-1.0, -1.0),
        vec2f(3.0, -1.0),
        vec2f(-1.0, 3.0)
    );
    let ndc = positions[vertexIndex];

    out.position = vec4f(ndc, 1.0, 1.0);
    out.ndc = ndc;

    return out;
}

fn saturate(value : f32) -> f32 {
    return clamp(value, 0.0, 1.0);
}

fn hash2(p : vec2f) -> f32 {
    return fract(sin(dot(p, vec2f(127.1, 311.7))) * 43758.5453);
}

fn valueNoise(p : vec2f) -> f32 {
    let i = floor(p);
    let f = fract(p);
    let u = f * f * (3.0 - 2.0 * f);

    let a = hash2(i);
    let b = hash2(i + vec2f(1.0, 0.0));
    let c = hash2(i + vec2f(0.0, 1.0));
    let d = hash2(i + vec2f(1.0, 1.0));

    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

fn fbm(p : vec2f) -> f32 {
    var q = p;
    var amplitude = 0.5;
    var total = 0.0;
    var normalization = 0.0;

    for (var i = 0; i < 5; i = i + 1) {
        total = total + valueNoise(q) * amplitude;
        normalization = normalization + amplitude;
        q = q * 2.03 + vec2f(17.2, 11.7);
        amplitude = amplitude * 0.52;
    }

    return total / normalization;
}

@fragment
fn fsSky(input : SkyVertexOut) -> @location(0) vec4f {
    let aspect = frame.cameraRightAndAspect.w;
    let tanHalfFov = frame.cameraUpAndTanHalfFov.w;
    let screen = vec2f(input.ndc.x, -input.ndc.y);

    let ray = normalize(
        frame.cameraForwardAndTime.xyz +
        frame.cameraRightAndAspect.xyz * screen.x * aspect * tanHalfFov +
        frame.cameraUpAndTanHalfFov.xyz * screen.y * tanHalfFov
    );

    let sunDir = normalize(vec3f(0.52, 0.66, -0.54));
    let sunAmount = saturate(dot(ray, sunDir));
    let vertical = saturate(ray.y);
    let horizonHaze = exp(-abs(ray.y) * 7.0);

    var sky = mix(
        vec3f(0.64, 0.72, 0.80),
        vec3f(0.24, 0.52, 0.92),
        saturate(ray.y * 1.45 + 0.28)
    );
    sky = mix(sky, vec3f(0.05, 0.18, 0.46), pow(vertical, 1.35) * 0.62);
    sky = mix(sky, vec3f(0.84, 0.89, 0.93), horizonHaze * 0.35);

    let warmScatter =
        pow(sunAmount, 8.0) * 0.12 +
        pow(sunAmount, 36.0) * 0.28;
    sky = sky + vec3f(1.0, 0.74, 0.42) * warmScatter;
    sky = mix(sky, vec3f(1.0, 0.88, 0.58), smoothstep(0.9991, 1.0, sunAmount));

    let time = frame.cameraForwardAndTime.w;
    let cloudLayer =
        smoothstep(0.03, 0.22, ray.y) *
        (1.0 - smoothstep(0.70, 0.93, ray.y));
    let cloudPlaneUv =
        ray.xz / max(ray.y + 0.18, 0.12) +
        vec2f(time * 0.012, time * 0.004);
    let broadCloud = fbm(cloudPlaneUv * 0.55);
    let fineCloud = fbm(cloudPlaneUv * 2.2 + vec2f(3.7, 8.1));
    let cloudMask =
        smoothstep(0.53, 0.77, broadCloud + fineCloud * 0.19) *
        cloudLayer;
    let cloudLit = 0.72 + 0.28 * sunAmount;
    let cloudColor = mix(
        vec3f(0.72, 0.76, 0.80),
        vec3f(1.0, 0.96, 0.89),
        cloudLit
    );
    sky = mix(sky, cloudColor, cloudMask * 0.62);

    return vec4f(clamp(sky, vec3f(0.0), vec3f(1.0)), 1.0);
}

@vertex
fn vsMain(input : VertexIn) -> VertexOut {
    var out : VertexOut;

    let uprightPosition = vec3f(
        input.position.x,
        input.position.z,
        -input.position.y
    );
    let uprightNormal = vec3f(
        input.normal.x,
        input.normal.z,
        -input.normal.y
    );

    let c = input.positionAndCos.w;
    let s = input.colorAndSin.w;

    let rotatedPosition = vec3f(
        uprightPosition.x * c + uprightPosition.z * s,
        uprightPosition.y,
        -uprightPosition.x * s + uprightPosition.z * c
    );

    let rotatedNormal = normalize(vec3f(
        uprightNormal.x * c + uprightNormal.z * s,
        uprightNormal.y,
        -uprightNormal.x * s + uprightNormal.z * c
    ));

    out.position = frame.viewProjection * vec4f(
        rotatedPosition + input.positionAndCos.xyz,
        1.0
    );
    out.normal = rotatedNormal;
    out.color = vec4f(input.colorAndSin.rgb, 1.0);

    return out;
}

@fragment
fn fsMain(input : VertexOut) -> @location(0) vec4f {
    let lightDir = normalize(vec3f(0.52, 0.66, -0.54));
    let n = normalize(input.normal);

    let diffuse = max(dot(n, lightDir), 0.0);
    let ambient = 0.22;

    let lighting = ambient + diffuse * 0.78;
    let finalColor = input.color.rgb * lighting;

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
    bindEntry.buffer.minBindingSize = sizeof(FrameUniforms);

    wgpu::BindGroupLayoutDescriptor bindLayoutDesc{};
    bindLayoutDesc.entryCount = 1;
    bindLayoutDesc.entries = &bindEntry;

    frameBindGroupLayout_ =
        device_.CreateBindGroupLayout(&bindLayoutDesc);

    wgpu::BufferDescriptor frameUniformDesc{};
    frameUniformDesc.size = sizeof(FrameUniforms);
    frameUniformDesc.usage =
        wgpu::BufferUsage::Uniform |
        wgpu::BufferUsage::CopyDst;
    frameUniformDesc.mappedAtCreation = false;

    frameUniformBuffer_ =
        device_.CreateBuffer(&frameUniformDesc);

    wgpu::BindGroupEntry frameBindEntry{};
    frameBindEntry.binding = 0;
    frameBindEntry.buffer = frameUniformBuffer_;
    frameBindEntry.offset = 0;
    frameBindEntry.size = sizeof(FrameUniforms);

    wgpu::BindGroupDescriptor frameBindDesc{};
    frameBindDesc.layout = frameBindGroupLayout_;
    frameBindDesc.entryCount = 1;
    frameBindDesc.entries = &frameBindEntry;

    frameBindGroup_ =
        device_.CreateBindGroup(&frameBindDesc);

    wgpu::PipelineLayoutDescriptor pipelineLayoutDesc{};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &frameBindGroupLayout_;

    pipelineLayout_ =
        device_.CreatePipelineLayout(&pipelineLayoutDesc);

    wgpu::ShaderModule shader = createShaderModule();

    std::array<wgpu::VertexAttribute, 2> vertexAttributes{};

    vertexAttributes[0].shaderLocation = 0;
    vertexAttributes[0].offset = offsetof(PrismVertex, px);
    vertexAttributes[0].format = wgpu::VertexFormat::Float32x3;

    vertexAttributes[1].shaderLocation = 1;
    vertexAttributes[1].offset = offsetof(PrismVertex, nx);
    vertexAttributes[1].format = wgpu::VertexFormat::Float32x3;

    wgpu::VertexBufferLayout vertexLayouts[2]{};
    vertexLayouts[0].arrayStride = sizeof(PrismVertex);
    vertexLayouts[0].stepMode = wgpu::VertexStepMode::Vertex;
    vertexLayouts[0].attributeCount =
        static_cast<uint32_t>(vertexAttributes.size());
    vertexLayouts[0].attributes = vertexAttributes.data();

    std::array<wgpu::VertexAttribute, 2> instanceAttributes{};

    instanceAttributes[0].shaderLocation = 2;
    instanceAttributes[0].offset =
        offsetof(PrismInstanceData, positionAndCos);
    instanceAttributes[0].format = wgpu::VertexFormat::Float32x4;

    instanceAttributes[1].shaderLocation = 3;
    instanceAttributes[1].offset =
        offsetof(PrismInstanceData, colorAndSin);
    instanceAttributes[1].format = wgpu::VertexFormat::Float32x4;

    vertexLayouts[1].arrayStride = sizeof(PrismInstanceData);
    vertexLayouts[1].stepMode = wgpu::VertexStepMode::Instance;
    vertexLayouts[1].attributeCount =
        static_cast<uint32_t>(instanceAttributes.size());
    vertexLayouts[1].attributes = instanceAttributes.data();

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

    wgpu::DepthStencilState skyDepthStencil{};
    skyDepthStencil.format = wgpu::TextureFormat::Depth24Plus;
    skyDepthStencil.depthWriteEnabled = false;
    skyDepthStencil.depthCompare = wgpu::CompareFunction::Always;

    wgpu::FragmentState skyFragment{};
    skyFragment.module = shader;
    skyFragment.entryPoint = "fsSky";
    skyFragment.targetCount = 1;
    skyFragment.targets = &colorTarget;

    wgpu::RenderPipelineDescriptor skyPipelineDesc{};
    skyPipelineDesc.layout = pipelineLayout_;
    skyPipelineDesc.vertex.module = shader;
    skyPipelineDesc.vertex.entryPoint = "vsSky";
    skyPipelineDesc.vertex.bufferCount = 0;
    skyPipelineDesc.vertex.buffers = nullptr;
    skyPipelineDesc.fragment = &skyFragment;
    skyPipelineDesc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    skyPipelineDesc.primitive.cullMode = wgpu::CullMode::None;
    skyPipelineDesc.depthStencil = &skyDepthStencil;
    skyPipelineDesc.multisample.count = 1;

    skyPipeline_ = device_.CreateRenderPipeline(&skyPipelineDesc);

    if (!skyPipeline_) {
        std::cerr << "Failed to create sky render pipeline.\n";
        return false;
    }

    wgpu::RenderPipelineDescriptor pipelineDesc{};
    pipelineDesc.layout = pipelineLayout_;

    pipelineDesc.vertex.module = shader;
    pipelineDesc.vertex.entryPoint = "vsMain";
    pipelineDesc.vertex.bufferCount = 2;
    pipelineDesc.vertex.buffers = vertexLayouts;

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

void WebGpuRenderer::ensurePrismInstanceBufferCapacity(
    std::size_t count
) {
    if (count <= prismInstanceCapacity_) {
        return;
    }

    prismInstanceCapacity_ = std::max<std::size_t>(count, 1);

    wgpu::BufferDescriptor instanceDesc{};
    instanceDesc.size =
        prismInstanceCapacity_ * sizeof(PrismInstanceData);
    instanceDesc.usage =
        wgpu::BufferUsage::Vertex |
        wgpu::BufferUsage::CopyDst;
    instanceDesc.mappedAtCreation = false;

    prismInstanceBuffer_ =
        device_.CreateBuffer(&instanceDesc);
}

void WebGpuRenderer::updatePrismInstanceBuffer(
    const std::vector<Prism>& prisms,
    uint64_t prismRevision
) {
    if (prisms.empty()) {
        uploadedPrismRevision_ = prismRevision;
        uploadedPrismCount_ = 0;
        return;
    }

    if (
        prismRevision != 0 &&
        uploadedPrismRevision_ == prismRevision &&
        uploadedPrismCount_ == prisms.size()
    ) {
        return;
    }

    ensurePrismInstanceBufferCapacity(prisms.size());

    prismInstanceData_.resize(prisms.size());

    constexpr std::array<float, 6> rotationCos{
        1.0f,
        0.5f,
        -0.5f,
        -1.0f,
        -0.5f,
        0.5f
    };
    constexpr std::array<float, 6> rotationSin{
        0.0f,
        0.8660254038f,
        0.8660254038f,
        0.0f,
        -0.8660254038f,
        -0.8660254038f
    };

    for (std::size_t i = 0; i < prisms.size(); ++i) {
        int step = prisms[i].rotationStep % 6;

        if (step < 0) {
            step += 6;
        }

        prismInstanceData_[i] = PrismInstanceData{
            glm::vec4(
                prisms[i].position,
                rotationCos[static_cast<std::size_t>(step)]
            ),
            glm::vec4(
                prisms[i].color,
                rotationSin[static_cast<std::size_t>(step)]
            )
        };
    }

    queue_.WriteBuffer(
        prismInstanceBuffer_,
        0,
        prismInstanceData_.data(),
        prismInstanceData_.size() * sizeof(PrismInstanceData)
    );

    uploadedPrismRevision_ = prismRevision;
    uploadedPrismCount_ = prisms.size();
}

void WebGpuRenderer::render(
    const std::vector<Prism>& prisms,
    const Camera& camera,
    uint64_t prismRevision
) {
    instance_.ProcessEvents();

    if (!resizeIfNeeded()) {
        return;
    }

    glm::vec3 forward = getCameraForward(camera);

    glm::mat4 view =
        glm::lookAt(
            camera.position,
            camera.position + forward,
            getCameraUp(camera)
        );

    constexpr float verticalFovDegrees = 45.0f;
    float aspect =
        static_cast<float>(surfaceConfig_.width) /
        static_cast<float>(surfaceConfig_.height);

    glm::mat4 proj =
        glm::perspective(
            glm::radians(verticalFovDegrees),
            aspect,
            0.1f,
            100.0f
        );

    proj[1][1] *= -1.0f;

    FrameUniforms frameUniforms{};
    frameUniforms.viewProjection = proj * view;
    frameUniforms.cameraRightAndAspect =
        glm::vec4(getCameraRight(camera), aspect);
    frameUniforms.cameraUpAndTanHalfFov =
        glm::vec4(
            getCameraUp(camera),
            std::tan(glm::radians(verticalFovDegrees) * 0.5f)
        );
    frameUniforms.cameraForwardAndTime =
        glm::vec4(
            forward,
            static_cast<float>(SDL_GetTicks()) * 0.001f
        );

    queue_.WriteBuffer(
        frameUniformBuffer_,
        0,
        &frameUniforms,
        sizeof(FrameUniforms)
    );

    updatePrismInstanceBuffer(prisms, prismRevision);

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

    pass.SetPipeline(skyPipeline_);
    pass.SetBindGroup(0, frameBindGroup_);
    pass.Draw(3);

    pass.SetPipeline(pipeline_);
    pass.SetBindGroup(0, frameBindGroup_);
    pass.SetVertexBuffer(0, prismVertexBuffer_);
    pass.SetIndexBuffer(prismIndexBuffer_, wgpu::IndexFormat::Uint16);

    if (!prisms.empty()) {
        pass.SetVertexBuffer(1, prismInstanceBuffer_);
        pass.DrawIndexed(
            static_cast<uint32_t>(prismMesh_.indices.size()),
            static_cast<uint32_t>(prisms.size())
        );
    }

    pass.End();

    wgpu::CommandBuffer commands = encoder.Finish();

    queue_.Submit(1, &commands);

    surface_.Present();
}
