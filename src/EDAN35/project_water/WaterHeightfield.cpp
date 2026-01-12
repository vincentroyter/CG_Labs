#include "WaterHeightfield.hpp"
#include <algorithm>
#include <cmath>

// 2D curvature / Laplacian.
// The Müller-Fischer slides use this curvature to push v up/down.
float WaterHeightfield::laplacianU(int x, int y) const
{
	const float uC = m_u[idx(x, y)];
	return (m_u[idx(x - 1, y)] + m_u[idx(x + 1, y)] +
		m_u[idx(x, y - 1)] + m_u[idx(x, y + 1)] -
		4.0f * uC);
}

WaterHeightfield::WaterHeightfield(int n, float size)
	: m_n(n)
	, m_size(size)
	, m_dx(size / float(n - 1))
	, m_u(std::size_t(n)* std::size_t(n), 0.0f)
	, m_uNew(std::size_t(n)* std::size_t(n), 0.0f)
	, m_v(std::size_t(n)* std::size_t(n), 0.0f)
	, m_vNew(std::size_t(n)* std::size_t(n), 0.0f)
	, m_packedHV(std::size_t(n)* std::size_t(n) * 2, 0.0f)
	, m_gTop(n, 0.0f)
	, m_gBottom(n, 0.0f)
	, m_gLeft(n, 0.0f)
	, m_gRight(n, 0.0f)
{
}

void WaterHeightfield::reset()
{
	// Make everything flat and still
	std::fill(m_u.begin(), m_u.end(), 0.0f);
	std::fill(m_uNew.begin(), m_uNew.end(), 0.0f);
	std::fill(m_v.begin(), m_v.end(), 0.0f);
	std::fill(m_vNew.begin(), m_vNew.end(), 0.0f);
	std::fill(m_packedHV.begin(), m_packedHV.end(), 0.0f);

	// Reset open-boundary memory
	std::fill(m_gTop.begin(), m_gTop.end(), 0.0f);
	std::fill(m_gBottom.begin(), m_gBottom.end(), 0.0f);
	std::fill(m_gLeft.begin(), m_gLeft.end(), 0.0f);
	std::fill(m_gRight.begin(), m_gRight.end(), 0.0f);
}

void WaterHeightfield::updatePackedHV()
{
	// Pack height + velocity into one array
	const int N = m_n;
	const int count = N * N;

	if ((int)m_packedHV.size() != count * 2) {
		m_packedHV.resize(count * 2);
	}

	for (int i = 0; i < count; ++i) {
		m_packedHV[2 * i + 0] = m_u[i];
		m_packedHV[2 * i + 1] = m_v[i];
	}
}

void WaterHeightfield::update(float dt)
{
	// Avoid giant dt spikes
	dt = std::min(dt, 1.0f / 30.0f);

	if (dt <= 0.0f) {
		updatePackedHV();
		return;
	}

	// CFL stability limit from the heightfield wave equation idea:
	// dt < h / c
	const float h = m_dx;
	const float c = std::max(0.01f, m_c);

	const float dt_max = 0.25f * (h / (c * 1.41421356f));

	int steps = (dt > 0.0f) ? int(std::ceil(dt / dt_max)) : 1;
	steps = std::clamp(steps, 1, 16);

	const float dt_sub = dt / float(steps);
	for (int s = 0; s < steps; ++s) {
		step(dt_sub);
	}

	updatePackedHV();
}

void WaterHeightfield::step(float dt)
{
	// Core "column simulation step":
	// curvature -> acceleration -> update velocity -> update height
	//
	// f      = c^2 * lap(u) / h^2
	// v      = v + f * dt
	// u_new  = u + v * dt
	//

	const float h = m_dx;
	const float h2 = h * h;
	const float c2 = m_c * m_c;

	std::copy(m_u.begin(), m_u.end(), m_uNew.begin());

	// 1) Main wave update
	for (int y = 1; y < m_n - 1; ++y) {
		for (int x = 1; x < m_n - 1; ++x) {
			const std::size_t i = idx(x, y);

			const float lap = laplacianU(x, y);
			const float f = c2 * lap / h2;

			// Push velocity by curvature
			m_v[i] += f * dt;

			// Simple damping
			m_v[i] *= std::exp(-m_velDampPerSec * dt);

			// Hard clamp to prevent explosions
			m_v[i] = std::clamp(m_v[i], -m_vMax, m_vMax);

			// Integrate height
			m_uNew[i] = m_u[i] + m_v[i] * dt;
		}
	}

	// 2) viscosity
	// Set the viscosity of the simulation, good to suppress a weird shimmer
	if (m_viscosity > 0.0f) {
		const float nu = m_viscosity;
		const float invH2 = 1.0f / h2;

		std::copy(m_v.begin(), m_v.end(), m_vNew.begin());

		for (int y = 1; y < m_n - 1; ++y) {
			for (int x = 1; x < m_n - 1; ++x) {
				const std::size_t i = idx(x, y);

				const float vC = m_v[i];
				const float lapV =
					(m_v[idx(x - 1, y)] + m_v[idx(x + 1, y)] +
						m_v[idx(x, y - 1)] + m_v[idx(x, y + 1)] -
						4.0f * vC);

				m_vNew[i] = vC + (nu * lapV * dt * invH2);
			}
		}

		m_v.swap(m_vNew);
	}

	// 3) Commit height update
	m_u.swap(m_uNew);

	// 4) Slope limiter
	if (m_maxSlope > 0.0f) {
		const float maxDu = m_maxSlope * m_dx;
		const float maxDu2 = maxDu * maxDu;

		std::copy(m_u.begin(), m_u.end(), m_uNew.begin());

		const float kappa = 0.35f;

		for (int y = 1; y < m_n - 1; ++y) {
			for (int x = 1; x < m_n - 1; ++x) {
				const std::size_t i = idx(x, y);

				const float uC = m_u[i];
				const float uL = m_u[idx(x - 1, y)];
				const float uR = m_u[idx(x + 1, y)];
				const float uD = m_u[idx(x, y - 1)];
				const float uU = m_u[idx(x, y + 1)];

				const float dxu = 0.5f * (uR - uL);
				const float dyu = 0.5f * (uU - uD);
				const float g2 = dxu * dxu + dyu * dyu;

				if (g2 > maxDu2) {
					const float lapU = (uL + uR + uD + uU - 4.0f * uC);

					float excess = (std::sqrt(g2) - maxDu) / (maxDu + 1e-6f);
					excess = std::clamp(excess, 0.0f, 1.0f);

					m_uNew[i] = uC + (kappa * excess) * lapU;

					// Also damp velocity locally so crests don't instantly re-explode.
					float velDampLocal = 1.0f - 0.5f * (kappa * excess);
					velDampLocal = std::clamp(velDampLocal, 0.2f, 1.0f);
					m_v[i] *= velDampLocal;
				}
			}
		}

		m_u.swap(m_uNew);
	}

	// 5) Boundaries
	applyBoundaries(dt);

	// 6) Keep water level centered
	if (m_lockWaterLevel) {
		removeMeanHeight();
	}
}

void WaterHeightfield::applyBoundaries(float dt)
{
	if (m_openBoundary) {
		// Open boundary idea from the slides:
		// use a "ghost column" memory g so waves can leave without reflecting as much.
		const float h = m_dx;
		const float a = m_c * dt;
		const float denom = h + a;
		if (denom <= 0.0f) return;

		for (int x = 1; x < m_n - 1; ++x) {
			float u_in = m_u[idx(x, 1)];
			float gnew = (a * u_in + h * m_gTop[x]) / denom;
			m_gTop[x] = gnew;
			m_u[idx(x, 0)] = gnew;
		}

		for (int x = 1; x < m_n - 1; ++x) {
			float u_in = m_u[idx(x, m_n - 2)];
			float gnew = (a * u_in + h * m_gBottom[x]) / denom;
			m_gBottom[x] = gnew;
			m_u[idx(x, m_n - 1)] = gnew;
		}

		for (int y = 1; y < m_n - 1; ++y) {
			float u_in = m_u[idx(1, y)];
			float gnew = (a * u_in + h * m_gLeft[y]) / denom;
			m_gLeft[y] = gnew;
			m_u[idx(0, y)] = gnew;
		}

		for (int y = 1; y < m_n - 1; ++y) {
			float u_in = m_u[idx(m_n - 2, y)];
			float gnew = (a * u_in + h * m_gRight[y]) / denom;
			m_gRight[y] = gnew;
			m_u[idx(m_n - 1, y)] = gnew;
		}

		auto cornerBlend = [&](int x, int y, float& gA, float uA, float& gB, float uB) {
			float gnewA = (a * uA + h * gA) / denom;
			float gnewB = (a * uB + h * gB) / denom;
			float gnew = 0.5f * (gnewA + gnewB);
			gA = gnew; gB = gnew;
			m_u[idx(x, y)] = gnew;
			};

		cornerBlend(0, 0,
			m_gTop[0], m_u[idx(0, 1)],
			m_gLeft[0], m_u[idx(1, 0)]);

		cornerBlend(m_n - 1, 0,
			m_gTop[m_n - 1], m_u[idx(m_n - 1, 1)],
			m_gRight[0], m_u[idx(m_n - 2, 0)]);

		cornerBlend(0, m_n - 1,
			m_gBottom[0], m_u[idx(0, m_n - 2)],
			m_gLeft[m_n - 1], m_u[idx(1, m_n - 1)]);

		cornerBlend(m_n - 1, m_n - 1,
			m_gBottom[m_n - 1], m_u[idx(m_n - 1, m_n - 2)],
			m_gRight[m_n - 1], m_u[idx(m_n - 2, m_n - 1)]);

	}
	else {
		// Closed boundary: reflect height, kill velocity
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
}

void WaterHeightfield::removeMeanHeight()
{
	// Removes global height offset
	// Without this, tiny numerical drift can slowly "raise" or "lower" the whole surface.
	//
	// If open boundaries are enabled. Also shift the ghost memory arrays so they don't
	// re-introduce the offset.
	double sum = 0.0;
	int count = 0;

	for (int y = 0; y < m_n; ++y) {
		for (int x = 0; x < m_n; ++x) {
			sum += m_u[idx(x, y)];
			++count;
		}
	}
	if (count == 0) return;

	const float mean = float(sum / double(count));

	for (int y = 0; y < m_n; ++y) {
		for (int x = 0; x < m_n; ++x) {
			m_u[idx(x, y)] -= mean;
		}
	}

	if (m_openBoundary) {
		for (float& g : m_gTop)    g -= mean;
		for (float& g : m_gBottom) g -= mean;
		for (float& g : m_gLeft)   g -= mean;
		for (float& g : m_gRight)  g -= mean;
	}
}

void WaterHeightfield::pullPointTargetHeight(
	int x, int y, float dt, float targetU, float widthCells, float k, float d)
{
	// Pull towards a target height at a point
	if (x < 1 || x >= m_n - 1 || y < 1 || y >= m_n - 1) return;

	widthCells = std::max(0.5f, widthCells);

	const float sigma = std::max(0.5f, widthCells * 0.45f);
	const float inv2Sigma2 = 1.0f / (2.0f * sigma * sigma);

	// Only touch a limited neighborhood
	const float reach = 3.0f * sigma;
	const float reach2 = reach * reach;

	int r = int(std::ceil(reach + 1.0f));
	int xmin = std::clamp(x - r, 1, m_n - 2);
	int xmax = std::clamp(x + r, 1, m_n - 2);
	int ymin = std::clamp(y - r, 1, m_n - 2);
	int ymax = std::clamp(y + r, 1, m_n - 2);

	for (int yy = ymin; yy <= ymax; ++yy) {
		for (int xx = xmin; xx <= xmax; ++xx) {
			float dx = float(xx - x);
			float dy = float(yy - y);
			float r2 = dx * dx + dy * dy;

			if (r2 > reach2) continue;

			float w = std::exp(-r2 * inv2Sigma2);

			std::size_t i = idx(xx, yy);

			float du = (targetU - m_u[i]);
			float a = (k * du) - (d * m_v[i]);
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
	// Same pull as above, but distributed along a line segment
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
