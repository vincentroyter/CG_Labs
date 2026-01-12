#pragma once
#include <vector>
#include <atomic>
#include <cstdint>

class AudioAnalyzer
{
public:
	AudioAnalyzer();

	void pushInterleaved(const float* samples, uint32_t frameCount, uint32_t channels);

	void update(float dt, uint32_t sampleRate);

	void reset();

	float getBandEnergy(float fLowHz, float fHighHz, uint32_t sampleRate) const;

	const std::vector<float>* spectrumPtr() const { return &m_mag; }

	int spectrumSize() const { return int(m_mag.size()); }

	void setFFTSize(int n);
	int  fftSize() const { return m_fftSize; }

	uint32_t lastSampleRate() const { return m_lastSampleRate; }



private:
	std::vector<float> m_ring;
	std::atomic<uint32_t> m_write = 0;
	std::atomic<uint32_t> m_read = 0;
	uint32_t m_lastSampleRate = 0;


	// FFT
	int m_fftSize = 2048;
	std::vector<float> m_window;
	std::vector<float> m_time;
	std::vector<float> m_re;
	std::vector<float> m_im;
	std::vector<float> m_mag;

	void ensureBuffers();
	void computeHann();
	bool popWindow();
	void fftInPlace();
	void computeMagnitude();
};
