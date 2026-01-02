#include "WaterUI.hpp"
#include "DriverSource.hpp"

#include <imgui.h>
#include <algorithm>
#include <string>

// Slider with input widget
static bool SliderFloatWithInput(const char* label, float* value, float min, float max, const char* format = "%.2f")
{
	ImGui::PushID(label);
	ImGui::TextUnformatted(label);
	ImGui::SameLine();

	ImGui::PushItemWidth(130.0f);
	bool changed1 = ImGui::SliderFloat("##slider", value, min, max, format);
	ImGui::SameLine();

	ImGui::PushItemWidth(60.0f);
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

	ImGui::PushItemWidth(60.0f);
	int step = 1;
	bool changed2 = ImGui::InputInt("##input", value, step, step, ImGuiInputTextFlags_EnterReturnsTrue);
	ImGui::PopItemWidth();
	ImGui::PopItemWidth();

	*value = std::clamp(*value, min, max);
	ImGui::PopID();
	return changed1 || changed2;
}

void WaterUI::drawMain(WaterUIState& s, int currentNsim, int currentNr, float fps)
{
	if (!ImGui::Begin("Water Project")) {
		ImGui::End();
		return;
	}

	// Presets row (in main window)
	if (s.savePreset && ImGui::Button("Save Preset")) {
		s.savePreset();
	}
	ImGui::SameLine();
	if (s.loadPreset && ImGui::Button("Load Preset")) {
		s.loadPreset();
	}
	ImGui::Separator();

	ImGui::Text("FPS: %.1f", fps);
	ImGui::Separator();

	ImGui::Text("Camera and simulation");
	if (ImGui::Button("Top-down view")) s.setCameraTopDown();
	ImGui::SameLine();
	if (ImGui::Button("Default view")) s.setCameraDefault();
	ImGui::SameLine();
	if (ImGui::Button("Reset surface")) s.resetSurface();

	ImGui::Separator();
	ImGui::Text("Windows");
	if (s.showDriverWindow) ImGui::Checkbox("Driver sources window", s.showDriverWindow);
	if (s.showAudioWindow)  ImGui::Checkbox("Audio window", s.showAudioWindow);

	ImGui::Separator();
	if (ImGui::CollapsingHeader("Mesh Resolution (recreate on Apply)")) {
		ImGui::Text("Current: Nsim=%d, Nr=%d", currentNsim, currentNr);

		SliderIntWithInput("Nsim (simulation)", s.Nsim, 16, 512);
		SliderIntWithInput("Nr (render mesh)", s.Nr, 32, 2048);

		ImGui::Text("Pending: Nsim=%d, Nr=%d", *s.Nsim, *s.Nr);

		bool canApply = (*s.Nsim != currentNsim) || (*s.Nr != currentNr);

		ImGui::BeginDisabled(!canApply || !s.applyResolution);
		if (ImGui::Button("Apply")) {
			s.applyResolution(*s.Nsim, *s.Nr);
		}
		ImGui::EndDisabled();

		ImGui::SameLine();
		ImGui::BeginDisabled(!canApply);
		if (ImGui::Button("Revert")) {
			*s.Nsim = currentNsim;
			*s.Nr = currentNr;
		}
		ImGui::EndDisabled();
	}

	if (ImGui::CollapsingHeader("Global simulation", ImGuiTreeNodeFlags_DefaultOpen)) {
		SliderFloatWithInput("Wave speed c", s.waveSpeed, 0.2f, 3.0f, "%.2f");
		SliderFloatWithInput("Damping gamma (1/s)", s.velDamp, 0.0f, 6.0f, "%.2f");
		SliderFloatWithInput("Clamp maxSlope", s.maxSlope, 0.05f, 3.5f, "%.2f");
		SliderFloatWithInput("Viscosity nu", s.viscosity, 0.0f, 0.05f, "%.4f");
		ImGui::Checkbox("Lock water level", s.setLockWaterLevel);
		ImGui::Checkbox("Use open boundaries", s.openBoundary);


		ImGui::Separator();
		ImGui::Text("Rendering");
		ImGui::Checkbox("Height sampling: NEAREST", s.nearestHeight);

		ImGui::Separator();
		ImGui::Text("Node lines");
		ImGui::Checkbox("Show node lines", s.showNodes);
		SliderFloatWithInput("Node threshold", s.nodeEps, 0.0001f, 0.05f, "%.4f");
		SliderFloatWithInput("Node strength", s.nodeStrength, 0.0f, 1.0f, "%.2f");

		ImGui::Separator();
		ImGui::Text("Visual effects");

		if (s.useHeightColoring) ImGui::Checkbox("Height coloring", s.useHeightColoring);

		if (s.colorTheme) {
			bool enableTheme = (s.useHeightColoring && *s.useHeightColoring);
			ImGui::BeginDisabled(!enableTheme);
			const char* themes[] = { "Ocean", "Thermal", "Psychedelic" };
			ImGui::Combo("Theme", s.colorTheme, themes, IM_ARRAYSIZE(themes));
			ImGui::EndDisabled();
		}

		ImGui::Separator();
		ImGui::Text("Specular");
		if (s.enableSpecular) ImGui::Checkbox("Enable specular", s.enableSpecular);

		bool specOn = (s.enableSpecular && *s.enableSpecular);
		if (s.specularStrength) {
			ImGui::BeginDisabled(!specOn);
			SliderFloatWithInput("Specular strength", s.specularStrength, 0.0f, 1.5f, "%.3f");
			ImGui::EndDisabled();
		}
		if (s.specularPower) {
			ImGui::BeginDisabled(!specOn);
			SliderFloatWithInput("Specular power", s.specularPower, 1.0f, 256.0f, "%.1f");
			ImGui::EndDisabled();
		}

		ImGui::Separator();
		ImGui::Text("Derivative visualizers");

		if (ImGui::CollapsingHeader("Velocity magnitude", ImGuiTreeNodeFlags_DefaultOpen)) {
			if (s.velocityEnabled) ImGui::Checkbox("Enable velocity tint", s.velocityEnabled);

			bool on = (s.velocityEnabled && *s.velocityEnabled);
			ImGui::BeginDisabled(!on);

			if (s.velocityScale)     SliderFloatWithInput("Sensitivity", s.velocityScale, 0.0f, 10.0f, "%.2f");
			if (s.velocityThreshold) SliderFloatWithInput("Threshold", s.velocityThreshold, 0.0f, 5.0f, "%.2f");
			if (s.velocityStrength)  SliderFloatWithInput("Strength", s.velocityStrength, 0.0f, 2.0f, "%.2f");

			if (s.velocityColor) {
				ImGui::TextUnformatted("Color");
				ImGui::ColorEdit3("##velcol", s.velocityColor);
			}

			ImGui::EndDisabled();
		}

		if (ImGui::CollapsingHeader("Slope magnitude (1st derivative)", ImGuiTreeNodeFlags_DefaultOpen)) {
			if (s.slopeEnabled) ImGui::Checkbox("Enable slope tint", s.slopeEnabled);

			bool on = (s.slopeEnabled && *s.slopeEnabled);
			ImGui::BeginDisabled(!on);

			if (s.slopeScale)     SliderFloatWithInput("Sensitivity", s.slopeScale, 0.0f, 10.0f, "%.2f");
			if (s.slopeThreshold) SliderFloatWithInput("Threshold", s.slopeThreshold, 0.0f, 2.0f, "%.2f");
			if (s.slopeStrength)  SliderFloatWithInput("Strength", s.slopeStrength, 0.0f, 2.0f, "%.2f");

			if (s.slopeColor) {
				ImGui::TextUnformatted("Color");
				ImGui::ColorEdit3("##slopecol", s.slopeColor);
			}

			ImGui::EndDisabled();
		}

		if (ImGui::CollapsingHeader("Curvature magnitude (2nd derivative)", ImGuiTreeNodeFlags_DefaultOpen)) {
			if (s.curvatureEnabled) ImGui::Checkbox("Enable curvature tint", s.curvatureEnabled);

			bool on = (s.curvatureEnabled && *s.curvatureEnabled);
			ImGui::BeginDisabled(!on);

			if (s.curvatureScale)     SliderFloatWithInput("Sensitivity", s.curvatureScale, 0.0f, 50.0f, "%.2f");
			if (s.curvatureThreshold) SliderFloatWithInput("Threshold", s.curvatureThreshold, 0.0f, 5.0f, "%.2f");
			if (s.curvatureStrength)  SliderFloatWithInput("Strength", s.curvatureStrength, 0.0f, 2.0f, "%.2f");

			if (s.curvatureColor) {
				ImGui::TextUnformatted("Color");
				ImGui::ColorEdit3("##curvcol", s.curvatureColor);
			}

			ImGui::EndDisabled();
		}
	}

	if (ImGui::CollapsingHeader("Mouse interaction")) {
		SliderFloatWithInput("Click/press strength", s.clickMag, -50.0f, 50.0f, "%.2f");
		SliderIntWithInput("Splash radius (cells)", s.clickRadius, 1, 25);
	}

	ImGui::End();
}

void WaterUI::drawDriversWindow(WaterUIState& s)
{
	if (!ImGui::Begin("Driver Sources")) {
		ImGui::End();
		return;
	}

	if (ImGui::Button("Add point")) {
		DriverSource d;
		d.type = DriverType::Point;
		d.name = "Point " + std::to_string((*s.driverCounter)++);
		s.drivers->push_back(d);
		*s.selectedDriver = int(s.drivers->size()) - 1;
	}
	ImGui::SameLine();
	if (ImGui::Button("Add line")) {
		DriverSource d;
		d.type = DriverType::Line;
		d.name = "Line " + std::to_string((*s.driverCounter)++);
		s.drivers->push_back(d);
		*s.selectedDriver = int(s.drivers->size()) - 1;
	}

	ImGui::Separator();

	if (ImGui::BeginListBox("##drivers", ImVec2(-FLT_MIN, 140.0f))) {
		for (int i = 0; i < (int)s.drivers->size(); ++i) {
			std::string label = (*s.drivers)[i].name;
			if (ImGui::Selectable(label.c_str(), *s.selectedDriver == i)) {
				*s.selectedDriver = i;
			}
		}
		ImGui::EndListBox();
	}

	if (*s.selectedDriver >= 0 && *s.selectedDriver < (int)s.drivers->size()) {
		auto& d = (*s.drivers)[*s.selectedDriver];

		ImGui::Separator();
		ImGui::Text("Selected: %s", d.name.c_str());

		ImGui::Checkbox("Enabled", &d.enabled);
		ImGui::Checkbox("Oscillation (sine)", &d.oscOn);
		ImGui::SameLine();
		ImGui::Checkbox("Spin/orbit", &d.spinOn);

		ImGui::Separator();
		SliderFloatWithInput("Source Strength", &d.amp, -1.0f, 1.0f, "%.2f");
		SliderFloatWithInput("Width", &d.width, 1.0f, 20.0f, "%.1f");
		SliderFloatWithInput("Center U", &d.pos01.x, 0.0f, 1.0f, "%.3f");
		SliderFloatWithInput("Center V", &d.pos01.y, 0.0f, 1.0f, "%.3f");

		ImGui::Separator();
		ImGui::Text("Oscillation");
		ImGui::BeginDisabled(!d.oscOn);
		SliderFloatWithInput("Frequency (Hz)", &d.freqHz, 0.1f, 20.0f, "%.2f");
		ImGui::SliderAngle("Phase (deg)", &d.oscPhaseRad, -180.0f, 180.0f);
		ImGui::EndDisabled();

		ImGui::Separator();
		ImGui::Text("Spin/orbit");
		ImGui::BeginDisabled(!d.spinOn);
		SliderFloatWithInput("Spin frequency (Hz)", &d.spinHz, 0.0f, 5.0f, "%.2f");
		ImGui::SliderAngle("Spin phase", &d.spinPhaseRad, -180.0f, 180.0f);
		if (d.type == DriverType::Point) {
			SliderFloatWithInput("Orbit radius", &d.orbitRadius01, 0.0f, 0.35f, "%.3f");
		}
		ImGui::EndDisabled();

		if (d.type == DriverType::Line) {
			ImGui::Separator();
			ImGui::Text("Line shape");
			SliderFloatWithInput("Length", &d.length01, 0.05f, 1.0f, "%.2f");
			ImGui::SliderAngle("Base orientation", &d.angleRad, -180.0f, 180.0f);
		}

		ImGui::Separator();
		if (ImGui::Button("Delete source")) {
			s.drivers->erase(s.drivers->begin() + *s.selectedDriver);
			if (s.drivers->empty())
				*s.selectedDriver = -1;
			else
				*s.selectedDriver = std::clamp(*s.selectedDriver, 0, (int)s.drivers->size() - 1);
		}
	}
	else {
		ImGui::TextDisabled("No driver selected.");
	}

	ImGui::End();
}
