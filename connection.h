#pragma once

#include <message.h>
#include <boost/asio.hpp>

#include <deque>
#include <memory>
#include <iostream>

class Connection : public std::enable_shared_from_this<Connection> {
public:
	Connection(
		boost::asio::ip::tcp::socket socket,
		std::function<void(const std::string&)> on_read,
		std::function<void(boost::system::error_code, std::shared_ptr<Connection>)> on_disconnect) :
		socket(std::move(socket)),
		on_read(on_read),
		on_disconnect(on_disconnect)
	{}

	virtual ~Connection() = default;

	void do_read_header()
	{
		this->message.content.resize(this->message.header_size());
		boost::asio::async_read(this->socket, this->message.header(),
			[self = this->shared_from_this()](boost::system::error_code ec, size_t /*length*/) {
			if (!ec) {
				self->do_read_body();
			}
			else {
				self->on_disconnect(ec, self);
			}
		});
	}

	void do_read_body()
	{
		this->message.content.resize(this->message.size());
		boost::asio::async_read(this->socket, this->message.body(),
			[self = this->shared_from_this()](boost::system::error_code ec, size_t /*length*/) {
			if (!ec) {
				self->on_read(self->message.to_string());
				self->do_read_header();
			}
			else {
				self->on_disconnect(ec, self);
			}
		});
	}

	void write(const Message& message)
	{
		this->write_queue.push_back(message);
		if (this->write_queue.size() == 1) {
			this->do_write();
		}
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
				self->on_disconnect(ec, self);
			}
		});
	}

	boost::asio::ip::tcp::socket socket;
	boost::asio::ip::tcp::endpoint endpoint;

	std::function<void(const std::string&)> on_read;
	std::function<void(boost::system::error_code, std::shared_ptr<Connection>)> on_disconnect;
	Message message;
	// TODO: write_queue must be thread-safe
	std::deque<Message> write_queue;
};
