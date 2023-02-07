#include <string>
#include <string_view>

#include "image_viewer.h"

int main(int argc, char* argv[])
{
	std::string config_path;

	for (int i = 1; i < argc; i++)
	{
		std::string_view arg(argv[i]);

		if (arg == "--config")
			config_path = argv[++i];
	}

	ImageViewerApp app(config_path);
	app.run();
	return 0;
}
