#pragma once

#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <SFML/Graphics.hpp>
#include <Magick++.h>

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
		std::map<std::string, sf::Vector2f> texture_size;
		std::map<std::string, sf::Texture> loaded_textures;

		std::map<int, std::vector<std::string>>::iterator curr_tag_images;
		int curr_image_index = 0;

		int last_render_tag = 0;
		int last_render_image_index = -1;
		ViewMode last_render_mode = ViewMode::Invalid;

		std::map<int, std::vector<std::vector<int>>> double_pages;
		bool double_paging_change = false;

		float wide_factor;

		std::string repage_save_file;

		bool reset_view = true;

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

		void load_repage_save(std::string_view save)
		{
			repage_save_file = save;
			std::ifstream stream(repage_save_file);

			std::string image;
			while (std::getline(stream, image))
				texture_wide[image] = 2;
		}

		int get_double_page_index(int image_index, int tag) const
		{
			auto it = double_pages.find(tag);
			if (it == double_pages.end())
				return 0;

			const auto& curr_double_pages = it->second;
			auto curr_double_page_it = std::find_if(curr_double_pages.begin(), curr_double_pages.end(), [image_index](const auto& double_indices)
				{
					return std::find(double_indices.begin(), double_indices.end(), image_index) != double_indices.end();
				});

			return curr_double_page_it - curr_double_pages.begin();
		}


		void update_double_pages(int tag)
		{
			const auto& tag_images_it = images.find(tag);
			if (tag_images_it == images.end())
				return;

			const auto& tag_images = tag_images_it->second;
			auto& tag_double_pages = double_pages[tag];

			tag_double_pages.clear();
			int is_prev_wide = true;
			for (int i = 0; i < (int)tag_images.size(); ++i)
			{
				int is_wide = texture_wide[tag_images[i]];
				if (is_wide || is_prev_wide || tag_double_pages.back().size() == 2)
					tag_double_pages.emplace_back();

				tag_double_pages.back().push_back(i);
				is_prev_wide = is_wide;
			}

			if (tag == curr_tag())
				double_paging_change = true;
		}

		void fix_double_pages(int image_index, int tag)
		{
			const auto& tag_images_it = images.find(tag);
			if (tag_images_it == images.end())
				return;

			const auto& tag_images = images[tag];
			auto& tag_double_pages = double_pages[tag];

			if (texture_wide[tag_images[image_index]] == 1)
				return;

			int begin_change_page;
			for (begin_change_page = get_double_page_index(image_index, tag); begin_change_page >= 0; --begin_change_page)
			{
				const auto& image = tag_images[tag_double_pages[begin_change_page][0]];
				if (texture_wide[image] == 1)
				{
					begin_change_page++;
					break;
				}
			}
			begin_change_page = std::max(0, begin_change_page);

			int first_changed_index = tag_double_pages[begin_change_page][0];
			auto first_changed_image = tag_images[first_changed_index];

			if (texture_wide[first_changed_image] == 2)
				texture_wide[first_changed_image] = 0;
			else
				texture_wide[first_changed_image] = 2;

			update_double_pages(tag);
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

		int curr_tag() const
		{
			if (images.empty())
				return -1;

			return curr_tag_images->first;
		}

		const std::string& curr_image() const
		{
			return curr_tag_images->second[curr_image_index];
		}

		void prepare_render()
		{
			bool image_changed = curr_image_index != last_render_image_index || curr_tag() != last_render_tag;
			bool mode_changed = mode != last_render_mode;

			if (mode == ViewMode::SinglePage)
			{
				if (image_changed || reset_view || mode_changed)
				{
					auto tex_size = texture_size[curr_image()];

					float scale_x = window_view.getSize().x / tex_size.x;
					float scale_y = window_view.getSize().y / tex_size.y;
					float scale = std::min(scale_x, scale_y);
					window_view.setCenter(tex_size * scale / 2.f);
					window.setView(window_view);

					loaded_textures.clear();
					sprites.clear();
					sprites.emplace_back(load_texture(curr_image(), scale));
				}
			}
			else if (mode == ViewMode::DoublePage || mode == ViewMode::DoublePageManga)
			{
				if (mode_changed)
					for (auto[tag, images_vec] : images)
						update_double_pages(tag);

				if (image_changed || mode_changed || reset_view || double_paging_change)
				{
					const auto& curr_double_pages = double_pages[curr_tag()];
					int curr_double_index = get_double_page_index(curr_image_index, curr_tag());

					int first_image_index = curr_double_pages[curr_double_index][0];

					float scale2 = 1.f;
					sf::Vector2f double_tex_size;
					double_tex_size = texture_size[images[curr_tag()][first_image_index]];
					if (curr_double_pages[curr_double_index].size() == 2)
					{
						int second_image_index = curr_double_pages[curr_double_index][1];

						scale2 = double_tex_size.y / texture_size[images[curr_tag()][second_image_index]].y;
						double_tex_size.x += texture_size[images[curr_tag()][second_image_index]].x * scale2;
					}

					float scale_x = window_view.getSize().x / double_tex_size.x;
					float scale_y = window_view.getSize().y / double_tex_size.y;
					float scale = std::min(scale_x, scale_y);
					window_view.setCenter(double_tex_size * scale / 2.f);
					window.setView(window_view);

					loaded_textures.clear();
					sprites.clear();

					sprites.emplace_back(load_texture(images[curr_tag()][first_image_index], scale));

					if (curr_double_pages[curr_double_index].size() == 2)
					{
						int second_image_index = curr_double_pages[curr_double_index][1];
						sprites.emplace_back(load_texture(images[curr_tag()][second_image_index], scale * scale2));

						if (mode == ViewMode::DoublePageManga)
							sprites[0].move(sprites[1].getGlobalBounds().width, 0);
						else
							sprites[1].move(sprites[0].getGlobalBounds().width, 0);
					}
				}
			}
			else if (mode == ViewMode::ContinuousVert)
			{
			}
		}

		void render()
		{
			if (curr_image_index != last_render_image_index || curr_tag() != last_render_tag)
			{
				if (images.empty())
					window.setTitle("no images loaded");
				else
				{
					std::string title = std::to_string(curr_tag()) + " - " + curr_image() + " [" + std::to_string(curr_image_index + 1) + "/" + std::to_string(images[curr_tag()].size()) + "]";
					window.setTitle(title);

					std::cout << "current_image=\"" << curr_image() << '"' << std::endl;
				}
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

			if (!images.empty())
			{
				prepare_render();
				for (const auto& sprite : sprites)
					window.draw(sprite);
			}

			last_render_image_index = curr_image_index;
			last_render_tag = curr_tag();
			last_render_mode = mode;
			reset_view = false;
			double_paging_change = false;
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
					reset_view = true;
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

							int double_index = get_double_page_index(curr_image_index, curr_tag());
							double_index += offset;

							int corrected_index = std::clamp(double_index, 0, (int)double_pages[curr_tag()].size() - 1);
							if (double_index != corrected_index)
							{
								if ((offset == 1 && std::next(curr_tag_images) == images.end()) || (offset == -1 && curr_tag_images == images.begin()))
									std::cout << "last_in_dir=" << (offset > 0) - (offset < 0) << std::endl;
								else
								{
									std::advance(curr_tag_images, offset);

									if (offset == 1)
										corrected_index = 0;
									else
										corrected_index = double_pages[curr_tag()].size() - 1;
								}
							}

							curr_image_index = double_pages[curr_tag()][corrected_index][0];
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

			if (action == "add_image")
			{
				if (sf::Texture tex; tex.loadFromFile(args[0]))
				{
					tex.setSmooth(true);
					bool is_wide = tex.getSize().x > (tex.getSize().y * wide_factor);
					if (!texture_wide.contains(args[0]) || is_wide)
						texture_wide[args[0]] = is_wide;

					texture_size[args[0]] = (sf::Vector2f)tex.getSize();

					int tag = 0;
					if (args.size() == 2)
						tag = std::stoi(args[1]);

					auto& tag_images_vec = images[tag];

					auto inserted_it = tag_images_vec.insert(std::upper_bound(tag_images_vec.begin(), tag_images_vec.end(), args[0]), args[0]);
					int new_index = inserted_it - tag_images_vec.begin();

					if (images.size() == 1 && tag_images_vec.size() == 1)
					{
						curr_image_index = 0;
						curr_tag_images = images.find(tag);
					}
					else if (tag == curr_tag() && new_index <= curr_image_index)
					{
						curr_image_index++;
						last_render_image_index++;
					}

					if (mode == ViewMode::DoublePage || mode == ViewMode::DoublePageManga)
						update_double_pages(tag);
				}
			}
			else if (action == "goto_image_byindex")
			{
				int new_index = std::stoi(args[0]);
				int tag = curr_tag();
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
						curr_image_index = new_index;
					else
						std::cerr << "index " << new_index << " not present on tag " << tag << std::endl;
				}
				else
					std::cerr << "tag " << args[0] << " not present" << std::endl;
			}
			else if (action == "goto_image_byname")
			{
				int tag = curr_tag();
				if (args.size() == 2)
					tag = std::stoi(args[1]);

				auto tag_images_iter = images.find(tag);

				if (tag_images_iter != images.end())
				{
					curr_tag_images = tag_images_iter;

					auto& tag_images_vec = tag_images_iter->second;
					auto image_iter = std::find(tag_images_vec.begin(), tag_images_vec.end(), args[0]);
					if (image_iter != tag_images_vec.end())
						curr_image_index = image_iter - tag_images_vec.begin();
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
						if (tag == curr_tag())
						{
							curr_image_index = 0;
							if (std::next(curr_tag_images) != images.end())
								std::advance(curr_tag_images, 1);
							else if (curr_tag_images != images.begin())
								std::advance(curr_tag_images, -1);
							else curr_image_index = -1;
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
					mode = ViewMode::SinglePage;
				else if (args[0] == "double")
					mode = ViewMode::DoublePage;
				else if (args[0] == "manga")
					mode = ViewMode::DoublePageManga;
				else if (args[0] == "vert")
					mode = ViewMode::ContinuousVert;
				else
					std::cerr << args[0] << "is not a valid mode" << std::endl;
			}
			else if (action == "repage")
			{
				if (mode != ViewMode::DoublePage && mode != ViewMode::DoublePageManga)
					std::cerr << "repaging only with double page layout" << std::endl;
				else
				{
					int change_index = curr_image_index;
					int change_tag = curr_tag();
					if (!args.empty())
					{
						change_index = std::stoi(args[0]);
						change_tag = std::stoi(args[1]);
					}

					fix_double_pages(change_index, change_tag);
				}
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
		ImageViewerApp(std::string_view config_path, std::string_view save_path, float wide_fact)
		{
			window.create(sf::VideoMode(800, 600), "image viewer", sf::Style::Default);
			window.setKeyRepeatEnabled(false);
			window.setFramerateLimit(60);

			if (!config_path.empty())
				load_config(config_path);
			if (!save_path.empty())
				load_repage_save(save_path);

			wide_factor = wide_fact;

			std::cin.sync_with_stdio(false);
		}

		~ImageViewerApp()
		{
			std::ofstream repage_file(repage_save_file);
			for (const auto&[image, wide] : texture_wide)
				if (wide == 2)
					repage_file << image << std::endl;
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
