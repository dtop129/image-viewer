#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <SFML/Graphics.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "lancir.h"

#include "BS_thread_pool_light.hpp"


std::wstring s2ws(const std::string& s) {
	std::string curLocale = setlocale(LC_ALL, ""); 
	std::wstring ws(s.size(), L' '); // Overestimate number of code points.
	ws.resize(std::mbstowcs(&ws[0], s.c_str(), s.size())); // Shrink to fit.
	setlocale(LC_ALL, curLocale.c_str());
	return ws;
}

class LazyLoadBase
{
	protected:
		static BS::thread_pool_light pool;
};

BS::thread_pool_light LazyLoadBase::pool(std::thread::hardware_concurrency() - 1);

template<class T>
class LazyLoad : LazyLoadBase
{
	private:
		T resource;
		std::future<T> loading_resource;
		std::function<T()> getter;

	public:
		LazyLoad(std::function<T()> f) : getter(f)
		{
			loading_resource = pool.submit(f);
		}

		LazyLoad(const T& value) : resource(value) {}

		const T& get()
		{
			if (loading_resource.valid())
				resource = loading_resource.get();

			return resource;
		}

		bool available() const
		{
			if (!loading_resource.valid())
				return true;

			return loading_resource.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
		}
};

struct TextureData
{
	std::vector<uint8_t> pixels;
	sf::Vector2u size;
};

TextureData load_texture(const std::string& image_path, float scale = 1.f)
{
	int h, w, c;
	uint8_t* pixels = stbi_load(image_path.c_str(), &w, &h, &c, 3);
	if (pixels == nullptr)
		return TextureData();

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

	return {rgba_pixels, {new_w, new_h}};
}

std::pair<int, int> get_texture_pageside(const std::string& image)
{
	std::pair<int, int> page_side;

	int w, h, c;
	uint8_t* pixels = stbi_load(image.c_str(), &w, &h, &c, 3);
	if (pixels == nullptr)
		return page_side;

	unsigned int color_left = 0, color_right = 0;
	for (int y = 0; y < h; y++)
	{
		for (int x = 0; x < 3; x++)
		{
			uint8_t* pixel = pixels + (x + w * y) * 3;
			color_left += pixel[0] + pixel[1] + pixel[2];
		}
		for (int x = w - 1; x >= w - 3; x--)
		{
			uint8_t* pixel = pixels + (x + w * y) * 3;
			color_right += pixel[0] + pixel[1] + pixel[2];
		}
	}
	color_left = color_left / (3 * h * 3);
	color_right = color_right / (3 * h * 3);
	stbi_image_free(pixels);

	page_side = {color_left > 250 || color_left < 5, color_right > 250 || color_right < 5};
	return page_side;
}


enum class ViewMode { Manga, Vertical, Single };

class ImageViewerApp
{
	private:
		sf::RenderWindow window;

		ViewMode mode = ViewMode::Manga;
		std::vector<std::string> images;

		std::vector<sf::Vector2f> textures_sizes;
		std::map<int, std::pair<std::atomic<unsigned int>, unsigned int>> n_pageside_available;
		std::vector<LazyLoad<std::pair<int, int>>> texture_pageside;

		std::map<std::pair<int, float>, sf::Texture> loaded_textures;
		std::map<std::pair<int, float>, LazyLoad<TextureData>> loading_textures;

		std::map<int, std::vector<int>> tags_indices;
		std::map<int, std::vector<std::vector<int>>> pages;

		std::map<int, std::pair<int, int>> pages_side;  //FOR MANGA PAGING
		std::map<int, std::vector<int>> repage_indices; //FOR MANGA PAGING
		float vertical_offset = 0.f; //FOR VERTICAL PAGING

		int curr_image_index = 0;
		int curr_page_index = 0;
		int curr_tag;

		bool update_title = true;
		bool page_changed = true;

		std::map<sf::Keyboard::Key, bool> keys_state;

		std::map<int, std::string> keyboard_bindings;
		std::map<int, std::string> mouse_bindings;


		void load_config(std::string_view config_path)
		{
			std::string binding_name;
			std::string command;

			std::ifstream stream = std::ifstream(std::string(config_path));

			while (stream >> binding_name)
			{
				std::getline(stream, command);
				auto str_begin = command.find_first_not_of(" ");
				command = command.substr(str_begin);

				if (binding_name[0] == '<' && binding_name.length() > 1)
				{
					std::size_t end_bracket_index = binding_name.find('>');
					if (end_bracket_index == std::string::npos)
					{
						std::cerr << binding_name << " is an invalid binding\n";
						continue;
					}

					binding_name = binding_name.substr(1, end_bracket_index - 1);
					int key;
					if (binding_name == "space")
						key = sf::Keyboard::Space;
					else if (binding_name == "backspace")
						key = sf::Keyboard::Backspace;

					keyboard_bindings[key] = command;
				}
				else if (binding_name[0] == 'm' && binding_name.length() == 2)
				{
					//0 LEFT
					//1 RIGHT
					//2 MIDDLE
					int button_number = binding_name[1] - '0';
					int button = sf::Mouse::Left + button_number;
					mouse_bindings[button] = command;
				}
				else if (binding_name.length() == 1)
				{
					char binding = binding_name[0];
					int key = sf::Keyboard::Key::A + (binding - 'a');
					keyboard_bindings[key] = command;
				}
				else
					std::cerr << binding_name << " is an invalid binding\n";
			}
		}

		void update_status()
		{
			if (update_title || page_changed)
			{
				if (tags_indices.empty())
					window.setTitle("no images loaded");
				else
				{
					const auto& tag_indices = tags_indices[curr_tag];
					int relative_index = std::find(tag_indices.begin(), tag_indices.end(), curr_image_index) - tag_indices.begin();

					std::string base_filename = images[curr_image_index].substr(images[curr_image_index].find_last_of("/\\") + 1);
					std::string title = std::to_string(curr_tag) + " - " + base_filename + " [" + std::to_string(relative_index + 1) + "/" + std::to_string(tag_indices.size()) + "]";
					window.setTitle(s2ws(title));
				}
			}
			if (!tags_indices.empty() && page_changed)
			{
				std::cout << "current_image=";
				for (const auto& image_index : pages[curr_tag][curr_page_index])
					std::cout << images[image_index] << '\t';
				std::cout << std::endl;
			}
		}

		void update_paging(int tag)
		{
			auto tag_it = tags_indices.find(tag);
			if (tag_it == tags_indices.end())
				return;

			auto& tag_indices = tag_it->second;
			auto& tag_pages = pages[tag];

			std::vector<int> old_current_page;
			if (tag == curr_tag && !tag_pages.empty())
				old_current_page = tag_pages[curr_page_index];

			if (mode == ViewMode::Manga)
			{
				auto& tag_repage_indices = repage_indices[tag];

				std::vector<int> lone_page(tag_indices.size(), 0);
				for (int i = 0; i < (int)tag_indices.size(); i++)
				{
					const auto& image_size = textures_sizes[tag_indices[i]];
					if (image_size.x > image_size.y * 0.8)
						lone_page[i] = 1;
				}

				/*
				   CHECK STATUS
				   2 = STREAK BEGINS
				   1 = CHECKING INSIDE STREAK
				   0 = STREAK ENDS
				   */
				int check_status = 0;
				int start0 = 0, start1 = 0;
				int streak_begin = 0;
				bool change_paging = false;
				for (int i = 0; i < (int)tag_indices.size(); i++)
				{
					if (!lone_page[i] && check_status != 1 &&
							(i == 0 || lone_page[i - 1]))
						check_status = 2;

					if (check_status > 0)
					{
						if (check_status == 2)
						{
							check_status = 1;
							start0 = start1 = 0;
							streak_begin = i;
							change_paging = false;

							//DON'T TRUST WIDE PAGES AS FIRST PAGE, USUALLY IT IS NOT PART OF MANGA
							if (i > 1)
								start0++;
						}

						if (std::find(tag_repage_indices.begin(), tag_repage_indices.end(), tag_indices[i]) != tag_repage_indices.end())
							change_paging = !change_paging;

						if (texture_pageside[tag_indices[i]].available())
						{
							auto [is_right, is_left] = texture_pageside[tag_indices[i]].get();

							if (is_right)
							{
								if ((i - streak_begin) % 2 == 0)
									start0++;
								else
									start1++;
							}
							if (is_left)
							{
								if ((i - streak_begin) % 2 == 0)
									start1++;
								else
									start0++;
							}
						}

						if (i + 1 == (int)tag_indices.size() || lone_page[i + 1])
						{
							if (i + 1 != (int)tag_indices.size())
							{
								if ((i - streak_begin) % 2 == 0)
									start1++;
								else
									start0++;
							}

							lone_page[streak_begin] = start1 >= start0;
							if (change_paging)
								lone_page[streak_begin] = 1 - lone_page[streak_begin];

							check_status = 0;
						}
					}
				}

				tag_pages.clear();
				for (int i = 0; i < (int)tag_indices.size(); i++)
				{
					if (i + 1 == (int)tag_indices.size() || lone_page[i] || lone_page[i + 1])
					{
						tag_pages.emplace_back(std::vector{tag_indices[i]});
					}
					else
					{
						tag_pages.emplace_back(std::vector{tag_indices[i], tag_indices[i+1]});
						i++;
					}
				}
			}
			else
			{
				tag_pages.clear();
				for (auto image_index : tag_indices)
					tag_pages.emplace_back(std::vector{image_index});
			}

			if (tag == curr_tag)
			{
				for (int i = 0; i < (int)tag_pages.size(); ++i)
				{
					if (std::find(tag_pages[i].begin(), tag_pages[i].end(), curr_image_index) != tag_pages[i].end())
					{
						curr_page_index = i;
						break;
					}
				}
				if (old_current_page != tag_pages[curr_page_index])
					page_changed = true;
			}
		}

		std::pair<std::vector<float>, sf::Vector2i> get_scale_centering(const std::vector<int>& page)
		{
			std::vector<float> scales(page.size(), 1.f);
			sf::Vector2i center_offset(0, 0);

			if (mode == ViewMode::Manga || mode == ViewMode::Single)
			{
				sf::Vector2f drawn_size(0.f, (float)window.getSize().y);
				for (unsigned int i = 0; i < page.size(); i++)
				{
					scales[i] = (float)window.getSize().y / textures_sizes[page[i]].y;
					drawn_size.x += textures_sizes[page[i]].x * scales[i];
				}
				float scale_x = std::min(1.f, window.getSize().x / drawn_size.x);
				drawn_size *= scale_x;
				for (auto& scale : scales)
					scale *= scale_x;

				center_offset = (sf::Vector2i(window.getSize()) - sf::Vector2i(drawn_size)) / 2;
			}
			else
			{
				int image_index = page[0];
				int width = textures_sizes[image_index].x;

				float scale = std::min(700.f, 0.8f * window.getSize().x) / width;
				center_offset.x = (window.getSize().x - width * scale) / 2.f;

				scales = { scale };
			}

			return {scales, center_offset};
		}

		void prepare_texture(int image_index, float scale)
		{
			std::pair key{image_index, scale};
			if (loaded_textures.contains(key) || loading_textures.contains(key))
				return;

			loading_textures.try_emplace(key, [image_path = images[image_index], scale]
				{
					return load_texture(image_path, scale);
				});
		}

		const sf::Texture& get_texture(int image_index, float scale)
		{
			std::pair key{image_index, scale};
			auto it = loaded_textures.find(key);
			if (it == loaded_textures.end())
			{
				auto loading_it = loading_textures.find(key);
				bool tex_available = loading_it != loading_textures.end() && loading_it->second.available();
				TextureData tex_data;
				if (tex_available)
					tex_data = loading_it->second.get();
				else
					tex_data = load_texture(images[image_index], scale);

				it = loaded_textures.try_emplace(key).first;
				sf::Texture& tex = it->second;
				if (tex.create(tex_data.size))
					tex.update(tex_data.pixels.data());

				if (loading_it != loading_textures.end())
					loading_textures.erase(key);
			}
			return it->second;
		}

		//returns size of drawn sprite
		sf::Vector2i draw_image(int image_index, float scale, sf::Vector2i pos)
		{
			sf::Sprite sprite(get_texture(image_index, scale));
			sprite.setPosition(sf::Vector2f(pos));
			window.draw(sprite);

			return sf::Vector2i(sprite.getGlobalBounds().width, sprite.getGlobalBounds().height);
		}

		int render_manga()
		{
			const auto& curr_pages = pages[curr_tag];
			auto[scales, center_offset] = get_scale_centering(curr_pages[curr_page_index]);

			int index = curr_pages[curr_page_index].size() - 1;
			int pos_x = 0;

			const auto& page = curr_pages[curr_page_index];
			for (auto it = page.rbegin(); it != page.rend(); ++it)
			{
				sf::Vector2i drawn_size = draw_image(*it, scales[index], center_offset + sf::Vector2i(pos_x, 0));
				pos_x += drawn_size.x;
				index--;
			}

			return 1;
		}

		int render_vertical()
		{
			int draw_tag = curr_tag;
			int draw_page_index = curr_page_index;

			bool lastpage = false;
			int pos_y = std::round(-vertical_offset);
			int n_pages = 0;
			while (pos_y < (int)window.getSize().y && !lastpage)
			{
				const auto& draw_page = pages[draw_tag][draw_page_index];
				auto[scales, center_offset] = get_scale_centering(draw_page);

				sf::Vector2i drawn_size = draw_image(draw_page[0], scales[0], center_offset + sf::Vector2i(0, pos_y));

				std::tie(draw_tag, draw_page_index, lastpage) = advance_page(draw_tag, draw_page_index, 1);

				pos_y += drawn_size.y;
				n_pages++;
			}

			return n_pages;
		}

		void render()
		{
			if (tags_indices.empty())
				return;

			int n_drawn_pages;
			if (mode == ViewMode::Manga || mode == ViewMode::Single)
				n_drawn_pages = render_manga();
			else
				n_drawn_pages = render_vertical();

			std::vector<std::pair<int, float>> used_textures;

			//for (int offset = 0; offset < n_drawn_pages; offset++) //WITHOUT PRELOADING, MOSTLY FOR DEBUGGING PURPOSES
			for (int offset = -1; offset <= n_drawn_pages; offset++)
			{
				auto[preload_tag, preload_page_index, hit_border] = advance_page(curr_tag, curr_page_index, offset);
				if (hit_border)
					continue;

				const auto& preload_page = pages[preload_tag][preload_page_index];
				auto[scales, centering] = get_scale_centering(preload_page);
				int index = 0;
				for (int image_index : preload_page)
				{
					prepare_texture(image_index, scales[index]);
					used_textures.emplace_back(image_index, scales[index]);
					index++;
				}
			}

			std::erase_if(loaded_textures, [&used_textures](const auto& item)
				{
					return std::find(used_textures.begin(), used_textures.end(), item.first)
						== used_textures.end();
				});
			std::erase_if(loading_textures, [&used_textures](const auto& item)
				{
					return std::find(used_textures.begin(), used_textures.end(), item.first)
						== used_textures.end();
				});
		}

		//pair returns {new tag, new index, hit border}
		//hit border doesn't mean that the indices didn't advance
		std::tuple<int, int, bool> advance_page(int tag, int page_index, int offset)
		{
			auto tag_pages_it = pages.find(tag);
			if (tag_pages_it == pages.end())
				return {0, 0, 0};

			while (offset != 0)
			{
				auto& tag_pages = tag_pages_it->second;

				int new_page_index = std::clamp(page_index + offset, 0, (int)tag_pages.size() - 1);
				int real_offset = new_page_index - page_index;
				offset = offset - real_offset;

				if (offset == 0)
					page_index = new_page_index;
				else
				{
					int page_offset = 1;
					if (offset < 0)
						page_offset = -1;

					offset = offset - page_offset;

					int tag_pages_index = std::distance(pages.begin(), tag_pages_it);
					int new_pages_index = std::clamp(tag_pages_index + page_offset, 0, (int)pages.size() - 1);

					if (new_pages_index != tag_pages_index)
					{
						std::advance(tag_pages_it, page_offset);

						page_index = 0;
						if (page_offset < 0)
							page_index = tag_pages_it->second.size() - 1;
					}
					else
					{
						page_index = tag_pages_it->second.size() - 1;
						if (page_offset < 0)
							page_index = 0;

						return {tag_pages_it->first, page_index, 1};
					}
				}
			}

			return {tag_pages_it->first, page_index, 0};
		}

		void vertical_scroll(float offset)
		{
			int old_tag = curr_tag;
			int old_page_index = curr_page_index;

			auto curr_page = pages[curr_tag][curr_page_index];
			auto[scales, center_offset] = get_scale_centering(curr_page);

			vertical_offset += offset;
			float curr_tex_height = textures_sizes[curr_page[0]].y;

			bool hit_border;
			while (vertical_offset >= curr_tex_height * scales[0] || vertical_offset < 0)
			{
				if (vertical_offset >= curr_tex_height * scales[0])
				{
					vertical_offset -= curr_tex_height * scales[0];

					std::tie(curr_tag, curr_page_index, hit_border) = advance_page(curr_tag, curr_page_index, 1);
					if (hit_border)
					{
						vertical_offset = curr_tex_height * scales[0];
						break;
					}
				}
				else if (vertical_offset < 0)
				{
					std::tie(curr_tag, curr_page_index, hit_border) = advance_page(curr_tag, curr_page_index, -1);
					if (hit_border){
						vertical_offset = 0;
						break;
					}

					curr_tex_height = textures_sizes[pages[curr_tag][curr_page_index][0]].y;
					auto[scales, center_offset] = get_scale_centering(pages[curr_tag][curr_page_index]);
					vertical_offset += curr_tex_height * scales[0];
				}

				curr_page = pages[curr_tag][curr_page_index];
				std::tie(scales, center_offset) = get_scale_centering(curr_page);
				curr_tex_height = textures_sizes[curr_page[0]].y;
			}

			if (old_tag != curr_tag || old_page_index != curr_page_index)
			{
				curr_image_index = curr_page[0];
				page_changed = true;
			}
		}

		void poll_events()
		{
			sf::Event event;
			while (window.pollEvent(event))
			{
				if (event.type == sf::Event::Closed)
					window.close();
				else if (event.type == sf::Event::Resized)
				{
					sf::View new_view(sf::FloatRect(sf::Vector2f(0.f, 0.f), sf::Vector2f(event.size.width, event.size.height)));
					window.setView(new_view);
				}
				else if (event.type == sf::Event::KeyPressed)
				{
					if (auto it = keyboard_bindings.find((int)event.key.code); it != keyboard_bindings.end())
						run_command(it->second);

					keys_state[event.key.code] = 1;
				}
				else if (event.type == sf::Event::KeyReleased)
				{
					keys_state[event.key.code] = 0;
				}
				else if (event.type == sf::Event::MouseButtonPressed)
				{
					if (auto it = mouse_bindings.find((int)event.mouseButton.button); it != mouse_bindings.end())
						run_command(it->second);
				}
			}
			
			if (mode != ViewMode::Manga)
				return;
			for (auto it = n_pageside_available.begin(), next_it = it; it != n_pageside_available.cend(); it = next_it)
			{
				++next_it;

				int tag = it->first;
				auto tag_it = tags_indices.find(tag);
				if (tag_it == tags_indices.end())
					n_pageside_available.erase(it);
				else
				{
					const auto& tag_indices = tag_it->second;
					auto&[total, prev_total] = it->second;

					if (total != prev_total || total == tag_indices.size())
					{
						update_paging(tag);
						prev_total = total;
						if (total == tag_indices.size())
							n_pageside_available.erase(it);
					}
				}
			}
		}

		void handle_keyboard(float dt)
		{
			if (!window.hasFocus() || mode != ViewMode::Vertical || tags_indices.empty())
				return;

			float scroll_speed = 1500;
			if (keys_state[sf::Keyboard::Key::J])
				vertical_scroll(scroll_speed * dt);
			if (keys_state[sf::Keyboard::Key::K])
				vertical_scroll(-scroll_speed * dt);
		}

		void run_command(std::string_view cmd)
		{
			std::string_view action(cmd.begin(), cmd.find('('));

			std::vector<std::string> args;
			auto arg_begin = cmd.begin() + action.length() + 1;
			while (arg_begin != cmd.end())
			{
				std::vector<unsigned int> backslash_indices;
				auto next_sep_iter = std::find(arg_begin, cmd.end(), ',');
				while (*(next_sep_iter - 1) == '\\' && next_sep_iter != cmd.end())
				{
					backslash_indices.push_back(next_sep_iter - 1 - arg_begin);
					next_sep_iter = std::find(next_sep_iter + 1, cmd.end(), ',');
				}

				if (next_sep_iter == cmd.end())
					next_sep_iter--;

				if (next_sep_iter - arg_begin > 0)
				{
					std::string_view arg(arg_begin, next_sep_iter);

					unsigned int start_index = arg.find_first_not_of(' ');
					unsigned int end_index = arg.find_last_not_of(' ') + 1;
					arg = arg.substr(start_index, end_index - start_index);

					args.emplace_back(arg);
					int counter = 0;
					for (auto i : backslash_indices)
					{
						args.back().erase(i - counter, 1);
						++counter;
					}
				}

				arg_begin = next_sep_iter + 1;
			}

			if (action == "add_images")
			{
				int tag = 0;
				if (args.size() > 1)
				{
					tag = std::stoi(args[0]);
					args.erase(args.begin());
				}
				for (const auto& image_path : args)
				{
					int w, h;
					if (stbi_info(image_path.c_str(), &w, &h, nullptr) == 0)
					{
						std::cerr << "error loading " << image_path << '\n';
						continue;
					}

					auto image_it = std::find(images.begin(), images.end(), image_path);
					int new_index = std::distance(images.begin(), image_it);

					if (image_it == images.end())
					{
						images.push_back(image_path);
						textures_sizes.emplace_back(w, h);

						auto [it, inserted] = n_pageside_available.try_emplace(tag);
						texture_pageside.emplace_back([&counter = it->second.first, image_path] 
							{ 
								auto pageside = get_texture_pageside(image_path);
								++counter;
								return pageside; 
							});
					}

					if (tags_indices.empty())
					{
						curr_tag = tag;
						curr_image_index = new_index;
					}

					auto& tag_indices = tags_indices[tag];
					tag_indices.insert(std::upper_bound(tag_indices.begin(), tag_indices.end(), new_index,
								[this] (int index1, int index2)
							{
								return images[index1] < images[index2];
							}), new_index);

					update_title = true;
				}
				update_paging(tag);
			}
			else if (action == "goto_tag" || action == "remove_tag")
			{
				int tag = std::stoi(args[0]);
				auto tag_pages_it = pages.find(tag);

				if (tag_pages_it != pages.end())
				{
					if (action == "goto_tag")
					{
						curr_tag = tag;
						curr_page_index = 0;
						curr_image_index = tag_pages_it->second[0][0];

						page_changed = true;
					}
					else
					{
						if (tag == curr_tag)
						{
							if (std::next(tag_pages_it) != pages.end())
								std::advance(tag_pages_it, 1);
							else if (tag_pages_it != pages.begin())
								std::advance(tag_pages_it, -1);

							curr_tag = tag_pages_it->first;
							curr_page_index = 0;
							curr_image_index = tag_pages_it->second[0][0];

							page_changed = true;
						}

						tags_indices.erase(tag);
						pages.erase(tag);
					}
				}
				else
					std::cerr << "tag " << tag << " not present" << std::endl;
			}
			else if (action == "goto_relative")
			{
				int offset = std::stoi(args[0]);

				auto[prev_tag, prev_page_index] = std::tie(curr_tag, curr_page_index);
				auto[new_tag, new_page_index, hit_border] = advance_page(curr_tag, curr_page_index, offset);

				if (prev_tag == new_tag && prev_page_index == new_page_index)
					std::cout << "last_in_dir=" << offset << std::endl;
				else
				{
					vertical_offset = 0.f;
					curr_tag = new_tag;
					curr_page_index = new_page_index;
					curr_image_index = pages[curr_tag][curr_page_index][0];
					page_changed = true;
				}
			}
			else if (action == "repage")
			{
				if (!pages.empty() && mode == ViewMode::Manga)
				{
					auto& curr_repage_indices = repage_indices[curr_tag];
					if (auto it = std::find(curr_repage_indices.begin(), curr_repage_indices.end(), curr_image_index); it != curr_repage_indices.end())
						curr_repage_indices.erase(it);
					else
						curr_repage_indices.push_back(curr_image_index);

					update_paging(curr_tag);
				}
			}
			else if (action == "output_string")
				std::cout << args[0] << std::endl;
			else if (action == "change_mode")
			{
				ViewMode prev_mode = mode;
				if (args[0] == "manga")
					mode = ViewMode::Manga;
				else if (args[0] == "single")
					mode = ViewMode::Single;
				else if (args[0] == "vertical")
					mode = ViewMode::Vertical;
				else
					std::cerr << "Mode " << args[0] << " not recognized" << std::endl;

				if (mode != prev_mode)
				{
					std::cout << "current_mode=" << args[0] << std::endl;

					for (const auto&[tag, pages] : pages)
						update_paging(tag);

					if (mode == ViewMode::Manga || mode == ViewMode::Single)
						window.setFramerateLimit(20);
					else if (mode == ViewMode::Vertical)
						window.setFramerateLimit(0);
				}
			}
			else if (action == "quit")
				window.close();
			else
			{
				std::cerr << action << " is not a valid command" << std::endl;
			}
		}

		void check_stdin()
		{
			while (std::cin.rdbuf()->in_avail())
			{
				std::string cmd;
				std::getline(std::cin, cmd);

				run_command(cmd);
			}
		}

	public:
		ImageViewerApp(std::string_view config_path)
		{
			//window.create(sf::VideoMode(800, 600), "image viewer", sf::Style::Default);
			window.create(sf::VideoMode(sf::Vector2u(800, 600)), "image viewer", sf::Style::Default);
			//window.setKeyRepeatEnabled(false);
			window.setFramerateLimit(20);
			window.setVerticalSyncEnabled(true);

			if (!config_path.empty())
				load_config(config_path);

			std::ios_base::sync_with_stdio(false);
			std::cin.tie(NULL);

			std::cout << "current_mode=manga" << std::endl;
		}

		void run()
		{
			sf::Clock clock;
			while (window.isOpen())
			{
				float dt = clock.restart().asSeconds();

				check_stdin();
				poll_events();
				handle_keyboard(dt);

				update_status();

				window.clear();
				render();
				window.display();

				page_changed = false;
				update_title = false;

				//std::cout << 1 / dt << std::endl;
			}
		}
};
