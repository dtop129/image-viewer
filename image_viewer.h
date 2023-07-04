#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <map>
#include <numeric>
#include <string>
#include <string_view>
#include <vector>

#include "BS_thread_pool_light.hpp"
#include "util.h"
#include "LazyLoad.h"

#include <SFML/Graphics.hpp>
#include <stb_image.h>

enum class ViewMode { Manga, Vertical, Single, Undefined };

class ImageViewerApp
{
	private:
		sf::RenderWindow window;

		int curr_image_index = 0;
		int curr_page_index = 0;
		int curr_tag;

		ViewMode curr_mode = ViewMode::Manga;

		float zoom_factor = 1.f;
		sf::Vector2f render_pos; //POSITION WHERE QUAD WILL BE DRAWN AT render_offsets {0, 0}

		bool update_title = true;
		bool pages_changed = true;

		std::vector<int> render_indices;
		std::vector<sf::Vector2i> render_offsets; //offsets relative to render_pos
		std::vector<float> render_scales;

		std::vector<std::string> images;
		std::vector<sf::Vector2f> textures_sizes;

		std::map<std::pair<int, float>, sf::Texture> loaded_textures;
		std::map<std::pair<int, float>, LazyLoad<sf::Image>> loading_textures;

		std::map<int, std::vector<int>> tags_indices;
		std::map<int, std::vector<std::vector<int>>> pages;

		//FOR MANGA PAGING
		std::map<int, std::vector<int>> repage_indices;
		std::vector<LazyLoad<std::pair<int, int>>> texture_pageside;
		std::map<int, std::pair<std::atomic<unsigned int>, unsigned int>> n_pageside_available;

		std::map<sf::Keyboard::Key, bool> keys_state;

		std::map<std::pair<int, ViewMode>, std::string> keyboard_bindings;
		std::map<std::pair<int, ViewMode>, std::string> mouse_bindings;

		void load_config(std::string_view config_path)
		{
			std::string line;

			std::ifstream stream = std::ifstream(std::string(config_path));

			while (std::getline(stream, line))
			{
				if (line.empty())
					continue;

				int button = -1, key = -1;
				auto sep = line.find_first_of(" ");

				std::string piece = line.substr(0, sep);
				ViewMode mode = ViewMode::Undefined;

				if (piece[0] == '*' && piece.length() == 2)
				{
					if (piece[1] == 'm')
						mode = ViewMode::Manga;
					else if (piece[1] == 's')
						mode = ViewMode::Single;
					else if (piece[1] == 'v')
						mode = ViewMode::Vertical;

					sep = line.find_first_of(" ", sep + 1);
					piece = line.substr(3, sep - 3);
				}

				if (piece[0] == '<')
				{
					if (piece == "<space>")
						key = sf::Keyboard::Space;
					else if (piece == "<backspace>")
						key = sf::Keyboard::Backspace;
					else if (piece == "<plus>")
						key = sf::Keyboard::Add;
					else if (piece == "<minus>")
						key = sf::Keyboard::Subtract;
					else
					{
						std::cerr << piece << " is an invalid binding\n";
						continue;
					}
				}
				else if (piece[0] == 'm' && piece.length() == 2)
				{
					//0 LEFT
					//1 RIGHT
					//2 MIDDLE
					int button_number = piece[1] - '0';
					button = sf::Mouse::Left + button_number;
				}
				else if (piece.length() == 1)
				{
					char binding = piece[0];
					key = sf::Keyboard::Key::A + (binding - 'a');
				}
				else
					std::cerr << line << " not valid\n";

				piece = line.substr(sep + 1);

				if (key != -1)
					keyboard_bindings[{key, mode}] = piece;
				else if (button != -1)
					mouse_bindings[{button, mode}] = piece;
			}
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
				sf::Image tex_data;
				if (tex_available)
					tex_data = loading_it->second.get();
				else
					tex_data = load_texture(images[image_index], scale);

				it = loaded_textures.try_emplace(key).first;
				sf::Texture& tex = it->second;
				tex.loadFromImage(tex_data);

				if (loading_it != loading_textures.end())
					loading_textures.erase(key);
			}
			return it->second;
		}

		void update_status()
		{
			if (update_title || pages_changed)
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
			if (!tags_indices.empty() && pages_changed)
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

			if (curr_mode == ViewMode::Manga)
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
				{
					zoom_factor = 1.f;
					render_pos = {0, 0};
					pages_changed = true;
				}
			}
		}

		float get_horizontal_fit_scale(int image_index)
		{
			return std::min(700.f, (float)window.getSize().x) / textures_sizes[image_index].x;
		}

		std::vector<float> get_fit_scales(const std::vector<int>& page)
		{
			if (curr_mode == ViewMode::Vertical)
				return {get_horizontal_fit_scale(page[0])};

			std::vector<float> scales(page.size());

			sf::Vector2f window_size = sf::Vector2f(window.getSize());

			sf::Vector2f drawn_size(0, window_size.y);
			for (unsigned int i = 0; i < page.size(); i++)
			{
				scales[i] = window_size.y / textures_sizes[page[i]].y;
				drawn_size.x += textures_sizes[page[i]].x * scales[i];
			}
			if (drawn_size.x > window_size.x)
			{
				float scale_x = window_size.x / drawn_size.x;
				for (auto& scale : scales)
					scale *= scale_x;
			}

			return scales;
		}

		//resets render pos and scales to center fit
		//the image will be centered with a render_pos = {0, 0}
		void reset_scales_offsets()
		{
			if (tags_indices.empty())
				return;

			render_indices.clear();
			render_scales.clear();
			render_offsets.clear();

			sf::Vector2i window_size = sf::Vector2i(window.getSize());

			if (curr_mode == ViewMode::Manga || curr_mode == ViewMode::Single)
			{
				const auto& page = pages[curr_tag][curr_page_index];

				render_indices = page;
				render_scales = get_fit_scales(page);
				render_offsets.resize(page.size());

				sf::Vector2f drawn_size(0, textures_sizes[page[0]].y * render_scales[0]);
				for (unsigned int i = 0; i < page.size(); i++)
					drawn_size.x += textures_sizes[page[i]].x * render_scales[i];

				sf::Vector2i offset = (window_size - (sf::Vector2i)drawn_size) / 2;
				for (int i = page.size() - 1; i >= 0; i--)
				{
					render_offsets[i] = offset;
					offset.x += textures_sizes[page[i]].x * render_scales[i];
				}
			}
			else
			{
				int draw_tag = curr_tag;
				int draw_page_index = curr_page_index;

				bool lastpage = false;
				int offset_y = 0;
				while (!lastpage)
				{
					const auto& draw_index = pages[draw_tag][draw_page_index][0];
					float scale = get_horizontal_fit_scale(draw_index);

					int offset_x = (window_size.x - textures_sizes[draw_index].x * scale) / 2.f;

					render_indices.push_back(draw_index);
					render_offsets.emplace_back(offset_x, offset_y);
					render_scales.push_back(scale);

					offset_y += textures_sizes[draw_index].y * scale;

					if (offset_y + render_pos.y >= window_size.y)
						break;

					std::tie(draw_tag, draw_page_index, lastpage) = advance_page(draw_tag, draw_page_index, 1);
				}
			}
		}

		void render()
		{
			if (pages_changed)
				reset_scales_offsets();

			sf::Vector2f window_center = sf::Vector2f(window.getSize()) / 2.f;
			sf::Vector2f zoom_render_pos = window_center - (window_center - render_pos) * zoom_factor;

			std::vector<std::pair<int, float>> used_textures;
			for (unsigned int i = 0; i < render_indices.size(); ++i)
			{
				sf::Sprite sprite(get_texture(render_indices[i], render_scales[i] * zoom_factor));
				sprite.setPosition(zoom_render_pos + sf::Vector2f(render_offsets[i]) * zoom_factor);
				window.draw(sprite);

				used_textures.emplace_back(render_indices[i], render_scales[i] * zoom_factor);
			}

			auto preload_page_offset = [this, &used_textures](int offset)
			{
				auto[preload_tag, preload_page_index, hit_border] = advance_page(curr_tag, curr_page_index, offset);
				if (hit_border) return;

				const auto& page = pages[preload_tag][preload_page_index];

				auto scales = get_fit_scales(page);
				for (unsigned int i = 0; i < page.size(); ++i)
				{
					prepare_texture(page[i], scales[i] * zoom_factor);
					used_textures.emplace_back(page[i], scales[i] * zoom_factor);
				}
			};

			int n_drawn_pages = 1;
			if (curr_mode == ViewMode::Vertical)
				n_drawn_pages = render_indices.size();

			preload_page_offset(-1);
			preload_page_offset(n_drawn_pages);

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
			render_pos.y += offset;
			if (curr_mode != ViewMode::Vertical)
				return;

			int old_tag = curr_tag;
			int old_page_index = curr_page_index;

			auto image_index = pages[curr_tag][curr_page_index][0];

			float scale = get_horizontal_fit_scale(image_index);
			float curr_image_height = textures_sizes[image_index].y * scale;

			bool hit_border;
			while (-render_pos.y >= curr_image_height || render_pos.y > 0)
			{
				if (-render_pos.y >= curr_image_height)
				{
					render_pos.y += curr_image_height;

					std::tie(curr_tag, curr_page_index, hit_border) = advance_page(curr_tag, curr_page_index, 1);
					if (hit_border)
					{
						render_pos.y = -curr_image_height;
						break;
					}
				}
				else if (render_pos.y > 0)
				{
					std::tie(curr_tag, curr_page_index, hit_border) = advance_page(curr_tag, curr_page_index, -1);
					if (hit_border){
						render_pos.y = 0;
						break;
					}

					image_index = pages[curr_tag][curr_page_index][0];
					scale = get_horizontal_fit_scale(image_index);
					curr_image_height = textures_sizes[image_index].y * scale;

					render_pos.y -= curr_image_height;
				}

				image_index = pages[curr_tag][curr_page_index][0];
				scale = get_horizontal_fit_scale(image_index);
				curr_image_height = textures_sizes[image_index].y * scale;
			}

			if (old_tag != curr_tag || old_page_index != curr_page_index)
			{
				curr_image_index = image_index;
				pages_changed = true;
			}
			else //when new pages come in and out of view
			{
				int last_index = render_indices.back();
				int last_height = textures_sizes[last_index].y * render_scales.back();
				int last_offset_y = render_offsets.back().y;
				if (render_pos.y + last_offset_y + last_height < window.getSize().y || render_pos.y + last_offset_y >= window.getSize().y)
					pages_changed = true;
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
					reset_scales_offsets();

					sf::View new_view(sf::FloatRect(sf::Vector2f(0.f, 0.f), sf::Vector2f(event.size.width, event.size.height)));
					window.setView(new_view);
				}
				else if (event.type == sf::Event::KeyPressed)
				{
					if (auto it = keyboard_bindings.find({(int)event.key.code, curr_mode}); it != keyboard_bindings.end())
						run_command(it->second);
					else if (auto it = keyboard_bindings.find({(int)event.key.code, ViewMode::Undefined}); it != keyboard_bindings.end())
						run_command(it->second);

					keys_state[event.key.code] = 1;
				}
				else if (event.type == sf::Event::KeyReleased)
				{
					keys_state[event.key.code] = 0;
				}
				else if (event.type == sf::Event::MouseButtonPressed)
				{
					if (auto it = mouse_bindings.find({(int)event.mouseButton.button, curr_mode}); it != mouse_bindings.end())
						run_command(it->second);
					else if (auto it = mouse_bindings.find({(int)event.mouseButton.button, ViewMode::Undefined}); it != mouse_bindings.end())
						run_command(it->second);
				}
			}

			if (curr_mode != ViewMode::Manga)
				return;

			for (auto it = n_pageside_available.begin(), next_it = it; it != n_pageside_available.end(); it = next_it)
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
			if (!window.hasFocus() || tags_indices.empty())
				return;

			float scroll_speed = 1500 / zoom_factor;
			if (keys_state[sf::Keyboard::Key::J])
				vertical_scroll(-scroll_speed * dt);
			if (keys_state[sf::Keyboard::Key::K])
				vertical_scroll(scroll_speed * dt);
			if (keys_state[sf::Keyboard::Key::L])
				render_pos.x -= scroll_speed * dt;
			if (keys_state[sf::Keyboard::Key::H])
				render_pos.x += scroll_speed * dt;
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

						zoom_factor = 1.f;
						render_pos = {0, 0};
						pages_changed = true;
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

							zoom_factor = 1.f;
							render_pos = {0, 0};
							pages_changed = true;
						}

						tags_indices.erase(tag);
						pages.erase(tag);
					}
				}
				else
					std::cerr << "tag " << tag << " not present" << std::endl;
			}
			else if (action == "goto_tag_relative")
			{
				int offset = std::stoi(args[0]);
				auto tag_pages_it = pages.find(curr_tag);

				if (tag_pages_it != pages.end())
				{
					int min_offset = -std::distance(pages.begin(), tag_pages_it);
					int max_offset = std::distance(tag_pages_it, std::prev(pages.end()));
					int clamped_offset = std::clamp(offset, min_offset, max_offset);

					if (clamped_offset == 0)
						std::cout << "last_in_tag_dir=" << offset << std::endl;
					else
					{
						std::advance(tag_pages_it, clamped_offset);
						curr_tag = tag_pages_it->first;
						curr_page_index = 0;
						curr_image_index = tag_pages_it->second[0][0];

						zoom_factor = 1.f;
						render_pos = {0, 0};
						pages_changed = true;
					}
				}
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
					curr_tag = new_tag;
					curr_page_index = new_page_index;
					curr_image_index = pages[curr_tag][curr_page_index][0];

					zoom_factor = 1.f;
					render_pos = {0, 0};
					pages_changed = true;
				}
			}
			else if (action == "scroll")
			{
				if (!pages.empty() && curr_mode == ViewMode::Vertical)
				{
					int offset = std::stoi(args[0]);
					vertical_scroll(-offset / zoom_factor);
				}
			}
			else if (action == "zoom")
			{
				float factor = std::stof(args[0]);
				zoom_factor *= factor;
				zoom_factor = std::min(3.f, zoom_factor);
			}
			else if (action == "repage")
			{
				if (!pages.empty() && curr_mode == ViewMode::Manga)
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
				ViewMode prev_mode = curr_mode;
				if (args[0] == "manga")
					curr_mode = ViewMode::Manga;
				else if (args[0] == "single")
					curr_mode = ViewMode::Single;
				else if (args[0] == "vertical")
					curr_mode = ViewMode::Vertical;
				else
					std::cerr << "Mode " << args[0] << " not recognized" << std::endl;

				if (curr_mode != prev_mode)
				{
					std::cout << "current_mode=" << args[0] << std::endl;

					for (const auto&[tag, pages] : pages)
						update_paging(tag);


					zoom_factor = 1.f;
					render_pos = {0, 0};
					reset_scales_offsets();
				}
			}
			else if (action == "quit")
				window.close();
			else
				std::cerr << action << " is not a valid command" << std::endl;
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
			//window.setFramerateLimit(20);
			window.setVerticalSyncEnabled(true);

			std::ios_base::sync_with_stdio(false);
			std::cin.tie(NULL);

			if (!config_path.empty())
				load_config(config_path);

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

				pages_changed = false;
				update_title = false;

				//std::cout << 1 / dt << std::endl;
			}
		}
};
