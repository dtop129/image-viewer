#pragma once

#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <map>
#include <ranges>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

#include <SFML/Graphics.hpp>
#include <Magick++.h>

template<class T>
class PreloadResource
{
	private:
		T resource;
		std::future<T> loading_resource;
		std::function<T()> getter;

		bool loaded = false;

	public:
		PreloadResource(std::function<T()> f) : getter(f)
		{
			loading_resource = std::async(std::launch::async, getter);
		}

		PreloadResource(const T& val) : resource(val)
		{
			loaded = true;
		}

		const T& get()
		{
			if (loaded)
				return resource;

			resource = loading_resource.get();
			loaded = true;

			return resource;
		}
};

sf::Texture load_texture(const std::string& image_path, float scale = 1.f)
{
	try
	{
		Magick::Image image(image_path);
		image.magick("RGBA");
		image.resize(Magick::Geometry(image.columns() * scale, image.rows() * scale));
		Magick::Blob blob;
		image.write(&blob);
		sf::Image sf_image;
		sf_image.create(image.columns(), image.rows(), (sf::Uint8*)blob.data());

		sf::Texture tex;
		tex.loadFromImage(sf_image);
		return tex;
	}
	catch (std::exception& e)
	{
		return sf::Texture();
	}
}

class ImageViewerApp
{
	private:
		sf::RenderWindow window;

		std::vector<std::string> images;

		std::map<std::pair<int, float>, PreloadResource<sf::Texture>> loaded_textures;

		std::map<int, std::vector<int>> tags_indices;
		std::map<int, std::vector<std::vector<int>>> pages;
		std::map<int, std::vector<int>> repage_indices;

		int curr_page_index = 0;
		int curr_tag = 0;

		bool update_title = true;
		bool page_changed = true;

		std::map<int, std::string> user_bindings;

		void load_config(std::string_view config_path)
		{
			char binding;
			std::string command;

			std::ifstream stream = std::ifstream(std::string(config_path));

			while (stream >> binding)
			{
				std::getline(stream, command);
				auto str_begin = command.find_first_not_of(" ");
				command = command.substr(str_begin);

				int key = sf::Keyboard::Key::A + (binding - 'a');
				user_bindings[key] = command;
			}
		}

		void preload_texture_async(int image_index, float scale)
		{
			if (loaded_textures.contains({image_index, scale}))
				return;

			loaded_textures.emplace(std::pair{image_index, scale},
					[image_path = images[image_index], scale]
					{ return load_texture(image_path, scale); });
		}

		const sf::Texture& get_texture(int image_index, float scale)
		{
			if (auto it = loaded_textures.find({image_index, scale}); it != loaded_textures.end())
				return it->second.get();

			sf::Texture tex = load_texture(images[image_index], scale);
			auto[it, inserted] = loaded_textures.emplace(std::pair{image_index, scale}, tex);
			return it->second.get();
		}

		sf::Vector2f get_texture_size(int image_index) const
		{
			static std::map<int, sf::Vector2f> texture_sizes;
			auto it = texture_sizes.find(image_index);
			if (it != texture_sizes.end())
				return it->second;
			else
			{
				try
				{
					Magick::Image img;
					img.ping(images[image_index]);
					texture_sizes[image_index] = sf::Vector2f(img.columns(), img.rows());
					return sf::Vector2f(img.columns(), img.rows());
				}
				catch(std::exception& e)
				{
					std::cerr << e.what() << std::endl;
					texture_sizes[image_index] = sf::Vector2f(0.f, 0.f);
					return sf::Vector2f(0.f, 0.f);
				}
			}
		}

		std::pair<int, int> get_texture_pageside(int image_index) const
		{
			static std::map<int, std::pair<int, int>> pages_side;
			auto it = pages_side.find(image_index);
			if (it != pages_side.end())
				return it->second;
			else
			{
				try
				{
					Magick::Image img;
					img.read(images[image_index]);

					Magick::Image img2 = img;
					img2.flop();

					img.crop(Magick::Geometry(3, img.rows()));
					img.resize(Magick::Geometry("1x1!"));
					img2.crop(Magick::Geometry(3, img2.rows()));
					img2.resize(Magick::Geometry("1x1!"));

					Magick::ColorGray avg_color_left = img.pixelColor(0, 0);
					Magick::ColorGray avg_color_right = img2.pixelColor(0, 0);
					float color_left = avg_color_left.shade();
					float color_right = avg_color_right.shade();

					std::pair<int, int> page_side = {color_left > 0.95 || color_left < 0.05, color_right > 0.95 || color_right < 0.05};

					pages_side[image_index] = page_side;
					return page_side;
				}
				catch(std::exception& e)
				{
					std::cerr << e.what() << std::endl;
					pages_side[image_index] = {0, 0};
					return {0, 0};
				}
			}
		}

		void update_status()
		{
			if (update_title || page_changed)
			{
				if (images.empty())
					window.setTitle("no images loaded");
				else
				{
					std::string title = std::to_string(curr_tag) + " - " + images[pages[curr_tag][curr_page_index][0]] + " [" + std::to_string(curr_page_index + 1) + "/" + std::to_string(pages[curr_tag].size()) + "]";
					window.setTitle(title);
				}
			}
			if (!images.empty() && page_changed)
			{
				std::cout << "current_image=";
				for (const auto& image_index : pages[curr_tag][curr_page_index])
					std::cout << images[image_index] << '\t';

				std::cout << std::endl;
			}
		}

		void update_paging(int tag, int restore_index = -1)
		{
			auto tag_it = tags_indices.find(tag);
			if (tag_it == tags_indices.end())
				return;

			auto& tag_indices = tag_it->second;
			auto& tag_repage_indices = repage_indices[tag];
			auto& tag_pages = pages[tag];

			std::vector<int> lone_page(tag_indices.size(), 0);
			for (int i = 0; i < (int)tag_indices.size(); ++i)
			{
				const auto& image_size = get_texture_size(tag_indices[i]);
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
			for (int i = 0; i < (int)tag_indices.size(); ++i)
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

						if (i != 0)
							start0++;
					}

					if (std::find(tag_repage_indices.begin(), tag_repage_indices.end(), tag_indices[i]) != tag_repage_indices.end())
						change_paging = !change_paging;

					auto[is_right, is_left] = get_texture_pageside(tag_indices[i]);

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

					if (i + 1 == (int)tag_indices.size() || lone_page[i + 1])
					{
						if (i + 1 != (int)tag_indices.size())
						{
							if (i - streak_begin % 2 == 0)
								start0++;
							else
								start1++;
						}

						lone_page[streak_begin] = start1 > start0;
						if (change_paging)
							lone_page[streak_begin] = 1 - lone_page[streak_begin];

						check_status = 0;
					}
				}
			}

			int old_page_index = curr_page_index;
			auto old_pages = tag_pages;

			tag_pages.clear();
			for (int i = 0; i < (int)tag_indices.size(); ++i)
			{
				std::cout << tag_indices[i] << std::endl;
				if (i + 1 == (int)tag_indices.size() || lone_page[i] || lone_page[i + 1])
				{
					if (tag == curr_tag && tag_indices[i] == restore_index)
						curr_page_index = tag_pages.size();

					tag_pages.emplace_back(std::vector{tag_indices[i]});
				}
				else
				{
					if (tag == curr_tag && (tag_indices[i] == restore_index || tag_indices[i+1] == restore_index))
						curr_page_index = tag_pages.size();

					tag_pages.emplace_back(std::vector{tag_indices[i], tag_indices[i+1]});
					i++;
				}
			}

			if (old_pages.empty() || old_pages[old_page_index] != tag_pages[curr_page_index])
				page_changed = true;
		}

		std::pair<float, sf::Vector2i> get_scale_centering(const std::vector<int>& page) const
		{
			sf::Vector2f drawn_area(0, 0);
			for (const auto& image_index : page)
			{
				auto image_size = get_texture_size(image_index);

				drawn_area.x += image_size.x;
				drawn_area.y = std::max(drawn_area.y, image_size.y);
			}

			float scale_x = window.getSize().x / drawn_area.x;
			float scale_y = window.getSize().y / drawn_area.y;
			float scale = std::min(scale_x, scale_y);

			sf::Vector2i center_offset(0, 0);
			center_offset.x = (window.getSize().x - drawn_area.x * scale) / 2.f;
			center_offset.y = (window.getSize().y - drawn_area.y * scale) / 2.f;

			return {scale, center_offset};
		}

		void render_page()
		{
			const auto& curr_pages = pages[curr_tag];
			auto[scale, center_offset] = get_scale_centering(curr_pages[curr_page_index]);

			float pos_x = 0;
			for (auto image_index : curr_pages[curr_page_index] | std::views::reverse)
			{
				sf::Sprite sprite(get_texture(image_index, scale));
				sprite.setPosition(pos_x, 0);
				sprite.move(sf::Vector2f(center_offset));
				window.draw(sprite);

				pos_x += sprite.getGlobalBounds().width;
			}
		}

		void render()
		{
			if (images.empty())
				return;

			std::vector<std::pair<int, float>> used_textures;
			int preload_depth = 1;
			for (int offset = -preload_depth; offset <= preload_depth; ++offset)
			{
				auto[preload_tag, preload_page_index, hit_border] = advance_page(curr_tag, curr_page_index, offset);
				if (hit_border)
					continue;

				const auto& preload_page = pages[preload_tag][preload_page_index];

				auto[scale, centering] = get_scale_centering(preload_page);
				for (int image_index : preload_page)
				{
					preload_texture_async(image_index, scale);
					used_textures.emplace_back(image_index, scale);
				}
			}

			render_page();

			std::erase_if(loaded_textures, [&used_textures](const auto& item)
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

		void poll_events()
		{
			sf::Event event;
			while (window.pollEvent(event))
			{
				if (event.type == sf::Event::Closed)
					window.close();
				else if (event.type == sf::Event::Resized)
				{
					sf::View new_view(sf::FloatRect(0.f, 0.f, event.size.width, event.size.height));
					window.setView(new_view);
				}
				else if (event.type == sf::Event::KeyPressed)
				{
					if ((event.key.code == sf::Keyboard::Space || event.key.code == sf::Keyboard::BackSpace))
					{
						int offset = 1;
						if (event.key.code == sf::Keyboard::BackSpace)
							offset = -1;

						auto[prev_tag, prev_page_index] = std::tie(curr_tag, curr_page_index);
						auto[new_tag, new_page_index, hit_border] = advance_page(curr_tag, curr_page_index, offset);

						if (prev_tag == new_tag && prev_page_index == new_page_index)
							std::cout << "last_in_dir=" << offset << std::endl;
						else
						{
							curr_tag = new_tag;
							curr_page_index = new_page_index;
							page_changed = true;
						}
					}
					else
					{
						auto it = user_bindings.find((int)event.key.code);
						if (it != user_bindings.end())
							run_command(it->second);
					}
				}
			}
		}

		void run_command(std::string_view cmd)
		{
			std::string_view action(cmd.begin(), cmd.begin() + cmd.find('('));

			std::vector<std::string> args;
			auto arg_begin = cmd.begin() + action.length() + 1;
			while (arg_begin != cmd.end())
			{
				auto next_sep_iter = std::find(arg_begin, cmd.end(), ',');
				if (next_sep_iter == cmd.end())
					next_sep_iter--;

				if (next_sep_iter - arg_begin > 0)
					args.emplace_back(arg_begin, next_sep_iter);

				arg_begin = next_sep_iter + 1;
			}

			if (action == "add_images")
			{
				int tag = 0;
				if (args.size() > 1)
					tag = std::stoi(args[0]);

				auto& tag_indices = tags_indices[tag];

				int added_count = 0;
				int restore_index = -1;
				if (tag == curr_tag && !images.empty())
					restore_index = pages[curr_tag][curr_page_index][0];

				for (int i = (args.size() > 1); i < (int)args.size(); ++i)
				{
					auto image_path = std::regex_replace(args[i], std::regex("^ +| +$"), "$1");
					if (image_path.empty())
						continue;

					auto image_it = std::find(images.begin(), images.end(), image_path);
					int new_index = std::distance(images.begin(), image_it);

					if (image_it == images.end())
						images.push_back(image_path);

					tag_indices.insert(std::upper_bound(tag_indices.begin(), tag_indices.end(), new_index,
								[this] (int index1, int index2)
							{
								return images[index1] < images[index2];
							}), new_index);

					if (images.size() == 1)
					{
						curr_page_index = 0;
						curr_tag = tag;
						restore_index = 0;
					}

					update_title = true;
					added_count++;
				}

				if (added_count > 0)
					update_paging(tag, restore_index);
			}
			else if (action == "goto_tag" || action == "remove_tag")
			{
				int tag = std::stoi(args[0]);
				auto tag_pages_it = pages.find(tag);

				if (tag_pages_it != pages.end())
				{
					if (action == "goto_tag")
					{
						curr_page_index = 0;
						curr_tag = tag;

						page_changed = true;
					}
					else
					{
						if (tag == curr_tag)
						{
							curr_page_index = 0;
							if (std::next(tag_pages_it) != pages.end())
								std::advance(tag_pages_it, 1);
							else if (tag_pages_it != pages.begin())
								std::advance(tag_pages_it, -1);

							curr_tag = tag_pages_it->first;

							page_changed = true;
						}

						tags_indices.erase(tag);
						pages.erase(tag);
					}
				}
				else
					std::cerr << "tag " << tag << " not present" << std::endl;
			}
			else if (action == "repage")
			{
				if (!pages.empty())
				{
					auto& curr_repage_indices = repage_indices[curr_tag];
					int curr_image_index = pages[curr_tag][curr_page_index][0];
					if (auto it = std::find(curr_repage_indices.begin(), curr_repage_indices.end(), curr_image_index); it != curr_repage_indices.end())
					{
						curr_repage_indices.erase(it);
					}
					else
						curr_repage_indices.push_back(curr_image_index);
					update_paging(curr_tag);
				}
			}
			else if (action == "output_string")
				std::cout << args[0] << std::endl;
			else if (action == "quit")
				window.close();
			else
			{
				std::cerr << action << " is not a valid command" << std::endl;
			}
		}

		void check_stdin()
		{
			if (std::cin.rdbuf()->in_avail())
			{
				std::string cmd;
				std::getline(std::cin, cmd);

				run_command(cmd);
			}
		}

	public:
		ImageViewerApp(std::string_view config_path)
		{
			window.create(sf::VideoMode(800, 600), "image viewer", sf::Style::Default);
			window.setKeyRepeatEnabled(false);
			window.setFramerateLimit(30);

			if (!config_path.empty())
				load_config(config_path);

			std::cin.sync_with_stdio(false);
		}

		void run()
		{
			while (window.isOpen())
			{
				//std::cerr << "BOI1" << std::endl;
				check_stdin();
				//std::cerr << "BOI2" << std::endl;
				poll_events();
				//std::cerr << "BOI3" << std::endl;
				update_status();
				//std::cerr << "BOI4" << std::endl;

				window.clear();
				render();
				window.display();
				//std::cerr << "BOI5" << std::endl;

				page_changed = false;
				update_title = false;
			}
		}
};
