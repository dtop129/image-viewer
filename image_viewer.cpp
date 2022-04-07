#include <iostream>
#include <map>
#include <string>
#include <string_view>

#include "image_viewer.h"

int main(int argc, char* argv[])
{
	std::string config_path;
	std::string save_file;
	float wide_fac = 1.f;

	for (int i = 1; i < argc; i++)
	{
		std::string_view arg(argv[i]);
		if (arg == "--config")
			config_path = argv[++i];
		else if (arg == "--save-repage")
			save_file = argv[++i];
		else if (arg == "--wide-factor")
			wide_fac = std::stof(argv[++i]);
	}

	ImageViewerApp app(config_path, save_file, wide_fac);
	app.run();
	return 0;
}
