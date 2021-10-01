
#include <Network.h>
#include <boost/program_options.hpp>
namespace bpo = boost::program_options;

int main(int argc, char* argv[])
{
	try {
		boost::asio::io_context io_context;
		auto work_guard = boost::asio::make_work_guard(io_context);

		scm::fun_ptr test = [](const scm::List& lst) {
			std::cout << "test" << std::endl;
			return true;
		};

		auto env = std::make_shared<scm::Env>(
			std::unordered_map<std::string, std::any>{
				{ scm::Symbol("test"), test },
		});

		auto network = std::make_shared<Network>(io_context, env);

		std::string script;
		std::string expression;
		bpo::options_description generic("Generic options");
		generic.add_options()
			("help", "produce help message")
			("script,s", bpo::value<std::string>(&script), "expression read from a text file")
			("expression,e", bpo::value<std::string>(&expression), "expression passed in as string");

		bpo::variables_map vm;
		bpo::store(bpo::parse_command_line(argc, argv, generic), vm);
		bpo::notify(vm);

		bpo::positional_options_description p;
		p.add("script", -1);

		bpo::store(bpo::command_line_parser(argc, argv).options(generic).positional(p).run(), vm);
		bpo::notify(vm);

		if (vm.count("help")) {
			std::cout << generic << std::endl;
			return EXIT_SUCCESS;
		}
		if (vm.count("script")) {
			std::ifstream stream(script, std::ios::in);
			if (!stream.is_open()) {
				std::cerr << "unable to open " + script << std::endl;
				return EXIT_FAILURE;
			}
			const std::string expression{
				std::istreambuf_iterator<char>(stream),
				std::istreambuf_iterator<char>()
			};
			network->eval(expression);
		}
		if (vm.count("expression")) {
			network->eval(expression);
		}

		std::thread io_thread(
			[network] {
				while (true) {
					try {
						while (true) {
							std::string expression;
							std::getline(std::cin, expression);
							network->eval(expression);
						}
					}
					catch (std::exception& e) {
						std::cerr << e.what() << std::endl;
					}
				}
			});

		io_context.run();
		io_thread.join();
	}
	catch (std::exception& e) {
		std::cerr << "Exception: " << e.what() << std::endl;
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
