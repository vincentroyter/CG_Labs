#version 330 core

in vec3 vPosWS;
in vec3 vNrmWS;

uniform vec3 light_dir_ws;
uniform vec3 camera_pos_ws;

out vec4 FragColor;

void main()
{
    // Basic lighting to make waves readable.
    vec3 N = normalize(vNrmWS);
    vec3 L = normalize(-light_dir_ws);
    vec3 V = normalize(camera_pos_ws - vPosWS);

    float ndotl = max(dot(N, L), 0.0);

    // Simple Blinn-Phong specular.
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 64.0);

    vec3 base = vec3(0.02, 0.30, 0.55);
    vec3 col  = base * (0.15 + 0.85 * ndotl) + vec3(0.7) * spec * 0.15;

    FragColor = vec4(col, 1.0);
}
