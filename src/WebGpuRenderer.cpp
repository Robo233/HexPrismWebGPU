#include "WebGpuRenderer.hpp"

#include <SDL3/SDL.h>

#include <glm/gtc/matrix_transform.hpp>

#include <windows.h>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

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

    if (!createShadowDepthTexture()) {
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


bool WebGpuRenderer::createShadowDepthTexture() {
    wgpu::TextureDescriptor desc{};
    desc.size = { ShadowMapSize, ShadowMapSize, 1 };
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;
    desc.dimension = wgpu::TextureDimension::e2D;
    desc.format = wgpu::TextureFormat::Depth24Plus;
    desc.usage =
        wgpu::TextureUsage::RenderAttachment |
        wgpu::TextureUsage::TextureBinding;

    shadowDepthTexture_ = device_.CreateTexture(&desc);

    if (!shadowDepthTexture_) {
        std::cerr << "Failed to create shadow depth texture.\n";
        return false;
    }

    shadowDepthTextureView_ = shadowDepthTexture_.CreateView();

    if (!shadowDepthTextureView_) {
        std::cerr << "Failed to create shadow depth texture view.\n";
        return false;
    }

    return true;
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
    lightViewProjection : mat4x4f,
    cameraRightAndAspect : vec4f,
    cameraUpAndTanHalfFov : vec4f,
    cameraForwardAndTime : vec4f,
};

@group(0) @binding(0)
var<uniform> frame : FrameUniforms;

// Step 3K: manual shadow-map depth reads. The light camera is now centered
// from the actual uploaded prism bounds instead of the viewer camera, so
// shadow-map artifacts should not slide around like a cloud attached to
// the player.
@group(0) @binding(1)
var shadowMap : texture_depth_2d;

@group(0) @binding(2)
var shadowSampler : sampler_comparison;

struct VertexIn {
    @location(0) position : vec3f,
    @location(1) normal : vec3f,
    @location(2) positionAndCos : vec4f,
    @location(3) colorAndSin : vec4f,
    @location(4) alpha : f32,
};

struct VertexOut {
    @builtin(position) position : vec4f,
    @location(0) normal : vec3f,
    @location(1) color : vec4f,
    @location(2) worldPosition : vec3f,
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

fn sunDirection() -> vec3f {
    return normalize(vec3f(0.52, 0.66, -0.54));
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

    let sunDir = sunDirection();
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

    let worldPosition = rotatedPosition + input.positionAndCos.xyz;

    out.position = frame.viewProjection * vec4f(worldPosition, 1.0);
    out.normal = rotatedNormal;
    out.color = vec4f(input.colorAndSin.rgb, input.alpha);
    out.worldPosition = worldPosition;

    return out;
}

@vertex
fn vsShadow(input : VertexIn) -> VertexOut {
    var out : VertexOut;

    let uprightPosition = vec3f(
        input.position.x,
        input.position.z,
        -input.position.y
    );

    let c = input.positionAndCos.w;
    let s = input.colorAndSin.w;

    let rotatedPosition = vec3f(
        uprightPosition.x * c + uprightPosition.z * s,
        uprightPosition.y,
        -uprightPosition.x * s + uprightPosition.z * c
    );

    let worldPosition = rotatedPosition + input.positionAndCos.xyz;

    out.position = frame.lightViewProjection * vec4f(worldPosition, 1.0);
    out.normal = vec3f(0.0, 1.0, 0.0);
    out.color = vec4f(1.0);
    out.worldPosition = worldPosition;

    return out;
}


fn manualShadowMapAmount(worldPosition : vec3f, normal : vec3f, lightDir : vec3f) -> f32 {
    let lightClip = frame.lightViewProjection * vec4f(worldPosition, 1.0);

    if (lightClip.w <= 0.0) {
        return 0.0;
    }

    let lightNdc = lightClip.xyz / lightClip.w;

    // WebGPU depth is already 0..1 because the C++ side uses
    // GLM_FORCE_DEPTH_ZERO_TO_ONE. TextureLoad coordinates use texture
    // coordinates, so Y is flipped here to match the depth texture rows.
    let shadowUv = vec2f(
        lightNdc.x * 0.5 + 0.5,
        0.5 - lightNdc.y * 0.5
    );
    let currentDepth = lightNdc.z;

    if (
        shadowUv.x < 0.0 || shadowUv.x > 1.0 ||
        shadowUv.y < 0.0 || shadowUv.y > 1.0 ||
        currentDepth < 0.0 || currentDepth > 1.0
    ) {
        return 0.0;
    }

    let shadowMapSize = vec2f(1024.0, 1024.0);
    let baseTexel = vec2i(clamp(
        shadowUv * shadowMapSize,
        vec2f(1.0, 1.0),
        shadowMapSize - vec2f(2.0, 2.0)
    ));

    // Step 3L: now that the broad moving cloud is gone, lower the bias so
    // thin/tall objects such as trees can cast visible shadows. Bias is still
    // slope-aware to reduce acne on steep faces.
    let slopeBias = (1.0 - saturate(dot(normal, lightDir))) * 0.0025;
    let bias = 0.0012 + slopeBias;

    var occludedSamples = 0.0;
    var totalSamples = 0.0;

    for (var y = -1; y <= 1; y = y + 1) {
        for (var x = -1; x <= 1; x = x + 1) {
            let sampleTexel = baseTexel + vec2i(x, y);
            let storedDepth = textureLoad(shadowMap, sampleTexel, 0);

            if (currentDepth - bias > storedDepth) {
                occludedSamples = occludedSamples + 1.0;
            }

            totalSamples = totalSamples + 1.0;
        }
    }

    let occlusion = occludedSamples / totalSamples;

    // Step 3L: stronger shadows so tree shadows are visible during tuning.
    return occlusion * 0.58;
}

fn softShadowAmount(
    worldPosition : vec3f,
    normal : vec3f,
    lightDir : vec3f
) -> f32 {
    let upwardFacing = smoothstep(0.45, 0.95, normal.y);
    let projectedToGround =
        worldPosition.xz - lightDir.xz * (worldPosition.y / max(lightDir.y, 0.18));

    let broadShadow = fbm(projectedToGround * 0.42 + vec2f(19.3, -7.1));
    let fineShadow = fbm(projectedToGround * 1.35 + vec2f(-4.7, 28.9));
    let shadowMask = smoothstep(0.50, 0.78, broadShadow + fineShadow * 0.18);

    let heightFade = 1.0 - smoothstep(16.0, 34.0, worldPosition.y);
    let contactShade = (1.0 - upwardFacing) * 0.08;

    return clamp(shadowMask * upwardFacing * heightFade * 0.65 + contactShade * 2.0, 0.0, 0.70);
}

@fragment
fn fsMain(input : VertexOut) -> @location(0) vec4f {
    let n = normalize(input.normal);
    let isWater = input.color.a < 0.999;

    if (isWater) {
        if (n.y < 0.75) {
            discard;
        }

        return vec4f(input.color.rgb, input.color.a);
    }

    let lightDir = sunDirection();
    let diffuse = saturate(dot(n, lightDir));
    let ambient = 0.22;

    let lighting = ambient + diffuse * 0.78;

    // Step 3L: keep the old procedural fake cloud shadow disabled.
    // Use only the real shadow map, but allow it to be strong enough to see.
    let realShadow = manualShadowMapAmount(input.worldPosition, n, lightDir);
    let shadow = clamp(realShadow, 0.0, 0.62);

    let finalColor = input.color.rgb * lighting * (1.0 - shadow);

    return vec4f(finalColor, input.color.a);
}
)";

    wgpu::ShaderSourceWGSL source{};
    source.code = wgsl;

    wgpu::ShaderModuleDescriptor desc{};
    desc.nextInChain = &source;

    return device_.CreateShaderModule(&desc);
}

bool WebGpuRenderer::createPipeline() {
    wgpu::BindGroupLayoutEntry uniformBindEntry{};
    uniformBindEntry.binding = 0;
    uniformBindEntry.visibility =
        wgpu::ShaderStage::Vertex |
        wgpu::ShaderStage::Fragment;
    uniformBindEntry.buffer.type = wgpu::BufferBindingType::Uniform;
    uniformBindEntry.buffer.minBindingSize = sizeof(FrameUniforms);

    wgpu::BindGroupLayoutEntry shadowTextureBindEntry{};
    shadowTextureBindEntry.binding = 1;
    shadowTextureBindEntry.visibility = wgpu::ShaderStage::Fragment;
    shadowTextureBindEntry.texture.sampleType = wgpu::TextureSampleType::Depth;
    shadowTextureBindEntry.texture.viewDimension = wgpu::TextureViewDimension::e2D;
    shadowTextureBindEntry.texture.multisampled = false;

    wgpu::BindGroupLayoutEntry shadowSamplerBindEntry{};
    shadowSamplerBindEntry.binding = 2;
    shadowSamplerBindEntry.visibility = wgpu::ShaderStage::Fragment;
    shadowSamplerBindEntry.sampler.type = wgpu::SamplerBindingType::Comparison;

    std::array<wgpu::BindGroupLayoutEntry, 3> mainBindEntries{
        uniformBindEntry,
        shadowTextureBindEntry,
        shadowSamplerBindEntry
    };

    wgpu::BindGroupLayoutDescriptor bindLayoutDesc{};
    bindLayoutDesc.entryCount =
        static_cast<uint32_t>(mainBindEntries.size());
    bindLayoutDesc.entries = mainBindEntries.data();

    frameBindGroupLayout_ =
        device_.CreateBindGroupLayout(&bindLayoutDesc);

    if (!frameBindGroupLayout_) {
        std::cerr << "Failed to create main frame bind group layout.
";
        return false;
    }

    wgpu::BindGroupLayoutDescriptor shadowBindLayoutDesc{};
    shadowBindLayoutDesc.entryCount = 1;
    shadowBindLayoutDesc.entries = &uniformBindEntry;

    shadowFrameBindGroupLayout_ =
        device_.CreateBindGroupLayout(&shadowBindLayoutDesc);

    if (!shadowFrameBindGroupLayout_) {
        std::cerr << "Failed to create shadow frame bind group layout.
";
        return false;
    }

    wgpu::BufferDescriptor frameUniformDesc{};
    frameUniformDesc.size = sizeof(FrameUniforms);
    frameUniformDesc.usage =
        wgpu::BufferUsage::Uniform |
        wgpu::BufferUsage::CopyDst;
    frameUniformDesc.mappedAtCreation = false;

    frameUniformBuffer_ =
        device_.CreateBuffer(&frameUniformDesc);

    if (!frameUniformBuffer_) {
        std::cerr << "Failed to create frame uniform buffer.
";
        return false;
    }

    wgpu::SamplerDescriptor shadowSamplerDesc{};
    shadowSamplerDesc.addressModeU = wgpu::AddressMode::ClampToEdge;
    shadowSamplerDesc.addressModeV = wgpu::AddressMode::ClampToEdge;
    shadowSamplerDesc.addressModeW = wgpu::AddressMode::ClampToEdge;
    shadowSamplerDesc.magFilter = wgpu::FilterMode::Linear;
    shadowSamplerDesc.minFilter = wgpu::FilterMode::Linear;
    shadowSamplerDesc.mipmapFilter = wgpu::MipmapFilterMode::Nearest;
    shadowSamplerDesc.compare = wgpu::CompareFunction::LessEqual;
    shadowSamplerDesc.lodMinClamp = 0.0f;
    shadowSamplerDesc.lodMaxClamp = 1.0f;

    shadowSampler_ = device_.CreateSampler(&shadowSamplerDesc);

    if (!shadowSampler_) {
        std::cerr << "Failed to create shadow comparison sampler.
";
        return false;
    }

    wgpu::BindGroupEntry uniformBindGroupEntry{};
    uniformBindGroupEntry.binding = 0;
    uniformBindGroupEntry.buffer = frameUniformBuffer_;
    uniformBindGroupEntry.offset = 0;
    uniformBindGroupEntry.size = sizeof(FrameUniforms);

    wgpu::BindGroupEntry shadowTextureBindGroupEntry{};
    shadowTextureBindGroupEntry.binding = 1;
    shadowTextureBindGroupEntry.textureView = shadowDepthTextureView_;

    wgpu::BindGroupEntry shadowSamplerBindGroupEntry{};
    shadowSamplerBindGroupEntry.binding = 2;
    shadowSamplerBindGroupEntry.sampler = shadowSampler_;

    std::array<wgpu::BindGroupEntry, 3> mainBindGroupEntries{
        uniformBindGroupEntry,
        shadowTextureBindGroupEntry,
        shadowSamplerBindGroupEntry
    };

    wgpu::BindGroupDescriptor frameBindDesc{};
    frameBindDesc.layout = frameBindGroupLayout_;
    frameBindDesc.entryCount =
        static_cast<uint32_t>(mainBindGroupEntries.size());
    frameBindDesc.entries = mainBindGroupEntries.data();

    frameBindGroup_ =
        device_.CreateBindGroup(&frameBindDesc);

    if (!frameBindGroup_) {
        std::cerr << "Failed to create main frame bind group.
";
        return false;
    }

    wgpu::BindGroupDescriptor shadowFrameBindDesc{};
    shadowFrameBindDesc.layout = shadowFrameBindGroupLayout_;
    shadowFrameBindDesc.entryCount = 1;
    shadowFrameBindDesc.entries = &uniformBindGroupEntry;

    shadowFrameBindGroup_ =
        device_.CreateBindGroup(&shadowFrameBindDesc);

    if (!shadowFrameBindGroup_) {
        std::cerr << "Failed to create shadow frame bind group.
";
        return false;
    }

    wgpu::PipelineLayoutDescriptor pipelineLayoutDesc{};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &frameBindGroupLayout_;

    pipelineLayout_ =
        device_.CreatePipelineLayout(&pipelineLayoutDesc);

    if (!pipelineLayout_) {
        std::cerr << "Failed to create main pipeline layout.
";
        return false;
    }

    wgpu::PipelineLayoutDescriptor shadowPipelineLayoutDesc{};
    shadowPipelineLayoutDesc.bindGroupLayoutCount = 1;
    shadowPipelineLayoutDesc.bindGroupLayouts = &shadowFrameBindGroupLayout_;

    shadowPipelineLayout_ =
        device_.CreatePipelineLayout(&shadowPipelineLayoutDesc);

    if (!shadowPipelineLayout_) {
        std::cerr << "Failed to create shadow pipeline layout.
";
        return false;
    }

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

    std::array<wgpu::VertexAttribute, 3> instanceAttributes{};

    instanceAttributes[0].shaderLocation = 2;
    instanceAttributes[0].offset =
        offsetof(PrismInstanceData, positionAndCos);
    instanceAttributes[0].format = wgpu::VertexFormat::Float32x4;

    instanceAttributes[1].shaderLocation = 3;
    instanceAttributes[1].offset =
        offsetof(PrismInstanceData, colorAndSin);
    instanceAttributes[1].format = wgpu::VertexFormat::Float32x4;

    instanceAttributes[2].shaderLocation = 4;
    instanceAttributes[2].offset =
        offsetof(PrismInstanceData, alpha);
    instanceAttributes[2].format = wgpu::VertexFormat::Float32;

    vertexLayouts[1].arrayStride = sizeof(PrismInstanceData);
    vertexLayouts[1].stepMode = wgpu::VertexStepMode::Instance;
    vertexLayouts[1].attributeCount =
        static_cast<uint32_t>(instanceAttributes.size());
    vertexLayouts[1].attributes = instanceAttributes.data();

    wgpu::BlendState alphaBlend{};
    alphaBlend.color.operation = wgpu::BlendOperation::Add;
    alphaBlend.color.srcFactor = wgpu::BlendFactor::SrcAlpha;
    alphaBlend.color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
    alphaBlend.alpha.operation = wgpu::BlendOperation::Add;
    alphaBlend.alpha.srcFactor = wgpu::BlendFactor::One;
    alphaBlend.alpha.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;

    wgpu::ColorTargetState colorTarget{};
    colorTarget.format = surfaceFormat_;
    colorTarget.blend = &alphaBlend;
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

    wgpu::DepthStencilState shadowDepthStencil{};
    shadowDepthStencil.format = wgpu::TextureFormat::Depth24Plus;
    shadowDepthStencil.depthWriteEnabled = true;
    shadowDepthStencil.depthCompare = wgpu::CompareFunction::Less;

    wgpu::RenderPipelineDescriptor shadowPipelineDesc{};
    shadowPipelineDesc.layout = shadowPipelineLayout_;
    shadowPipelineDesc.vertex.module = shader;
    shadowPipelineDesc.vertex.entryPoint = "vsShadow";
    shadowPipelineDesc.vertex.bufferCount = 2;
    shadowPipelineDesc.vertex.buffers = vertexLayouts;
    shadowPipelineDesc.fragment = nullptr;
    shadowPipelineDesc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    shadowPipelineDesc.primitive.cullMode = wgpu::CullMode::None;
    shadowPipelineDesc.depthStencil = &shadowDepthStencil;
    shadowPipelineDesc.multisample.count = 1;

    shadowPipeline_ = device_.CreateRenderPipeline(&shadowPipelineDesc);

    if (!shadowPipeline_) {
        std::cerr << "Failed to create shadow render pipeline.\n";
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
            ),
            std::clamp(prisms[i].alpha, 0.0f, 1.0f)
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

    const glm::vec3 sunDir =
        glm::normalize(glm::vec3(0.52f, 0.66f, -0.54f));

    glm::vec3 shadowMin = camera.position - glm::vec3(64.0f, 16.0f, 64.0f);
    glm::vec3 shadowMax = camera.position + glm::vec3(64.0f, 48.0f, 64.0f);

    if (!prisms.empty()) {
        shadowMin = prisms.front().position;
        shadowMax = prisms.front().position;

        for (const Prism& prism : prisms) {
            shadowMin = glm::min(shadowMin, prism.position);
            shadowMax = glm::max(shadowMax, prism.position);
        }

        // Include prism geometry radius/height plus extra room for tree tops
        // and for casters just outside the camera view. Keep this margin
        // moderate so the 1024 shadow map has enough detail for tree shadows.
        shadowMin -= glm::vec3(8.0f, 4.0f, 8.0f);
        shadowMax += glm::vec3(8.0f, 18.0f, 8.0f);
    }

    glm::vec3 shadowCenter = (shadowMin + shadowMax) * 0.5f;
    shadowCenter.y = std::max(shadowCenter.y, 8.0f);

    glm::vec3 lightUp(0.0f, 1.0f, 0.0f);
    if (std::abs(glm::dot(lightUp, sunDir)) > 0.92f) {
        lightUp = glm::vec3(0.0f, 0.0f, 1.0f);
    }

    const glm::vec3 lightForward = glm::normalize(-sunDir);
    const glm::vec3 lightRight = glm::normalize(glm::cross(lightForward, lightUp));
    const glm::vec3 lightTrueUp = glm::normalize(glm::cross(lightRight, lightForward));

    float maxProjectedRadius = 48.0f;

    const std::array<glm::vec3, 8> boundsCorners{
        glm::vec3(shadowMin.x, shadowMin.y, shadowMin.z),
        glm::vec3(shadowMin.x, shadowMin.y, shadowMax.z),
        glm::vec3(shadowMin.x, shadowMax.y, shadowMin.z),
        glm::vec3(shadowMin.x, shadowMax.y, shadowMax.z),
        glm::vec3(shadowMax.x, shadowMin.y, shadowMin.z),
        glm::vec3(shadowMax.x, shadowMin.y, shadowMax.z),
        glm::vec3(shadowMax.x, shadowMax.y, shadowMin.z),
        glm::vec3(shadowMax.x, shadowMax.y, shadowMax.z)
    };

    for (const glm::vec3& corner : boundsCorners) {
        const glm::vec3 relative = corner - shadowCenter;
        maxProjectedRadius = std::max(maxProjectedRadius, std::abs(glm::dot(relative, lightRight)));
        maxProjectedRadius = std::max(maxProjectedRadius, std::abs(glm::dot(relative, lightTrueUp)));
    }

    const float shadowHalfExtent = std::clamp(maxProjectedRadius + 8.0f, 48.0f, 140.0f);
    const float shadowDistance = shadowHalfExtent * 2.0f;
    const float shadowFarPlane = shadowHalfExtent * 4.0f;

    // Snap the bounds-based light camera center to shadow-map texel increments
    // in light space. Unlike Step 3J, this center does not continuously follow
    // the player camera. It only changes when the uploaded prism bounds change.
    const float shadowTexelWorldSize =
        (shadowHalfExtent * 2.0f) / static_cast<float>(ShadowMapSize);

    const float centerRight = glm::dot(shadowCenter, lightRight);
    const float centerUp = glm::dot(shadowCenter, lightTrueUp);
    const float snappedRight = std::floor(centerRight / shadowTexelWorldSize) * shadowTexelWorldSize;
    const float snappedUp = std::floor(centerUp / shadowTexelWorldSize) * shadowTexelWorldSize;

    shadowCenter +=
        lightRight * (snappedRight - centerRight) +
        lightTrueUp * (snappedUp - centerUp);

    glm::mat4 lightView = glm::lookAt(
        shadowCenter + sunDir * shadowDistance,
        shadowCenter,
        lightUp
    );

    glm::mat4 lightProjection = glm::ortho(
        -shadowHalfExtent,
        shadowHalfExtent,
        -shadowHalfExtent,
        shadowHalfExtent,
        0.1f,
        shadowFarPlane
    );
    lightProjection[1][1] *= -1.0f;

    FrameUniforms frameUniforms{};
    frameUniforms.viewProjection = proj * view;
    frameUniforms.lightViewProjection = lightProjection * lightView;
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

    wgpu::CommandEncoder encoder =
        device_.CreateCommandEncoder();

    if (!prisms.empty()) {
        wgpu::RenderPassDepthStencilAttachment shadowDepthAttachment{};
        shadowDepthAttachment.view = shadowDepthTextureView_;
        shadowDepthAttachment.depthLoadOp = wgpu::LoadOp::Clear;
        shadowDepthAttachment.depthStoreOp = wgpu::StoreOp::Store;
        shadowDepthAttachment.depthClearValue = 1.0f;

        wgpu::RenderPassDescriptor shadowPassDesc{};
        shadowPassDesc.colorAttachmentCount = 0;
        shadowPassDesc.colorAttachments = nullptr;
        shadowPassDesc.depthStencilAttachment = &shadowDepthAttachment;

        wgpu::RenderPassEncoder shadowPass =
            encoder.BeginRenderPass(&shadowPassDesc);

        shadowPass.SetPipeline(shadowPipeline_);
        shadowPass.SetBindGroup(0, shadowFrameBindGroup_);
        shadowPass.SetVertexBuffer(0, prismVertexBuffer_);
        shadowPass.SetVertexBuffer(1, prismInstanceBuffer_);
        shadowPass.SetIndexBuffer(prismIndexBuffer_, wgpu::IndexFormat::Uint16);
        shadowPass.DrawIndexed(
            static_cast<uint32_t>(prismMesh_.indices.size()),
            static_cast<uint32_t>(prisms.size())
        );
        shadowPass.End();
    }

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
