#include "project_water.hpp"
#include "core/Bonobo.h"

#include <clocale>
#include <stdexcept>

int main()
{
	std::setlocale(LC_ALL, "");

	Bonobo framework;
	try {
		edan35::ProjectWater app(framework.GetWindowManager());
		app.run();
	}
	catch (std::runtime_error const& e) {
		LogError(e.what());
	}

	return 0;
}
