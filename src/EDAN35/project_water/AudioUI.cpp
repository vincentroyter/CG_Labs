#include "AudioUI.hpp"
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

// Helpers
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

// Spectrum Window
void AudioUI::drawSpectrumWindow(AudioUIState& s)
{
	if (!s.settings) return;
	auto& set = *s.settings;

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

	SliderFloatWithInput("Smooth", &set.audio.spectrumSmooth, 0.0f, 0.95f, "%.2f");
	SliderFloatWithInput("Max Hz", &set.audio.spectrumMaxHz, 1000.0f, 20000.0f, "%.0f");
	ImGui::Checkbox("Freeze", &set.audio.freezeSpectrum);

	const bool freeze = set.audio.freezeSpectrum;

	float smooth = std::clamp(set.audio.spectrumSmooth, 0.0f, 0.99f);
	set.audio.spectrumSmooth = smooth;

	float maxHz = std::clamp(set.audio.spectrumMaxHz, 1000.0f, 20000.0f);
	set.audio.spectrumMaxHz = maxHz;

	const float nyq = 0.5f * float(sr);
	const int N = (int)spec.size();

	int binsToShow = (int)std::round((maxHz / nyq) * float(N));
	binsToShow = std::clamp(binsToShow, 8, N);

	static std::vector<float> disp;
	static std::vector<float> dispSm;
	disp.resize(binsToShow);
	dispSm.resize(binsToShow);

	float peak = 0.0f;
	for (int i = 0; i < binsToShow; ++i) {
		float v = spec[i];
		if (!std::isfinite(v) || v < 0.0f) v = 0.0f;
		peak = std::max(peak, v);
	}
	peak = std::max(peak, 1e-12f);

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

	if (!freeze) {
		const float a = 1.0f - smooth;
		for (int i = 0; i < binsToShow; ++i) {
			dispSm[i] = dispSm[i] + a * (disp[i] - dispSm[i]);
		}
	}

	ImVec2 plotSize(0.0f, 200.0f);
	ImGui::PlotHistogram("##spec_hist",
		dispSm.data(),
		binsToShow,
		0,
		"",
		0.0f, 1.0f,
		plotSize
	);

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

// Main Audio Window
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

	ImGui::Checkbox("Enable Audio Input", &set.audio.enabled);

	ImGui::Separator();
	ImGui::TextUnformatted("File and playback");

	if (ImGui::Button("Load WAV")) {
		if (s.onLoadWav) s.onLoadWav();
	}

	const bool loaded = (s.isLoaded && *s.isLoaded);

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

	SliderFloatWithInput("Volume", &set.audio.volume, 0.0f, 0.7f, "%.2f");

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

	ImGui::Separator();
	ImGui::TextUnformatted("Spectrum Window");
	ImGui::Checkbox("Show spectrum window", &set.win.showSpectrumWindow);

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

		copy.energyRaw = 0.0f;
		copy.energyNorm = 0.0f;
		copy.energySmoothed = 0.0f;
		copy.prevTargetU = 0.0f;
		copy.agcRunning = 0.0f;

		bands.push_back(copy);
		selBand = (int)bands.size() - 1;
		counter++;
	}
	ImGui::EndDisabled();

	ImGui::Separator();

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

	if (selBand >= 0 && selBand < (int)bands.size()) {
		auto& b = bands[selBand];

		ImGui::Separator();
		ImGui::Text("Selected: %s", b.name.c_str());
		ImGui::Checkbox("Enabled", &b.enabled);

		const char* types[] = { "Point", "Line" };
		int t = (int)b.type;
		ImGui::Combo("Type", &t, types, IM_ARRAYSIZE(types));
		b.type = (AudioBandType)t;
		const int gain_factor = (b.type == AudioBandType::Line) ? 2 : 1;

		if (b.type == AudioBandType::Line) {
			ImGui::Separator();
			ImGui::TextUnformatted("Line Geometry");
			SliderFloatWithInput("Length", &b.length01, 0.05f, 1.0f, "%.2f");
			ImGui::SliderAngle("Angle", &b.angleRad, -90.0f, 90.0f);
		}

		ImGui::Separator();
		ImGui::TextUnformatted("Band Settings");

		SliderFloatWithInput("fLow (Hz)", &b.fLowHz, 0.0f, 20000.0f, "%.1f");
		SliderFloatWithInput("fHigh (Hz)", &b.fHighHz, 0.0f, 20000.0f, "%.1f");
		if (b.fHighHz < b.fLowHz) std::swap(b.fLowHz, b.fHighHz);

		ImGui::Separator();
		ImGui::TextUnformatted("General Settings");

		SliderFloatWithInput("Gain", &b.gain, -2.0f * gain_factor, 2.0f * gain_factor, "%.2f");
		SliderFloatWithInput("Stiffness", &b.stiffness, 0.0f, 1000.0f, "%.2f");
		SliderFloatWithInput("Damping", &b.damping, 0.0f, 200.0f, "%.2f");
		SliderFloatWithInput("Radius/Width (cells)", &b.radiusCells, 1.0f, 30.0f, "%.1f");
		SliderFloatWithInput("Center U", &b.pos01.x, 0.0f, 1.0f, "%.3f");
		SliderFloatWithInput("Center V", &b.pos01.y, 0.0f, 1.0f, "%.3f");

		ImGui::Separator();
		ImGui::TextUnformatted("Envelope Settings");

		SliderFloatWithInput("Threshold", &b.threshold, 0.0f, 2.0f, "%.3f");
		SliderFloatWithInput("Attack (1/s)", &b.attack, 0.1f, 60.0f, "%.2f");
		SliderFloatWithInput("Release (1/s)", &b.release, 0.1f, 60.0f, "%.2f");

		ImGui::Separator();
		ImGui::TextUnformatted("Automatic Gain Control");
		ImGui::Checkbox("Enable", &b.agcEnabled);
		if (b.agcEnabled) {
			SliderFloatWithInput("AGC time (s)", &b.agcTimeSec, 0.1f, 6.0f, "%.2f");
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
