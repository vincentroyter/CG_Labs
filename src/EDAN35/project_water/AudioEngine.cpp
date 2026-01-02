#include "AudioEngine.hpp"
#include "AudioAnalyzer.hpp"

#include <algorithm>
#include <cstring>

#include <mutex>

#define MINIAUDIO_IMPLEMENTATION
#include "include/miniaudio.h"

struct AudioEngine::Impl
{
	ma_device device{};
	ma_decoder decoder{};
	std::atomic<bool> haveDecoder{ false };
	std::atomic<bool> playing{ false };
	float volume = 1.0f;

	AudioAnalyzer* analyzer = nullptr;
	uint32_t sampleRate = 48000;
	uint32_t channels = 2;

	std::mutex decoderMutex;
};


static void data_callback(ma_device* pDevice, void* pOutput, const void* /*pInput*/, ma_uint32 frameCount)
{
	auto* impl = (AudioEngine::Impl*)pDevice->pUserData;
	float* out = (float*)pOutput;

	const ma_uint32 ch = pDevice->playback.channels;

	// Always output silence unless we successfully decode.
	std::memset(out, 0, frameCount * ch * sizeof(float));

	if (!impl || !impl->haveDecoder.load() || !impl->playing.load()) {
		return;
	}

	ma_uint64 framesRead = 0;
	ma_result rr = MA_ERROR;

	{
		std::lock_guard<std::mutex> lock(impl->decoderMutex);
		rr = ma_decoder_read_pcm_frames(&impl->decoder, out, frameCount, &framesRead);

		// If we got nothing, try looping ONCE (EOF) then read again.
		if (framesRead == 0) {
			ma_decoder_seek_to_pcm_frame(&impl->decoder, 0);
			rr = ma_decoder_read_pcm_frames(&impl->decoder, out, frameCount, &framesRead);
		}
	}

	// If still nothing, stay silent. This avoids "noise" output.
	if (framesRead == 0) {
		static int tickFail = 0;
		if ((tickFail++ % 200) == 0) {
			printf("audio: read returned 0 frames (rr=%d) sr=%u ch=%u\n",
				(int)rr, (unsigned)pDevice->sampleRate, (unsigned)ch);
		}
		return;
	}

	// Apply volume only to the frames we actually got.
	const float vol = impl->volume;
	const ma_uint32 samples = (ma_uint32)framesRead * ch;
	for (ma_uint32 i = 0; i < samples; ++i) out[i] *= vol;

	// Tail is already zero (because we memset at the top), so partial reads are fine.

	// Optional debug: peak + framesRead
	float peak = 0.0f;
	for (ma_uint32 i = 0; i < samples; ++i) {
		float a = out[i];
		if (a < 0.0f) a = -a;
		if (a > peak) peak = a;
	}

	static int tick2 = 0;
	if ((tick2++ % 200) == 0) {
		printf("audio: framesRead=%llu peak=%f rr=%d sr=%u ch=%u\n",
			(unsigned long long)framesRead, peak, (int)rr,
			(unsigned)pDevice->sampleRate, (unsigned)ch);
	}

	if (impl->analyzer) {
		impl->analyzer->pushInterleaved(out, frameCount, ch);
	}
}



AudioEngine::AudioEngine()
{
	m = new Impl();
}

AudioEngine::~AudioEngine()
{
	unload();
	delete m;
	m = nullptr;
}

bool AudioEngine::loadWav(const std::string& path)
{
	unload();

	ma_decoder_config decCfg = ma_decoder_config_init(ma_format_f32, 0, 0);
	if (ma_decoder_init_file(path.c_str(), &decCfg, &m->decoder) != MA_SUCCESS) {
		return false;
	}
	m->haveDecoder.store(true);

	{
		ma_uint64 testRead = 0;
		float testBuf[256] = {};
		ma_result tr;
		{
			std::lock_guard<std::mutex> lock(m->decoderMutex);
			tr = ma_decoder_read_pcm_frames(&m->decoder, testBuf, 128, &testRead);
			ma_decoder_seek_to_pcm_frame(&m->decoder, 0);
		}
		printf("loadWav test: tr=%d testRead=%llu\n", (int)tr, (unsigned long long)testRead);
	}


	// We forced the decoder output to 2ch/48k above.
	m_sampleRate = (uint32_t)m->decoder.outputSampleRate;
	m_channels = (uint32_t)m->decoder.outputChannels;
	m->sampleRate = m_sampleRate;
	m->channels = m_channels;


	ma_device_config devCfg = ma_device_config_init(ma_device_type_playback);
	devCfg.playback.format = ma_format_f32;
	devCfg.playback.channels = m_channels;
	devCfg.sampleRate = m_sampleRate;
	devCfg.dataCallback = data_callback;
	devCfg.pUserData = m;

	// IMPORTANT: you must init the device before starting it
	if (ma_device_init(nullptr, &devCfg, &m->device) != MA_SUCCESS) {
		ma_decoder_uninit(&m->decoder);
		m->haveDecoder.store(false);
		return false;
	}

	if (ma_device_start(&m->device) != MA_SUCCESS) {
		ma_device_uninit(&m->device);
		ma_decoder_uninit(&m->decoder);
		m->haveDecoder.store(false);
		return false;
	}

	m->volume = m_volume;
	m->analyzer = m_analyzer;

	m_path = path;
	m_loaded.store(true);

	// Start paused by default
	m_playing.store(false);
	m->playing.store(false);

	return true;
}


void AudioEngine::unload()
{
	if (!m) return;

	if (m->device.pContext) {
		ma_device_stop(&m->device);
		ma_device_uninit(&m->device);
		std::memset(&m->device, 0, sizeof(m->device));
	}

	if (m->haveDecoder.load()) {
		std::lock_guard<std::mutex> lock(m->decoderMutex);
		ma_decoder_uninit(&m->decoder);
		m->haveDecoder.store(false);
	}

	m_loaded.store(false);
	m_playing.store(false);
	m->playing.store(false);
	m_path.clear();
}


void AudioEngine::play()
{
	if (!m_loaded.load()) return;
	m_playing.store(true);
	m->playing.store(true);
}



void AudioEngine::pause()
{
	m_playing.store(false);
	m->playing.store(false);
}



void AudioEngine::stop()
{
	if (!m_loaded.load()) return;
	pause();
	seekSeconds(0.0f);
}

float AudioEngine::durationSeconds() const
{
	if (!m_loaded.load()) return 0.0f;

	ma_uint64 lenFrames = 0;
	{
		std::lock_guard<std::mutex> lock(m->decoderMutex);
		ma_result r = ma_decoder_get_length_in_pcm_frames(&m->decoder, &lenFrames);
		if (r != MA_SUCCESS || m_sampleRate == 0) return 0.0f;
	}
	return float(double(lenFrames) / double(m_sampleRate));
}

float AudioEngine::cursorSeconds() const
{
	if (!m_loaded.load()) return 0.0f;

	ma_uint64 curFrames = 0;
	{
		std::lock_guard<std::mutex> lock(m->decoderMutex);
		ma_result r = ma_decoder_get_cursor_in_pcm_frames(&m->decoder, &curFrames);
		if (r != MA_SUCCESS || m_sampleRate == 0) return 0.0f;
	}
	return float(double(curFrames) / double(m_sampleRate));

}

void AudioEngine::seekSeconds(float t)
{
	if (!m_loaded.load()) return;

	float dur = durationSeconds();
	t = std::clamp(t, 0.0f, dur);

	ma_uint64 frame = (ma_uint64)(double(t) * double(m_sampleRate));
	{
		std::lock_guard<std::mutex> lock(m->decoderMutex);
		ma_decoder_seek_to_pcm_frame(&m->decoder, frame);
	}

}

void AudioEngine::setVolume(float v01)
{
	m_volume = std::clamp(v01, 0.0f, 1.0f);
	if (m) m->volume = m_volume;
}
