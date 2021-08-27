
#include <Scheme/Scheme.h>

#include <message.h>
#include <connection.h>

#include <boost/asio.hpp>

#include <memory>
#include <iostream>
#include <exception>


class Client : public std::enable_shared_from_this<Client> {
public:
	Client(
		boost::asio::io_context& io_context) :
		socket(io_context)
	{
		auto print = [this](const scm::List& lst) {
			std::cout << lst.front() << std::endl;
			return true;
		};

		auto write = [this](const scm::List& lst) {
			if (auto connection = this->connection.lock()) {
				connection->write(scm::print(lst.front()));
			}
			return true;
		};

		auto connect = [this](const scm::List& lst)
		{
			if (lst.size() != 2 || lst[0].type() != typeid(scm::String) || lst[1].type() != typeid(scm::Number)) {
				throw std::invalid_argument("connect: invalid argument");
			}

			auto addr = std::any_cast<scm::String>(lst[0]);
			auto port = std::any_cast<scm::Number>(lst[1]);

			auto address = boost::asio::ip::address::from_string(addr);
			auto endpoint = boost::asio::ip::tcp::endpoint(address, port);

			this->do_connect(endpoint);
			return true;
		};

		this->env = scm::global_env();
		this->env->outer = std::make_shared<scm::Env>(
			std::unordered_map<std::string, std::any>{
				{ scm::Symbol("write"), scm::fun_ptr(write) },
				{ scm::Symbol("print"), scm::fun_ptr(print) },
				{ scm::Symbol("connect"), scm::fun_ptr(connect) },
		});
	}

	void do_connect(boost::asio::ip::tcp::endpoint endpoint)
	{
		this->socket.async_connect(endpoint,
			[self = this->shared_from_this()](boost::system::error_code ec) {
			std::cout << ec.message() << std::endl;
			if (!ec) {
				auto connection = std::make_shared<Connection>(std::move(self->socket),
					[self](const std::string& message) {
						scm::eval(scm::read(message), self->env);
					},
					[self](boost::system::error_code ec, std::shared_ptr<Connection> connection) {
						std::cout << ec.message() << std::endl;
					});
				connection->do_read_header();
				self->connection = connection;
			}
		});
	}

	void eval(const std::string& expression)
	{
		scm::eval(scm::read(expression), this->env);
	}

	boost::asio::ip::tcp::socket socket;

	std::shared_ptr<scm::Env> env;
	std::weak_ptr<Connection> connection;
};


int main(int argc, char* argv[])
{
	try {
		boost::asio::io_context io_context;
		auto work_guard = boost::asio::make_work_guard(io_context);
		auto client = std::make_shared<Client>(io_context);

		auto port = 80;
		auto address = boost::asio::ip::address::from_string("127.0.0.1");
		auto endpoint = boost::asio::ip::tcp::endpoint(address, port);
		client->do_connect(endpoint);

		std::thread io_thread(
			[client] {
				while (true) {
					try {
						while (true) {
							std::string expression;
							std::getline(std::cin, expression);
							client->eval(expression);
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
