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
	std::fill(m_uNew.begin(), m_uNew.end(), 0.0f);
	std::fill(m_v.begin(), m_v.end(), 0.0f);
	std::fill(m_packedHV.begin(), m_packedHV.end(), 0.0f);


	std::fill(m_gTop.begin(), m_gTop.end(), 0.0f);
	std::fill(m_gBottom.begin(), m_gBottom.end(), 0.0f);
	std::fill(m_gLeft.begin(), m_gLeft.end(), 0.0f);
	std::fill(m_gRight.begin(), m_gRight.end(), 0.0f);

}

void WaterHeightfield::updatePackedHV()
{
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




void WaterHeightfield::update(float dt)
{
	// Avoid giant dt spikes
	dt = std::min(dt, 1.0f / 30.0f);

	// CFL condition: dt < h / c  (use a conservative safety factor)
	const float h = m_dx;
	const float dt_max = 0.25f * (h / (m_c * 1.41421356f)); // 2D safety: /sqrt(2)


	int steps = (dt > 0.0f) ? int(std::ceil(dt / dt_max)) : 1;
	steps = std::clamp(steps, 1, 16);

	const float dt_sub = dt / float(steps);
	for (int s = 0; s < steps; ++s)
		step(dt_sub);

	updatePackedHV();

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
	// Reuse persistent buffer (avoids allocating each substep)
	std::copy(m_u.begin(), m_u.end(), m_uNew.begin());


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

			// HARD CLAMP (prevents runaway explosions)
			m_v[i] = std::clamp(m_v[i], -m_vMax, m_vMax);

			m_uNew[i] = m_u[i] + m_v[i] * dt;


		}
	}

	// Viscosity: diffuse velocity to kill cell-to-cell shimmer.
	// nu is in "grid units". Small values like 0..0.05 are useful.
	if (m_viscosity > 0.0f) {
		const float nu = m_viscosity;
		const float invH2 = 1.0f / (h2);

		std::copy(m_v.begin(), m_v.end(), m_vNew.begin());

		for (int y = 1; y < m_n - 1; ++y) {
			for (int x = 1; x < m_n - 1; ++x) {
				const std::size_t i = idx(x, y);

				float vC = m_v[i];
				float lapV =
					(m_v[idx(x - 1, y)] + m_v[idx(x + 1, y)] +
						m_v[idx(x, y - 1)] + m_v[idx(x, y + 1)] -
						4.0f * vC);

				m_vNew[i] = vC + (nu * lapV * dt * invH2);
			}
		}

		m_v.swap(m_vNew);
	}




	// --- 3) Commit update (u = unew) -----------------------------------
	m_u.swap(m_uNew);

	if (m_heightDiffusion > 0.0f) {
		const float a = m_heightDiffusion;
		const float invH2 = 1.0f / (h2);

		std::copy(m_u.begin(), m_u.end(), m_uNew.begin());

		for (int y = 1; y < m_n - 1; ++y) {
			for (int x = 1; x < m_n - 1; ++x) {
				const std::size_t i = idx(x, y);
				float uC = m_u[i];
				float lapU =
					(m_u[idx(x - 1, y)] + m_u[idx(x + 1, y)] +
						m_u[idx(x, y - 1)] + m_u[idx(x, y + 1)] -
						4.0f * uC);

				m_uNew[i] = uC + (a * lapU * dt * invH2);
			}
		}

		m_u.swap(m_uNew);
	}



	// Symmetric "slope limiter": add local diffusion when gradients get too steep.
	// This avoids the axis-aligned clamp artifacts that can cause corner avalanches.
	if (m_maxSlope > 0.0f) {
		const float maxDu = m_maxSlope * m_dx; // allowed height change per cell edge
		const float maxDu2 = maxDu * maxDu;

		// Make a temp copy of u to write into (reuse m_uNew buffer)
		std::copy(m_u.begin(), m_u.end(), m_uNew.begin());

		// Strength of extra diffusion when over limit (tune range in UI if needed)
		const float kappa = 0.35f; // 0..1, higher = stronger limiting

		for (int y = 1; y < m_n - 1; ++y) {
			for (int x = 1; x < m_n - 1; ++x) {
				const std::size_t i = idx(x, y);

				float uC = m_u[i];
				float uL = m_u[idx(x - 1, y)];
				float uR = m_u[idx(x + 1, y)];
				float uD = m_u[idx(x, y - 1)];
				float uU = m_u[idx(x, y + 1)];

				// Gradient magnitude squared (symmetric)
				float dxu = 0.5f * (uR - uL);
				float dyu = 0.5f * (uU - uD);
				float g2 = dxu * dxu + dyu * dyu;

				// If too steep, apply extra Laplacian smoothing locally
				if (g2 > maxDu2) {
					float lapU = (uL + uR + uD + uU - 4.0f * uC);
					// Amount grows smoothly with how much we exceed the limit
					float excess = (std::sqrt(g2) - maxDu) / (maxDu + 1e-6f);
					excess = std::clamp(excess, 0.0f, 1.0f);

					m_uNew[i] = uC + (kappa * excess) * lapU;

					// Also damp velocity locally so we don't keep "pushing" a crest we just limited.
					// This removes sharp kinks when the wave falls back.
					float velDampLocal = 1.0f - 0.5f * (kappa * excess); // 0.5 is a good start
					velDampLocal = std::clamp(velDampLocal, 0.2f, 1.0f);
					m_v[i] *= velDampLocal;

				}
			}
		}

		m_u.swap(m_uNew);
	}



	// --- 4) Boundary conditions ---------------------------------------
	applyBoundaries(dt);

	if (m_lockWaterLevel)
		removeMeanHeight();
}

void WaterHeightfield::applyBoundaries(float dt)
{
	if (m_openBoundary)
	{
		const float h = m_dx;
		const float a = m_c * dt;
		const float denom = h + a;
		if (denom <= 0.0f) return;

		// TOP (y=0) uses interior y=1 (excluding corners)
		for (int x = 1; x < m_n - 1; ++x) {
			float u_in = m_u[idx(x, 1)];
			float gnew = (a * u_in + h * m_gTop[x]) / denom;
			m_gTop[x] = gnew;
			m_u[idx(x, 0)] = gnew;
		}

		// BOTTOM (y=n-1) uses interior y=n-2 (excluding corners)
		for (int x = 1; x < m_n - 1; ++x) {
			float u_in = m_u[idx(x, m_n - 2)];
			float gnew = (a * u_in + h * m_gBottom[x]) / denom;
			m_gBottom[x] = gnew;
			m_u[idx(x, m_n - 1)] = gnew;
		}

		// LEFT (x=0) uses interior x=1 (excluding corners)
		for (int y = 1; y < m_n - 1; ++y) {
			float u_in = m_u[idx(1, y)];
			float gnew = (a * u_in + h * m_gLeft[y]) / denom;
			m_gLeft[y] = gnew;
			m_u[idx(0, y)] = gnew;
		}

		// RIGHT (x=n-1) uses interior x=n-2 (excluding corners)
		for (int y = 1; y < m_n - 1; ++y) {
			float u_in = m_u[idx(m_n - 2, y)];
			float gnew = (a * u_in + h * m_gRight[y]) / denom;
			m_gRight[y] = gnew;
			m_u[idx(m_n - 1, y)] = gnew;
		}

		// --- Corners: blend the two edge estimates to avoid directional bias ---
		auto cornerBlend = [&](int x, int y, float& gA, float uA, float& gB, float uB) {
			float gnewA = (a * uA + h * gA) / denom;
			float gnewB = (a * uB + h * gB) / denom;
			float gnew = 0.5f * (gnewA + gnewB);
			gA = gnew; gB = gnew;
			m_u[idx(x, y)] = gnew;
			};

		// (0,0): blend TOP[x=0] and LEFT[y=0]
		cornerBlend(0, 0, m_gTop[0], m_u[idx(0, 1)],
			m_gLeft[0], m_u[idx(1, 0)]);

		// (n-1,0): blend TOP[x=n-1] and RIGHT[y=0]
		cornerBlend(m_n - 1, 0, m_gTop[m_n - 1], m_u[idx(m_n - 1, 1)],
			m_gRight[0], m_u[idx(m_n - 2, 0)]);

		// (0,n-1): blend BOTTOM[x=0] and LEFT[y=n-1]
		cornerBlend(0, m_n - 1, m_gBottom[0], m_u[idx(0, m_n - 2)],
			m_gLeft[m_n - 1], m_u[idx(1, m_n - 1)]);

		// (n-1,n-1): blend BOTTOM[x=n-1] and RIGHT[y=n-1]
		cornerBlend(m_n - 1, m_n - 1, m_gBottom[m_n - 1], m_u[idx(m_n - 1, m_n - 2)],
			m_gRight[m_n - 1], m_u[idx(m_n - 2, m_n - 1)]);

		// Keep boundary velocities sane (optional)
		for (int x = 0; x < m_n; ++x) {
			m_v[idx(x, 0)] = m_v[idx(x, 1)];
			m_v[idx(x, m_n - 1)] = m_v[idx(x, m_n - 2)];
		}
		for (int y = 0; y < m_n; ++y) {
			m_v[idx(0, y)] = m_v[idx(1, y)];
			m_v[idx(m_n - 1, y)] = m_v[idx(m_n - 2, y)];
		}
	}

	else
	{
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


void WaterHeightfield::disturb(int x, int y, float magnitude, int radius)
{
	// Ignore clicks outside the simulation
	if (x < 1 || x >= m_n - 1 || y < 1 || y >= m_n - 1)
		return;

	/*
	  Apply a localized impulse to the velocity field.
	  Gaussian kernel, but with a circular cutoff to avoid square footprint
	  when radius is large.
	*/

	radius = std::max(1, radius);

	// Pick sigma relative to radius; not too large, otherwise weights become uniform
	const float sigma = std::max(0.5f, radius * 0.45f);
	const float inv2Sigma2 = 1.0f / (2.0f * sigma * sigma);
	const float r2Max = float(radius * radius);

	for (int oy = -radius; oy <= radius; ++oy) {
		for (int ox = -radius; ox <= radius; ++ox) {

			float r2 = float(ox * ox + oy * oy);
			if (r2 > r2Max) continue;

			int xi = x + ox;
			int yi = y + oy;

			if (xi < 1 || xi >= m_n - 1 || yi < 1 || yi >= m_n - 1)
				continue;

			float w = std::exp(-r2 * inv2Sigma2);

			m_v[idx(xi, yi)] += magnitude * w;
		}
	}
}


void WaterHeightfield::removeMeanHeight()
{
	/*
	  Remove global height offset (DC component).

	  With open boundaries, the ghost memory (gTop/gBottom/gLeft/gRight)
	  can drift too. If we only recenter the interior, the edges can appear
	  to "lift" into a dome/bowl. So we recenter the full grid AND the ghost
	  memory arrays to keep everything consistent.
	*/

	double sum = 0.0;
	int count = 0;

	// Mean over ALL cells (including boundaries)
	for (int y = 0; y < m_n; ++y) {
		for (int x = 0; x < m_n; ++x) {
			sum += m_u[idx(x, y)];
			++count;
		}
	}
	if (count == 0) return;

	const float mean = float(sum / double(count));

	// Subtract mean from ALL cells (including boundaries)
	for (int y = 0; y < m_n; ++y) {
		for (int x = 0; x < m_n; ++x) {
			m_u[idx(x, y)] -= mean;
		}
	}

	// Also shift open-boundary ghost memory so boundaries don't reintroduce offset
	if (m_openBoundary) {
		for (float& g : m_gTop)    g -= mean;
		for (float& g : m_gBottom) g -= mean;
		for (float& g : m_gLeft)   g -= mean;
		for (float& g : m_gRight)  g -= mean;
	}
}


static constexpr float PI = 3.14159265359f;

void WaterHeightfield::pullPointTargetHeight(
	int x, int y, float dt, float targetU, float widthCells, float k, float d)
{
	if (x < 1 || x >= m_n - 1 || y < 1 || y >= m_n - 1) return;

	widthCells = std::max(0.5f, widthCells);

	// Slightly tighter sigma helps avoid "almost-uniform" weight over a big square
	const float sigma = std::max(0.5f, widthCells * 0.45f);
	const float inv2Sigma2 = 1.0f / (2.0f * sigma * sigma);

	// We only need to touch a finite neighborhood; use ~3*sigma
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

			// Circular cutoff: prevents square footprint from showing up
			if (r2 > reach2) continue;

			float w = std::exp(-r2 * inv2Sigma2);

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





