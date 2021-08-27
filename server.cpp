
#include <Scheme/Scheme.h>

#include <message.h>
#include <connection.h>

#include <boost/asio.hpp>

#include <memory>
#include <iostream>


class Server : public std::enable_shared_from_this<Server> {
public:
	Server(
		boost::asio::io_context& io_context,
		boost::asio::ip::tcp::endpoint endpoint) :
		acceptor(io_context, endpoint)
	{
		auto print = [this](const scm::List& lst) {
			std::cout << lst.front() << std::endl;
			return true;
		};

		auto write = [this](const scm::List& lst) {
			for (auto& connection : this->connections) {
				connection->write(scm::print(lst.front()));
			}
			return true;
		};

		this->env = scm::global_env();
		this->env->outer = std::make_shared<scm::Env>(
			std::unordered_map<std::string, std::any>{
				{ scm::Symbol("write"), scm::fun_ptr(write) },
				{ scm::Symbol("print"), scm::fun_ptr(print) },
		});
	}

	void do_accept()
	{
		this->acceptor.async_accept(
			[self = this->shared_from_this()](boost::system::error_code ec, boost::asio::ip::tcp::socket socket) {
			std::cout << ec.message() << std::endl;
			if (!ec) {
				auto connection = std::make_shared<Connection>(std::move(socket),
					[self](const std::string& message) {
						scm::eval(scm::read(message), self->env);
					},
					[self](boost::system::error_code ec, std::shared_ptr<Connection> connection) {
						std::cout << ec.message() << std::endl;
						self->connections.remove(connection);
					});

				connection->do_read_header();
				self->connections.push_back(connection);
			}
			self->do_accept();
		});
	}

	boost::asio::ip::tcp::acceptor acceptor;
	std::shared_ptr<scm::Env> env;
	std::list<std::shared_ptr<Connection>> connections;
};


int main(int argc, char* argv[])
{
	try {
		boost::asio::io_context io_context;
		boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::tcp::v4(), 80);

		auto connection = std::make_shared<Server>(io_context, endpoint);
		connection->do_accept();

		io_context.run();
	}
	catch (std::exception& e) {
		std::cerr << "Exception: " << e.what() << std::endl;
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
