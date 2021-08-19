
#include <cstdlib>
#include <deque>
#include <iostream>
#include <thread>
#include <boost/asio.hpp>
#include "message.hpp"

using boost::asio::ip::tcp;

typedef std::deque<chat_message> chat_message_queue;

class chat_client {
public:
	chat_client(
		boost::asio::io_context& io_context,
		const tcp::resolver::results_type& endpoints) :
		io_context(io_context),
		socket(io_context)
	{
		this->do_connect(endpoints);
	}

	void write(const chat_message& msg)
	{
		boost::asio::post(this->io_context, [this, msg]() {
			bool write_in_progress = !this->write_msgs.empty();
			this->write_msgs.push_back(msg);
			if (!write_in_progress) {
				this->do_write();
			}});
	}

	void close()
	{
		boost::asio::post(this->io_context, [this]() { this->socket.close(); });
	}

private:
	void do_connect(const tcp::resolver::results_type& endpoints)
	{
		boost::asio::async_connect(this->socket, endpoints,
			[this](boost::system::error_code ec, tcp::endpoint) {
				if (!ec) {
					this->do_read_header();
				}
			});
	}

	void do_read_header()
	{
		boost::asio::async_read(this->socket, boost::asio::buffer(this->read_msg.data(), chat_message::header_length),
			[this](boost::system::error_code ec, std::size_t /*length*/) {
				if (!ec && this->read_msg.decode_header()) {
					this->do_read_body();
				}
				else {
					this->socket.close();
				}
			});
	}

	void do_read_body()
	{
		boost::asio::async_read(this->socket, boost::asio::buffer(this->read_msg.body(), this->read_msg.body_length()),
			[this](boost::system::error_code ec, std::size_t /*length*/) {
				if (!ec) {
					std::cout.write(this->read_msg.body(), this->read_msg.body_length());
					std::cout << "\n";
					this->do_read_header();
				}
				else {
					this->socket.close();
				}
			});
	}

	void do_write()
	{
		boost::asio::async_write(socket, boost::asio::buffer(this->write_msgs.front().data(), this->write_msgs.front().length()),
			[this](boost::system::error_code ec, std::size_t /*length*/) {
				if (!ec) {
					this->write_msgs.pop_front();
					if (!this->write_msgs.empty()) {
						do_write();
					}
				}
				else {
					this->socket.close();
				}
			});
	}

private:
	boost::asio::io_context& io_context;
	tcp::socket socket;
	chat_message read_msg;
	chat_message_queue write_msgs;
};

int main(int argc, char* argv[])
{
	try {
		boost::asio::io_context io_context;

		tcp::resolver resolver(io_context);
		auto endpoints = resolver.resolve("127.0.0.1", "80");
		chat_client c(io_context, endpoints);

		std::thread t([&io_context]() { io_context.run(); });

		char line[chat_message::max_body_length + 1];
		while (std::cin.getline(line, chat_message::max_body_length + 1)) {
			chat_message msg;
			msg.body_length(std::strlen(line));
			std::memcpy(msg.body(), line, msg.body_length());
			msg.encode_header();
			c.write(msg);
		}

		c.close();
		t.join();
	}
	catch (std::exception& e) {
		std::cerr << "Exception: " << e.what() << "\n";
	}

	return 0;
}