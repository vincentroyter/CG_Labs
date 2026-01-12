#include "WaterMesh.hpp"
#include <cstddef>

WaterMesh::WaterMesh(int n_render, float size)
	: m_n(n_render), m_size(size)
{
	m_vtx.resize(std::size_t(m_n) * std::size_t(m_n) * 3u, 0.0f);

	const float half = 0.5f * m_size;
	const float dx = m_size / float(m_n - 1);

	// Generate vertex positions
	for (int y = 0; y < m_n; ++y) {
		for (int x = 0; x < m_n; ++x) {
			const float px = -half + float(x) * dx;
			const float pz = -half + float(y) * dx;

			const std::size_t i = (std::size_t(y) * std::size_t(m_n) + std::size_t(x)) * 3u;
			m_vtx[i + 0] = px;
			m_vtx[i + 1] = 0.0f;
			m_vtx[i + 2] = pz;
		}
	}

	// Generate triangle indices
	m_idx.reserve(std::size_t(m_n - 1) * std::size_t(m_n - 1) * 6u);
	for (int y = 0; y < m_n - 1; ++y) {
		for (int x = 0; x < m_n - 1; ++x) {
			const std::uint32_t i0 = std::uint32_t(y * m_n + x);
			const std::uint32_t i1 = std::uint32_t(y * m_n + (x + 1));
			const std::uint32_t i2 = std::uint32_t((y + 1) * m_n + x);
			const std::uint32_t i3 = std::uint32_t((y + 1) * m_n + (x + 1));

			// Each grid cell becomes two triangles
			m_idx.push_back(i0); m_idx.push_back(i2); m_idx.push_back(i1);
			m_idx.push_back(i1); m_idx.push_back(i2); m_idx.push_back(i3);
		}
	}
}
