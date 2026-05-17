#include "WebGpuRenderer.hpp"

#include "Settings.hpp"

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
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace {

std::string wgslFloat(float value) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(6) << value;

    return stream.str();
}

std::string wgslColor(Settings::Color3 color) {
    return
        "vec3f(" +
        wgslFloat(color.r) + ", " +
        wgslFloat(color.g) + ", " +
        wgslFloat(color.b) + ")";
}

void replaceAll(
    std::string& text,
    const std::string& token,
    const std::string& replacement
) {
    std::size_t position = 0;

    while ((position = text.find(token, position)) != std::string::npos) {
        text.replace(position, token.length(), replacement);
        position += replacement.length();
    }
}

} // namespace

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
    std::string wgsl = R"(
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
    @location(4) alpha : f32,
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

const pi : f32 = 3.14159265359;
const twoPi : f32 = 6.28318530718;
const dayNightCycleSeconds : f32 = {{DAY_NIGHT_CYCLE_SECONDS}};
const dayNightStartPhase : f32 = {{DAY_NIGHT_START_PHASE}};

fn dayCyclePhase() -> f32 {
    return fract(
        dayNightStartPhase +
        frame.cameraForwardAndTime.w / dayNightCycleSeconds
    );
}

fn sunDirectionForPhase(phase : f32) -> vec3f {
    let angle = phase * twoPi;

    return normalize(vec3f(
        cos(angle) * 0.78,
        sin(angle),
        -0.54
    ));
}

fn sunDirection() -> vec3f {
    return sunDirectionForPhase(dayCyclePhase());
}

fn moonDirection(sunDir : vec3f) -> vec3f {
    return normalize(-sunDir);
}

fn daylightAmount(sunDir : vec3f) -> f32 {
    return smoothstep(-0.06, 0.22, sunDir.y);
}

fn twilightAmount(sunDir : vec3f) -> f32 {
    let nearHorizon = 1.0 - smoothstep(0.04, 0.42, abs(sunDir.y));

    return nearHorizon * smoothstep(-0.28, 0.10, sunDir.y);
}

fn sunLightColor(sunDir : vec3f) -> vec3f {
    let highSun = smoothstep(0.18, 0.86, sunDir.y);

    return mix(sunriseSunColor(), {{HIGH_SUN_COLOR}}, highSun) *
        daylightAmount(sunDir);
}

fn moonlightAmount(moonDir : vec3f, day : f32) -> f32 {
    return smoothstep(0.02, 0.36, moonDir.y) * (1.0 - day);
}

fn moonLightColor(moonDir : vec3f, day : f32) -> vec3f {
    return {{MOON_LIGHT_COLOR}} * moonlightAmount(moonDir, day);
}

fn sunriseSunColor() -> vec3f {
    return {{SUNRISE_SUN_COLOR}};
}

fn sunriseSkyAmbientColor() -> vec3f {
    return {{SUNRISE_SKY_AMBIENT_COLOR}};
}

fn daySkyAmbientColor() -> vec3f {
    return {{DAY_SKY_AMBIENT_COLOR}};
}

fn nightSkyAmbientColor() -> vec3f {
    return {{NIGHT_SKY_AMBIENT_COLOR}};
}

fn sunriseHorizonColor() -> vec3f {
    return {{SUNRISE_HORIZON_COLOR}};
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

    for (var i = 0; i < 3; i = i + 1) {
        total = total + valueNoise(q) * amplitude;
        normalization = normalization + amplitude;
        q = q * 2.03 + vec2f(17.2, 11.7);
        amplitude = amplitude * 0.52;
    }

    return total / normalization;
}
)";

    wgsl += R"(

fn rotate2D(p : vec2f, angle : f32) -> vec2f {
    let c = cos(angle);
    let s = sin(angle);

    return vec2f(
        p.x * c - p.y * s,
        p.x * s + p.y * c
    );
}

fn starLayer(uv : vec2f, scale : f32, seed : f32) -> f32 {
    let p = uv * scale + vec2f(seed, seed * 1.37);
    let baseCell = floor(p);
    var result = 0.0;

    for (var y = -1; y <= 1; y = y + 1) {
        for (var x = -1; x <= 1; x = x + 1) {
            let cell = baseCell + vec2f(f32(x), f32(y));
            let randomPoint = vec2f(
                hash2(cell + vec2f(seed + 19.17, 3.11)),
                hash2(cell + vec2f(5.73, seed + 41.29))
            );
            let densityRoll = hash2(cell + vec2f(seed + 97.23, seed - 61.71));
            let starPresence = smoothstep({{STAR_DENSITY_THRESHOLD}}, 1.0, densityRoll);
            let sizeRoll = hash2(cell + vec2f(seed - 13.37, seed + 83.91));
            let brightnessRoll = hash2(cell + vec2f(seed + 31.41, seed + 11.17));
            let radius = mix({{STAR_MIN_RADIUS}}, {{STAR_MAX_RADIUS}}, sizeRoll * sizeRoll);
            let starCenter = cell + randomPoint;
            let distanceToStar = length(p - starCenter);
            let normalizedDistance = distanceToStar / radius;
            let disk = exp(-normalizedDistance * normalizedDistance * 4.5);

            result = max(
                result,
                disk * starPresence * mix(0.55, 1.0, brightnessRoll)
            );
        }
    }

    return result;
}

fn starField(ray : vec3f, night : f32, moonAmount : f32, moonDisk : f32) -> vec3f {
    let domeFade = smoothstep(-0.02, 0.18, ray.y);
    let moonFade = 1.0 - smoothstep(0.90, 0.998, moonAmount) * 0.42;
    let projection = ray.xz / max(ray.y + 0.38, 0.20);
    let uvA = rotate2D(projection, 0.41);
    let uvB = rotate2D(projection * 1.17 + vec2f(7.3, -3.8), -0.73);

    let coolStars = starLayer(uvA, 42.0, 12.7);
    let warmStars = starLayer(uvB, 28.0, 49.3);

    return (
        {{STAR_COOL_COLOR}} * coolStars +
        {{STAR_WARM_COLOR}} * warmStars * 0.58
    ) * {{STAR_BRIGHTNESS}} * night * domeFade * moonFade * (1.0 - moonDisk * 0.85);
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
    let moonDir = moonDirection(sunDir);
    let sunAmount = saturate(dot(ray, sunDir));
    let moonAmount = saturate(dot(ray, moonDir));
    let day = daylightAmount(sunDir);
    let twilight = twilightAmount(sunDir);
    let night = 1.0 - day;
    let moonStrength = moonlightAmount(moonDir, day);
    let vertical = saturate(ray.y);
    let horizonHaze = exp(-abs(ray.y) * 7.0);

    let dayHorizon = mix({{DAY_HORIZON_COLOR}}, sunriseHorizonColor(), twilight);
    let dayZenith = mix({{DAY_ZENITH_COLOR}}, {{TWILIGHT_ZENITH_COLOR}}, twilight);
    let nightHorizon = {{NIGHT_HORIZON_COLOR}};
    let nightZenith = {{NIGHT_ZENITH_COLOR}};

    var sky = mix(
        mix(nightHorizon, dayHorizon, day),
        mix(nightZenith, dayZenith, day),
        saturate(ray.y * 1.30 + 0.25)
    );
    sky = mix(sky, nightZenith, pow(vertical, 1.35) * night * 0.72);
    sky = mix(sky, sunriseHorizonColor(), horizonHaze * twilight * 0.52);

    let warmScatter =
        pow(sunAmount, 8.0) * 0.18 +
        pow(sunAmount, 36.0) * 0.42;
    sky = sky + sunLightColor(sunDir) * warmScatter;
    sky = mix(sky, sunLightColor(sunDir), smoothstep(0.9991, 1.0, sunAmount) * day);

    let moonDisk = smoothstep(0.9988, 0.9997, moonAmount) * moonStrength;
    let moonHalo = pow(moonAmount, 48.0) * moonStrength;
    sky =
        sky +
        {{MOON_HALO_COLOR}} * moonHalo * 0.32;
    sky = mix(
        sky,
        {{MOON_DISK_COLOR}},
        moonDisk
    );

    sky = sky + starField(ray, night, moonAmount, moonDisk);

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
    let cloudLit = 0.42 + 0.58 * max(sunAmount, twilight * 0.55);
    let cloudColor = mix(
        mix({{CLOUD_NIGHT_COLOR}}, {{CLOUD_TWILIGHT_COLOR}}, day),
        mix({{CLOUD_DAY_COLOR}}, {{CLOUD_SUNSET_COLOR}}, twilight),
        cloudLit
    );
    sky = mix(
        sky,
        cloudColor,
        cloudMask * mix(0.26, 0.62, saturate(day + twilight * 0.35))
    );

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

    let rotatedNormal = vec3f(
        uprightNormal.x * c + uprightNormal.z * s,
        uprightNormal.y,
        -uprightNormal.x * s + uprightNormal.z * c
    );

    let worldPosition = rotatedPosition + input.positionAndCos.xyz;

    out.position = frame.viewProjection * vec4f(worldPosition, 1.0);
    out.normal = rotatedNormal;
    out.color = vec4f(input.colorAndSin.rgb, input.alpha);

    return out;
}

@fragment
fn fsMain(input : VertexOut) -> @location(0) vec4f {
    let n = input.normal;
    let isWater = input.color.a < 0.999;
    let lightDir = sunDirection();
    let moonDir = moonDirection(lightDir);
    let day = daylightAmount(lightDir);
    let twilight = twilightAmount(lightDir);
    let moonStrength = moonlightAmount(moonDir, day);

    if (isWater) {
        if (n.y < 0.75) {
            discard;
        }

        let waterSunGlow =
            smoothstep(0.25, 0.95, dot(normalize(frame.cameraForwardAndTime.xyz), lightDir));
        let waterMoonGlow =
            smoothstep(0.55, 0.95, dot(normalize(frame.cameraForwardAndTime.xyz), moonDir)) *
            moonStrength;
        let waterSkyTint =
            mix({{WATER_NIGHT_TINT}}, {{WATER_DAY_TINT}}, day);
        let waterColor =
            input.color.rgb * waterSkyTint +
            sunLightColor(lightDir) * (0.08 + waterSunGlow * 0.12) +
            moonLightColor(moonDir, day) * (0.22 + waterMoonGlow * 0.34) +
            sunriseHorizonColor() * twilight * 0.06;

        return vec4f(clamp(waterColor, vec3f(0.0), vec3f(1.0)), input.color.a);
    }

    let diffuse = pow(saturate(dot(n, lightDir)), 1.15);
    let moonDiffuse = pow(saturate(dot(n, moonDir)), 1.35);
    let skyFacing = saturate(n.y * 0.5 + 0.5);
    let horizonFacing = pow(saturate(dot(n, normalize(vec3f(lightDir.x, 0.0, lightDir.z)))), 2.0);
    let upwardWarmth = smoothstep(0.20, 1.0, n.y);

    let ambientColor =
        mix(nightSkyAmbientColor(), daySkyAmbientColor(), day);
    let ambientLight =
        ambientColor * (0.28 + skyFacing * 0.38) +
        sunriseSkyAmbientColor() * twilight * (0.20 + skyFacing * 0.18) +
        sunriseHorizonColor() * horizonFacing * twilight * 0.16;
    let directLight =
        sunLightColor(lightDir) * diffuse * (0.92 + upwardWarmth * 0.22);
    let moonLight =
        moonLightColor(moonDir, day) * moonDiffuse * (0.36 + skyFacing * 0.28);

    let finalColor =
        input.color.rgb * (ambientLight + moonLight + directLight);

    return vec4f(finalColor, input.color.a);
}
)";

    replaceAll(
        wgsl,
        "{{DAY_NIGHT_CYCLE_SECONDS}}",
        wgslFloat(Settings::Time::dayNightCycleSeconds)
    );
    replaceAll(
        wgsl,
        "{{DAY_NIGHT_START_PHASE}}",
        wgslFloat(Settings::Time::dayNightStartPhase)
    );
    replaceAll(
        wgsl,
        "{{SUNRISE_SUN_COLOR}}",
        wgslColor(Settings::Lighting::sunriseSun)
    );
    replaceAll(
        wgsl,
        "{{HIGH_SUN_COLOR}}",
        wgslColor(Settings::Lighting::highSun)
    );
    replaceAll(
        wgsl,
        "{{SUNRISE_SKY_AMBIENT_COLOR}}",
        wgslColor(Settings::Lighting::sunriseSkyAmbient)
    );
    replaceAll(
        wgsl,
        "{{DAY_SKY_AMBIENT_COLOR}}",
        wgslColor(Settings::Lighting::daySkyAmbient)
    );
    replaceAll(
        wgsl,
        "{{NIGHT_SKY_AMBIENT_COLOR}}",
        wgslColor(Settings::Lighting::nightSkyAmbient)
    );
    replaceAll(
        wgsl,
        "{{SUNRISE_HORIZON_COLOR}}",
        wgslColor(Settings::Lighting::sunriseHorizon)
    );
    replaceAll(
        wgsl,
        "{{MOON_LIGHT_COLOR}}",
        wgslColor(Settings::Lighting::moonLight)
    );
    replaceAll(
        wgsl,
        "{{DAY_HORIZON_COLOR}}",
        wgslColor(Settings::Lighting::dayHorizon)
    );
    replaceAll(
        wgsl,
        "{{DAY_ZENITH_COLOR}}",
        wgslColor(Settings::Lighting::dayZenith)
    );
    replaceAll(
        wgsl,
        "{{TWILIGHT_ZENITH_COLOR}}",
        wgslColor(Settings::Lighting::twilightZenith)
    );
    replaceAll(
        wgsl,
        "{{NIGHT_HORIZON_COLOR}}",
        wgslColor(Settings::Lighting::nightHorizon)
    );
    replaceAll(
        wgsl,
        "{{NIGHT_ZENITH_COLOR}}",
        wgslColor(Settings::Lighting::nightZenith)
    );
    replaceAll(
        wgsl,
        "{{MOON_HALO_COLOR}}",
        wgslColor(Settings::Lighting::moonHalo)
    );
    replaceAll(
        wgsl,
        "{{MOON_DISK_COLOR}}",
        wgslColor(Settings::Lighting::moonDisk)
    );
    replaceAll(
        wgsl,
        "{{STAR_COOL_COLOR}}",
        wgslColor(Settings::Lighting::starCool)
    );
    replaceAll(
        wgsl,
        "{{STAR_WARM_COLOR}}",
        wgslColor(Settings::Lighting::starWarm)
    );
    replaceAll(
        wgsl,
        "{{STAR_BRIGHTNESS}}",
        wgslFloat(Settings::Lighting::starBrightness)
    );
    replaceAll(
        wgsl,
        "{{STAR_DENSITY_THRESHOLD}}",
        wgslFloat(Settings::Lighting::starDensityThreshold)
    );
    replaceAll(
        wgsl,
        "{{STAR_MIN_RADIUS}}",
        wgslFloat(Settings::Lighting::starMinRadius)
    );
    replaceAll(
        wgsl,
        "{{STAR_MAX_RADIUS}}",
        wgslFloat(Settings::Lighting::starMaxRadius)
    );
    replaceAll(
        wgsl,
        "{{CLOUD_NIGHT_COLOR}}",
        wgslColor(Settings::Lighting::cloudNight)
    );
    replaceAll(
        wgsl,
        "{{CLOUD_TWILIGHT_COLOR}}",
        wgslColor(Settings::Lighting::cloudTwilight)
    );
    replaceAll(
        wgsl,
        "{{CLOUD_DAY_COLOR}}",
        wgslColor(Settings::Lighting::cloudDay)
    );
    replaceAll(
        wgsl,
        "{{CLOUD_SUNSET_COLOR}}",
        wgslColor(Settings::Lighting::cloudSunset)
    );
    replaceAll(
        wgsl,
        "{{WATER_NIGHT_TINT}}",
        wgslColor(Settings::Lighting::waterNightTint)
    );
    replaceAll(
        wgsl,
        "{{WATER_DAY_TINT}}",
        wgslColor(Settings::Lighting::waterDayTint)
    );

    wgpu::ShaderSourceWGSL source{};
    source.code = wgsl.c_str();

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

    wgpu::BindGroupLayoutDescriptor bindLayoutDesc{};
    bindLayoutDesc.entryCount = 1;
    bindLayoutDesc.entries = &uniformBindEntry;

    frameBindGroupLayout_ =
        device_.CreateBindGroupLayout(&bindLayoutDesc);

    if (!frameBindGroupLayout_) {
        std::cerr << "Failed to create main frame bind group layout.\n";
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
        std::cerr << "Failed to create frame uniform buffer.\n";
        return false;
    }

    wgpu::BindGroupEntry uniformBindGroupEntry{};
    uniformBindGroupEntry.binding = 0;
    uniformBindGroupEntry.buffer = frameUniformBuffer_;
    uniformBindGroupEntry.offset = 0;
    uniformBindGroupEntry.size = sizeof(FrameUniforms);

    wgpu::BindGroupDescriptor frameBindDesc{};
    frameBindDesc.layout = frameBindGroupLayout_;
    frameBindDesc.entryCount = 1;
    frameBindDesc.entries = &uniformBindGroupEntry;

    frameBindGroup_ =
        device_.CreateBindGroup(&frameBindDesc);

    if (!frameBindGroup_) {
        std::cerr << "Failed to create main frame bind group.\n";
        return false;
    }

    wgpu::PipelineLayoutDescriptor pipelineLayoutDesc{};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &frameBindGroupLayout_;

    pipelineLayout_ =
        device_.CreatePipelineLayout(&pipelineLayoutDesc);

    if (!pipelineLayout_) {
        std::cerr << "Failed to create main pipeline layout.\n";
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
    glm::vec3 cameraUp = getCameraUp(camera);
    glm::vec3 cameraRight = glm::normalize(glm::cross(forward, cameraUp));
    const float currentTimeSeconds =
        static_cast<float>(SDL_GetTicks()) * 0.001f;

    glm::mat4 view =
        glm::lookAt(
            camera.position,
            camera.position + forward,
            cameraUp
        );

    constexpr float verticalFovDegrees = Settings::Camera::verticalFovDegrees;
    constexpr float cameraNearClipDistance = Settings::Camera::nearClipDistance;
    constexpr float cameraFarClipDistance = Settings::Camera::farClipDistance;
    float aspect =
        static_cast<float>(surfaceConfig_.width) /
        static_cast<float>(surfaceConfig_.height);

    glm::mat4 proj =
        glm::perspective(
            glm::radians(verticalFovDegrees),
            aspect,
            cameraNearClipDistance,
            cameraFarClipDistance
        );

    proj[1][1] *= -1.0f;

    FrameUniforms frameUniforms{};
    frameUniforms.viewProjection = proj * view;
    frameUniforms.cameraRightAndAspect =
        glm::vec4(cameraRight, aspect);
    frameUniforms.cameraUpAndTanHalfFov =
        glm::vec4(
            cameraUp,
            std::tan(glm::radians(verticalFovDegrees) * 0.5f)
        );
    frameUniforms.cameraForwardAndTime =
        glm::vec4(
            forward,
            currentTimeSeconds
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
