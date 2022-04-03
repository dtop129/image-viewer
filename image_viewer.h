#pragma once

#include <iostream>
#include <map>
#include <string>
#include <vector>

#include <SFML/Graphics.hpp>

enum class ViewMode { SinglePage, DoublePage, DoublePageManga, ContinuousVert };

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
		ViewMode last_render_mode = ViewMode::SinglePage;

		std::map<int, std::vector<int>> double_pages;
		int current_double_page_index = 0;

		float scroll_speed = 1000.f;

		bool reset_view = true;

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
			{
				if (std::find(tag_double_pages.begin(), tag_double_pages.end(), curr_image_index) == tag_double_pages.end())
					curr_image_index--;

				current_double_page_index = std::find(tag_double_pages.begin(), tag_double_pages.end(), curr_image_index) - tag_double_pages.begin();
			}
		}

		void fix_double_pages()
		{
			int current_tag = curr_tag_images->first;
			auto& current_double_pages = double_pages[current_tag];

			if (texture_wide[current_image()] == 1 || curr_image_index == (int)images[current_tag].size() - 1)
				return;

			auto begin_change_page = current_double_pages.begin();
			for (auto it = std::find(current_double_pages.rbegin(), current_double_pages.rend(), curr_image_index) + 1; it != current_double_pages.rend(); ++it)
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
			last_render_image_index = curr_image_index - 1; //HACK TO ALLOW REDRAWING
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

		const std::string& next_tag_image() const
		{
			if (curr_image_index + 1 != (int)curr_tag_images->second.size())
				return curr_tag_images->second[curr_image_index + 1];

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

				if (image_changed || mode_changed)
				{
					loaded_textures.clear();
					sprites.clear();

					sprites.emplace_back(load_texture(current_image()));
					if (curr_image_index != (int)images[current_tag].size() - 1 && std::find(double_pages[current_tag].begin(), double_pages[current_tag].end(), curr_image_index + 1) == double_pages[current_tag].end())
					{
						sprites.emplace_back(load_texture(next_tag_image()));

						float scale_factor = sprites[0].getLocalBounds().height / sprites[1].getLocalBounds().height;
						sprites[1].setScale(scale_factor, scale_factor);

						int offset = sprites[0].getGlobalBounds().width;
						if (mode == ViewMode::DoublePageManga)
							offset = -sprites[1].getGlobalBounds().width;
						sprites[1].move(offset, 0);
					}
				}

				if (image_changed || mode_changed || reset_view)
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

			prepare_render();
			for (const auto& sprite : sprites)
				window.draw(sprite);

			last_render_image_index = curr_image_index;
			last_render_mode = mode;
			reset_view = false;
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
					if (event.key.code == sf::Keyboard::Q)
						window.close();
					else if (event.key.code == sf::Keyboard::S)
						change_mode(ViewMode::SinglePage);
					else if (event.key.code == sf::Keyboard::D)
						change_mode(ViewMode::DoublePage);
					else if (event.key.code == sf::Keyboard::M)
						change_mode(ViewMode::DoublePageManga);
					//else if (event.key.code == sf::Keyboard::V)
					//	change_mode(ViewMode::ContinuousVert);
					else if (event.key.code == sf::Keyboard::C &&
							(mode == ViewMode::DoublePage || mode == ViewMode::DoublePageManga))
						fix_double_pages();
					else if ((event.key.code == sf::Keyboard::Space ||
							event.key.code == sf::Keyboard::BackSpace) && !images.empty())
					{
						if (mode == ViewMode::DoublePage || mode == ViewMode::DoublePageManga)
						{
							int offset = 1;
							if (event.key.code == sf::Keyboard::BackSpace)
								offset = -1;

							current_double_page_index += offset;

							int corrected_index = std::clamp(current_double_page_index, 0, (int)double_pages[curr_tag_images->first].size() - 1);
							if (current_double_page_index != corrected_index)
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

							current_double_page_index = corrected_index;
							curr_image_index = double_pages[curr_tag_images->first][current_double_page_index];
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
				}
			}
		}

		void run_action(std::string_view action, const std::vector<std::string>& args)
		{
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

					//for (auto[tag, image_vec] : images)
					//{
					//	for (auto image : image_vec)
					//	{
					//		std::cerr << "TAG " << tag << " : " << image << '\n';
					//	}
					//}
					//std::cerr << std::endl;
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

							current_double_page_index = std::find(current_double_pages.begin(), current_double_pages.end(), curr_image_index) - current_double_pages.begin();
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

				run_action(action, args);
			}
		}

		void handle_keyboard(float dt)
		{
			if (!window.hasFocus())
				return;

			if (sf::Keyboard::isKeyPressed(sf::Keyboard::L))
			{
				window_view.move(scroll_speed * dt, 0);
				window.setView(window_view);
			}
			else if (sf::Keyboard::isKeyPressed(sf::Keyboard::H))
			{
				window_view.move(-scroll_speed * dt, 0);
				window.setView(window_view);
			}
			if (sf::Keyboard::isKeyPressed(sf::Keyboard::K))
			{
				window_view.move(0, -scroll_speed * dt);
				window.setView(window_view);
			}
			else if (sf::Keyboard::isKeyPressed(sf::Keyboard::J))
			{
				window_view.move(0, scroll_speed * dt);
				window.setView(window_view);
			}
			if (sf::Keyboard::isKeyPressed(sf::Keyboard::Equal))
			{
				reset_view = true;
			}
		}

	public:
		ImageViewerApp()
		{
			sf::ContextSettings settings;
			settings.antialiasingLevel = 8;
			window.create(sf::VideoMode(800, 600), "image viewer", sf::Style::Default, settings);
			//window.setKeyRepeatEnabled(false);
			window.setFramerateLimit(60);

			std::cin.sync_with_stdio(false);
		}

		void run()
		{
			sf::Clock clock;
			float dt = 0.f;
			while (window.isOpen())
			{
				handle_keyboard(dt);
				check_stdin();
				poll_events(dt);

				window.clear();
				render();
				window.display();

				dt = clock.restart().asSeconds();
			}
		}
};
