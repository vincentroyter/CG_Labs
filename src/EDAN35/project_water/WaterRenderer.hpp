#pragma once
#include <vector>
#include <cstdint>
#include <glad/glad.h>

// All shader/visual toggles collected in one place.
// This includes the existing "node" effect.
struct WaterVisualParams
{
	// Existing node effect (keep!)
	bool  showNodes = false;
	float nodeEps = 0.01f;  // threshold in height units
	float nodeStrength = 0.5f;   // 0..1-ish

	// New effects (we'll implement later, but params are ready)
	bool  useHeightColoring = false;

	bool  enableSpecular = true;
	float specularStrength = 0.15f;
	float specularPower = 64.0f;

	// 0=Ocean, 1=Thermal, 2=Psychedelic
	int   colorTheme = 0;

	// Velocity visualization
	bool  velocityEnabled = false;
	float velocityScale = 1.0f;
	float velocityThreshold = 0.0f;
	float velocityStrength = 1.0f;
	float velocityColor[3] = { 0.6f, 0.95f, 1.0f };

	// First derivative (slope) visualization
	bool  slopeEnabled = false;
	float slopeScale = 1.0f;
	float slopeThreshold = 0.5f;
	float slopeStrength = 1.0f;
	float slopeColor[3] = { 0.92f, 0.98f, 1.0f };

	// Second derivative (curvature) visualization
	bool  curvatureEnabled = false;
	float curvatureScale = 1.0f;
	float curvatureThreshold = 0.5f;
	float curvatureStrength = 1.0f;
	float curvatureColor[3] = { 1.0f, 0.6f, 0.2f };

};

class WaterRenderer {
public:
	WaterRenderer();
	~WaterRenderer();

	void setMesh(const std::vector<float>& vertexData,
		const std::vector<std::uint32_t>& indices);

	// Call every frame (upload sim heightfield as float texture)
	void updateHeightTexture(const std::vector<float>& packedHV, int N);


	void render(GLuint shaderProgram,
		const float* world_to_clip,
		const float* model_to_world,
		const float* normal_to_world,
		const float* light_dir_ws,
		const float* camera_pos_ws,
		float water_size,
		float sim_dx,
		const WaterVisualParams& vis);


	void setHeightFiltering(bool nearest);


private:
	GLuint m_vao = 0, m_vbo = 0, m_ebo = 0;
	int    m_indexCount = 0;

	GLuint m_heightTex = 0;
	int    m_heightN = 0;
};
