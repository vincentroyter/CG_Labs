#include "AudioUI.hpp"
#include <imgui.h>
#include <algorithm>
#include <cmath>

static bool SliderFloatWithInput(const char* label, float* value, float min, float max, const char* format = "%.2f")
{
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

#include <cmath>
#include <vector>

#include <cmath> // std::isfinite, std::log10

void AudioUI::drawSpectrumWindow(AudioUIState& s)
{
	if (!s.showSpectrumWindow || !(*s.showSpectrumWindow)) return;

	if (!ImGui::Begin("Spectrum", s.showSpectrumWindow)) {
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

	bool freeze = (s.freezeSpectrum && *s.freezeSpectrum);
	float smooth = (s.spectrumSmooth ? *s.spectrumSmooth : 0.0f);
	smooth = std::clamp(smooth, 0.0f, 0.99f);

	float maxHz = (s.spectrumMaxHz ? *s.spectrumMaxHz : 20000.0f);
	maxHz = std::clamp(maxHz, 1000.0f, 20000.0f);

	if (s.spectrumSmooth) ImGui::SliderFloat("Smooth", s.spectrumSmooth, 0.0f, 0.95f, "%.2f");
	if (s.spectrumMaxHz) ImGui::SliderFloat("Max Hz", s.spectrumMaxHz, 1000.0f, 20000.0f, "%.0f");

	const float nyq = 0.5f * float(sr);
	const int N = (int)spec.size(); // fftSize/2

	int binsToShow = (int)std::round((maxHz / nyq) * float(N));
	binsToShow = std::clamp(binsToShow, 8, N);


	// ---- Debug min/max + NaN check in shown range ----
	float vMin = +1e30f, vMax = -1e30f;
	int badCount = 0;
	for (int i = 0; i < binsToShow; ++i) {
		float v = spec[i];
		if (!std::isfinite(v)) { badCount++; continue; }
		vMin = std::min(vMin, v);
		vMax = std::max(vMax, v);
	}
	if (vMin > vMax) { vMin = 0.0f; vMax = 0.0f; }

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

		float db = 20.0f * std::log10(v / peak); // 0 at peak
		db = std::clamp(db, dbMin, 0.0f);

		float y = (db - dbMin) / (-dbMin);
		disp[i] = std::clamp(y, 0.0f, 1.0f);
	}

	if (!freeze) {
		float a = 1.0f - smooth;
		for (int i = 0; i < binsToShow; ++i) {
			dispSm[i] = dispSm[i] + a * (disp[i] - dispSm[i]);
		}
	}

	// ---- Plot as histogram (much more visible than a thin line) ----
	ImVec2 plotSize(0.0f, 200.0f); // 0 = auto width (avoid -1)
	ImGui::PlotHistogram("##spec_hist",
		dispSm.data(),
		binsToShow,
		0,
		"",
		0.0f, 1.0f,
		plotSize
	);

	// Hover tooltip with correct Hz (MUST be right after PlotHistogram!)
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
		ImGui::Text("Hz:  %.1f", hz);
		ImGui::Text("dB:  %.1f", db);
		ImGui::EndTooltip();
	}

	if (s.freezeSpectrum) ImGui::Checkbox("Freeze", s.freezeSpectrum);

	ImGui::End();
}



void AudioUI::draw(AudioUIState& s)
{
	if (!ImGui::Begin("Audio")) {
		ImGui::End();
		return;
	}

	// Enable
	if (s.enabled) {
		ImGui::Checkbox("Enable audio driving", s.enabled);
	}

	// File / playback
	ImGui::Separator();
	ImGui::Text("File and playback");

	if (ImGui::Button("Load WAV...")) {
		if (s.onLoadWav) s.onLoadWav();
	}

	bool loaded = (s.isLoaded && *s.isLoaded);
	bool playing = (s.isPlaying && *s.isPlaying);

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

	if (s.volume) {
		SliderFloatWithInput("Volume", s.volume, 0.0f, 1.0f, "%.2f");
	}

	// Seek (pause while dragging to avoid artifacts)
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
	ImGui::Text("Windows");
	if (s.showSpectrumWindow) ImGui::Checkbox("Spectrum window", s.showSpectrumWindow);


	// Global stability params for audio driving
	ImGui::Separator();
	ImGui::Text("Audio driver physics");
	if (s.k_stiff) SliderFloatWithInput("Stiffness k", s.k_stiff, 10.0f, 400.0f, "%.1f");
	if (s.d_damp)  SliderFloatWithInput("Damping d", s.d_damp, 0.0f, 80.0f, "%.1f");

	// Band sources
	ImGui::Separator();
	ImGui::Text("Band sources");

	if (ImGui::Button("Add band")) {
		if (s.bands && s.bandCounter && s.selectedBand) {
			AudioBandSource b;
			b.name = "Band " + std::to_string((*s.bandCounter)++);
			b.fLowHz = 0.0f;
			b.fHighHz = 200.0f;
			b.pos01 = { 0.5f, 0.5f };
			s.bands->push_back(b);
			*s.selectedBand = int(s.bands->size()) - 1;
		}
	}

	ImGui::SameLine();
	bool canDup = (s.bands && s.selectedBand && *s.selectedBand >= 0 &&
		*s.selectedBand < (int)s.bands->size());
	if (!canDup) ImGui::BeginDisabled();

	if (ImGui::Button("Duplicate") && canDup) {
		auto& bands = *s.bands;
		int idx = *s.selectedBand;

		AudioBandSource copy = bands[idx];          // copy ALL settings
		copy.name = copy.name + " copy";

		// Reset runtime-only state so it behaves cleanly
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
		*s.selectedBand = (int)bands.size() - 1;

		if (s.bandCounter) (*s.bandCounter)++;
	}

	if (!canDup) ImGui::EndDisabled();

	ImGui::Separator();

	if (s.bands && s.selectedBand) {
		if (ImGui::BeginListBox("##bands", ImVec2(-FLT_MIN, 140.0f))) {
			for (int i = 0; i < (int)s.bands->size(); ++i) {
				const auto& b = (*s.bands)[i];
				std::string label = b.name;
				if (!b.enabled) label += " (off)";
				if (ImGui::Selectable(label.c_str(), *s.selectedBand == i)) {
					*s.selectedBand = i;
				}
			}
			ImGui::EndListBox();
		}

		// --- Selected band UI (copy-paste replacement) ---
		// NOTE: This version avoids ImGui::SeparatorText() (older ImGui compatibility).

		if (*s.selectedBand >= 0 && *s.selectedBand < (int)s.bands->size()) {
			auto& b = (*s.bands)[*s.selectedBand];

			ImGui::Separator();
			ImGui::Text("Selected: %s", b.name.c_str());
			ImGui::Checkbox("Enabled", &b.enabled);

			// ---- Type ----
			const char* types[] = { "Point", "Line" };
			int t = (int)b.type;
			ImGui::Combo("Type", &t, types, IM_ARRAYSIZE(types));
			b.type = (AudioBandType)t;

			// ---- Drive mode ----
			const char* modes[] = { "Envelope", "Impulse", "Hybrid" };
			int m = (int)b.mode;
			ImGui::Combo("Drive", &m, modes, IM_ARRAYSIZE(modes));
			b.mode = (AudioDriveMode)m;

			const bool showEnv = (b.mode == AudioDriveMode::Envelope || b.mode == AudioDriveMode::Hybrid);
			const bool showImp = (b.mode == AudioDriveMode::Impulse || b.mode == AudioDriveMode::Hybrid);

			// ---- Band ----
			ImGui::Separator();
			ImGui::TextUnformatted("Band");

			SliderFloatWithInput("fLow (Hz)", &b.fLowHz, 0.0f, 20000.0f, "%.1f");
			SliderFloatWithInput("fHigh (Hz)", &b.fHighHz, 0.0f, 20000.0f, "%.1f");
			if (b.fHighHz < b.fLowHz) std::swap(b.fLowHz, b.fHighHz);

			SliderFloatWithInput("Radius/Width (cells)", &b.radiusCells, 1.0f, 80.0f, "%.1f");
			SliderFloatWithInput("Center U", &b.pos01.x, 0.0f, 1.0f, "%.3f");
			SliderFloatWithInput("Center V", &b.pos01.y, 0.0f, 1.0f, "%.3f");

			// ---- Envelope controls ----
			if (showEnv) {
				ImGui::Separator();
				ImGui::TextUnformatted("Envelope (slow / continuous)");

				SliderFloatWithInput("Gain (envelope)", &b.gain, -50.0f, 50.0f, "%.2f");
				SliderFloatWithInput("Threshold (env)", &b.threshold, 0.0f, 2.0f, "%.3f");
				SliderFloatWithInput("Attack (1/s)", &b.attack, 0.1f, 60.0f, "%.2f");
				SliderFloatWithInput("Release (1/s)", &b.release, 0.1f, 60.0f, "%.2f");
			}

			// ---- Impulse controls ----
			if (showImp) {
				ImGui::Separator();
				ImGui::TextUnformatted("Impulse / onsets (transients)");

				SliderFloatWithInput("Impulse gain", &b.impulseGain, -80.0f, 80.0f, "%.2f");
				SliderFloatWithInput("Onset threshold", &b.onsetThreshold, 0.0f, 5.0f, "%.3f");
				SliderFloatWithInput("Cooldown (s)", &b.impulseCooldownSec, 0.0f, 0.5f, "%.3f");
				SliderFloatWithInput("Flux smooth (1/s)", &b.fluxSmoothRate, 0.0f, 80.0f, "%.1f");
				SliderFloatWithInput("Onset HP time (s)", &b.onsetHPTimeSec, 0.02f, 1.5f, "%.3f");
			}


			// ---- AGC ----
			ImGui::Separator();
			ImGui::TextUnformatted("AGC (auto normalization)");

			ImGui::Checkbox("Enable AGC", &b.agcEnabled);
			if (b.agcEnabled) {
				SliderFloatWithInput("AGC time (s)", &b.agcTimeSec, 0.1f, 6.0f, "%.2f");
			}

			// ---- Line settings ----
			if (b.type == AudioBandType::Line) {
				ImGui::Separator();
				ImGui::TextUnformatted("Line geometry");

				SliderFloatWithInput("Length", &b.length01, 0.05f, 1.0f, "%.2f");
				ImGui::SliderAngle("Angle", &b.angleRad, -180.0f, 180.0f);
			}

			// ---- Debug (actionable) ----
			ImGui::Separator();
			ImGui::TextUnformatted("Debug");

			ImGui::Text("Energy raw:  %.6f", b.energyRaw);
			ImGui::Text("AGC running: %.6f", b.agcRunning);
			ImGui::Text("Energy norm: %.4f", b.energyNorm);

			if (showEnv) {
				ImGui::Text("Env (smoothed): %.4f  (thr=%.3f)", b.energySmoothed, b.threshold);

				float envMeter = 0.0f;
				if (b.threshold > 1e-6f) envMeter = b.energySmoothed / (b.threshold * 2.0f);
				envMeter = std::clamp(envMeter, 0.0f, 1.0f);
				ImGui::ProgressBar(envMeter, ImVec2(-1, 0), "Env level (rel)");
			}

			if (showImp) {
				ImGui::Text("Flux smoothed: %.4f  (thr=%.3f)", b.fluxSmoothed, b.onsetThreshold);
				ImGui::Text("Cooldown: %.3f / %.3f", b.cooldownTimer, b.impulseCooldownSec);
				ImGui::Text("Baseline (slow): %.4f", b.energySlow);


				const bool ready = (b.cooldownTimer <= 0.0f);
				const bool above = (b.fluxSmoothed > b.onsetThreshold);

				ImGui::Text("Trigger: %s",
					(ready && above) ? "READY (will fire)"
					: (above ? "Blocked (cooldown)" : "Not above threshold"));

				float fluxMeter = 0.0f;
				if (b.onsetThreshold > 1e-6f) fluxMeter = b.fluxSmoothed / (b.onsetThreshold * 2.0f);
				fluxMeter = std::clamp(fluxMeter, 0.0f, 1.0f);
				ImGui::ProgressBar(fluxMeter, ImVec2(-1, 0), "Flux level (rel)");
			}

			ImGui::Separator();
			if (ImGui::Button("Delete band")) {
				s.bands->erase(s.bands->begin() + *s.selectedBand);
				if (s.bands->empty())
					*s.selectedBand = -1;
				else
					*s.selectedBand = std::clamp(*s.selectedBand, 0, (int)s.bands->size() - 1);
			}
		}
		else {
			ImGui::TextDisabled("No band selected.");
		}

	}

	ImGui::End();
}

