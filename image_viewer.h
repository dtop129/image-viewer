#pragma once

#include <fstream>
#include <iostream>
#include <map>
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
		sf::View window_view;

		std::vector<sf::Sprite> sprites;

		std::vector<std::string> images;
		std::map<int, std::vector<std::vector<int>>> pages;

		std::map<std::string, sf::Texture> loaded_textures;

		int curr_page_index = 0;
		int curr_tag = 0;

		bool title_changed = true;
		bool page_changed = true;

		std::string paging_save_file;

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

		sf::Texture& load_texture(const std::string& image_path, float scale = 1.f)
		{
			Magick::Image image(image_path);
			image.magick("RGBA");
			image.resize(Magick::Geometry(image.columns() * scale, image.rows() * scale));
			Magick::Blob blob;
			image.write(&blob);
			sf::Image sf_image;
			sf_image.create(image.columns(), image.rows(), (sf::Uint8*)blob.data());

			auto& tex = loaded_textures[image_path];
			tex.loadFromImage(sf_image);
			return tex;
		}

		int page_from_index(int tag, int image_index) const
		{
			auto page_it = pages.find(tag);
			if (page_it == pages.end())
				return -1;
			const auto& tag_images = page_it->second;

			auto it = std::find_if(tag_images.begin(), tag_images.end(), [image_index](const std::vector<int>& page)
					{
						return std::find(page.begin(), page.end(), image_index) != page.end();
					});

			if (it == tag_images.end())
				return -1;

			return it - tag_images.begin();
		}

		void update_status()
		{
			if (title_changed || page_changed)
			{
				if (pages.empty())
					window.setTitle("no images loaded");
				else
				{
					std::string title = std::to_string(curr_tag) + " [" + std::to_string(curr_page_index + 1) + "/" + std::to_string(pages[curr_tag].size()) + "]";
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

		void prepare_render()
		{
			if (page_changed)
			{
				sprites.clear();
				if (!pages.empty())
					sprites.emplace_back(load_texture(images[pages[curr_tag][curr_page_index][0]]));
			}
		}

		void render()
		{
			prepare_render();
			for (const auto& sprite : sprites)
				window.draw(sprite);

			page_changed = false;
		}

		void poll_events(float dt)
		{
			sf::Event event;
			while (window.pollEvent(event))
			{
				if (event.type == sf::Event::Closed)
					window.close();
				else if (event.type == sf::Event::Resized)
				{
					window_view.setSize(event.size.width, event.size.height);
					window.setView(window_view);
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
								std::cout << std::distance(pages.begin(), curr_pages_it) << std::endl;
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

				std::vector<int> added_indices;

				for (int i = (args.size() > 1); i < (int)args.size(); ++i)
				{
					auto image_path = std::regex_replace(args[i], std::regex("^ +| +$"), "$1");
					if (image_path.empty())
						continue;

					if (sf::Texture tex; tex.loadFromFile(image_path))
					{
						auto image_it = std::find(images.begin(), images.end(), image_path);
						if (image_it == images.end())
						{
							added_indices.push_back(images.size());
							images.push_back(image_path);
						}
						else
							added_indices.push_back(image_it - images.begin());

						title_changed = true;
					}
				}
				auto inserted_it = pages[tag].insert(std::upper_bound(pages[tag].begin(), pages[tag].end(), added_indices, [this]
						(const auto& page1, const auto& page2)
						{
							return images[page1[0]] < images[page2[0]];
						}), added_indices);
				int inserted_index = inserted_it - pages[tag].begin();

				if (pages[tag].size() > 1 && inserted_index <= curr_page_index)
					curr_page_index++;

				if (pages.size() == 1)
				{
					curr_page_index = 0;
					curr_tag = tag;
					page_changed = true;
				}

				//for (auto indices : pages[tag])
				//{
				//	std::cout << "[";
				//	for (auto index : indices)
				//	{
				//		std::cout << images[index] << ',';
				//	}
				//	std::cout << "],";
				//}
				//std::cout << std::endl;
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
		ImageViewerApp(std::string_view config_path, std::string_view paging_save_file)
		{
			window.create(sf::VideoMode(800, 600), "image viewer", sf::Style::Default);
			window.setKeyRepeatEnabled(false);
			window.setFramerateLimit(60);

			if (!config_path.empty())
				load_config(config_path);

			this->paging_save_file = paging_save_file;

			std::cin.sync_with_stdio(false);
		}

		~ImageViewerApp()
		{
			if (paging_save_file.empty())
				return;

			//OUTPUT ALL PAGING TO FILE
			std::ofstream out(paging_save_file);

			std::string delim = "";
			for (const auto&[tag, tags_pages] : pages)
			{
				for (const auto& tag_pages : tags_pages)
				{
					for (const auto& image_index : tag_pages)
					{
						out << delim << images[image_index];
						delim = "\t";
					}
					out << "\n";
				}
			}
		}

		void run()
		{
			sf::Clock clock;
			float dt = 0.f;
			while (window.isOpen())
			{
				//std::cout << "BOI1" << std::endl;
				check_stdin();
				//std::cout << "BOI2" << std::endl;
				poll_events(dt);
				//std::cout << "BOI3" << std::endl;
				update_status();
				//std::cout << "BOI4" << std::endl;

				window.clear();
				render();
				window.display();
				//std::cout << "BOI5" << std::endl;

				dt = clock.restart().asSeconds();
			}
		}
};
