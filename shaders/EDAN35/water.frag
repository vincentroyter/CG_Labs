#version 330 core

in vec3 vPosWS;
in vec3 vNrmWS;
in vec2 vUV;

// Heightfield texture
// RG32F texture:
// R = height (u)
// G = vertical velocity (v)
uniform sampler2D heightTex;

// Node / zero-crossing visualization
uniform bool  u_showNodes;
uniform float u_nodeEps;
uniform float u_nodeStrength;

// Lighting
uniform vec3 light_dir_ws;
uniform vec3 camera_pos_ws;

// Height-based coloring
uniform bool  u_useHeightColoring;
uniform int   u_colorTheme;   // 0 = Ocean, 1 = Thermal, 2 = Psychedelic

// Specular lighting
uniform bool  u_enableSpecular;
uniform float u_specularStrength;
uniform float u_specularPower;

// Foam effect
uniform bool  u_enableFoam;
uniform float u_foamThreshold;

// Velocity visualization
uniform bool  u_velocityColoring;

// Derivative visualizers
uniform bool  u_velocityEnabled;
uniform float u_velocityScale;
uniform float u_velocityThreshold;
uniform float u_velocityStrength;
uniform vec3  u_velocityColor;

uniform bool  u_slopeEnabled;
uniform float u_slopeScale;
uniform float u_slopeThreshold;
uniform float u_slopeStrength;
uniform vec3  u_slopeColor;

uniform bool  u_curvatureEnabled;
uniform float u_curvatureScale;
uniform float u_curvatureThreshold;
uniform float u_curvatureStrength;
uniform vec3  u_curvatureColor;



out vec4 FragColor;

// ------------------------------------------------------------
// Helper functions
// ------------------------------------------------------------

float sampleHeight(vec2 uv)
{
    return texture(heightTex, uv).r;
}

float curvatureMag(vec2 uv)
{
    ivec2 ts = textureSize(heightTex, 0);
    vec2 texel = 1.0 / vec2(ts);

    float c  = sampleHeight(uv);
    float hl = sampleHeight(uv - vec2(texel.x, 0.0));
    float hr = sampleHeight(uv + vec2(texel.x, 0.0));
    float hd = sampleHeight(uv - vec2(0.0, texel.y));
    float hu = sampleHeight(uv + vec2(0.0, texel.y));

    float lap = (hl + hr + hd + hu - 4.0 * c);
    return abs(lap);
}


// Convert HSV color to RGB
vec3 hsv2rgb(vec3 c)
{
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

// Height-based color themes
vec3 themeColorFromHeight(float h, int theme)
{
    float range = 0.25;
    float t = clamp(0.5 + 0.5 * (h / range), 0.0, 1.0);

    if (theme == 0) {
        vec3 deep  = vec3(0.02, 0.10, 0.25);
        vec3 mid   = vec3(0.05, 0.35, 0.60);
        vec3 crest = vec3(0.85, 0.95, 1.00);
        vec3 a = mix(deep, mid, smoothstep(0.0, 0.7, t));
        return mix(a, crest, smoothstep(0.7, 1.0, t));
    }
    else if (theme == 1) {
        vec3 cold  = vec3(0.05, 0.00, 0.10);
        vec3 mid1  = vec3(0.25, 0.00, 0.35);
        vec3 mid2  = vec3(0.85, 0.25, 0.05);
        vec3 hot   = vec3(1.00, 0.95, 0.80);
        vec3 a = mix(cold, mid1, smoothstep(0.0, 0.4, t));
        vec3 b = mix(mid1, mid2, smoothstep(0.3, 0.75, t));
        return mix(b, hot, smoothstep(0.75, 1.0, t));
    }
    else {
        float hue = fract(0.85 * t + 0.15);
        return hsv2rgb(vec3(hue, 0.85, 0.95));
    }
}

// ------------------------------------------------------------
// Main
// ------------------------------------------------------------
void main()
{
    // Sample height and vertical velocity
    vec2 hv = texture(heightTex, vUV).rg;
    float h = hv.r;
    float v = hv.g;

    // Lighting vectors
    vec3 N = normalize(vNrmWS);
    vec3 L = normalize(-light_dir_ws);
    vec3 V = normalize(camera_pos_ws - vPosWS);

    float ndotl = max(dot(N, L), 0.0);

    // Base color
    vec3 base;
    if (u_useHeightColoring) {
        base = themeColorFromHeight(h, u_colorTheme);
    } else {
        base = vec3(0.02, 0.30, 0.55);
    }

    // Diffuse lighting
    vec3 col = base * (0.15 + 0.85 * ndotl);

    // Specular lighting
    if (u_enableSpecular) {
        vec3 H = normalize(L + V);
        float spec = pow(max(dot(N, H), 0.0), max(u_specularPower, 1.0));
        col += vec3(0.7) * spec * u_specularStrength;
    }

	// Velocity tint (abs vertical velocity)
	if (u_velocityEnabled) {
		float m = abs(v) * u_velocityScale;
		float x = clamp(m - u_velocityThreshold, 0.0, 1.0);
		x = smoothstep(0.0, 1.0, x);
		col = mix(col, u_velocityColor, u_velocityStrength * x);
	}

	// Slope tint (first derivative magnitude)
	if (u_slopeEnabled) {
		float slope = sqrt(max(0.0, 1.0 - N.y * N.y));
		float m = slope * u_slopeScale;
		float x = clamp(m - u_slopeThreshold, 0.0, 1.0);
		x = smoothstep(0.0, 1.0, x);
		col = mix(col, u_slopeColor, u_slopeStrength * x);
	}

	// Curvature tint (second derivative magnitude)
	if (u_curvatureEnabled) {
		float curv = curvatureMag(vUV);
		float m = curv * u_curvatureScale;
		float x = clamp(m - u_curvatureThreshold, 0.0, 1.0);
		x = smoothstep(0.0, 1.0, x);
		col = mix(col, u_curvatureColor, u_curvatureStrength * x);
	}



    // Node visualization (darken near height zero)
    if (u_showNodes) {
        float node = 1.0 - smoothstep(0.0, u_nodeEps, abs(h));
        col *= (1.0 - u_nodeStrength * node);
    }

    FragColor = vec4(col, 1.0);
}
