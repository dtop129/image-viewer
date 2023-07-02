#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <SFML/System/Vector2.hpp>

struct TextureData
{
	std::vector<uint8_t> pixels;
	sf::Vector2u size;
};

TextureData load_texture(const std::string& image_path, float scale = 1.f);
std::pair<int, int> get_texture_pageside(const std::string& image);

std::wstring s2ws(const std::string& s);
