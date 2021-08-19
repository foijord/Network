
#include <worker.h>
#include <message.h>
#include <connection.h>

#include <iostream>
#include <thread>
#include <boost/asio.hpp>


int main(int argc, char* argv[])
{
	try {
		std::weak_ptr<Connection> connection;

		do {
			boost::asio::io_context io_context;
			tcp::endpoint endpoint(boost::asio::ip::address::from_string("127.0.0.1"), 80);
			tcp::socket socket(io_context);

			do {
				try {
					socket.connect(endpoint);
					std::cout << "connect: Connected to endpoint " << endpoint << std::endl;

					auto connection_shared_ptr = std::make_shared<Connection>(io_context, std::move(socket),
						[](Message& message) {
							std::cout << message << std::endl;
						},
						[](boost::system::error_code ec)
						{
							std::cout << "connection to server closed. Press enter to reconnect..." << std::endl;
						});

					connection_shared_ptr->start();
					connection = connection_shared_ptr;
				}
				catch (std::exception& e) {
					std::cerr << e.what() << std::endl;
				}
			} while (!connection.lock());

			std::thread io_thread(
				[connection]() {
					while (true) {
						std::string message;
						std::getline(std::cin, message);
						if (auto shared_ptr = connection.lock()) {
							shared_ptr->write(message);
						}
						else {
							break;
						}
					}
				});

			io_context.run();
			io_thread.join();

		} while (!connection.lock());
	}
	catch (std::exception& e) {
		std::cerr << "Exception: " << e.what() << std::endl;
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}