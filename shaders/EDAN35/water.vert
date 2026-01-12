#version 330 core

layout(location = 0) in vec3 aPos;

uniform mat4 world_to_clip;
uniform mat4 model_to_world;
uniform mat4 normal_to_world;

uniform sampler2D heightTex;

uniform float water_size;

uniform float sim_dx;

out vec3 vPosWS;
out vec3 vNrmWS;
out vec2 vUV;


float sampleHeight(vec2 uv)
{
    return texture(heightTex, uv).r;
}

void main()
{
    float half_size = 0.5 * water_size;
    vec2 uv = (aPos.xz + vec2(half_size)) / water_size;
	vUV = uv;

    // Displace the vertex in Y using the simulated height u(x,z).
    float h = sampleHeight(uv);
    vec3 posMS = vec3(aPos.x, h, aPos.z);

    vec4 pws = model_to_world * vec4(posMS, 1.0);

    // Normal from heightfield
    // Approximate du/dx and du/dz by sampling neighbor heights.
    ivec2 ts = textureSize(heightTex, 0);
    vec2 texel = 1.0 / vec2(ts); // 1 texel step in uv

    float hl = sampleHeight(uv - vec2(texel.x, 0.0));
    float hr = sampleHeight(uv + vec2(texel.x, 0.0));
    float hd = sampleHeight(uv - vec2(0.0, texel.y));
    float hu = sampleHeight(uv + vec2(0.0, texel.y));

    float du_dx = (hr - hl) / (2.0 * sim_dx);
    float du_dz = (hu - hd) / (2.0 * sim_dx);

    vec3 nrmMS = normalize(vec3(-du_dx, 1.0, -du_dz));
    vec3 nrmWS = normalize((normal_to_world * vec4(nrmMS, 0.0)).xyz);

    vPosWS = pws.xyz;
    vNrmWS = nrmWS;
    gl_Position = world_to_clip * pws;
}
