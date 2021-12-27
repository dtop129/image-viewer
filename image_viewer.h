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
		sf::Vector2f window_size = {800, 600};
		sf::FloatRect view_rect = {0, 0, 1, 1};;

		ViewMode mode = ViewMode::SinglePage;

		std::vector<std::string> images;
		std::map<std::string, int> image_tags;
		std::map<std::string, sf::Vector2f> texture_sizes;
		std::map<std::string, sf::Texture> loaded_textures;

		int curr_image_index = 0;
		unsigned int drawn_images_count;

		float scroll_speed = 1000.f;

		bool image_changed = false;
		bool window_resized = true;

		sf::Texture& load_texture(const std::string& image_path)
		{
			auto tex_iter = loaded_textures.find(image_path);
			if (tex_iter != loaded_textures.end())
				return tex_iter->second;

			auto& tex = loaded_textures[image_path];
			tex.loadFromFile(image_path);
			return tex;
		}

		void render()
		{
			if (images.empty())
				return;

			std::vector<sf::Sprite> sprites;
			sf::FloatRect drawn_area(0, 0, 0, 0);
			bool scroll_image_change = false;

			if (mode == ViewMode::SinglePage || mode == ViewMode::DoublePage || mode == ViewMode::DoublePageManga)
			{
				const auto& image_path = images[curr_image_index];

				sprites.emplace_back(load_texture(image_path));
				drawn_area.width += sprites.back().getGlobalBounds().width;
				drawn_area.height = sprites.back().getGlobalBounds().height;

				if ((mode == ViewMode::DoublePage || mode == ViewMode::DoublePageManga) &&
						curr_image_index + 1 < (int)images.size() &&
						drawn_area.width < drawn_area.height)
				{
					const auto& image2_path = images[curr_image_index + 1];
					const auto& image2_size = texture_sizes[image2_path];

					if (image2_size.x < image2_size.y &&
							image_tags[image2_path] == image_tags[image_path])
					{
						sprites.emplace_back(load_texture(image2_path));

						if (mode == ViewMode::DoublePageManga)
						{
							float offset = image2_size.x * drawn_area.height / image2_size.y;
							sprites.back().setPosition(-offset, 0.f);
							drawn_area.left = -offset;
						}
						else
							sprites.back().setPosition(drawn_area.width, 0.f);

						float factor = drawn_area.height / image2_size.y;
						sprites.back().setScale(factor, factor);

						drawn_area.width += sprites.back().getGlobalBounds().width;
					}
				}

				if (image_changed || window_resized)
				{
					view_rect = drawn_area;
					view_rect.width = view_rect.height * (window_size.x / window_size.y);

					if (drawn_area.width > view_rect.width)
					{
						view_rect = drawn_area;
						view_rect.height = view_rect.width * (window_size.y / window_size.x);
						view_rect.top -= (view_rect.height - drawn_area.height) / 2;
					}
					else
						view_rect.left -= (view_rect.width - drawn_area.width) / 2;

					window.setView(sf::View(view_rect));
				}
			}
			else if (mode == ViewMode::ContinuousVert)
			{
				int lasty = 0;
				if (view_rect.top < 0 && curr_image_index > 0)
				{
					curr_image_index--;
					view_rect.top += texture_sizes[images[curr_image_index]].y;
					window.setView(sf::View(view_rect));

					scroll_image_change = true;
				}
				else if (view_rect.top > texture_sizes[images[curr_image_index]].y &&
						curr_image_index + 1 < images.size())
				{
					loaded_textures.erase(images[curr_image_index]);
					view_rect.top -= texture_sizes[images[curr_image_index]].y;
					window.setView(sf::View(view_rect));
					curr_image_index++;

					scroll_image_change = true;
				}

				for (auto image_iter = images.begin() + curr_image_index; image_iter != images.end(); ++image_iter)
				{
					const auto& image = *image_iter;
					const auto& tex_size = texture_sizes[image];

					sf::FloatRect image_rect(0, lasty, tex_size.x, tex_size.y);

					if (!view_rect.intersects(image_rect))
					{
						loaded_textures.erase(image);
						break;
					}

					sprites.emplace_back(load_texture(image));
					sprites.back().setPosition(image_rect.left, image_rect.top);

					drawn_area.height += image_rect.height;
					drawn_area.width = std::max(drawn_area.width, image_rect.width);

					lasty += image_rect.height;
				}

				if (image_changed || window_resized)
				{
					view_rect.width = window_size.x;
					view_rect.height = window_size.y;

					view_rect.left = -(view_rect.width - drawn_area.width) / 2;
					if (image_changed)
						view_rect.top = drawn_area.top;
					window.setView(sf::View(view_rect));
				}

			}

			for (const auto& sprite : sprites)
				window.draw(sprite);

			drawn_images_count = sprites.size();

			if (image_changed || scroll_image_change)
			{
				std::cout << "current_image=\"" << images[curr_image_index] << "\"" << std::endl;
				window.setTitle(images[curr_image_index]);
			}
			image_changed = window_resized = false;
		}

		void change_mode(ViewMode new_mode)
		{
			if (mode != new_mode)
				window_resized = true;
			mode = new_mode;
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
					window_size = sf::Vector2f(event.size.width, event.size.height);
					window_resized = true;
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
					else if (event.key.code == sf::Keyboard::V)
						change_mode(ViewMode::ContinuousVert);
					else if (event.key.code == sf::Keyboard::Space ||
							event.key.code == sf::Keyboard::BackSpace)
					{
						int offset = 1;
						if (event.key.code == sf::Keyboard::BackSpace)
							offset = -1;
						if ((mode == ViewMode::DoublePage || mode == ViewMode::DoublePageManga) && !event.key.control)
						{
							offset *= 2;
							if (drawn_images_count == 1 && offset == 2)
								offset = 1;
							else if (offset == -2 && curr_image_index > 1)
							{
								int prev_image_tag = image_tags[images[curr_image_index - 1]];
								int prev2_image_tag = image_tags[images[curr_image_index - 2]];
								sf::Vector2f prev_image_size = texture_sizes[images[curr_image_index - 1]];

								if (prev_image_size.x >= prev_image_size.y || prev_image_tag != prev2_image_tag)
									offset = -1;
							}
						}

						int prev_index = curr_image_index;
						if (images.size() > 1 && curr_image_index + offset < (int)images.size())
						{
							curr_image_index = std::max(0, curr_image_index + offset);

							if (curr_image_index != prev_index)
							{
								image_changed = true;
								loaded_textures.clear();
							}
						}
						if (curr_image_index == prev_index)
							std::cout << "last_in_dir=" << (offset > 0) - (offset < 0) << std::endl;
					}
				}
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

				if (action == "add_image")
				{
					if (sf::Texture tex; tex.loadFromFile(args[0]))
					{
						auto new_iter = images.insert(std::upper_bound(images.begin(), images.end(), args[0]), args[0]);
						texture_sizes[args[0]] = static_cast<sf::Vector2f>(tex.getSize());

						if (args.size() == 2)
							image_tags[args[0]] = std::stoi(args[1]);

						if (images.size() == 1)
							image_changed = true;
						else if (new_iter - images.begin() <= curr_image_index)
							curr_image_index++;
					}
				}
				else if (action == "goto_image_byname" || action == "remove_image_byname")
				{
					auto image_iter = std::find(images.begin(), images.end(), args[0]);
					if (image_iter != images.end())
					{
						int prev_index = curr_image_index;

						if (action == "goto_image_byname")
							curr_image_index = image_iter - images.begin();
						else
						{
							loaded_textures.erase(args[0]);
							images.erase(image_iter);
							if (image_iter - images.begin() < curr_image_index)
								curr_image_index--;
						}

						if (prev_index != curr_image_index)
							image_changed = true;
					}
					else
						std::cerr << args[0] << " not found\n";
				}
				else if (action == "goto_image_byindex" || action == "remove_image_byindex")
				{
					int index = std::stoi(args[0]);
					if (index < 0)
						index = images.size() + index;

					if (index >= 0 && index < (int)images.size())
					{
						int prev_index = curr_image_index;

						if (action == "goto_image_byindex")
							curr_image_index = index;
						else
						{
							loaded_textures.erase(images[index]);
							images.erase(images.begin() + index);
							if (index < curr_image_index)
								curr_image_index--;
						}

						if (prev_index != curr_image_index)
							image_changed = true;
					}
				}
				else if (action == "change_mode")
				{
					if (args[0] == "single")
						change_mode(ViewMode::SinglePage);
					else if (args[0] == "double")
						change_mode(ViewMode::DoublePage);
					else if (args[0] == "manga")
						change_mode(ViewMode::DoublePageManga);
					else if (args[0] == "vert")
						change_mode(ViewMode::ContinuousVert);
				}
			}
		}

		void handle_keyboard(float dt)
		{
			if (!window.hasFocus())
				return;

			if (sf::Keyboard::isKeyPressed(sf::Keyboard::L))
			{
				view_rect.left += scroll_speed * dt;
				window.setView(sf::View(view_rect));
			}
			if (sf::Keyboard::isKeyPressed(sf::Keyboard::H))
			{
				view_rect.left -= scroll_speed * dt;
				window.setView(sf::View(view_rect));
			}
			if (sf::Keyboard::isKeyPressed(sf::Keyboard::K))
			{
				view_rect.top -= scroll_speed * dt;
				window.setView(sf::View(view_rect));
			}
			if (sf::Keyboard::isKeyPressed(sf::Keyboard::J))
			{
				view_rect.top += scroll_speed * dt;
				window.setView(sf::View(view_rect));
			}
			if (sf::Keyboard::isKeyPressed(sf::Keyboard::W))
			{
				view_rect.width *= (1.f + dt);
				view_rect.height *= (1.f + dt);
				window.setView(sf::View(view_rect));
			}
		}

	public:
		ImageViewerApp() : window(sf::VideoMode(800, 600), "Image Viewer")
		{
			window.setKeyRepeatEnabled(false);
			window.setVerticalSyncEnabled(true);
			std::cin.sync_with_stdio(false);
		}

		void run()
		{
			sf::Clock clock;
			float dt = 0.f;
			while (window.isOpen())
			{
				poll_events();
				handle_keyboard(dt);
				check_stdin();

				window.clear();
				render();
				window.display();

				dt = clock.restart().asSeconds();
			}
		}
};
