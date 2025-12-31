#pragma once
#include <vector>
#include <cstdint>
#include <glad/glad.h>

class WaterRenderer {
public:
	WaterRenderer();
	~WaterRenderer();

	void setMesh(const std::vector<float>& vertexData,
		const std::vector<std::uint32_t>& indices);

	// Call every frame (upload sim heightfield as float texture)
	void updateHeightTexture(const std::vector<float>& heights, int N);

	void render(GLuint shaderProgram,
		const float* world_to_clip,
		const float* model_to_world,
		const float* normal_to_world,
		const float* light_dir_ws,
		const float* camera_pos_ws,
		float water_size,
		float sim_dx,
		bool showNodes,
		float nodeEps,
		float nodeStrength);

	void setHeightFiltering(bool nearest);


private:
	GLuint m_vao = 0, m_vbo = 0, m_ebo = 0;
	int    m_indexCount = 0;

	GLuint m_heightTex = 0;
	int    m_heightN = 0;
};
