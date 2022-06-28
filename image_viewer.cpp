#include <iostream>
#include <map>
#include <string>
#include <string_view>

#include <Magick++.h>

#include "image_viewer.h"

int main(int argc, char* argv[])
{
	Magick::InitializeMagick(*argv);
	std::string config_path;
	std::string save_file;

	for (int i = 1; i < argc; i++)
	{
		std::string_view arg(argv[i]);
		if (arg == "--config")
			config_path = argv[++i];
		else if (arg == "--save-repage")
			save_file = argv[++i];
	}

	ImageViewerApp app(config_path, save_file);
	app.run();
	return 0;
}
