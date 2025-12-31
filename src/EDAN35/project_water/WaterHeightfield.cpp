#include "WaterHeightfield.hpp"
#include <algorithm>
#include <cmath>

// 2D curvature / Laplacian
// (u[i+1,j] + u[i-1,j] + u[i,j+1] + u[i,j-1] - 4u[i,j]) / h^2
float WaterHeightfield::laplacianU(int x, int y) const
{
	const float uC = m_u[idx(x, y)];
	return  (m_u[idx(x - 1, y)] + m_u[idx(x + 1, y)] +
		m_u[idx(x, y - 1)] + m_u[idx(x, y + 1)] -
		4.0f * uC);
}

void WaterHeightfield::reset()
{
	std::fill(m_u.begin(), m_u.end(), 0.0f);
	std::fill(m_v.begin(), m_v.end(), 0.0f);
}

WaterHeightfield::WaterHeightfield(int n, float size)
	: m_n(n)                                   // grid resolution (Nsim)
	, m_size(size)                              // physical width/depth
	, m_dx(size / float(n - 1))                 // h in the PDF (grid spacing)
	, m_u(std::size_t(n)* std::size_t(n), 0.0f) // u[i,j] in the PDF (height)
	, m_v(std::size_t(n)* std::size_t(n), 0.0f) // v[i,j] in the PDF (velocity)
{
}


void WaterHeightfield::update(float dt)
{
	// Avoid giant dt spikes
	dt = std::min(dt, 1.0f / 30.0f);

	// CFL condition: dt < h / c  (use a conservative safety factor)
	const float h = m_dx;
	const float dt_max = 0.25f * (h / m_c);

	int steps = (dt > 0.0f) ? int(std::ceil(dt / dt_max)) : 1;
	steps = std::clamp(steps, 1, 16);

	const float dt_sub = dt / float(steps);
	for (int s = 0; s < steps; ++s)
		step(dt_sub);
}

void WaterHeightfield::step(float dt)
{
	// --- Notation matching the reference PDF ------------------------------
	//   h  = grid spacing
	//   f  = c^2 * lap(u) / h^2
	//   v  = v + f*dt
	//   u' = u + v*dt
	const float h = m_dx;
	const float h2 = h * h;
	const float c2 = m_c * m_c;

	// Store u' (unew) explicitly like the to make it easier to follow the reference
	// Note: We only update interior cells; boundaries are handled separately.
	std::vector<float> u_new = m_u;

	// --- 1) Column Simulation Step -----------------------------------
	// forall i,j:
	//   f      = c^2 * lap(u) / h^2
	//   v[i,j] = v[i,j] + f * dt
	//   u_new  = u + v * dt
	for (int y = 1; y < m_n - 1; ++y) {
		for (int x = 1; x < m_n - 1; ++x) {

			const std::size_t i = idx(x, y);

			// 2D Curvature
			const float lap = laplacianU(x, y);

			// Force term:
			const float f = c2 * lap / h2;

			// v = v + f * dt
			m_v[i] += f * dt;

			// dt-correct damping: v *= exp(-gamma * dt)
			m_v[i] *= std::exp(-m_velDampPerSec * dt);


			// u_new = u + v * dt
			u_new[i] = m_u[i] + m_v[i] * dt;
		}
	}

	// --- 3) Commit update (u = unew) -----------------------------------
	m_u.swap(u_new);

	// --- 4) Boundary conditions ---------------------------------------
	applyBoundaries();

	if (m_lockWaterLevel)
		removeMeanHeight();
}

void WaterHeightfield::applyBoundaries()
{
	// Here we implement a reflective boundary:
	// copy neighbor height and zero velocity at the border.
	for (int x = 0; x < m_n; ++x) {
		m_u[idx(x, 0)] = m_u[idx(x, 1)];
		m_u[idx(x, m_n - 1)] = m_u[idx(x, m_n - 2)];
		m_v[idx(x, 0)] = 0.0f;
		m_v[idx(x, m_n - 1)] = 0.0f;
	}
	for (int y = 0; y < m_n; ++y) {
		m_u[idx(0, y)] = m_u[idx(1, y)];
		m_u[idx(m_n - 1, y)] = m_u[idx(m_n - 2, y)];
		m_v[idx(0, y)] = 0.0f;
		m_v[idx(m_n - 1, y)] = 0.0f;
	}
}

void WaterHeightfield::disturb(int x, int y, float magnitude, int radius)
{
	// Ignore clicks outside the simulation
	if (x < 1 || x >= m_n - 1 || y < 1 || y >= m_n - 1)
		return;

	/*
	  Apply a localized impulse to the velocity field.

	  We modify velocity v (not height u) so the interaction
	  behaves like a physical impact: momentum is added and
	  height responds smoothly over time via u += v*dt.
	*/

	// Gaussian kernel: smooth, radially symmetric footprint
	// Used to distribute the impulse over nearby cells
	const float sigma = radius * 0.6f;
	const float inv2Sigma2 = 1.0f / (2.0f * sigma * sigma);

	for (int oy = -radius; oy <= radius; ++oy) {
		for (int ox = -radius; ox <= radius; ++ox) {

			int xi = x + ox;
			int yi = y + oy;

			if (xi < 1 || xi >= m_n - 1 || yi < 1 || yi >= m_n - 1)
				continue;

			float r2 = float(ox * ox + oy * oy);
			float w = std::exp(-r2 * inv2Sigma2);

			// Inject momentum into velocity
			m_v[idx(xi, yi)] += magnitude * w;
		}
	}
}

void WaterHeightfield::removeMeanHeight()
{
	/*
	  Remove global height offset (DC component).

	  Repeated impulses + numerical errors can slowly shift the
	  average water level up or down. This function recenters
	  the surface so that the mean height stays at zero.

	  This does NOT affect wave shape or propagation, only the
	  absolute vertical offset of the whole surface.
	*/

	double sum = 0.0;
	int count = 0;

	// Compute mean height over interior cells
	// (boundaries are constrained and should not participate)
	for (int y = 1; y < m_n - 1; ++y) {
		for (int x = 1; x < m_n - 1; ++x) {
			sum += m_u[idx(x, y)];
			++count;
		}
	}

	if (count == 0)
		return;

	const float mean = float(sum / double(count));

	// Subtract mean from all interior cells to lock the water plane in place
	for (int y = 1; y < m_n - 1; ++y) {
		for (int x = 1; x < m_n - 1; ++x) {
			m_u[idx(x, y)] -= mean;
		}
	}
}

static constexpr float PI = 3.14159265359f;

void WaterHeightfield::pullPointTargetHeight(
	int x, int y, float dt, float targetU, float widthCells, float k, float d)
{
	if (x < 1 || x >= m_n - 1 || y < 1 || y >= m_n - 1) return;

	float sigma = std::max(0.5f, widthCells * 0.6f);
	float inv2Sigma2 = 1.0f / (2.0f * sigma * sigma);

	int r = int(std::ceil(3.0f * widthCells + 2.0f));
	int xmin = std::clamp(x - r, 1, m_n - 2);
	int xmax = std::clamp(x + r, 1, m_n - 2);
	int ymin = std::clamp(y - r, 1, m_n - 2);
	int ymax = std::clamp(y + r, 1, m_n - 2);

	for (int yy = ymin; yy <= ymax; ++yy) {
		for (int xx = xmin; xx <= xmax; ++xx) {
			float dx = float(xx - x);
			float dy = float(yy - y);
			float w = std::exp(-(dx * dx + dy * dy) * inv2Sigma2);

			std::size_t i = idx(xx, yy);

			float du = (targetU - m_u[i]);
			float a = (k * du) - (d * m_v[i]);   // spring + damping
			m_v[i] += a * dt * w;
		}
	}
}

void WaterHeightfield::pullSegmentTargetHeight(
	float cx, float cy,
	float dirx, float diry,
	float lengthCells,
	float dt,
	float targetU,
	float widthCells,
	float k, float d)
{
	float L = std::sqrt(dirx * dirx + diry * diry);
	if (L < 1e-6f) { dirx = 1.0f; diry = 0.0f; L = 1.0f; }
	dirx /= L; diry /= L;

	float halfLen = 0.5f * std::max(0.0f, lengthCells);

	float sigma = std::max(0.5f, widthCells * 0.6f);
	float inv2Sigma2 = 1.0f / (2.0f * sigma * sigma);

	float reach = halfLen + 3.0f * widthCells + 2.0f;
	int r = int(std::ceil(reach));

	int xmin = std::clamp(int(std::floor(cx)) - r, 1, m_n - 2);
	int xmax = std::clamp(int(std::floor(cx)) + r, 1, m_n - 2);
	int ymin = std::clamp(int(std::floor(cy)) - r, 1, m_n - 2);
	int ymax = std::clamp(int(std::floor(cy)) + r, 1, m_n - 2);

	for (int y = ymin; y <= ymax; ++y) {
		for (int x = xmin; x <= xmax; ++x) {

			float px = float(x) - cx;
			float py = float(y) - cy;

			float along = px * dirx + py * diry;
			float alongClamped = std::clamp(along, -halfLen, halfLen);

			float qx = alongClamped * dirx;
			float qy = alongClamped * diry;

			float dx = px - qx;
			float dy = py - qy;
			float dist2 = dx * dx + dy * dy;

			float w = std::exp(-dist2 * inv2Sigma2);

			std::size_t i = idx(x, y);
			float du = (targetU - m_u[i]);
			float a = (k * du) - (d * m_v[i]);
			m_v[i] += a * dt * w;
		}
	}
}















