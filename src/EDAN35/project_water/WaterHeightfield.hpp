#pragma once
#include <vector>
#include <cstddef>
#include <algorithm>

class WaterHeightfield {
public:
	WaterHeightfield(int n, float size);

	void update(float dt);

	int   N()    const { return m_n; }
	float size() const { return m_size; }
	float dx()   const { return m_dx; }

	float height(int x, int y) const { return m_u[idx(x, y)]; }
	const std::vector<float>& heights()    const { return m_u; }
	const std::vector<float>& velocities() const { return m_v; }
	const std::vector<float>& packedHV()   const { return m_packedHV; }

	// UI parameter accessors

	float waveSpeed() const { return m_c; }
	void  setWaveSpeed(float c) { m_c = std::max(0.01f, c); }

	float velDamp() const { return m_velDampPerSec; }
	void  setVelDamp(float g) { m_velDampPerSec = std::max(0.0f, g); }

	bool  lockWaterLevel() const { return m_lockWaterLevel; }
	void  setLockWaterLevel(bool on) { m_lockWaterLevel = on; }

	float viscosity() const { return m_viscosity; }
	void  setViscosity(float nu) { m_viscosity = std::clamp(nu, 0.0f, 1.0f); }

	float maxSlope() const { return m_maxSlope; }
	void  setMaxSlope(float s) { m_maxSlope = std::max(0.0f, s); }

	bool isOpenBoundary() const { return m_openBoundary; }
	void setOpenBoundary(bool enabled) {
		if (m_openBoundary != enabled) {
			m_openBoundary = enabled;
			// Reset ghost memory when we toggle between modes.
			std::fill(m_gTop.begin(), m_gTop.end(), 0.0f);
			std::fill(m_gBottom.begin(), m_gBottom.end(), 0.0f);
			std::fill(m_gLeft.begin(), m_gLeft.end(), 0.0f);
			std::fill(m_gRight.begin(), m_gRight.end(), 0.0f);
		}
	}

	// Helpers for "drivers", used to excite the surface
	void pullPointTargetHeight(int x, int y, float dt, float targetU, float widthCells, float k, float d);

	// Same idea but along a segment/line.
	void pullSegmentTargetHeight(
		float cx, float cy,
		float dirx, float diry,
		float lengthCells,
		float dt,
		float targetU,
		float widthCells,
		float k, float d
	);

	// Reset water surface
	void reset();

private:
	int   m_n;
	float m_size;
	float m_dx;

	// Wave params
	float m_c = 1.2f;
	float m_velDampPerSec = 1.0f;
	float m_maxSlope = 1.5f;
	bool  m_openBoundary = false;

	float m_viscosity = 0.0f;

	// Hard safety clamp
	float m_vMax = 6.0f;

	// State
	std::vector<float> m_u;        // height
	std::vector<float> m_uNew;     // scratch buffer
	std::vector<float> m_v;        // vertical velocity
	std::vector<float> m_vNew;     // scratch buffer
	std::vector<float> m_packedHV; // interleaved for GPU upload [h0,v0,h1,v1,...]

	// Ghost memory for open boundaries.
	std::vector<float> m_gTop, m_gBottom, m_gLeft, m_gRight;

	std::size_t idx(int x, int y) const { return std::size_t(y) * std::size_t(m_n) + std::size_t(x); }

	// One stable substep (used by update() when dt is too large).
	void step(float dt);

	// Applies either closed or open boundary rules.
	void applyBoundaries(float dt);

	// 2D curvature (Laplacian) of u.
	float laplacianU(int x, int y) const;

	// Removes slow drift in the average height (keeps water level centered).
	bool m_lockWaterLevel = true;
	void removeMeanHeight();

	// Packs u and v into one array for easy upload to the GPU.
	void updatePackedHV();
};
