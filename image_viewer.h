#pragma once

#include <fstream>
#include <iostream>
#include <map>
#include <ranges>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

#include <SFML/Graphics.hpp>
#include <Magick++.h>

class ImageViewerApp
{
	private:
		sf::RenderWindow window;

		std::vector<std::string> images;
		std::vector<sf::Vector2f> texture_sizes;
		std::vector<std::pair<int, int>> image_sides;

		std::map<int, sf::Texture> loaded_textures;

		//MAYBE REPLACE WITH VECTOR OF PAIRS
		std::map<int, std::vector<std::vector<int>>> pages;

		int curr_page_index = 0;
		int curr_tag = 0;

		bool update_title = true;
		bool page_changed = true;
		bool view_changed = true;

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

		sf::Texture& load_texture(int image_index, float scale = 1.f)
		{
			if (auto it = loaded_textures.find(image_index); it != loaded_textures.end())
				return it->second;

			Magick::Image image(images[image_index]);
			image.magick("RGBA");
			image.resize(Magick::Geometry(image.columns() * scale, image.rows() * scale));
			Magick::Blob blob;
			image.write(&blob);
			sf::Image sf_image;
			sf_image.create(image.columns(), image.rows(), (sf::Uint8*)blob.data());

			sf::Texture& tex = loaded_textures[image_index];
			tex.loadFromImage(sf_image);

			return tex;
		}

		void update_status()
		{
			if (update_title || page_changed)
			{
				if (pages.empty())
					window.setTitle("no images loaded");
				else
				{
					std::string title = std::to_string(curr_tag) + " - " + images[pages[curr_tag][curr_page_index][0]] + " [" + std::to_string(curr_page_index + 1) + "/" + std::to_string(pages[curr_tag].size()) + "]";
					window.setTitle(title);
				}
			}
			if (!pages.empty() && page_changed)
			{
				std::cout << "current_image=";
				for (const auto& image_index : pages[curr_tag][curr_page_index])
					std::cout << images[image_index] << '\t';

				std::cout << std::endl;
			}
		}

		void update_paging(int tag)
		{
			auto tag_pages_it = pages.find(tag);
			if (tag_pages_it == pages.end())
				return;

			auto& tag_pages = tag_pages_it->second;

			std::vector<int> image_indices;
			for (const auto& page : tag_pages)
				image_indices.insert(image_indices.end(), page.begin(), page.end());

			std::vector<int> lone_page(image_indices.size(), 0);

			for (int i = 0; i < (int)image_indices.size(); ++i)
			{
				int index = image_indices[i];
				const auto& image_size = texture_sizes[index];

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
			for (int i = 0; i < (int)image_indices.size(); ++i)
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

						if (i != 0)
							start0++;
					}

					auto[is_right, is_left] = image_sides[image_indices[i]];

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

					if (i + 1 == (int)image_indices.size() || lone_page[i + 1])
					{
						if (i + 1 != (int)image_indices.size())
						{
							if (i - streak_begin % 2 == 0)
								start0++;
							else
								start1++;
						}
						lone_page[streak_begin] = start1 > start0;
						check_status = 0;
					}
				}
			}

			auto old_pages = tag_pages;
			tag_pages.clear();
			for (int i = 0; i < (int)image_indices.size(); ++i)
			{
				if (i + 1 == (int)image_indices.size() || lone_page[i] || lone_page[i + 1])
				{
					tag_pages.emplace_back(std::vector{image_indices[i]});
				}
				else
				{
					tag_pages.emplace_back(std::vector{image_indices[i], image_indices[i+1]});
					i++;
				}
			}

			if (tag == curr_tag)
			{
				auto new_page_it = std::find_if(tag_pages.begin(), tag_pages.end(),
						[find_index = old_pages[curr_page_index][0]](const auto& page)
						{ return std::find(page.begin(), page.end(), find_index) != page.end(); });

				if (new_page_it == tag_pages.end())
					curr_page_index = 0;
				else
				{
					if (old_pages[curr_page_index] != *new_page_it)
						page_changed = true;

					curr_page_index = std::distance(tag_pages.begin(), new_page_it);
				}
			}
		}

		std::vector<int> render_pages()
		{
			auto curr_pages_it = pages.find(curr_tag);
			if (curr_pages_it == pages.end())
				return std::vector<int>();

			const auto& curr_pages = curr_pages_it->second;

			sf::Vector2f drawn_area(0, 0);
			for (const auto& image_index : curr_pages[curr_page_index])
			{
				auto image_size = texture_sizes[image_index];

				drawn_area.x += image_size.x;
				drawn_area.y = std::max(drawn_area.y, image_size.y);
			}

			float scale_x = window.getSize().x / drawn_area.x;
			float scale_y = window.getSize().y / drawn_area.y;
			float scale = std::min(scale_x, scale_y);

			sf::Vector2i center_offset(0, 0);
			center_offset.x = (window.getSize().x - drawn_area.x * scale) / 2.f;
			center_offset.y = (window.getSize().y - drawn_area.y * scale) / 2.f;

			float pos_x = 0;
			for (auto image_index : curr_pages[curr_page_index] | std::views::reverse)
			{
				sf::Sprite sprite;

				sprite.setTexture(load_texture(image_index, scale));
				sprite.setPosition(pos_x, 0);
				sprite.move(sf::Vector2f(center_offset));
				window.draw(sprite);

				pos_x += sprite.getGlobalBounds().width;
			}

			return curr_pages[curr_page_index];
		}

		void render()
		{
			if (!page_changed && !view_changed)
				return;

			std::vector<int> drawn_indices;

			if (view_changed)
				loaded_textures.clear();

			window.clear();
			drawn_indices = render_pages();

			std::erase_if(loaded_textures, [&drawn_indices](const auto& item)
				{
					return std::find(drawn_indices.begin(), drawn_indices.end(), item.first) == drawn_indices.end();
				});
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
					view_changed = true;
				}
				else if (event.type == sf::Event::KeyPressed)
				{
					if ((event.key.code == sf::Keyboard::Space || event.key.code == sf::Keyboard::BackSpace))
					{
						auto curr_pages_it = pages.find(curr_tag);
						if (curr_pages_it == pages.end())
							return;

						int prev_tag = curr_tag;
						int prev_page_index = curr_page_index;
						const auto& curr_tag_pages = curr_pages_it->second;

						int offset = 1;
						if (event.key.code == sf::Keyboard::BackSpace)
							offset = -1;

						int new_page_index = std::clamp(curr_page_index + offset, 0, (int)curr_tag_pages.size() - 1);
						int real_offset = new_page_index - curr_page_index;

						if (real_offset != 0)
							curr_page_index = new_page_index;
						else
						{
							int curr_pages_index = std::distance(pages.begin(), curr_pages_it);
							int new_pages_index = std::clamp(curr_pages_index + offset, 0, (int)pages.size() - 1);

							int real_offset = new_pages_index - curr_pages_index;
							if (real_offset != 0)
							{
								std::advance(curr_pages_it, real_offset);
								curr_tag = curr_pages_it->first;

								curr_page_index = 0;
								if (real_offset < 0)
									curr_page_index = curr_pages_it->second.size() - 1;
							}
							else
								std::cout << "last_in_dir=" << offset << std::endl;
						}

						if (prev_tag != curr_tag || prev_page_index != curr_page_index)
							page_changed = true;
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

		void repage(int tag, int start_index)
		{
			auto tag_pages_it = pages.find(tag);
			if (tag_pages_it == pages.end())
				return;

			auto& tag_pages = tag_pages_it->second;

			auto image_size = texture_sizes[tag_pages[curr_page_index][0]];

			if (image_size.x > image_size.y * 0.8)
				return;

			int change_begin = 0;
			for (int i = start_index; i >= 0; --i)
			{
				auto image_size = texture_sizes[tag_pages[i][0]];
				if (image_size.x > image_size.y * 0.8)
				{
					change_begin = i + 1;
					break;
				}
			}

			for (int i = change_begin; i < (int)tag_pages.size(); ++i)
			{
				bool next_page_wide = false;
				if (i + 1 != (int)tag_pages.size())
				{
					auto next_image_size = texture_sizes[tag_pages[i + 1][0]];
					if (next_image_size.x > next_image_size.y * 0.8)
						next_page_wide = true;
				}

				if (i + 1 == (int)tag_pages.size() || next_page_wide)
				{
					if (tag_pages[i].size() == 3)
					{
						tag_pages.insert(tag_pages.begin() + i + 1,
								std::vector(1, tag_pages[i].back()));
						tag_pages[i].pop_back();
					}
					break;
				}
				else
				{
					tag_pages[i+1].insert(tag_pages[i + 1].begin(), tag_pages[i].back());
					tag_pages[i].pop_back();
				}
			}
			if (tag_pages[change_begin].empty())
				tag_pages.erase(tag_pages.begin() + change_begin);

			page_changed = true;
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

				int added_count = 0;
				for (int i = (args.size() > 1); i < (int)args.size(); ++i)
				{
					auto image_path = std::regex_replace(args[i], std::regex("^ +| +$"), "$1");
					if (image_path.empty())
						continue;

					Magick::Image img;
					try
					{
						img.read(image_path);
						img.monochrome();

						auto image_it = std::find(images.begin(), images.end(), image_path);

						int new_index = image_it - images.begin();
						if (image_it == images.end())
						{
							images.push_back(image_path);
							texture_sizes.push_back(sf::Vector2f(img.columns(), img.rows()));

							Magick::Image img2 = img;
							img2.flop();

							img.crop(Magick::Geometry("3x10000+0+0"));
							img.resize(Magick::Geometry("1x1!"));
							img2.crop(Magick::Geometry("3x10000+0+0"));
							img2.resize(Magick::Geometry("1x1!"));

							Magick::ColorGray avg_color_left = img.pixelColor(0, 0);
							Magick::ColorGray avg_color_right = img2.pixelColor(0, 0);
							float color_left = avg_color_left.shade();
							float color_right = avg_color_right.shade();

							image_sides.emplace_back(color_left > 0.95 || color_left < 0.05, color_right > 0.95 || color_right < 0.05);
						}

						auto inserted_it = pages[tag].insert(std::upper_bound(pages[tag].begin(), pages[tag].end(), std::vector(1, new_index), [this]
								(const auto& page1, const auto& page2)
								{
									return images[page1[0]] < images[page2[0]];
								}), std::vector(1, new_index));
						int inserted_index = inserted_it - pages[tag].begin();

						if (tag == curr_tag && pages[tag].size() > 1 && inserted_index <= curr_page_index)
							curr_page_index++;

						if (pages.size() == 1 && pages[tag].size() == 1)
						{
							curr_page_index = 0;
							curr_tag = tag;
							page_changed = true;
						}

						update_title = true;
						added_count++;
					}
					catch(Magick::Exception& e)
					{
						std::cerr << "image loading error: " << e.what() << std::endl;
					}
				}

				if (added_count > 0)
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

						pages.erase(tag);
					}
				}
				else
					std::cerr << "tag " << tag << " not present" << std::endl;
			}
			else if (action == "repage")
				repage(curr_tag, curr_page_index);
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
			window.setFramerateLimit(60);

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

				render();
				window.display();
				//std::cerr << "BOI5" << std::endl;

				page_changed = false;
				update_title = false;
				view_changed = false;
			}
		}
};
