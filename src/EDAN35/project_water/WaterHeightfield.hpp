#pragma once
#include <vector>
#include <cstddef>

class WaterHeightfield {
public:
    WaterHeightfield(int n, float size);

    // Advance simulation by dt seconds
    void update(float dt);

    // Add an impulse at grid cell (x,y)
    void disturb(int x, int y, float magnitude, int radius = 2);

    int   N()    const { return m_n; }
    float size() const { return m_size; }
    float dx()   const { return m_dx; }

    // Access for mesh update
	float height(int x, int y) const { return m_u[idx(x, y)]; }
	const std::vector<float>& heights() const { return m_u; }
	const std::vector<float>& velocities() const { return m_v; }
	const std::vector<float>& packedHV() const { return m_packedHV; }



	// --- UI parameter accessors ---
	float waveSpeed() const { return m_c; }
	void  setWaveSpeed(float c) { m_c = c; }
	void setLockWaterLevel(bool on) { m_lockWaterLevel = on; }


	float velDamp() const { return m_velDampPerSec; }
	void  setVelDamp(float g) { m_velDampPerSec = g; }

	float viscosity() const { return m_viscosity; }
	void  setViscosity(float g) { m_viscosity = g; }

	void setHeightDiffusion(float a) { m_heightDiffusion = a; }
	float heightDiffusion() const { return m_heightDiffusion; }


	float maxSlope() const { return m_maxSlope; }
	void  setMaxSlope(float s) { m_maxSlope = s; }
	void setOpenBoundary(bool enabled) {
		if (m_openBoundary != enabled) {
			m_openBoundary = enabled;
			std::fill(m_gTop.begin(), m_gTop.end(), 0.0f);
			std::fill(m_gBottom.begin(), m_gBottom.end(), 0.0f);
			std::fill(m_gLeft.begin(), m_gLeft.end(), 0.0f);
			std::fill(m_gRight.begin(), m_gRight.end(), 0.0f);
		}
	}

	bool isOpenBoundary() const { return m_openBoundary; }



void pullPointTargetHeight(int x, int y, float dt, float targetU, float widthCells, float k, float d);
void pullSegmentTargetHeight(
    float cx, float cy,
    float dirx, float diry,
    float lengthCells,
    float dt,
    float targetU,
    float widthCells,
    float k, float d);

void reset();






private:
    int   m_n;
    float m_size;
    float m_dx;

	// Wave params
	float m_c = 1.2f;     // wave speed in world units / s
	float m_velDampPerSec = 1.0f; // damping rate gamma [1/s]  (tune 0..10)
	float m_maxSlope = 1.5f;     // clamp strength (height per meter-ish)
	bool m_openBoundary = false;
	float m_viscosity = 0.0f;
	float m_heightDiffusion = 0.0f; // 0..0.02 nice



	float m_vMax = 6.0f; // hard clamp on velocity (tune 2..10)



	// State
	std::vector<float> m_u;      // height
	std::vector<float> m_uNew;   // height buffer for next step (reused, avoids allocations)
	std::vector<float> m_v;      // vertical velocity
	std::vector<float> m_vNew;
	std::vector<float> m_packedHV; // interleaved [h0,v0,h1,v1,...]



	std::vector<float> m_gTop, m_gBottom, m_gLeft, m_gRight;



    std::size_t idx(int x, int y) const { return std::size_t(y) * std::size_t(m_n) + std::size_t(x); }

    void step(float dt);        // one stable substep
	void applyBoundaries(float dt);
	float laplacianU(int x, int y) const;

	bool m_lockWaterLevel = true;
	void removeMeanHeight();
	void updatePackedHV();


};
