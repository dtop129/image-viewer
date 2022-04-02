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
		//sf::FloatRect view_rect = {0, 0, 1, 1};

		ViewMode mode = ViewMode::SinglePage;

		std::vector<std::string> images;
		std::map<std::string, int> image_tags;
		std::map<std::string, sf::Vector2f> texture_sizes;
		std::map<std::string, sf::Texture> loaded_textures;

		int curr_image_index = 0;

		float scroll_speed = 1000.f;

		bool image_changed = false;
		bool mode_changed = true;
		bool reset_view = true;

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
			{
				if (image_changed)
					window.setTitle("no images loaded");

				image_changed = false;
				return;
			}

			static std::vector<sf::Sprite> sprites;

			if (mode == ViewMode::SinglePage)
			{
				if (image_changed || mode_changed)
				{
					loaded_textures.clear();
					sprites.clear();
					sprites.emplace_back(load_texture(images[curr_image_index]));
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
			else if (mode == ViewMode::DoublePage)
			{
				if (image_changed || mode_changed)
				{
					loaded_textures.clear();
					sprites.clear();
					sprites.emplace_back(load_texture(images[curr_image_index]));
					if (curr_image_index + 1 != (int)images.size())
					{
						sprites.emplace_back(load_texture(images[curr_image_index + 1]));

						float scale_factor = sprites[0].getLocalBounds().height / sprites[1].getLocalBounds().height;
						sprites[1].setScale(scale_factor, scale_factor);
						sprites[1].move(sprites[0].getGlobalBounds().width, 0);
					}
				}

				if (image_changed || reset_view || mode_changed)
				{
					auto drawn_rect1 = sprites[0].getGlobalBounds();
					auto drawn_rect2 = sprites[1].getGlobalBounds();

					sf::FloatRect drawn_rect(drawn_rect1.left, drawn_rect1.top, drawn_rect1.width + drawn_rect2.width, drawn_rect1.height);
					auto sprite_center = sf::Vector2f(drawn_rect.left + drawn_rect.width / 2.f, drawn_rect.top + drawn_rect.height / 2.f);

					float zoom_x = drawn_rect.width / window_view.getSize().x;
					float zoom_y = drawn_rect.height / window_view.getSize().y;

					window_view.setCenter(sprite_center);
					window_view.zoom(std::max(zoom_x, zoom_y));
					window.setView(window_view);
				}
			}

			for (const auto& sprite : sprites)
				window.draw(sprite);


			image_changed = false;
			mode_changed = false;
			reset_view = false;
		}

		void change_mode(ViewMode new_mode)
		{
			mode = new_mode;
			mode_changed = true;
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

					window.setView(window_view);
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
					//else if (event.key.code == sf::Keyboard::M)
					//	change_mode(ViewMode::DoublePageManga);
					//else if (event.key.code == sf::Keyboard::V)
					//	change_mode(ViewMode::ContinuousVert);
					else if (event.key.code == sf::Keyboard::Space ||
							event.key.code == sf::Keyboard::BackSpace)
					{
						int offset = 1;
						if (event.key.code == sf::Keyboard::BackSpace)
							offset = -1;

						if (mode == ViewMode::DoublePage)
							offset *= 2;

						curr_image_index += offset;

						int corrected_index = std::clamp(curr_image_index, 0, (int)images.size() - 1);
						if (curr_image_index != corrected_index)
							std::cout << "last_in_dir=" << (offset > 0) - (offset < 0) << '\n';

						curr_image_index = corrected_index;
						image_changed = true;
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
					texture_sizes[args[0]] = static_cast<sf::Vector2f>(tex.getSize());

					auto new_iter = images.insert(std::upper_bound(images.begin(), images.end(), args[0]), args[0]);

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
					if (action == "goto_image_byname")
						curr_image_index = image_iter - images.begin();
					else
					{
						if (image_iter - images.begin() < curr_image_index)
							curr_image_index--;
						else if (image_iter - images.begin() == curr_image_index)
							image_changed = true;

						images.erase(image_iter);
					}
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
					if (action == "goto_image_byindex")
						curr_image_index = index;
					else
					{
						if (index < curr_image_index)
							curr_image_index--;
						else if (index == curr_image_index)
							image_changed = true;

						images.erase(images.begin() + index);
					}
				}
				else
					std::cerr << "no image at index " << args[0] << '\n';
			}
			else if (action == "change_mode")
			{
				if (args[0] == "single")
					change_mode(ViewMode::SinglePage);
				else if (args[0] == "double")
					change_mode(ViewMode::DoublePage);
				//else if (args[0] == "manga")
				//	change_mode(ViewMode::DoublePageManga);
				//else if (args[0] == "vert")
				//	change_mode(ViewMode::ContinuousVert);
				else
					std::cerr << args[0] << "is not a valid mode\n";
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
				check_stdin();
				poll_events(dt);
				handle_keyboard(dt);

				window.clear();
				render();
				window.display();

				dt = clock.restart().asSeconds();

				std::flush(std::cout);
				std::flush(std::cerr);
			}
		}
};
