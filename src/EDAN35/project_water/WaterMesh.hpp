#pragma once
#include <vector>
#include <cstdint>

class WaterMesh {
public:
	WaterMesh(int n_render, float size);

	const std::vector<float>& vertexData() const { return m_vtx; } // pos(3)
	const std::vector<std::uint32_t>& indices() const { return m_idx; }

	int N() const { return m_n; }
	float size() const { return m_size; }

private:
	int   m_n;
	float m_size;

	std::vector<float> m_vtx;              // [x y z] * (n*n), y=0
	std::vector<std::uint32_t> m_idx;
};
