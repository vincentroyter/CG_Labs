#pragma once
#include <vector>
#include <atomic>
#include <cstdint>

class AudioAnalyzer
{
public:
	AudioAnalyzer();

	// Called from audio callback thread
	void pushInterleaved(const float* samples, uint32_t frameCount, uint32_t channels);

	// Called from main thread each frame
	void update(float dt, uint32_t sampleRate);

	void reset();

	// Query band energy after update()
	float getBandEnergy(float fLowHz, float fHighHz, uint32_t sampleRate) const;

	// For UI spectrum display (magnitudes, size = fftSize()/2, covers 0..Nyquist)
	const std::vector<float>& spectrum() const { return m_mag; }
	int spectrumSize() const { return int(m_mag.size()); }

	// Config
	void setFFTSize(int n); // must be power of 2
	int  fftSize() const { return m_fftSize; }

	uint32_t lastSampleRate() const { return m_lastSampleRate; }



private:
	// Ring buffer (mono float samples)
	std::vector<float> m_ring;
	std::atomic<uint32_t> m_write = 0;
	std::atomic<uint32_t> m_read = 0;
	uint32_t m_lastSampleRate = 0;
	std::vector<float> m_spectrum;


	// FFT
	int m_fftSize = 2048;
	std::vector<float> m_window;
	std::vector<float> m_time;
	std::vector<float> m_re;
	std::vector<float> m_im;
	std::vector<float> m_mag;

	void ensureBuffers();
	void computeHann();
	bool popWindow();     // fill m_time with fftSize samples
	void fftInPlace();    // compute m_re/m_im from m_time
	void computeMagnitude();
};
