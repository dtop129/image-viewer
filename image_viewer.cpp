#include <iostream>
#include <map>
#include <string>
#include <string_view>

#include "image_viewer.h"

int main(int argc, char** argv)
{
	std::string config_path;
	if (argc == 2)
		config_path = argv[1];
	ImageViewerApp app(config_path);
	app.run();
	return 0;
}
