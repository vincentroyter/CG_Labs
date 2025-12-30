#version 330 core

// Static grid vertex (flat plane)
layout(location = 0) in vec3 aPos;

uniform mat4 world_to_clip;
uniform mat4 model_to_world;
uniform mat4 normal_to_world;

// Heightfield from CPU simulation.
// heightTex stores u[i,j] as a float texture (GL_R32F).
uniform sampler2D heightTex;

// Used to map mesh xz in [-size/2, +size/2] to uv in [0,1].
uniform float water_size;

// Used to scale slope to normal correctly (derivative in world units).
uniform float sim_dx;

out vec3 vPosWS;
out vec3 vNrmWS;

float sampleHeight(vec2 uv)
{
    return texture(heightTex, uv).r;
}

void main()
{
    // Map model-space xz in [-size/2, +size/2] to texture uv in [0,1].
    float half_size = 0.5 * water_size;
    vec2 uv = (aPos.xz + vec2(half_size)) / water_size;

    // Displace the vertex in Y using the simulated height u(x,z).
    float h = sampleHeight(uv);
    vec3 posMS = vec3(aPos.x, h, aPos.z);

    // Transform displaced position to world and clip space.
    vec4 pws = model_to_world * vec4(posMS, 1.0);

    // Normal from heightfield (central differences).
    // Approximate du/dx and du/dz by sampling neighbor heights.
    ivec2 ts = textureSize(heightTex, 0);
    vec2 texel = 1.0 / vec2(ts); // 1 texel step in uv

    float hl = sampleHeight(uv - vec2(texel.x, 0.0));
    float hr = sampleHeight(uv + vec2(texel.x, 0.0));
    float hd = sampleHeight(uv - vec2(0.0, texel.y));
    float hu = sampleHeight(uv + vec2(0.0, texel.y));

    // Convert texture differences to world-space slope using sim_dx.
    float ddx = (hr - hl) / (2.0 * sim_dx);
    float ddz = (hu - hd) / (2.0 * sim_dx);

    // Surface normal for y = u(x,z): n ~ (-du/dx, 1, -du/dz).
    vec3 nrmMS = normalize(vec3(-ddx, 1.0, -ddz));
    vec3 nrmWS = normalize((normal_to_world * vec4(nrmMS, 0.0)).xyz);

    vPosWS = pws.xyz;
    vNrmWS = nrmWS;
    gl_Position = world_to_clip * pws;
}
