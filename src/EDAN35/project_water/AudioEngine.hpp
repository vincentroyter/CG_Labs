#pragma once
#include <string>
#include <atomic>
#include <cstdint>

class AudioAnalyzer;

class AudioEngine
{
public:
	AudioEngine();
	~AudioEngine();

	bool loadWav(const std::string& path);
	void unload();

	void play();
	void pause();
	void stop();

	bool isLoaded() const { return m_loaded; }
	bool isPlaying() const { return m_playing; }

	float durationSeconds() const;
	float cursorSeconds() const;
	void  seekSeconds(float t);

	void setVolume(float v01);
	float volume() const { return m_volume; }

	uint32_t sampleRate() const { return m_sampleRate; }
	uint32_t channels()   const { return m_channels; }

	const std::string& filePath() const { return m_path; }

	void setAnalyzer(AudioAnalyzer* analyzer) { m_analyzer = analyzer; }

	struct Impl;

private:
	Impl* m = nullptr;

	std::string m_path;

	std::atomic<bool> m_loaded = false;
	std::atomic<bool> m_playing = false;

	float m_volume = 1.0f;

	uint32_t m_sampleRate = 48000;
	uint32_t m_channels = 2;

	AudioAnalyzer* m_analyzer = nullptr;
};
