#pragma once

#include <message.h>
#include <boost/asio.hpp>

#include <deque>
#include <memory>
#include <iostream>

using boost::asio::ip::tcp;

class Connection : public std::enable_shared_from_this<Connection> {
public:
	Connection(
		boost::asio::io_context& io_context,
		tcp::socket socket,
		std::function<void(Message&)> on_read,
		std::function<void(boost::system::error_code)> on_disconnect) :
		io_context(io_context),
		socket(std::move(socket)),
		on_read(on_read),
		on_disconnect(on_disconnect)
	{}

	virtual ~Connection()
	{
		this->socket.close();
	}

	void start()
	{
		this->do_read_header();
	}

	void write(const Message& message)
	{
		this->write_queue.push_back(message);
		if (this->write_queue.size() == 1) {
			this->do_write();
		}
	}

	void do_read_header()
	{
		this->message.content.resize(this->message.header_size());
		boost::asio::async_read(this->socket, this->message.header(),
			[self = this->shared_from_this()](boost::system::error_code ec, size_t /*length*/) {
			if (!ec) {
				self->do_read_body();
			}
			else {
				self->on_disconnect(ec);
			}
		});
	}

	void do_read_body()
	{
		this->message.content.resize(this->message.size());
		boost::asio::async_read(this->socket, this->message.body(),
			[self = this->shared_from_this()](boost::system::error_code ec, size_t /*length*/) {
			if (!ec) {
				self->on_read(self->message);
				self->do_read_header();
			}
			else {
				self->on_disconnect(ec);
			}
		});
	}

	void do_write()
	{
		boost::asio::async_write(this->socket, this->write_queue.front().data(),
			[self = this->shared_from_this()](boost::system::error_code ec, size_t /*length*/) {
			if (!ec) {
				self->write_queue.pop_front();
				if (!self->write_queue.empty()) {
					self->do_write();
				}
			}
			else {
				self->on_disconnect(ec);
			}
		});
	}

	boost::asio::io_context& io_context;
	tcp::socket socket;
	std::function<void(Message&)> on_read;
	std::function<void(boost::system::error_code)> on_disconnect;
	Message message;
	// TODO: write_queue must be thread-safe
	std::deque<Message> write_queue;
};
