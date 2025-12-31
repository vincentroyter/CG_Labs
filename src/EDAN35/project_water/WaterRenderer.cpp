#include "WaterRenderer.hpp"
#include <algorithm>

WaterRenderer::WaterRenderer()
{
	// Create GPU objects:
	// VAO = stores vertex layout/state
	// VBO = vertex buffer (positions from WaterMesh)
	// EBO = index buffer (triangles from WaterMesh)
	glGenVertexArrays(1, &m_vao);
	glGenBuffers(1, &m_vbo);
	glGenBuffers(1, &m_ebo);

	glBindVertexArray(m_vao);

	glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);

	// Vertex layout matches the vertex shader:
	// layout(location=0) in vec3 aPos;
	// Note: mesh is flat and static; shader will displace Y using heightTex.
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(
		0, 3, GL_FLOAT, GL_FALSE,
		3 * sizeof(float),
		(void*)0
	);

	glBindVertexArray(0);

	// Height texture (stores simulation u[i,j] as a float grid)
	// We sample this in the vertex shader to displace the mesh and compute normals.
	glGenTextures(1, &m_heightTex);
	glBindTexture(GL_TEXTURE_2D, m_heightTex);

	// Filtering:
	// - LINEAR = smooth interpolation between simulation cells
	// - NEAREST = shows raw grid
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	// Clamp to edge avoids sampling outside [0,1] when we take neighbor samples for normals.
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glBindTexture(GL_TEXTURE_2D, 0);
}

WaterRenderer::~WaterRenderer()
{
	// Cleanup GPU resources
	glDeleteTextures(1, &m_heightTex);
	glDeleteBuffers(1, &m_ebo);
	glDeleteBuffers(1, &m_vbo);
	glDeleteVertexArrays(1, &m_vao);
}

void WaterRenderer::setMesh(const std::vector<float>& vertexData,
	const std::vector<std::uint32_t>& indices)
{
	// Upload static WaterMesh:
	// - positions only (flat grid)
	// - indices define triangles
	glBindVertexArray(m_vao);

	glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
	glBufferData(GL_ARRAY_BUFFER,
		vertexData.size() * sizeof(float),
		vertexData.data(),
		GL_STATIC_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER,
		indices.size() * sizeof(std::uint32_t),
		indices.data(),
		GL_STATIC_DRAW);

	m_indexCount = (int)indices.size();

	glBindVertexArray(0);
}

void WaterRenderer::updateHeightTexture(const std::vector<float>& heights, int N)
{
	// Upload simulation heightfield u as an NxN (Nsim) float texture each frame.
	// This is the CPU to GPU bridge: WaterHeightfield (CPU) to shader (GPU).
	if (N <= 0) return;

	glBindTexture(GL_TEXTURE_2D, m_heightTex);

	// If resolution changed (Nsim changed), reallocate texture storage.
	if (m_heightN != N) {
		m_heightN = N;
		glTexImage2D(GL_TEXTURE_2D, 0,
			GL_R32F, N, N, 0,
			GL_RED, GL_FLOAT,
			heights.data());
	}
	else {
		// Same resolution: update texels only.
		glTexSubImage2D(GL_TEXTURE_2D, 0,
			0, 0, N, N,
			GL_RED, GL_FLOAT,
			heights.data());
	}

	glBindTexture(GL_TEXTURE_2D, 0);
}

void WaterRenderer::setHeightFiltering(bool nearest)
{
	// GUI available toggle between NEAREST (blocky) and LINEAR (smooth).
	glBindTexture(GL_TEXTURE_2D, m_heightTex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
	glBindTexture(GL_TEXTURE_2D, 0);
}

void WaterRenderer::render(GLuint shaderProgram,
	const float* world_to_clip,
	const float* model_to_world,
	const float* normal_to_world,
	const float* light_dir_ws,
	const float* camera_pos_ws,
	float water_size,
	float sim_dx,
	bool showNodes,
	float nodeEps,
	float nodeStrength)
{
	// Bind shader and update uniforms.
	glUseProgram(shaderProgram);

	glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "world_to_clip"), 1, GL_FALSE, world_to_clip);
	glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model_to_world"), 1, GL_FALSE, model_to_world);
	glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "normal_to_world"), 1, GL_FALSE, normal_to_world);

	glUniform3fv(glGetUniformLocation(shaderProgram, "light_dir_ws"), 1, light_dir_ws);
	glUniform3fv(glGetUniformLocation(shaderProgram, "camera_pos_ws"), 1, camera_pos_ws);

	glUniform1f(glGetUniformLocation(shaderProgram, "water_size"), water_size);
	glUniform1f(glGetUniformLocation(shaderProgram, "sim_dx"), sim_dx);

	// Bind height texture to texture unit 0 and point sampler to it.
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, m_heightTex);
	glUniform1i(glGetUniformLocation(shaderProgram, "heightTex"), 0);

	glUniform1i(glGetUniformLocation(shaderProgram, "u_showNodes"), showNodes ? 1 : 0);
	glUniform1f(glGetUniformLocation(shaderProgram, "u_nodeEps"), nodeEps);
	glUniform1f(glGetUniformLocation(shaderProgram, "u_nodeStrength"), nodeStrength);


	// Draw static grid (WaterMesh). Vertex shader displaces it into water surface.
	glBindVertexArray(m_vao);
	glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
	glBindVertexArray(0);

	glBindTexture(GL_TEXTURE_2D, 0);
	glUseProgram(0);
}
