#pragma once

#include <chrono>
#include <cmath>
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
#include "BS_thread_pool.hpp"

class LazyLoadBase
{
	protected:
		static BS::thread_pool pool;
};

BS::thread_pool LazyLoadBase::pool;

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

		const T& get()
		{
			if (loading_resource.valid())
				resource = loading_resource.get();

			return resource;
		}
};

sf::Texture load_texture(const std::string& image_path, float scale = 1.f)
{
	Magick::Image image;
	try
	{
		image.read(image_path);
	}
	catch (std::exception& e)
	{
		return sf::Texture();
	}

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


enum class ViewMode { Manga, Vertical };

class ImageViewerApp
{
	private:
		sf::RenderWindow window;

		ViewMode mode = ViewMode::Manga;
		std::vector<std::string> images;

		std::map<std::pair<int, float>, LazyLoad<sf::Texture>> loaded_textures;
		std::vector<LazyLoad<sf::Vector2f>> texture_sizes;
		std::vector<std::pair<int, float>> used_textures;

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

			auto[it, inserted] = loaded_textures.emplace(std::pair{image_index, scale},
					[image_path = images[image_index], scale]
					{ return load_texture(image_path, scale); });
			return it->second.get();
		}

		std::pair<int, int> get_texture_pageside(int image_index)
		{
			auto it = pages_side.find(image_index);
			if (it != pages_side.end())
				return it->second;

			Magick::Image img;
			try
			{
				img.read(images[image_index]);
			}
			catch(std::exception& e)
			{
				std::cerr << e.what() << std::endl;
				pages_side[image_index] = {0, 0};
				return {0, 0};
			}

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

					std::string title = std::to_string(curr_tag) + " - " + images[curr_image_index] + " [" + std::to_string(relative_index + 1) + "/" + std::to_string(tag_indices.size()) + "]";
					window.setTitle(title);
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
				for (int i = 0; i < (int)tag_indices.size(); ++i)
				{
					const auto& image_size = texture_sizes[tag_indices[i]].get();
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

							//DON'T TRUST WIDE PAGES AS FIRST PAGE, USUALLY IT IS NOT PART OF MANGA
							if (i > 1)
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

				tag_pages.clear();
				for (int i = 0; i < (int)tag_indices.size(); ++i)
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

			if (mode == ViewMode::Manga)
			{
				sf::Vector2i drawn_area(0, 0);
				int index = 0;
				for (const auto& image_index : page)
				{
					sf::Vector2f image_size = texture_sizes[image_index].get();

					if (drawn_area.y != 0 && image_size.y != drawn_area.y)
					{
						scales[index] = drawn_area.y / image_size.y;
						image_size.x *= scales[index];
						image_size.y = drawn_area.y;
					}

					drawn_area.x += image_size.x;
					drawn_area.y = std::max(drawn_area.y, (int)image_size.y);

					index++;
				}

				float scale_x = (float)window.getSize().x / drawn_area.x;
				float scale_y = (float)window.getSize().y / drawn_area.y;

				float global_scale = std::min(scale_x, scale_y);
				center_offset.x = (window.getSize().x - drawn_area.x * global_scale) / 2.f;
				center_offset.y = (window.getSize().y - drawn_area.y * global_scale) / 2.f;

				for (auto& scale_fac : scales)
					scale_fac *= global_scale;
			}
			else
			{
				int image_index = page[0];
				int width = texture_sizes[image_index].get().x;

				float scale = std::min(700.f, 0.8f * window.getSize().x) / width;
				center_offset.x = (window.getSize().x - width * scale) / 2.f;

				scales = { scale };
			}

			return {scales, center_offset};
		}

		//returns size of drawn sprite
		sf::Vector2i draw_image(int image_index, float scale, sf::Vector2i pos)
		{
			sf::Sprite sprite(get_texture(image_index, scale));
			sprite.setPosition(sf::Vector2f(pos));
			window.draw(sprite);

			used_textures.emplace_back(image_index, scale);
			return sf::Vector2i(sprite.getGlobalBounds().width, sprite.getGlobalBounds().height);
		}

		int render_manga()
		{
			const auto& curr_pages = pages[curr_tag];
			auto[scales, center_offset] = get_scale_centering(curr_pages[curr_page_index]);

			int index = curr_pages[curr_page_index].size() - 1;
			int pos_x = 0;
			for (auto image_index : curr_pages[curr_page_index] | std::views::reverse)
			{
				sf::Vector2i drawn_size = draw_image(image_index, scales[index], center_offset + sf::Vector2i(pos_x, 0));
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
			if (mode == ViewMode::Manga)
				n_drawn_pages = render_manga();
			else
				n_drawn_pages = render_vertical();

			std::array preload_offsets{-1, n_drawn_pages};
			for (auto offset : preload_offsets)
			{
				auto[preload_tag, preload_page_index, hit_border] = advance_page(curr_tag, curr_page_index, offset);
				if (hit_border)
					continue;

				const auto& preload_page = pages[preload_tag][preload_page_index];
				auto[scales, centering] = get_scale_centering(preload_page);
				int index = 0;
				for (int image_index : preload_page)
				{
					preload_texture_async(image_index, scales[index]);
					used_textures.emplace_back(image_index, scales[index]);
					index++;
				}
			}

			std::erase_if(loaded_textures, [this](const auto& item)
				{
					return std::find(used_textures.begin(), used_textures.end(), item.first)
						== used_textures.end();
				});
			used_textures.clear();
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
			float curr_tex_height = texture_sizes[curr_page[0]].get().y;

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

					curr_tex_height = texture_sizes[pages[curr_tag][curr_page_index][0]].get().y;
					auto[scales, center_offset] = get_scale_centering(pages[curr_tag][curr_page_index]);
					vertical_offset += curr_tex_height * scales[0];
				}

				curr_page = pages[curr_tag][curr_page_index];
				std::tie(scales, center_offset) = get_scale_centering(curr_page);
				curr_tex_height = texture_sizes[curr_page[0]].get().y;
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
					if ((event.key.code == sf::Keyboard::Space || event.key.code == sf::Keyboard::Backspace))
					{
						if (tags_indices.empty())
							return;

						int offset = 1;
						if (event.key.code == sf::Keyboard::Backspace)
							offset = -1;

						if (mode == ViewMode::Manga)
						{
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
						else
							vertical_scroll(window.getSize().y * 0.5 * offset);
					}
					else if (auto it = user_bindings.find((int)event.key.code); it != user_bindings.end())
					{
						run_command(it->second);
					}

					keys_state[event.key.code] = 1;
				}
				else if (event.type == sf::Event::KeyReleased)
				{
					keys_state[event.key.code] = 0;
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
				{
					tag = std::stoi(args[0]);
					args.erase(args.begin());
				}

				for (const auto& arg : args)
				{
					auto image_path = std::regex_replace(arg, std::regex("^ +| +$"), "$1");
					if (image_path.empty())
						continue;

					auto image_it = std::find(images.begin(), images.end(), image_path);
					int new_index = std::distance(images.begin(), image_it);

					if (image_it == images.end())
					{
						images.push_back(image_path);
						texture_sizes.emplace_back([image_path]
							{
								try
								{
									Magick::Image img;
									img.ping(image_path);
									return sf::Vector2f(img.columns(), img.rows());
								}
								catch (std::exception& e)
								{
									std::cerr << e.what() << std::endl;
									return sf::Vector2f(0.f, 0.f);
								}
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

				if (!args.empty())
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
				else if (args[0] == "vertical")
					mode = ViewMode::Vertical;
				else
					std::cerr << "Mode " << args[0] << " not recognized" << std::endl;

				if (mode != prev_mode)
				{
					std::cout << "current_mode=" << args[0] << std::endl;

					for (const auto&[tag, pages] : pages)
						update_paging(tag);
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
			window.setVerticalSyncEnabled(true);

			if (!config_path.empty())
				load_config(config_path);

			std::cin.sync_with_stdio(false);
			std::cin.tie(NULL);

			std::cout << "current_mode=manga" << std::endl;
		}

		void run()
		{
			sf::Clock clock;
			while (window.isOpen())
			{
				float dt = clock.restart().asSeconds();
				//std::cerr << "BOI1" << std::endl;
				check_stdin();
				//std::cerr << "BOI2" << std::endl;
				poll_events();
				//std::cerr << "BOI23" << std::endl;
				handle_keyboard(dt);

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
