#include "util.h"

#include <algorithm>
#include <numeric>

#include <lancir.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

std::wstring s2ws(const std::string& s) {
	std::string curLocale = setlocale(LC_ALL, "");
	std::wstring ws(s.size(), L' '); // Overestimate number of code points.
	ws.resize(std::mbstowcs(&ws[0], s.c_str(), s.size())); // Shrink to fit.
	setlocale(LC_ALL, curLocale.c_str());
	return ws;
}

sf::Image load_texture(const std::string& image_path, float scale)
{
	int h, w, c;
	uint8_t* pixels = stbi_load(image_path.c_str(), &w, &h, &c, 3);
	if (pixels == nullptr)
		return sf::Image();

	unsigned int new_h = h * scale;
	unsigned int new_w = w * scale;
	std::vector<uint8_t> resized_pixels(new_w * new_h * 3);

	avir::CLancIR resizer;
	resizer.resizeImage(pixels, w, h, 0, resized_pixels.data(), new_w, new_h, 0, 3);
	stbi_image_free(pixels);

	std::vector<uint8_t> rgba_pixels;
	rgba_pixels.reserve(new_w * new_h * 4);
	for (unsigned int i = 0; i < new_w * new_h * 3; i += 3)
	{
		rgba_pixels.push_back(resized_pixels[i]);
		rgba_pixels.push_back(resized_pixels[i+1]);
		rgba_pixels.push_back(resized_pixels[i+2]);
		rgba_pixels.push_back(255);
	}

	sf::Image image;
	image.create({new_w, new_h}, rgba_pixels.data());
	return image;
}

std::pair<int, int> get_texture_pageside(const std::string& image)
{
	std::pair<int, int> page_side;

	int w, h, c;
	uint8_t* pixels = stbi_load(image.c_str(), &w, &h, &c, 3);
	if (pixels == nullptr)
		return page_side;

	// change to greyscale column major
	std::vector<unsigned int> pixels_cm;
	pixels_cm.reserve(w * h * 3);
	for (int x = 0; x < w; x++)
	{
		for (int y = 0; y < h; y++)
		{
			uint8_t* pixel = pixels + (x + w * y) * 3;
			pixels_cm.push_back((pixel[0] + pixel[1] + pixel[2]) / 3);
		}
	}
	stbi_image_free(pixels);

	unsigned int color_left = 0, color_right = 0;
	color_left = std::accumulate(pixels_cm.begin(), pixels_cm.begin() + h, 0);
	color_right = std::accumulate(pixels_cm.end() - h, pixels_cm.end(), 0);

	color_left = color_left / h;
	color_right = color_right / h;

	unsigned int accum = 0;
	std::for_each (pixels_cm.begin(), pixels_cm.begin() + h, [&](unsigned int d) {
		accum += (d - color_left) * (d - color_left);
	});
	unsigned int var_left = accum / (h-1);

	accum = 0;
	std::for_each (pixels_cm.end() - h, pixels_cm.end(), [&](unsigned int d) {
		accum += (d - color_right) * (d - color_right);
	});
	unsigned int var_right = accum / (h-1);

	page_side = {var_left < 500, var_right < 500};
	return page_side;
}
