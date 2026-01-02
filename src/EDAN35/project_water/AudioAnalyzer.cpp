#include "AudioAnalyzer.hpp"
#include <algorithm>
#include <cmath>

static bool isPowerOfTwo(int n) { return n > 0 && (n & (n - 1)) == 0; }

AudioAnalyzer::AudioAnalyzer()
{
	m_ring.resize(1 << 18); // 262144 samples ~ 5.4s at 48k
	ensureBuffers();
}

void AudioAnalyzer::setFFTSize(int n)
{
	if (!isPowerOfTwo(n)) return;
	m_fftSize = n;
	ensureBuffers();
}

void AudioAnalyzer::ensureBuffers()
{
	m_window.resize(m_fftSize);
	m_time.resize(m_fftSize);
	m_re.resize(m_fftSize);
	m_im.resize(m_fftSize);
	m_mag.resize(m_fftSize / 2);

	computeHann();
}

void AudioAnalyzer::computeHann()
{
	for (int i = 0; i < m_fftSize; ++i) {
		float x = float(i) / float(m_fftSize - 1);
		m_window[i] = 0.5f - 0.5f * std::cos(2.0f * 3.1415926535f * x);
	}
}

void AudioAnalyzer::pushInterleaved(const float* samples, uint32_t frameCount, uint32_t channels)
{
	if (!samples || frameCount == 0 || channels == 0) return;

	const uint32_t mask = uint32_t(m_ring.size() - 1);
	uint32_t w = m_write.load(std::memory_order_relaxed);

	for (uint32_t i = 0; i < frameCount; ++i) {
		float mono = 0.0f;
		for (uint32_t c = 0; c < channels; ++c) {
			mono += samples[i * channels + c];
		}
		mono /= float(channels);

		m_ring[w & mask] = mono;
		++w;
	}
	m_write.store(w, std::memory_order_release);
}

bool AudioAnalyzer::popWindow()
{
	const uint32_t mask = uint32_t(m_ring.size() - 1);
	uint32_t r = m_read.load(std::memory_order_relaxed);
	uint32_t w = m_write.load(std::memory_order_acquire);

	uint32_t available = w - r;
	if (available < uint32_t(m_fftSize)) return false;

	for (int i = 0; i < m_fftSize; ++i) {
		float s = m_ring[(r + uint32_t(i)) & mask];
		m_time[i] = s * m_window[i];
	}
	// Hop size: 1/2 window (overlap 50%)
	r += uint32_t(m_fftSize / 2);
	m_read.store(r, std::memory_order_release);
	return true;
}

// Iterative radix-2 FFT, in-place on m_re/m_im.
// Input: m_time
void AudioAnalyzer::fftInPlace()
{
	// Copy time into complex
	for (int i = 0; i < m_fftSize; ++i) {
		m_re[i] = m_time[i];
		m_im[i] = 0.0f;
	}

	// Bit reversal
	int j = 0;
	for (int i = 1; i < m_fftSize; ++i) {
		int bit = m_fftSize >> 1;
		while (j & bit) { j ^= bit; bit >>= 1; }
		j ^= bit;
		if (i < j) {
			std::swap(m_re[i], m_re[j]);
			std::swap(m_im[i], m_im[j]);
		}
	}

	// FFT stages
	for (int len = 2; len <= m_fftSize; len <<= 1) {
		float ang = -2.0f * 3.1415926535f / float(len);
		float wlenRe = std::cos(ang);
		float wlenIm = std::sin(ang);

		for (int i = 0; i < m_fftSize; i += len) {
			float wRe = 1.0f;
			float wIm = 0.0f;

			for (int k = 0; k < len / 2; ++k) {
				int u = i + k;
				int v = i + k + len / 2;

				float vr = m_re[v] * wRe - m_im[v] * wIm;
				float vi = m_re[v] * wIm + m_im[v] * wRe;

				float ur = m_re[u];
				float ui = m_im[u];

				m_re[u] = ur + vr;
				m_im[u] = ui + vi;
				m_re[v] = ur - vr;
				m_im[v] = ui - vi;

				float nextWRe = wRe * wlenRe - wIm * wlenIm;
				float nextWIm = wRe * wlenIm + wIm * wlenRe;
				wRe = nextWRe;
				wIm = nextWIm;
			}
		}
	}
}

void AudioAnalyzer::computeMagnitude()
{
	for (int i = 0; i < m_fftSize / 2; ++i) {
		float re = m_re[i];
		float im = m_im[i];
		// power
		m_mag[i] = re * re + im * im;
	}
}

void AudioAnalyzer::reset()
{
	std::fill(m_mag.begin(), m_mag.end(), 0.0f);
	m_read.store(m_write.load(std::memory_order_relaxed), std::memory_order_relaxed);
	m_lastSampleRate = 0;
}




void AudioAnalyzer::update(float /*dt*/, uint32_t sampleRate)
{
	m_lastSampleRate = sampleRate;
	while (popWindow()) {
		fftInPlace();
		computeMagnitude();
	}
}


float AudioAnalyzer::getBandEnergy(float fLowHz, float fHighHz, uint32_t sampleRate) const
{
	if (sampleRate == 0) return 0.0f;
	if (fHighHz <= fLowHz) return 0.0f;

	float nyquist = 0.5f * float(sampleRate);
	fLowHz = std::clamp(fLowHz, 0.0f, nyquist);
	fHighHz = std::clamp(fHighHz, 0.0f, nyquist);

	int binLow = int((fLowHz / nyquist) * float(m_fftSize / 2));
	int binHigh = int((fHighHz / nyquist) * float(m_fftSize / 2));

	binLow = std::clamp(binLow, 0, (m_fftSize / 2) - 1);
	binHigh = std::clamp(binHigh, 0, (m_fftSize / 2) - 1);
	if (binHigh <= binLow) return 0.0f;

	float sum = 0.0f;
	for (int i = binLow; i <= binHigh; ++i) sum += m_mag[i];

	float avg = sum / float((binHigh - binLow) + 1);

	// Normalize roughly. This is not "real units", just stable-ish.
	// You will tune gain in UI anyway.
	return avg;
}


