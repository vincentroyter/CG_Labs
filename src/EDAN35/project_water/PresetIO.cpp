#include "PresetIO.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>
#include <unordered_map>

// Helpers
static inline std::string trim(std::string s)
{
	auto notSpace = [](unsigned char c) { return !std::isspace(c); };
	s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
	s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
	return s;
}

static inline void writeKV(std::ostream& os, const char* key, const std::string& v)
{
	os << key << "=" << v << "\n";
}
static inline void writeKV(std::ostream& os, const char* key, const char* v)
{
	os << key << "=" << v << "\n";
}
static inline void writeKV(std::ostream& os, const char* key, int v)
{
	os << key << "=" << v << "\n";
}
static inline void writeKV(std::ostream& os, const char* key, bool v)
{
	os << key << "=" << (v ? 1 : 0) << "\n";
}
static inline void writeKV(std::ostream& os, const char* key, float v)
{
	os << key << "=" << v << "\n";
}

static inline bool parseBool(const std::string& s, bool& out)
{
	std::string t = s;
	std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	t = trim(t);

	if (t == "1" || t == "true" || t == "yes" || t == "on") { out = true;  return true; }
	if (t == "0" || t == "false" || t == "no" || t == "off") { out = false; return true; }
	return false;
}

static inline bool parseInt(const std::string& s, int& out)
{
	std::istringstream iss(s);
	iss.imbue(std::locale::classic());
	int v;
	if (!(iss >> v)) return false;
	out = v;
	return true;
}

static inline bool parseFloat(const std::string& s, float& out)
{
	std::istringstream iss(s);
	iss.imbue(std::locale::classic());
	float v;
	if (!(iss >> v)) return false;
	out = v;
	return true;
}

static std::unordered_map<std::string, std::string> loadPresetMap(const char* path)
{
	std::unordered_map<std::string, std::string> m;

	std::ifstream is(path);
	if (!is) return m;
	is.imbue(std::locale::classic());

	std::string line;
	while (std::getline(is, line)) {
		line = trim(line);
		if (line.empty()) continue;
		if (line[0] == '#') continue;

		auto eq = line.find('=');
		if (eq == std::string::npos) continue;

		std::string k = trim(line.substr(0, eq));
		std::string v = trim(line.substr(eq + 1));

		if (!k.empty())
			m[k] = v;
	}
	return m;
}

static inline const char* getStr(const std::unordered_map<std::string, std::string>& m,
	const char* key, const char* def)
{
	auto it = m.find(key);
	return (it == m.end()) ? def : it->second.c_str();
}

static inline int getInt(const std::unordered_map<std::string, std::string>& m,
	const char* key, int def)
{
	auto it = m.find(key);
	if (it == m.end()) return def;
	int v;
	return parseInt(it->second, v) ? v : def;
}

static inline float getFloat(const std::unordered_map<std::string, std::string>& m,
	const char* key, float def)
{
	auto it = m.find(key);
	if (it == m.end()) return def;
	float v;
	return parseFloat(it->second, v) ? v : def;
}

static inline bool getBool(const std::unordered_map<std::string, std::string>& m,
	const char* key, bool def)
{
	auto it = m.find(key);
	if (it == m.end()) return def;
	bool v;
	return parseBool(it->second, v) ? v : def;
}

namespace PresetIO
{
	bool Save(const char* path, const WaterSettings& s)
	{
		std::ofstream os(path);
		if (!os) return false;

		os.imbue(std::locale::classic());
		os << std::setprecision(9) << std::fixed;
		os << "# WaterSim preset\n";

		// Resolution
		writeKV(os, "res.Nsim", s.res.Nsim);
		writeKV(os, "res.Nr", s.res.Nr);

		// Sim
		writeKV(os, "sim.c", s.sim.c);
		writeKV(os, "sim.velDamp", s.sim.velDamp);
		writeKV(os, "sim.maxSlope", s.sim.maxSlope);
		writeKV(os, "sim.viscosity", s.sim.viscosity);
		writeKV(os, "sim.openBoundary", s.sim.openBoundary);
		writeKV(os, "sim.lockWaterLevel", s.sim.lockWaterLevel);

		// Mouse
		writeKV(os, "mouse.clickMag", s.mouse.clickMag);
		writeKV(os, "mouse.clickRadius", s.mouse.clickRadius);

		// Render
		writeKV(os, "vis.nearestHeight", s.render.nearestHeight);

		auto const& v = s.render.vis;
		writeKV(os, "vis.showNodes", v.showNodes);
		writeKV(os, "vis.nodeEps", v.nodeEps);
		writeKV(os, "vis.nodeStrength", v.nodeStrength);

		writeKV(os, "vis.useHeightColoring", v.useHeightColoring);
		writeKV(os, "vis.colorTheme", v.colorTheme);

		writeKV(os, "vis.enableSpecular", v.enableSpecular);
		writeKV(os, "vis.specularStrength", v.specularStrength);
		writeKV(os, "vis.specularPower", v.specularPower);

		writeKV(os, "vis.velocityEnabled", v.velocityEnabled);
		writeKV(os, "vis.velocityScale", v.velocityScale);
		writeKV(os, "vis.velocityThreshold", v.velocityThreshold);
		writeKV(os, "vis.velocityStrength", v.velocityStrength);
		writeKV(os, "vis.velocityColorR", v.velocityColor[0]);
		writeKV(os, "vis.velocityColorG", v.velocityColor[1]);
		writeKV(os, "vis.velocityColorB", v.velocityColor[2]);

		writeKV(os, "vis.slopeEnabled", v.slopeEnabled);
		writeKV(os, "vis.slopeScale", v.slopeScale);
		writeKV(os, "vis.slopeThreshold", v.slopeThreshold);
		writeKV(os, "vis.slopeStrength", v.slopeStrength);
		writeKV(os, "vis.slopeColorR", v.slopeColor[0]);
		writeKV(os, "vis.slopeColorG", v.slopeColor[1]);
		writeKV(os, "vis.slopeColorB", v.slopeColor[2]);

		writeKV(os, "vis.curvatureEnabled", v.curvatureEnabled);
		writeKV(os, "vis.curvatureScale", v.curvatureScale);
		writeKV(os, "vis.curvatureThreshold", v.curvatureThreshold);
		writeKV(os, "vis.curvatureStrength", v.curvatureStrength);
		writeKV(os, "vis.curvatureColorR", v.curvatureColor[0]);
		writeKV(os, "vis.curvatureColorG", v.curvatureColor[1]);
		writeKV(os, "vis.curvatureColorB", v.curvatureColor[2]);

		// Windows
		writeKV(os, "ui.showAudioWindow", s.win.showAudioWindow);

		// Audio
		writeKV(os, "audio.enabled", s.audio.enabled);
		writeKV(os, "audio.volume", s.audio.volume);

		writeKV(os, "audio.showSpectrum", s.win.showSpectrumWindow);
		writeKV(os, "audio.freezeSpectrum", s.audio.freezeSpectrum);
		writeKV(os, "audio.spectrumSmooth", s.audio.spectrumSmooth);
		writeKV(os, "audio.spectrumMaxHz", s.audio.spectrumMaxHz);

		// Bands
		writeKV(os, "bands.count", (int)s.audio.bands.size());
		for (int i = 0; i < (int)s.audio.bands.size(); ++i) {
			auto const& b = s.audio.bands[i];
			std::string p = "band." + std::to_string(i) + ".";
			writeKV(os, (p + "name").c_str(), b.name);
			writeKV(os, (p + "enabled").c_str(), b.enabled);
			writeKV(os, (p + "type").c_str(), (int)b.type);

			writeKV(os, (p + "fLowHz").c_str(), b.fLowHz);
			writeKV(os, (p + "fHighHz").c_str(), b.fHighHz);

			writeKV(os, (p + "posx").c_str(), b.pos01.x);
			writeKV(os, (p + "posy").c_str(), b.pos01.y);

			writeKV(os, (p + "gain").c_str(), b.gain);
			writeKV(os, (p + "stiffness").c_str(), b.stiffness);
			writeKV(os, (p + "damping").c_str(), b.damping);
			writeKV(os, (p + "threshold").c_str(), b.threshold);
			writeKV(os, (p + "radiusCells").c_str(), b.radiusCells);
			writeKV(os, (p + "attack").c_str(), b.attack);
			writeKV(os, (p + "release").c_str(), b.release);

			writeKV(os, (p + "agcEnabled").c_str(), b.agcEnabled);
			writeKV(os, (p + "agcTimeSec").c_str(), b.agcTimeSec);

			writeKV(os, (p + "angleRad").c_str(), b.angleRad);
			writeKV(os, (p + "length01").c_str(), b.length01);
		}

		return true;
	}

	bool Load(const char* path, WaterSettings& s)
	{
		auto m = loadPresetMap(path);
		if (m.empty()) return false;

		// Windows
		s.win.showAudioWindow = getBool(m, "ui.showAudioWindow", s.win.showAudioWindow);
		s.win.showSpectrumWindow = getBool(m, "audio.showSpectrum", s.win.showSpectrumWindow);

		// Resolution
		s.res.Nsim = std::clamp(getInt(m, "res.Nsim", s.res.Nsim), 16, 512);
		s.res.Nr = std::clamp(getInt(m, "res.Nr", s.res.Nr), 32, 2048);

		// Sim
		s.sim.c = getFloat(m, "sim.c", s.sim.c);
		s.sim.velDamp = getFloat(m, "sim.velDamp", s.sim.velDamp);
		s.sim.maxSlope = getFloat(m, "sim.maxSlope", s.sim.maxSlope);
		s.sim.viscosity = getFloat(m, "sim.viscosity", s.sim.viscosity);
		s.sim.openBoundary = getBool(m, "sim.openBoundary", s.sim.openBoundary);
		s.sim.lockWaterLevel = getBool(m, "sim.lockWaterLevel", s.sim.lockWaterLevel);

		// Mouse
		s.mouse.clickMag = getFloat(m, "mouse.clickMag", s.mouse.clickMag);
		s.mouse.clickRadius = getInt(m, "mouse.clickRadius", s.mouse.clickRadius);

		// Render
		s.render.nearestHeight = getBool(m, "vis.nearestHeight", s.render.nearestHeight);
		auto& v = s.render.vis;

		v.showNodes = getBool(m, "vis.showNodes", v.showNodes);
		v.nodeEps = getFloat(m, "vis.nodeEps", v.nodeEps);
		v.nodeStrength = getFloat(m, "vis.nodeStrength", v.nodeStrength);

		v.useHeightColoring = getBool(m, "vis.useHeightColoring", v.useHeightColoring);
		v.colorTheme = getInt(m, "vis.colorTheme", v.colorTheme);

		v.enableSpecular = getBool(m, "vis.enableSpecular", v.enableSpecular);
		v.specularStrength = getFloat(m, "vis.specularStrength", v.specularStrength);
		v.specularPower = getFloat(m, "vis.specularPower", v.specularPower);

		v.velocityEnabled = getBool(m, "vis.velocityEnabled", v.velocityEnabled);
		v.velocityScale = getFloat(m, "vis.velocityScale", v.velocityScale);
		v.velocityThreshold = getFloat(m, "vis.velocityThreshold", v.velocityThreshold);
		v.velocityStrength = getFloat(m, "vis.velocityStrength", v.velocityStrength);
		v.velocityColor[0] = getFloat(m, "vis.velocityColorR", v.velocityColor[0]);
		v.velocityColor[1] = getFloat(m, "vis.velocityColorG", v.velocityColor[1]);
		v.velocityColor[2] = getFloat(m, "vis.velocityColorB", v.velocityColor[2]);

		v.slopeEnabled = getBool(m, "vis.slopeEnabled", v.slopeEnabled);
		v.slopeScale = getFloat(m, "vis.slopeScale", v.slopeScale);
		v.slopeThreshold = getFloat(m, "vis.slopeThreshold", v.slopeThreshold);
		v.slopeStrength = getFloat(m, "vis.slopeStrength", v.slopeStrength);
		v.slopeColor[0] = getFloat(m, "vis.slopeColorR", v.slopeColor[0]);
		v.slopeColor[1] = getFloat(m, "vis.slopeColorG", v.slopeColor[1]);
		v.slopeColor[2] = getFloat(m, "vis.slopeColorB", v.slopeColor[2]);

		v.curvatureEnabled = getBool(m, "vis.curvatureEnabled", v.curvatureEnabled);
		v.curvatureScale = getFloat(m, "vis.curvatureScale", v.curvatureScale);
		v.curvatureThreshold = getFloat(m, "vis.curvatureThreshold", v.curvatureThreshold);
		v.curvatureStrength = getFloat(m, "vis.curvatureStrength", v.curvatureStrength);
		v.curvatureColor[0] = getFloat(m, "vis.curvatureColorR", v.curvatureColor[0]);
		v.curvatureColor[1] = getFloat(m, "vis.curvatureColorG", v.curvatureColor[1]);
		v.curvatureColor[2] = getFloat(m, "vis.curvatureColorB", v.curvatureColor[2]);

		// Audio
		s.audio.enabled = getBool(m, "audio.enabled", s.audio.enabled);
		s.audio.volume = getFloat(m, "audio.volume", s.audio.volume);

		s.audio.freezeSpectrum = getBool(m, "audio.freezeSpectrum", s.audio.freezeSpectrum);
		s.audio.spectrumSmooth = getFloat(m, "audio.spectrumSmooth", s.audio.spectrumSmooth);
		s.audio.spectrumMaxHz = getFloat(m, "audio.spectrumMaxHz", s.audio.spectrumMaxHz);

		// Bands
		int bcount = std::max(0, getInt(m, "bands.count", 0));
		s.audio.bands.clear();
		s.audio.bands.reserve(bcount);
		for (int i = 0; i < bcount; ++i) {
			AudioBandSource b;
			std::string p = "band." + std::to_string(i) + ".";

			b.name = getStr(m, (p + "name").c_str(), ("Band " + std::to_string(i)).c_str());
			b.enabled = getBool(m, (p + "enabled").c_str(), true);
			b.type = (AudioBandType)getInt(m, (p + "type").c_str(), 0);

			b.fLowHz = getFloat(m, (p + "fLowHz").c_str(), 0.0f);
			b.fHighHz = getFloat(m, (p + "fHighHz").c_str(), 200.0f);

			b.pos01.x = getFloat(m, (p + "posx").c_str(), 0.5f);
			b.pos01.y = getFloat(m, (p + "posy").c_str(), 0.5f);

			b.gain = getFloat(m, (p + "gain").c_str(), 0.0f);
			b.stiffness = getFloat(m, (p + "stiffness").c_str(), 0.0f);
			b.damping = getFloat(m, (p + "damping").c_str(), 0.0f);
			b.threshold = getFloat(m, (p + "threshold").c_str(), 0.0f);
			b.radiusCells = getFloat(m, (p + "radiusCells").c_str(), 6.0f);
			b.attack = getFloat(m, (p + "attack").c_str(), 20.0f);
			b.release = getFloat(m, (p + "release").c_str(), 10.0f);

			b.agcEnabled = getBool(m, (p + "agcEnabled").c_str(), false);
			b.agcTimeSec = getFloat(m, (p + "agcTimeSec").c_str(), 0.8f);

			b.angleRad = getFloat(m, (p + "angleRad").c_str(), 0.0f);
			b.length01 = getFloat(m, (p + "length01").c_str(), 0.5f);

			// runtime-only fields reset (keep safe defaults)
			b.energyRaw = b.energyNorm = b.energySmoothed = 0.0f;
			b.prevTargetU = 0.0f;
			b.agcRunning = 0.0f;

			s.audio.bands.push_back(b);
		}

		if (s.audio.bands.empty()) s.audio.selectedBand = -1;
		else if (s.audio.selectedBand < 0 || s.audio.selectedBand >= (int)s.audio.bands.size()) s.audio.selectedBand = 0;

		return true;
	}
}
