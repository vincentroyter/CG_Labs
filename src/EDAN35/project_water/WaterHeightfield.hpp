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

	// --- UI parameter accessors ---
	float waveSpeed() const { return m_c; }
	void  setWaveSpeed(float c) { m_c = c; }

	float velDamp() const { return m_velDamp; }
	void  setVelDamp(float d) { m_velDamp = d; }

	float maxSlope() const { return m_maxSlope; }
	void  setMaxSlope(float s) { m_maxSlope = s; }



private:
    int   m_n;
    float m_size;
    float m_dx;

	// Wave params
	float m_c = 1.2f;     // wave speed in world units / s
	float m_velDamp = 0.995f;   // per-substep velocity damping
	float m_maxSlope = 0.6f;     // clamp strength (height per meter-ish)

	// State
	std::vector<float> m_u;      // height
	std::vector<float> m_v;      // vertical velocity


    std::size_t idx(int x, int y) const { return std::size_t(y) * std::size_t(m_n) + std::size_t(x); }

    void step(float dt);        // one stable substep
    void applyBoundaries();     // clamp/reflect boundary
	float laplacianU(int x, int y) const;

	bool m_lockWaterLevel = true;
	void removeMeanHeight();

};
