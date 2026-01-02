#include "WaterUI.hpp"
#include "DriverSource.hpp"

#include <imgui.h>
#include <algorithm>
#include <string>

// Slider with input widget
static bool SliderFloatWithInput(const char* label, float* value, float min, float max, const char* format = "%.2f")
{
	if (!value) return false;

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
	if (!value) return false;

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

	// Settings must exist
	if (!s.settings) {
		ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Error: uiState.settings is null.");
		ImGui::End();
		return;
	}
	auto& set = *s.settings;

	// Presets row
	if (s.savePreset && ImGui::Button("Save Preset")) s.savePreset();
	ImGui::SameLine();
	if (s.loadPreset && ImGui::Button("Load Preset")) s.loadPreset();
	ImGui::Separator();

	ImGui::Text("FPS: %.1f", fps);
	ImGui::Separator();

	ImGui::TextUnformatted("Camera and simulation");
	if (ImGui::Button("Top-down view") && s.setCameraTopDown) s.setCameraTopDown();
	ImGui::SameLine();
	if (ImGui::Button("Default view") && s.setCameraDefault) s.setCameraDefault();
	ImGui::SameLine();
	if (ImGui::Button("Reset surface") && s.resetSurface) s.resetSurface();

	ImGui::Separator();
	ImGui::TextUnformatted("Windows");
	ImGui::Checkbox("Driver sources window", &set.win.showDriverWindow);
	ImGui::Checkbox("Audio window", &set.win.showAudioWindow);

	ImGui::Separator();
	if (ImGui::CollapsingHeader("Mesh Resolution (recreate on Apply)")) {
		ImGui::Text("Current: Nsim=%d, Nr=%d", currentNsim, currentNr);

		SliderIntWithInput("Nsim (simulation)", &set.res.Nsim, 16, 512);
		SliderIntWithInput("Nr (render mesh)", &set.res.Nr, 32, 2048);

		ImGui::Text("Pending: Nsim=%d, Nr=%d", set.res.Nsim, set.res.Nr);

		const bool canApply = (set.res.Nsim != currentNsim) || (set.res.Nr != currentNr);

		ImGui::BeginDisabled(!canApply || !s.applyResolution);
		if (ImGui::Button("Apply") && s.applyResolution) {
			s.applyResolution(set.res.Nsim, set.res.Nr);
		}
		ImGui::EndDisabled();

		ImGui::SameLine();
		ImGui::BeginDisabled(!canApply);
		if (ImGui::Button("Revert")) {
			set.res.Nsim = currentNsim;
			set.res.Nr = currentNr;
		}
		ImGui::EndDisabled();
	}

	if (ImGui::CollapsingHeader("Global simulation", ImGuiTreeNodeFlags_DefaultOpen)) {
		SliderFloatWithInput("Wave speed c", &set.sim.c, 0.2f, 3.0f, "%.2f");
		SliderFloatWithInput("Damping gamma (1/s)", &set.sim.velDamp, 0.0f, 6.0f, "%.2f");
		SliderFloatWithInput("Clamp maxSlope", &set.sim.maxSlope, 0.05f, 3.5f, "%.2f");
		SliderFloatWithInput("Viscosity nu", &set.sim.viscosity, 0.0f, 0.06f, "%.4f");
		ImGui::Checkbox("Lock water level", &set.sim.lockWaterLevel);
		ImGui::Checkbox("Use open boundaries", &set.sim.openBoundary);

		ImGui::Separator();
		ImGui::TextUnformatted("Rendering");
		ImGui::Checkbox("Height sampling: NEAREST", &set.render.nearestHeight);

		ImGui::Separator();
		ImGui::TextUnformatted("Node lines");
		ImGui::Checkbox("Show node lines", &set.render.vis.showNodes);
		SliderFloatWithInput("Node threshold", &set.render.vis.nodeEps, 0.0001f, 0.05f, "%.4f");
		SliderFloatWithInput("Node strength", &set.render.vis.nodeStrength, 0.0f, 1.0f, "%.2f");

		ImGui::Separator();
		ImGui::TextUnformatted("Visual effects");
		ImGui::Checkbox("Height coloring", &set.render.vis.useHeightColoring);
		{
			const bool enableTheme = set.render.vis.useHeightColoring;
			ImGui::BeginDisabled(!enableTheme);
			const char* themes[] = { "Ocean", "Thermal", "Psychedelic" };
			ImGui::Combo("Theme", &set.render.vis.colorTheme, themes, IM_ARRAYSIZE(themes));
			ImGui::EndDisabled();
		}

		ImGui::Separator();
		ImGui::TextUnformatted("Specular");
		ImGui::Checkbox("Enable specular", &set.render.vis.enableSpecular);
		{
			const bool specOn = set.render.vis.enableSpecular;
			ImGui::BeginDisabled(!specOn);
			SliderFloatWithInput("Specular strength", &set.render.vis.specularStrength, 0.0f, 1.5f, "%.3f");
			SliderFloatWithInput("Specular power", &set.render.vis.specularPower, 1.0f, 256.0f, "%.1f");
			ImGui::EndDisabled();
		}

		ImGui::Separator();
		ImGui::TextUnformatted("Derivative visualizers");

		if (ImGui::CollapsingHeader("Velocity magnitude", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::PushID("vel");
			ImGui::Checkbox("Enable velocity tint", &set.render.vis.velocityEnabled);

			const bool on = set.render.vis.velocityEnabled;
			ImGui::BeginDisabled(!on);

			SliderFloatWithInput("Sensitivity", &set.render.vis.velocityScale, 0.0f, 10.0f, "%.2f");
			SliderFloatWithInput("Threshold", &set.render.vis.velocityThreshold, 0.0f, 5.0f, "%.2f");
			SliderFloatWithInput("Strength", &set.render.vis.velocityStrength, 0.0f, 2.0f, "%.2f");

			ImGui::TextUnformatted("Color");
			ImGui::ColorEdit3("##velcol", set.render.vis.velocityColor);

			ImGui::EndDisabled();
			ImGui::PopID();
		}

		if (ImGui::CollapsingHeader("Slope magnitude (1st derivative)", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::PushID("slope");
			ImGui::Checkbox("Enable slope tint", &set.render.vis.slopeEnabled);

			const bool on = set.render.vis.slopeEnabled;
			ImGui::BeginDisabled(!on);

			SliderFloatWithInput("Sensitivity", &set.render.vis.slopeScale, 0.0f, 10.0f, "%.2f");
			SliderFloatWithInput("Threshold", &set.render.vis.slopeThreshold, 0.0f, 2.0f, "%.2f");
			SliderFloatWithInput("Strength", &set.render.vis.slopeStrength, 0.0f, 2.0f, "%.2f");

			ImGui::TextUnformatted("Color");
			ImGui::ColorEdit3("##slopecol", set.render.vis.slopeColor);

			ImGui::EndDisabled();
			ImGui::PopID();
		}

		if (ImGui::CollapsingHeader("Curvature magnitude (2nd derivative)", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::PushID("curve");
			ImGui::Checkbox("Enable curvature tint", &set.render.vis.curvatureEnabled);

			const bool on = set.render.vis.curvatureEnabled;
			ImGui::BeginDisabled(!on);

			SliderFloatWithInput("Sensitivity", &set.render.vis.curvatureScale, 0.0f, 50.0f, "%.2f");
			SliderFloatWithInput("Threshold", &set.render.vis.curvatureThreshold, 0.0f, 5.0f, "%.2f");
			SliderFloatWithInput("Strength", &set.render.vis.curvatureStrength, 0.0f, 2.0f, "%.2f");

			ImGui::TextUnformatted("Color");
			ImGui::ColorEdit3("##curvcol", set.render.vis.curvatureColor);

			ImGui::EndDisabled();
			ImGui::PopID();
		}
	}

	if (ImGui::CollapsingHeader("Mouse interaction")) {
		SliderFloatWithInput("Click/press strength", &set.mouse.clickMag, -50.0f, 50.0f, "%.2f");
		SliderIntWithInput("Splash radius (cells)", &set.mouse.clickRadius, 1, 25);
	}

	ImGui::End();
}

void WaterUI::drawDriversWindow(WaterUIState& s)
{
	if (!ImGui::Begin("Driver Sources")) {
		ImGui::End();
		return;
	}

	// Settings must exist
	if (!s.settings) {
		ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Error: uiState.settings is null.");
		ImGui::End();
		return;
	}
	auto& set = *s.settings;

	// These must exist inside WaterSettings
	auto& drivers = set.drivers;
	int& sel = set.selectedDriver;
	int& counter = set.driverCounter;

	if (drivers.empty()) sel = -1;
	else sel = std::clamp(sel, -1, (int)drivers.size() - 1);

	// Add buttons
	if (ImGui::Button("Add point")) {
		DriverSource d;
		d.type = DriverType::Point;
		d.name = "Point " + std::to_string(counter++);
		drivers.push_back(d);
		sel = (int)drivers.size() - 1;
	}
	ImGui::SameLine();
	if (ImGui::Button("Add line")) {
		DriverSource d;
		d.type = DriverType::Line;
		d.name = "Line " + std::to_string(counter++);
		drivers.push_back(d);
		sel = (int)drivers.size() - 1;
	}

	ImGui::Separator();

	// List
	if (ImGui::BeginListBox("##drivers", ImVec2(-FLT_MIN, 140.0f))) {
		for (int i = 0; i < (int)drivers.size(); ++i) {
			std::string label = drivers[i].name;
			if (!drivers[i].enabled) label += " (off)";
			if (ImGui::Selectable(label.c_str(), sel == i)) {
				sel = i;
			}
		}
		ImGui::EndListBox();
	}

	// Selected
	if (sel >= 0 && sel < (int)drivers.size()) {
		auto& d = drivers[sel];

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
		ImGui::TextUnformatted("Oscillation");
		ImGui::BeginDisabled(!d.oscOn);
		SliderFloatWithInput("Frequency (Hz)", &d.freqHz, 0.1f, 20.0f, "%.2f");
		ImGui::SliderAngle("Phase (deg)", &d.oscPhaseRad, -180.0f, 180.0f);
		ImGui::EndDisabled();

		ImGui::Separator();
		ImGui::TextUnformatted("Spin/orbit");
		ImGui::BeginDisabled(!d.spinOn);
		SliderFloatWithInput("Spin frequency (Hz)", &d.spinHz, 0.0f, 5.0f, "%.2f");
		ImGui::SliderAngle("Spin phase", &d.spinPhaseRad, -180.0f, 180.0f);
		if (d.type == DriverType::Point) {
			SliderFloatWithInput("Orbit radius", &d.orbitRadius01, 0.0f, 0.35f, "%.3f");
		}
		ImGui::EndDisabled();

		if (d.type == DriverType::Line) {
			ImGui::Separator();
			ImGui::TextUnformatted("Line shape");
			SliderFloatWithInput("Length", &d.length01, 0.05f, 1.0f, "%.2f");
			ImGui::SliderAngle("Base orientation", &d.angleRad, -180.0f, 180.0f);
		}

		ImGui::Separator();
		if (ImGui::Button("Delete source")) {
			drivers.erase(drivers.begin() + sel);
			if (drivers.empty()) sel = -1;
			else sel = std::clamp(sel, 0, (int)drivers.size() - 1);
		}
	}
	else {
		ImGui::TextDisabled("No driver selected.");
	}

	ImGui::End();
}
