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
		std::map<std::string, sf::Vector2f> texture_sizes;
		std::map<std::string, sf::Texture> loaded_textures;

		std::map<int, std::vector<std::string>>::iterator curr_tag_images;
		int curr_image_index = 0;

		std::map<int, std::vector<int>> double_pages;
		int current_double_page_index = 0;

		float scroll_speed = 1000.f;

		bool image_changed = false;
		bool mode_changed = true;
		bool reset_view = true;

		void create_double_pages(int tag) //MAYBE CREATE FUNCTION TO UPDATE DOUBLE PAGES; RESPECTING THE FIXES
		{
			auto it = images.find(tag);
			if (it == images.end())
				return;

			double_pages[tag].clear();

			const auto& images_vec = it->second;
			auto& current_double_pages = double_pages[tag];
			int distance_from_last = 0;

			bool prev_wide_page = true;
			for (auto it = images_vec.begin(); it != images_vec.end(); ++it)
			{
				distance_from_last++;

				const auto& image = *it;
				const auto& size = texture_sizes[image];
				bool is_wide = size.x > size.y;

				if (is_wide)
					distance_from_last++;

				if (distance_from_last > 1 || prev_wide_page)
				{
					current_double_pages.push_back(it - images_vec.begin());
					distance_from_last = 0;
				}

				prev_wide_page = is_wide;
			}

			//std::cout << "TAG " << tag << " with " << images_vec.size() << " images" << '\n';
			//for (auto doublebegin : current_double_pages)
			//{
			//	std::cout << doublebegin << ' ';
			//}
			//std::cout << '\n';

			if (tag == curr_tag_images->first)
				image_changed = true;
		}

		void fix_double_pages(int dir)
		{
			int current_tag = curr_tag_images->first;
			auto& current_double_pages = double_pages[current_tag];

			const auto& current_size = texture_sizes[current_image()];
			if (current_size.x > current_size.y)
				return;

			auto begin_wide_page = current_double_pages.begin() - 1;
			for (auto it = std::find(current_double_pages.rbegin(), current_double_pages.rend(), curr_image_index) + 1; it != current_double_pages.rend(); ++it)
			{
				const auto& image = images[current_tag][*it];
				const auto& size = texture_sizes[image];

				if (size.x > size.y)
				{
					begin_wide_page = (it + 1).base();
					break;
				}
			}

			auto end_wide_page = current_double_pages.end();
			for (auto it = begin_wide_page + 1; it != current_double_pages.end(); ++it)
			{
				const auto& image = images[current_tag][*it];
				const auto& size = texture_sizes[image];

				if (size.x > size.y)
				{
					end_wide_page = it;
					break;
				}
			}


			if (end_wide_page - begin_wide_page <= 2)
				return;

			int offset;
			if (*(begin_wide_page + 1) + 1 == *(begin_wide_page + 2))
				offset = -1;
			else
			{
				offset = 1;
				current_double_pages.push_back(*(begin_wide_page + 1));
			}

			for (auto it = begin_wide_page + 1; it != end_wide_page; ++it)
				*it += offset;

			if (offset == -1)
				current_double_pages.push_back(*(end_wide_page - 1) + 2);

			std::erase_if(current_double_pages,
					[&](int x){
						return x < 0 || x >= (int)images[current_tag].size();
					});

			std::sort(current_double_pages.begin(), current_double_pages.end());
			current_double_pages.erase(std::unique(current_double_pages.begin(), current_double_pages.end()), current_double_pages.end());

			image_changed = true;
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

		const std::string& next_image() const
		{
			if (curr_image_index + 1 != (int)curr_tag_images->second.size())
				return curr_tag_images->second[curr_image_index + 1];
			else if (curr_tag_images != std::prev(images.end()))
				return std::next(curr_tag_images)->second[0];

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
						create_double_pages(tag);

				if (image_changed)
				{
					const auto& current_double_pages = double_pages[current_tag];
					if (std::find(current_double_pages.begin(), current_double_pages.end(), curr_image_index) == current_double_pages.end())
						curr_image_index--;

					current_double_page_index = std::find(current_double_pages.begin(), current_double_pages.end(), curr_image_index) - current_double_pages.begin();

				}

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
				if (image_changed)
					window.setTitle("no images loaded");

				image_changed = false;
				return;
			}

			prepare_render();
			for (const auto& sprite : sprites)
				window.draw(sprite);

			if (image_changed)
			{
				std::string title = std::to_string(curr_tag_images->first) + " - " + std::to_string(curr_image_index);
				window.setTitle(title);
			}

			image_changed = false;
			mode_changed = false;
			reset_view = false;
		}

		void change_mode(ViewMode new_mode)
		{
			ViewMode prev_mode = mode;
			mode = new_mode;

			if (mode != prev_mode)
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
					else if (event.key.code == sf::Keyboard::M)
						change_mode(ViewMode::DoublePageManga);
					//else if (event.key.code == sf::Keyboard::V)
					//	change_mode(ViewMode::ContinuousVert);
					else if (event.key.code == sf::Keyboard::C)
						fix_double_pages(1);
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
									std::cout << "last_in_dir=" << (offset > 0) - (offset < 0) << '\n';
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
									std::cout << "last_in_dir=" << (offset > 0) - (offset < 0) << '\n';
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

					int tag = 0;
					if (args.size() == 2)
						tag = std::stoi(args[1]);

					auto& tag_images_vec = images[tag];

					auto new_iter = tag_images_vec.insert(std::upper_bound(tag_images_vec.begin(), tag_images_vec.end(), args[0]), args[0]);

					if (images.size() == 1 && tag_images_vec.size() == 1)
					{
						curr_tag_images = images.find(tag);
						image_changed = true;
					}
					else if (tag == curr_tag_images->first && new_iter - tag_images_vec.begin() <= curr_image_index)
						curr_image_index++;

					if (mode == ViewMode::DoublePage || mode == ViewMode::DoublePageManga)
						create_double_pages(tag);
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
					auto& tag_images_vec = tag_images_iter->second;

					if (new_index < 0)
						new_index = (int)tag_images_vec.size() + new_index;

					if (new_index >= 0 && new_index < (int)tag_images_vec.size())
					{
						curr_image_index = new_index;
						image_changed = true;
					}
					else
						std::cerr << "index " << new_index << " not present on tag " << tag << "\n";
				}
				else
					std::cerr << "tag " << args[0] << " not present\n";
			}
			else if (action == "goto_image_byname")
			{
				int tag = curr_tag_images->first;
				if (args.size() == 2)
					tag = std::stoi(args[1]);

				auto tag_images_iter = images.find(tag);

				if (tag_images_iter != images.end())
				{
					auto& tag_images_vec = tag_images_iter->second;
					auto image_iter = std::find(tag_images_vec.begin(), tag_images_vec.end(), args[0]);
					if (image_iter != tag_images_vec.end())
					{
						curr_image_index = image_iter - tag_images_vec.begin();
						image_changed = true;
					}
					else
						std::cerr << args[0] << " not present on tag " << tag << "\n";
				}
				else
					std::cerr << "tag " << tag << " not present\n";
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
						image_changed = true;
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

							image_changed = true;
						}

						images.erase(tag);
					}
				}
				else
					std::cerr << "tag " << tag << " not present\n";
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
					std::cerr << args[0] << "is not a valid mode\n";
			}
		}

		void check_stdin()
		{
			while (std::cin.rdbuf()->in_avail())
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

				std::flush(std::cout);
				std::flush(std::cerr);
			}
		}
};
