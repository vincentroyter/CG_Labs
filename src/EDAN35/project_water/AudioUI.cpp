#include "AudioUI.hpp"
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

// ---------- Helpers ----------
static bool SliderFloatWithInput(const char* label, float* value, float min, float max, const char* format = "%.2f")
{
	if (!value) return false;

	ImGui::PushID(label);
	ImGui::TextUnformatted(label);
	ImGui::SameLine();

	ImGui::PushItemWidth(130.0f);
	bool changed1 = ImGui::SliderFloat("##slider", value, min, max, format);
	ImGui::SameLine();

	ImGui::PushItemWidth(80.0f);
	bool changed2 = ImGui::InputFloat("##input", value, 0.0f, 0.0f, format, ImGuiInputTextFlags_EnterReturnsTrue);
	ImGui::PopItemWidth();
	ImGui::PopItemWidth();

	*value = std::clamp(*value, min, max);
	ImGui::PopID();
	return changed1 || changed2;
}

static bool SliderIntWithInput(const char* label, int* value, int min, int max)
{
	if (!value) return false;

	ImGui::PushID(label);
	ImGui::TextUnformatted(label);
	ImGui::SameLine();

	ImGui::PushItemWidth(130.0f);
	bool changed1 = ImGui::SliderInt("##slider", value, min, max);
	ImGui::SameLine();

	ImGui::PushItemWidth(80.0f);
	bool changed2 = ImGui::InputInt("##input", value, 1, 1, ImGuiInputTextFlags_EnterReturnsTrue);
	ImGui::PopItemWidth();
	ImGui::PopItemWidth();

	*value = std::clamp(*value, min, max);
	ImGui::PopID();
	return changed1 || changed2;
}

// ---------- Spectrum window ----------
void AudioUI::drawSpectrumWindow(AudioUIState& s)
{
	if (!s.settings) return;
	auto& set = *s.settings;

	// Window toggle is persisted in settings now
	if (!set.win.showSpectrumWindow) return;

	if (!ImGui::Begin("Spectrum", &set.win.showSpectrumWindow)) {
		ImGui::End();
		return;
	}

	const uint32_t sr = (s.sampleRate ? *s.sampleRate : 0u);
	const bool haveSpecPtr = (s.spectrum != nullptr);

	if (!haveSpecPtr || sr == 0u) {
		ImGui::Separator();
		ImGui::TextDisabled("No spectrum yet (load + play audio, or sampleRate is 0).");
		ImGui::End();
		return;
	}

	const std::vector<float>& spec = *s.spectrum;
	if (spec.empty()) {
		ImGui::Separator();
		ImGui::TextDisabled("Spectrum empty (analyzer hasn't produced a frame yet).");
		ImGui::End();
		return;
	}

	// Controls (persisted)
	SliderFloatWithInput("Smooth", &set.audio.spectrumSmooth, 0.0f, 0.95f, "%.2f");
	SliderFloatWithInput("Max Hz", &set.audio.spectrumMaxHz, 1000.0f, 20000.0f, "%.0f");
	ImGui::Checkbox("Freeze", &set.audio.freezeSpectrum);

	const bool freeze = set.audio.freezeSpectrum;

	float smooth = std::clamp(set.audio.spectrumSmooth, 0.0f, 0.99f);
	set.audio.spectrumSmooth = smooth;

	float maxHz = std::clamp(set.audio.spectrumMaxHz, 1000.0f, 20000.0f);
	set.audio.spectrumMaxHz = maxHz;

	const float nyq = 0.5f * float(sr);
	const int N = (int)spec.size(); // typically fftSize/2

	int binsToShow = (int)std::round((maxHz / nyq) * float(N));
	binsToShow = std::clamp(binsToShow, 8, N);

	// Static buffers persist between frames
	static std::vector<float> disp;
	static std::vector<float> dispSm;
	disp.resize(binsToShow);
	dispSm.resize(binsToShow);

	// Normalize to peak for visibility
	float peak = 0.0f;
	for (int i = 0; i < binsToShow; ++i) {
		float v = spec[i];
		if (!std::isfinite(v) || v < 0.0f) v = 0.0f;
		peak = std::max(peak, v);
	}
	peak = std::max(peak, 1e-12f);

	// Magnitude -> dB rel peak -> 0..1
	const float dbMin = -80.0f;
	for (int i = 0; i < binsToShow; ++i) {
		float v = spec[i];
		if (!std::isfinite(v) || v < 0.0f) v = 0.0f;
		v = std::max(v, 1e-12f);

		float db = 10.0f * std::log10(v / peak);
		db = std::clamp(db, dbMin, 0.0f);

		float y = (db - dbMin) / (-dbMin);
		disp[i] = std::clamp(y, 0.0f, 1.0f);
	}

	// Smooth displayed data unless frozen
	if (!freeze) {
		const float a = 1.0f - smooth;
		for (int i = 0; i < binsToShow; ++i) {
			dispSm[i] = dispSm[i] + a * (disp[i] - dispSm[i]);
		}
	}

	// Plot
	ImVec2 plotSize(0.0f, 200.0f); // 0 = auto width
	ImGui::PlotHistogram("##spec_hist",
		dispSm.data(),
		binsToShow,
		0,
		"",
		0.0f, 1.0f,
		plotSize
	);

	// Hover tooltip with Hz
	if (ImGui::IsItemHovered()) {
		ImVec2 p0 = ImGui::GetItemRectMin();
		ImVec2 p1 = ImGui::GetItemRectMax();
		float w = std::max(1.0f, p1.x - p0.x);

		float mx = ImGui::GetIO().MousePos.x;
		float t = (mx - p0.x) / w;
		t = std::clamp(t, 0.0f, 0.999999f);

		int bin = (int)(t * float(binsToShow));
		bin = std::clamp(bin, 0, binsToShow - 1);

		const float fftSize = float(N * 2);
		float hz = float(bin) * (float(sr) / fftSize);

		float y = dispSm[bin];
		float db = (y * (-dbMin)) + dbMin;

		ImGui::BeginTooltip();
		ImGui::Text("Hz: %.1f", hz);
		ImGui::Text("dB: %.1f", db);
		ImGui::EndTooltip();
	}

	ImGui::End();
}

// ---------- Main Audio window ----------
void AudioUI::draw(AudioUIState& s)
{
	if (!ImGui::Begin("Audio")) {
		ImGui::End();
		return;
	}

	if (!s.settings) {
		ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Error: AudioUIState.settings is null.");
		ImGui::End();
		return;
	}

	auto& set = *s.settings;

	// Enable (persisted)
	ImGui::Checkbox("Enable audio driving", &set.audio.enabled);

	// File / playback
	ImGui::Separator();
	ImGui::TextUnformatted("File and playback");

	if (ImGui::Button("Load WAV...")) {
		if (s.onLoadWav) s.onLoadWav();
	}

	const bool loaded = (s.isLoaded && *s.isLoaded);
	const bool playing = (s.isPlaying && *s.isPlaying);

	ImGui::SameLine();
	ImGui::BeginDisabled(!loaded);
	if (ImGui::Button("Play")) { if (s.onPlay)  s.onPlay(); }
	ImGui::SameLine();
	if (ImGui::Button("Pause")) { if (s.onPause) s.onPause(); }
	ImGui::SameLine();
	if (ImGui::Button("Stop")) { if (s.onStop)  s.onStop(); }
	ImGui::EndDisabled();

	if (s.loadedPath && !s.loadedPath->empty()) {
		ImGui::Text("Loaded: %s", s.loadedPath->c_str());
	}
	else {
		ImGui::TextDisabled("Loaded: (none)");
	}

	// Volume (persisted)
	SliderFloatWithInput("Volume", &set.audio.volume, 0.0f, 1.0f, "%.2f");

	// Seek (pause while dragging)
	if (s.durationSec && s.cursorSec) {
		float dur = *s.durationSec;
		float cur = *s.cursorSec;
		if (dur > 0.0f) {
			float seek = cur;

			static bool wasPlayingBeforeSeek = false;

			bool changed = ImGui::SliderFloat("Seek", &seek, 0.0f, dur, "%.2f s");
			bool active = ImGui::IsItemActive();
			bool released = ImGui::IsItemDeactivatedAfterEdit();

			if (active && !wasPlayingBeforeSeek) {
				wasPlayingBeforeSeek = (s.isPlaying && *s.isPlaying);
				if (wasPlayingBeforeSeek && s.onPause) s.onPause();
			}

			if (changed) {
				if (s.seekSeconds) s.seekSeconds(seek);
			}

			if (released) {
				if (wasPlayingBeforeSeek && s.onPlay) s.onPlay();
				wasPlayingBeforeSeek = false;
			}

			ImGui::Text("Time: %.2f / %.2f", cur, dur);
		}
	}

	// Windows (persisted)
	ImGui::Separator();
	ImGui::TextUnformatted("Windows");
	ImGui::Checkbox("Spectrum window", &set.win.showSpectrumWindow);

	// Physics knobs (persisted)
	ImGui::Separator();
	ImGui::TextUnformatted("Audio driver physics");
	SliderFloatWithInput("Stiffness k", &set.audio.k_stiff, 10.0f, 400.0f, "%.1f");
	SliderFloatWithInput("Damping d", &set.audio.d_damp, 0.0f, 80.0f, "%.1f");

	// Band sources (persisted)
	ImGui::Separator();
	ImGui::TextUnformatted("Band sources");

	auto& bands = set.audio.bands;
	int& selBand = set.audio.selectedBand;
	int& counter = set.audio.bandCounter;

	if (bands.empty()) selBand = -1;
	else selBand = std::clamp(selBand, -1, (int)bands.size() - 1);

	if (ImGui::Button("Add band")) {
		AudioBandSource b;
		b.name = "Band " + std::to_string(counter++);
		b.fLowHz = 0.0f;
		b.fHighHz = 200.0f;
		b.pos01 = { 0.5f, 0.5f };
		bands.push_back(b);
		selBand = (int)bands.size() - 1;
	}

	ImGui::SameLine();
	const bool canDup = (selBand >= 0 && selBand < (int)bands.size());
	ImGui::BeginDisabled(!canDup);
	if (ImGui::Button("Duplicate")) {
		AudioBandSource copy = bands[selBand];
		copy.name += " copy";

		// reset runtime-only accumulators if they exist in your struct
		copy.energyRaw = 0.0f;
		copy.energyNorm = 0.0f;
		copy.energySmoothed = 0.0f;
		copy.energySlow = 0.0f;
		copy.fluxRaw = 0.0f;
		copy.fluxSmoothed = 0.0f;
		copy.cooldownTimer = 0.0f;
		copy.prevTargetU = 0.0f;
		copy.agcRunning = 0.0f;

		bands.push_back(copy);
		selBand = (int)bands.size() - 1;
		counter++;
	}
	ImGui::EndDisabled();

	ImGui::Separator();

	// List
	if (ImGui::BeginListBox("##bands", ImVec2(-FLT_MIN, 140.0f))) {
		for (int i = 0; i < (int)bands.size(); ++i) {
			std::string label = bands[i].name;
			if (!bands[i].enabled) label += " (off)";
			if (ImGui::Selectable(label.c_str(), selBand == i)) {
				selBand = i;
			}
		}
		ImGui::EndListBox();
	}

	// Selected band editor
	if (selBand >= 0 && selBand < (int)bands.size()) {
		auto& b = bands[selBand];

		ImGui::Separator();
		ImGui::Text("Selected: %s", b.name.c_str());
		ImGui::Checkbox("Enabled", &b.enabled);

		// Type
		const char* types[] = { "Point", "Line" };
		int t = (int)b.type;
		ImGui::Combo("Type", &t, types, IM_ARRAYSIZE(types));
		b.type = (AudioBandType)t;
		const int gain_factor = (b.type == AudioBandType::Line) ? 2 : 1;

		// Drive mode
		const char* modes[] = { "Envelope", "Impulse", "Hybrid" };
		int m = (int)b.mode;
		ImGui::Combo("Drive", &m, modes, IM_ARRAYSIZE(modes));
		b.mode = (AudioDriveMode)m;

		const bool showEnv = (b.mode == AudioDriveMode::Envelope || b.mode == AudioDriveMode::Hybrid);
		const bool showImp = (b.mode == AudioDriveMode::Impulse || b.mode == AudioDriveMode::Hybrid);

		// Band params
		ImGui::Separator();
		ImGui::TextUnformatted("Band");

		SliderFloatWithInput("fLow (Hz)", &b.fLowHz, 0.0f, 20000.0f, "%.1f");
		SliderFloatWithInput("fHigh (Hz)", &b.fHighHz, 0.0f, 20000.0f, "%.1f");
		if (b.fHighHz < b.fLowHz) std::swap(b.fLowHz, b.fHighHz);

		SliderFloatWithInput("Radius/Width (cells)", &b.radiusCells, 1.0f, 30.0f, "%.1f");
		SliderFloatWithInput("Center U", &b.pos01.x, 0.0f, 1.0f, "%.3f");
		SliderFloatWithInput("Center V", &b.pos01.y, 0.0f, 1.0f, "%.3f");

		// Envelope controls
		if (showEnv) {
			ImGui::Separator();
			ImGui::TextUnformatted("Envelope (slow / continuous)");

			SliderFloatWithInput("Gain (envelope)", &b.gain, -2.0f * gain_factor, 2.0f * gain_factor, "%.2f");
			SliderFloatWithInput("Threshold (env)", &b.threshold, 0.0f, 2.0f, "%.3f");
			SliderFloatWithInput("Attack (1/s)", &b.attack, 0.1f, 60.0f, "%.2f");
			SliderFloatWithInput("Release (1/s)", &b.release, 0.1f, 60.0f, "%.2f");
		}

		// Impulse controls
		if (showImp) {
			ImGui::Separator();
			ImGui::TextUnformatted("Impulse / onsets (transients)");

			SliderFloatWithInput("Impulse gain", &b.impulseGain, -4.0f * gain_factor, 4.0f * gain_factor, "%.2f");
			SliderFloatWithInput("Onset threshold", &b.onsetThreshold, 0.0f, 5.0f, "%.3f");
			SliderFloatWithInput("Cooldown (s)", &b.impulseCooldownSec, 0.0f, 0.5f, "%.3f");
			SliderFloatWithInput("Flux smooth (1/s)", &b.fluxSmoothRate, 0.0f, 80.0f, "%.1f");
			SliderFloatWithInput("Onset HP time (s)", &b.onsetHPTimeSec, 0.02f, 1.5f, "%.3f");
		}

		// AGC
		ImGui::Separator();
		ImGui::TextUnformatted("AGC (auto normalization)");
		ImGui::Checkbox("Enable AGC", &b.agcEnabled);
		if (b.agcEnabled) {
			SliderFloatWithInput("AGC time (s)", &b.agcTimeSec, 0.1f, 6.0f, "%.2f");
		}

		// Line geometry
		if (b.type == AudioBandType::Line) {
			ImGui::Separator();
			ImGui::TextUnformatted("Line geometry");
			SliderFloatWithInput("Length", &b.length01, 0.05f, 1.0f, "%.2f");
			ImGui::SliderAngle("Angle", &b.angleRad, -90.0f, 90.0f);
		}

		// Debug
		ImGui::Separator();
		ImGui::TextUnformatted("Debug");

		ImGui::Text("Energy raw:    %.6f", b.energyRaw);
		ImGui::Text("AGC running:   %.6f", b.agcRunning);
		ImGui::Text("Energy norm:   %.4f", b.energyNorm);

		if (showEnv) {
			ImGui::Text("Env(smoothed): %.4f (thr=%.3f)", b.energySmoothed, b.threshold);
		}
		if (showImp) {
			ImGui::Text("Flux(smoothed): %.4f (thr=%.3f)", b.fluxSmoothed, b.onsetThreshold);
			ImGui::Text("Cooldown: %.3f / %.3f", b.cooldownTimer, b.impulseCooldownSec);
			ImGui::Text("Baseline(slow): %.4f", b.energySlow);
		}

		ImGui::Separator();
		if (ImGui::Button("Delete band")) {
			bands.erase(bands.begin() + selBand);
			if (bands.empty()) selBand = -1;
			else selBand = std::clamp(selBand, 0, (int)bands.size() - 1);
		}
	}
	else {
		ImGui::TextDisabled("No band selected.");
	}

	ImGui::End();
}
