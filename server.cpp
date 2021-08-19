
#include <deque>
#include <iostream>
#include <memory>
#include <set>

#include <boost/asio.hpp>
#include <message.h>
#include <connection.h>

using boost::asio::ip::tcp;

class Server {
public:
	Server(
		boost::asio::io_context& io_context,
		const tcp::endpoint& endpoint) :
		io_context(io_context),
		acceptor(io_context, endpoint)
	{
		this->do_accept();
	}

	void do_accept()
	{
		this->acceptor.async_accept([this](boost::system::error_code ec, tcp::socket socket) {
			if (!ec) {
				std::cout << "starting chat session" << std::endl;
				auto connection = std::make_shared<Connection>(this->io_context, std::move(socket),
					[this](Message& message) {
						this->on_message(message);
					},
					[](boost::system::error_code ec)
					{
					});

				connection->start();
				this->connections.push_back(connection);
			}

			this->do_accept();
			});
	}

	void on_message(Message& message)
	{
		std::erase_if(this->connections,
			[](auto& weak_ptr) {
				return weak_ptr.expired();
			});

		for (auto connection : this->connections) {
			if (auto ptr = connection.lock()) {
				ptr->write(message);
			}
			else {
				throw std::logic_error("Unexpected error occurred...");
			}
		}
	}

	tcp::acceptor acceptor;
	boost::asio::io_context& io_context;
	std::vector<std::weak_ptr<Connection>> connections;
};


int main(int argc, char* argv[])
{
	try {
		boost::asio::io_context io_context;
		Server server(io_context, tcp::endpoint(tcp::v4(), 80));
		io_context.run();
	}
	catch (std::exception& e) {
		std::cerr << "Exception: " << e.what() << std::endl;
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}