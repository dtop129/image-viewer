#pragma once

#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <SFML/Graphics.hpp>

enum class ViewMode { SinglePage, DoublePage, DoublePageManga, ContinuousVert, Invalid };

class ImageViewerApp
{
	private:
		sf::RenderWindow window;
		sf::View window_view;

		ViewMode mode = ViewMode::SinglePage;

		std::vector<sf::Sprite> sprites;

		std::map<int, std::vector<std::string>> images;
		std::map<std::string, int> image_tags;
		std::map<std::string, int> texture_wide;
		std::map<std::string, sf::Texture> loaded_textures;

		std::map<int, std::vector<std::string>>::iterator curr_tag_images;
		int curr_image_index = -1;

		int last_render_image_index = -2;
		ViewMode last_render_mode = ViewMode::Invalid;

		std::map<int, std::vector<int>> double_pages;
		bool double_paging_change = false;

		float scroll_speed = 1000.f;

		bool reset_view = true;

		std::map<int, std::string> user_bindings;

		void load_config(const std::string& config_path)
		{
			char binding;
			std::string command;

			std::ifstream stream(config_path);

			while (stream >> binding)
			{
				std::getline(stream, command);
				auto str_begin = command.find_first_not_of(" ");
				command = command.substr(str_begin);

				int key = sf::Keyboard::Key::A + (binding - 'a');
				user_bindings[key] = command;
			}
		}

		int curr_double_page_index() const
		{
			auto it = double_pages.find(curr_tag_images->first);
			if (it == double_pages.end())
				return 0;

			const auto& current_double_pages = it->second;
			auto current_double_page_it = std::find(current_double_pages.begin(), current_double_pages.end(), curr_image_index);
			if (current_double_page_it == current_double_pages.end())
				current_double_page_it = std::find(current_double_pages.begin(), current_double_pages.end(), curr_image_index - 1);

			return current_double_page_it - current_double_pages.begin();
		}

		void update_double_pages_from(int tag, int new_index)
		{
			const auto& tag_images = images.find(tag);
			if (tag_images == images.end())
				return;

			auto& tag_double_pages = double_pages[tag];

			if (tag_double_pages.empty())
				new_index = 0;
			else
			{
				auto change_begin = std::find(tag_double_pages.begin(), tag_double_pages.end(), new_index);
				if (change_begin == tag_double_pages.end())
				{
					change_begin = std::find(tag_double_pages.begin(), tag_double_pages.end(), new_index - 1);
				}

				if (change_begin != tag_double_pages.end())
					new_index = *change_begin;

				tag_double_pages.erase(change_begin, tag_double_pages.end());
			}

			const auto& images_vec = tag_images->second;

			int distance_from_last = 0;
			int prev_wide_page = true;
			for (auto it = images_vec.begin() + new_index; it != images_vec.end(); ++it)
			{
				distance_from_last++;
				int is_wide = texture_wide[*it];

				if (is_wide)
					distance_from_last++;

				if (distance_from_last > 1 || prev_wide_page)
				{
					tag_double_pages.push_back(it - images_vec.begin());
					distance_from_last = 0;
				}

				prev_wide_page = is_wide;
			}

			if (tag == curr_tag_images->first)
				double_paging_change = true;
		}

		void fix_double_pages()
		{
			int current_tag = curr_tag_images->first;
			auto& current_double_pages = double_pages[current_tag];

			if (texture_wide[current_image()] == 1)
				return;

			auto begin_change_page = current_double_pages.begin();

			for (auto it = std::make_reverse_iterator(current_double_pages.begin() + curr_double_page_index()); it != current_double_pages.rend(); ++it)
			{
				const auto& image = images[current_tag][*it];
				if (texture_wide[image] == 1)
				{
					begin_change_page = it.base();
					break;
				}
			}

			int first_changed_index = *begin_change_page;
			auto first_changed_image = images[current_tag][first_changed_index];

			if (texture_wide[first_changed_image] == 2)
				texture_wide[first_changed_image] = 0;
			else
				texture_wide[first_changed_image] = 2;

			update_double_pages_from(current_tag, first_changed_index);
		}

		sf::Texture& load_texture(const std::string& image_path)
		{
			auto tex_iter = loaded_textures.find(image_path);
			if (tex_iter != loaded_textures.end())
				return tex_iter->second;

			auto& tex = loaded_textures[image_path];
			tex.loadFromFile(image_path);
			return tex;
		}

		const std::string& current_image() const
		{
			return curr_tag_images->second[curr_image_index];
		}

		void prepare_render()
		{
			bool image_changed = curr_image_index != last_render_image_index;
			bool mode_changed = mode != last_render_mode;

			if (mode == ViewMode::SinglePage)
			{
				if (image_changed || mode_changed)
				{
					loaded_textures.clear();
					sprites.clear();
					sprites.emplace_back(load_texture(current_image()));
				}

				if (image_changed || reset_view || mode_changed)
				{
					auto drawn_rect = sprites[0].getGlobalBounds();
					auto sprite_center = sf::Vector2f(drawn_rect.left + drawn_rect.width / 2.f, drawn_rect.top + drawn_rect.height / 2.f);

					float zoom_x = drawn_rect.width / window_view.getSize().x;
					float zoom_y = drawn_rect.height / window_view.getSize().y;

					window_view.setCenter(sprite_center);
					window_view.zoom(std::max(zoom_x, zoom_y));
					window.setView(window_view);
				}
			}
			else if (mode == ViewMode::DoublePage || mode == ViewMode::DoublePageManga)
			{
				int current_tag = curr_tag_images->first;
				if (mode_changed)
					for (auto[tag, images_vec] : images)
						update_double_pages_from(tag, 0);

				if (image_changed || mode_changed || double_paging_change)
				{
					loaded_textures.clear();
					sprites.clear();

					const auto& curr_double_pages = double_pages[current_tag];
					int curr_double_index = curr_double_page_index();
					int first_image_index = curr_double_pages[curr_double_index];

					sprites.emplace_back(load_texture(images[current_tag][first_image_index]));

					if (first_image_index + 1 != (int)images[current_tag].size() &&
							(curr_double_index + 1 == (int)curr_double_pages.size() || curr_double_pages[curr_double_index + 1] != first_image_index + 1))
					{
						sprites.emplace_back(load_texture(images[current_tag][first_image_index + 1]));

						float scale_factor = sprites[0].getLocalBounds().height / sprites[1].getLocalBounds().height;
						sprites[1].setScale(scale_factor, scale_factor);

						int offset = sprites[0].getGlobalBounds().width;
						if (mode == ViewMode::DoublePageManga)
							offset = -sprites[1].getGlobalBounds().width;
						sprites[1].move(offset, 0);
					}
				}

				if (image_changed || mode_changed || reset_view || double_paging_change)
				{
					sf::FloatRect drawn_rect;
					for (const auto& sprite : sprites)
					{
						auto sprite_rect = sprite.getGlobalBounds();
						drawn_rect.left = std::min(sprite_rect.left, drawn_rect.left);
						drawn_rect.top = std::min(sprite_rect.top, drawn_rect.top);
						drawn_rect.width += sprite_rect.width;
						drawn_rect.height = std::max(sprite_rect.height, drawn_rect.height);
					}

					auto sprite_center = sf::Vector2f(drawn_rect.left + drawn_rect.width / 2.f, drawn_rect.top + drawn_rect.height / 2.f);

					float zoom_x = drawn_rect.width / window_view.getSize().x;
					float zoom_y = drawn_rect.height / window_view.getSize().y;

					window_view.setCenter(sprite_center);
					window_view.zoom(std::max(zoom_x, zoom_y));
					window.setView(window_view);
				}
			}
		}

		void render()
		{
			if (images.empty())
			{
				if (curr_image_index != last_render_image_index)
					window.setTitle("no images loaded");

				last_render_image_index = curr_image_index;
				return;
			}

			if (curr_image_index != last_render_image_index)
			{
				std::string title = std::to_string(curr_tag_images->first) + " - " + std::to_string(curr_image_index);
				window.setTitle(title);

				std::cout << "current_image=\"" << current_image() << '"' << std::endl;
			}
			if (mode != last_render_mode)
			{
				std::string mode_str;
				if (mode == ViewMode::SinglePage)
					mode_str = "single";
				else if (mode == ViewMode::DoublePage)
					mode_str = "double";
				else if (mode == ViewMode::DoublePageManga)
					mode_str = "manga";
				std::cout << "current_mode=" << mode_str << std::endl;
			}

			prepare_render();
			for (const auto& sprite : sprites)
				window.draw(sprite);

			last_render_image_index = curr_image_index;
			last_render_mode = mode;
			reset_view = false;
			double_paging_change = false;
		}

		void change_mode(ViewMode new_mode)
		{
			mode = new_mode;
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
					sf::Vector2f prevcorner = window_view.getCenter() - window_view.getSize() / 2.f;
					window_view.reset(sf::FloatRect(prevcorner, sf::Vector2f(event.size.width, event.size.height)));

					reset_view = true;
				}
				else if (event.type == sf::Event::MouseWheelScrolled)
				{
					window_view.move(0, -scroll_speed * event.mouseWheelScroll.delta * dt);
					window.setView(window_view);
				}
				else if (event.type == sf::Event::KeyPressed)
				{
					if ((event.key.code == sf::Keyboard::Space ||
							event.key.code == sf::Keyboard::BackSpace) && !images.empty())
					{
						if (mode == ViewMode::DoublePage || mode == ViewMode::DoublePageManga)
						{
							int offset = 1;
							if (event.key.code == sf::Keyboard::BackSpace)
								offset = -1;

							int double_index = curr_double_page_index();
							double_index += offset;

							int corrected_index = std::clamp(double_index, 0, (int)double_pages[curr_tag_images->first].size() - 1);
							if (double_index != corrected_index)
							{

								if ((offset == 1 && std::next(curr_tag_images) == images.end()) || (offset == -1 && curr_tag_images == images.begin()))
									std::cout << "last_in_dir=" << (offset > 0) - (offset < 0) << std::endl;
								else
								{
									std::advance(curr_tag_images, offset);

									corrected_index = 0;
									int new_tag = curr_tag_images->first;

									if (offset == 1)
										corrected_index = 0;
									else
										corrected_index = double_pages[new_tag].size() - 1;
								}
							}

							curr_image_index = double_pages[curr_tag_images->first][corrected_index];
						}
						else
						{
							int offset = 1;
							if (event.key.code == sf::Keyboard::BackSpace)
								offset = -1;

							curr_image_index += offset;

							int corrected_index = std::clamp(curr_image_index, 0, (int)curr_tag_images->second.size() - 1);
							if (curr_image_index != corrected_index)
							{
								if ((offset == 1 && curr_tag_images == std::prev(images.end())) || (offset == -1 && curr_tag_images == images.begin()))
									std::cout << "last_in_dir=" << (offset > 0) - (offset < 0) << std::endl;
								else
								{
									std::advance(curr_tag_images, offset);

									if (offset == 1)
										corrected_index = 0;
									else
										corrected_index = curr_tag_images->second.size() - 1;
								}
							}

							curr_image_index = corrected_index;
						}
					}
					else
					{
						auto it = user_bindings.find((int)event.key.code);
						if (it != user_bindings.end())
						{
							run_command(it->second);
						}
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

				args.emplace_back(arg_begin, next_sep_iter);
				arg_begin = next_sep_iter + 1;
			}

			if (action == "add_image")
			{
				if (sf::Texture tex; tex.loadFromFile(args[0]))
				{
					tex.setSmooth(true);
					texture_wide[args[0]] = tex.getSize().x > tex.getSize().y;

					int tag = 0;
					if (args.size() == 2)
						tag = std::stoi(args[1]);

					auto& tag_images_vec = images[tag];

					auto new_iter = tag_images_vec.insert(std::upper_bound(tag_images_vec.begin(), tag_images_vec.end(), args[0]), args[0]);

					if (images.size() == 1 && tag_images_vec.size() == 1)
					{
						curr_image_index = 0;
						curr_tag_images = images.find(tag);
					}
					else if (tag == curr_tag_images->first && new_iter - tag_images_vec.begin() <= curr_image_index)
						curr_image_index++;

					if (mode == ViewMode::DoublePage || mode == ViewMode::DoublePageManga)
						update_double_pages_from(tag, new_iter - tag_images_vec.begin());
				}
			}
			else if (action == "goto_image_byindex")
			{
				int new_index = std::stoi(args[0]);
				int tag = curr_tag_images->first;
				if (args.size() == 2)
					tag = std::stoi(args[1]);

				auto tag_images_iter = images.find(tag);

				if (tag_images_iter != images.end())
				{
					curr_tag_images = tag_images_iter;

					auto& tag_images_vec = tag_images_iter->second;

					if (new_index < 0)
						new_index = (int)tag_images_vec.size() + new_index;

					if (new_index >= 0 && new_index < (int)tag_images_vec.size())
					{
						curr_image_index = new_index;

						if (mode == ViewMode::DoublePage || mode == ViewMode::DoublePageManga)
						{
							const auto& current_double_pages = double_pages[tag];
							if (std::find(current_double_pages.begin(), current_double_pages.end(), curr_image_index) == current_double_pages.end())
								curr_image_index--;
						}
					}
					else
						std::cerr << "index " << new_index << " not present on tag " << tag << std::endl;
				}
				else
					std::cerr << "tag " << args[0] << " not present" << std::endl;
			}
			else if (action == "goto_image_byname")
			{
				int tag = curr_tag_images->first;
				if (args.size() == 2)
					tag = std::stoi(args[1]);

				auto tag_images_iter = images.find(tag);

				if (tag_images_iter != images.end())
				{
					curr_tag_images = tag_images_iter;

					auto& tag_images_vec = tag_images_iter->second;
					auto image_iter = std::find(tag_images_vec.begin(), tag_images_vec.end(), args[0]);
					if (image_iter != tag_images_vec.end())
					{
						curr_image_index = image_iter - tag_images_vec.begin();

						if (mode == ViewMode::DoublePage || mode == ViewMode::DoublePageManga)
						{
							const auto& current_double_pages = double_pages[tag];
							if (std::find(current_double_pages.begin(), current_double_pages.end(), curr_image_index) == current_double_pages.end())
								curr_image_index--;
						}
					}
					else
						std::cerr << args[0] << " not present on tag " << tag << std::endl;
				}
				else
					std::cerr << "tag " << tag << " not present" << std::endl;
			}
			else if (action == "goto_tag" || action == "remove_tag")
			{
				int tag = std::stoi(args[0]);
				auto tag_images_iter = images.find(tag);

				if (tag_images_iter != images.end())
				{
					if (action == "goto_tag")
					{
						curr_tag_images = tag_images_iter;
						curr_image_index = 0;
					}
					else
					{
						if (tag == curr_tag_images->first)
						{
							curr_image_index = 0;
							if (std::next(curr_tag_images) != images.end())
								std::advance(curr_tag_images, 1);
							else if (curr_tag_images != images.begin())
								std::advance(curr_tag_images, -1);
							else
								curr_image_index = -1;
						}

						images.erase(tag);
					}
				}
				else
					std::cerr << "tag " << tag << " not present" << std::endl;
			}
			else if (action == "change_mode")
			{
				if (args[0] == "single")
					change_mode(ViewMode::SinglePage);
				else if (args[0] == "double")
					change_mode(ViewMode::DoublePage);
				else if (args[0] == "manga")
					change_mode(ViewMode::DoublePageManga);
				//else if (args[0] == "vert")
				//	change_mode(ViewMode::ContinuousVert);
				else
					std::cerr << args[0] << "is not a valid mode" << std::endl;
			}
			else if (action == "repage")
			{
				if (mode != ViewMode::DoublePage && mode != ViewMode::DoublePageManga)
					std::cerr << "repaging only with double page layout" << std::endl;
				else
					fix_double_pages();
			}
			else if (action == "output_image_list")
			{
				for (const auto& [tag, vec] : images)
				{
					for (const auto& image : vec)
					{
						std::cout << "tag " << tag << " - " << image << std::endl;
					}
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
		ImageViewerApp(std::string config_path = std::string())
		{
			sf::ContextSettings settings;
			settings.antialiasingLevel = 8;
			window.create(sf::VideoMode(800, 600), "image viewer", sf::Style::Default, settings);
			//window.setKeyRepeatEnabled(false);
			window.setFramerateLimit(60);

			if (!config_path.empty())
				load_config(config_path);

			std::cin.sync_with_stdio(false);
		}

		void run()
		{
			sf::Clock clock;
			float dt = 0.f;
			while (window.isOpen())
			{
				check_stdin();
				poll_events(dt);

				window.clear();
				render();
				window.display();

				dt = clock.restart().asSeconds();
			}
		}
};
