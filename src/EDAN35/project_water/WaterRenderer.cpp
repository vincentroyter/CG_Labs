#include "WaterRenderer.hpp"

#include <algorithm>
#include <vector>
#include <cstdint>


// Helper: upload all visual params as uniforms
static void setWaterVisualUniforms(GLuint program, const WaterVisualParams& vis)
{
	{
		GLint loc = glGetUniformLocation(program, "u_showNodes");
		if (loc >= 0) glUniform1i(loc, vis.showNodes ? 1 : 0);

		loc = glGetUniformLocation(program, "u_nodeEps");
		if (loc >= 0) glUniform1f(loc, vis.nodeEps);

		loc = glGetUniformLocation(program, "u_nodeStrength");
		if (loc >= 0) glUniform1f(loc, vis.nodeStrength);
	}

	{
		auto set1i = [&](const char* name, bool b) {
			GLint loc = glGetUniformLocation(program, name);
			if (loc >= 0) glUniform1i(loc, b ? 1 : 0);
			};
		auto set1f = [&](const char* name, float f) {
			GLint loc = glGetUniformLocation(program, name);
			if (loc >= 0) glUniform1f(loc, f);
			};
		auto setInt = [&](const char* name, int v) {
			GLint loc = glGetUniformLocation(program, name);
			if (loc >= 0) glUniform1i(loc, v);
			};

		auto set3f = [&](const char* name, const float rgb[3]) {
			GLint loc = glGetUniformLocation(program, name);
			if (loc >= 0) glUniform3f(loc, rgb[0], rgb[1], rgb[2]);
			};

		set1i("u_velocityEnabled", vis.velocityEnabled);
		set1f("u_velocityScale", vis.velocityScale);
		set1f("u_velocityThreshold", vis.velocityThreshold);
		set1f("u_velocityStrength", vis.velocityStrength);
		set3f("u_velocityColor", vis.velocityColor);

		set1i("u_slopeEnabled", vis.slopeEnabled);
		set1f("u_slopeScale", vis.slopeScale);
		set1f("u_slopeThreshold", vis.slopeThreshold);
		set1f("u_slopeStrength", vis.slopeStrength);
		set3f("u_slopeColor", vis.slopeColor);

		set1i("u_curvatureEnabled", vis.curvatureEnabled);
		set1f("u_curvatureScale", vis.curvatureScale);
		set1f("u_curvatureThreshold", vis.curvatureThreshold);
		set1f("u_curvatureStrength", vis.curvatureStrength);
		set3f("u_curvatureColor", vis.curvatureColor);


		set1i("u_useHeightColoring", vis.useHeightColoring);

		set1i("u_enableSpecular", vis.enableSpecular);
		set1f("u_specularStrength", vis.specularStrength);
		set1f("u_specularPower", vis.specularPower);

		setInt("u_colorTheme", vis.colorTheme);
	}
}

WaterRenderer::WaterRenderer()
{
	glGenVertexArrays(1, &m_vao);
	glGenBuffers(1, &m_vbo);
	glGenBuffers(1, &m_ebo);

	glBindVertexArray(m_vao);

	glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(
		0, 3, GL_FLOAT, GL_FALSE,
		3 * sizeof(float),
		(void*)0
	);

	glBindVertexArray(0);

	// Height/velocity texture
	glGenTextures(1, &m_heightTex);
	glBindTexture(GL_TEXTURE_2D, m_heightTex);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glBindTexture(GL_TEXTURE_2D, 0);
}

WaterRenderer::~WaterRenderer()
{
	glDeleteTextures(1, &m_heightTex);
	glDeleteBuffers(1, &m_ebo);
	glDeleteBuffers(1, &m_vbo);
	glDeleteVertexArrays(1, &m_vao);
}

void WaterRenderer::setMesh(const std::vector<float>& vertexData,
	const std::vector<std::uint32_t>& indices)
{
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

	m_indexCount = static_cast<int>(indices.size());

	glBindVertexArray(0);
}

void WaterRenderer::updateHeightTexture(const std::vector<float>& packedHV, int N)
{
	if (N <= 0) return;

	glBindTexture(GL_TEXTURE_2D, m_heightTex);

	if (m_heightN != N) {
		m_heightN = N;
		glTexImage2D(GL_TEXTURE_2D, 0,
			GL_RG32F, N, N, 0,
			GL_RG, GL_FLOAT,
			packedHV.data());
	}
	else {
		glTexSubImage2D(GL_TEXTURE_2D, 0,
			0, 0, N, N,
			GL_RG, GL_FLOAT,
			packedHV.data());
	}

	glBindTexture(GL_TEXTURE_2D, 0);
}

void WaterRenderer::setHeightFiltering(bool nearest)
{
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
	const WaterVisualParams& vis)
{
	if (shaderProgram == 0u || m_vao == 0u || m_indexCount <= 0)
		return;

	glUseProgram(shaderProgram);

	glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "world_to_clip"), 1, GL_FALSE, world_to_clip);
	glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model_to_world"), 1, GL_FALSE, model_to_world);
	glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "normal_to_world"), 1, GL_FALSE, normal_to_world);

	glUniform3fv(glGetUniformLocation(shaderProgram, "light_dir_ws"), 1, light_dir_ws);
	glUniform3fv(glGetUniformLocation(shaderProgram, "camera_pos_ws"), 1, camera_pos_ws);

	glUniform1f(glGetUniformLocation(shaderProgram, "water_size"), water_size);
	glUniform1f(glGetUniformLocation(shaderProgram, "sim_dx"), sim_dx);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, m_heightTex);

	{
		const GLint heightLoc = glGetUniformLocation(shaderProgram, "heightTex");
		if (heightLoc >= 0) glUniform1i(heightLoc, 0);
	}

	// Visual params
	setWaterVisualUniforms(shaderProgram, vis);

	glBindVertexArray(m_vao);
	glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, (void*)0);
	glBindVertexArray(0);

	glBindTexture(GL_TEXTURE_2D, 0);
	glUseProgram(0);
}
