#pragma once

namespace Settings {

struct Color3 {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

namespace Terrain {
    constexpr int seed = 6564;

    constexpr int renderRadius = 500;

    constexpr int rebuildStride = 16;
    constexpr int minHeight = 1;
    constexpr int maxHeight = 256;
}

namespace Camera {
    constexpr float verticalFovDegrees = 45.0f;
    constexpr float nearClipDistance = 0.1f;

    // Measured in world units, not hex cells. This only controls clipping of
    // already-generated terrain; it does not load terrain by itself.
    constexpr float farClipDistance = 100.0f;
}

namespace Time {
    constexpr float dayNightCycleSeconds = 180.0f;
    constexpr float dayNightStartPhase = 2.0f;
}

namespace Lighting {
    constexpr Color3 sunriseSun{ 1.0f, 0.56f, 0.26f };
    constexpr Color3 highSun{ 1.0f, 0.92f, 0.76f };
    constexpr Color3 sunriseSkyAmbient{ 0.16f, 0.20f, 0.36f };
    constexpr Color3 daySkyAmbient{ 0.38f, 0.48f, 0.68f };
    constexpr Color3 nightSkyAmbient{ 0.070f, 0.090f, 0.170f };
    constexpr Color3 sunriseHorizon{ 0.95f, 0.34f, 0.16f };
    constexpr Color3 moonLight{ 0.46f, 0.56f, 0.92f };
    constexpr Color3 dayHorizon{ 0.82f, 0.76f, 0.70f };
    constexpr Color3 dayZenith{ 0.20f, 0.34f, 0.72f };
    constexpr Color3 twilightZenith{ 0.08f, 0.18f, 0.50f };
    constexpr Color3 nightHorizon{ 0.075f, 0.090f, 0.180f };
    constexpr Color3 nightZenith{ 0.018f, 0.026f, 0.075f };
    constexpr Color3 moonHalo{ 0.56f, 0.64f, 1.0f };
    constexpr Color3 moonDisk{ 0.86f, 0.90f, 1.0f };
    constexpr Color3 starCool{ 0.68f, 0.76f, 1.0f };
    constexpr Color3 starWarm{ 1.0f, 0.86f, 0.62f };
    constexpr float starBrightness = 1.35f;
    constexpr float starDensityThreshold = 0.985f;
    constexpr float starMinRadius = 0.060f;
    constexpr float starMaxRadius = 0.120f;
    constexpr Color3 cloudNight{ 0.10f, 0.12f, 0.20f };
    constexpr Color3 cloudTwilight{ 0.50f, 0.45f, 0.58f };
    constexpr Color3 cloudDay{ 0.86f, 0.88f, 0.92f };
    constexpr Color3 cloudSunset{ 1.0f, 0.72f, 0.46f };
    constexpr Color3 waterNightTint{ 0.16f, 0.19f, 0.32f };
    constexpr Color3 waterDayTint{ 0.62f, 0.72f, 0.88f };
}

} // namespace Settings
