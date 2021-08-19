//
// chat_server.cpp
// ~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2021 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <cstdlib>
#include <deque>
#include <iostream>
#include <list>
#include <memory>
#include <set>
#include <utility>
#include <boost/asio.hpp>
#include "message.hpp"

using boost::asio::ip::tcp;

//----------------------------------------------------------------------

typedef std::deque<chat_message> chat_message_queue;

//----------------------------------------------------------------------

class chat_participant {
public:
	virtual ~chat_participant() {}
	virtual void deliver(const chat_message& msg) = 0;
};

typedef std::shared_ptr<chat_participant> chat_participant_ptr;

//----------------------------------------------------------------------

class chat_room {
public:
	void join(chat_participant_ptr participant)
	{
		this->participants.insert(participant);
		for (auto msg : this->recent_msgs) {
			participant->deliver(msg);
		}
	}

	void leave(chat_participant_ptr participant)
	{
		this->participants.erase(participant);
	}

	void deliver(const chat_message& msg)
	{
		this->recent_msgs.push_back(msg);
		while (this->recent_msgs.size() > max_recent_msgs) {
			this->recent_msgs.pop_front();
		}
		for (auto participant : this->participants) {
			participant->deliver(msg);
		}
	}

private:
	std::set<chat_participant_ptr> participants;
	enum { max_recent_msgs = 100 };
	chat_message_queue recent_msgs;
};

//----------------------------------------------------------------------

class chat_session : public chat_participant, public std::enable_shared_from_this<chat_session> {
public:
	chat_session(tcp::socket socket, chat_room& room) :
		socket(std::move(socket)),
		room(room)
	{}

	void start()
	{
		this->room.join(shared_from_this());
		this->do_read_header();
	}

	void deliver(const chat_message& msg)
	{
		bool write_in_progress = !this->write_msgs.empty();
		this->write_msgs.push_back(msg);
		if (!write_in_progress) {
			this->do_write();
		}
	}

private:
	void do_read_header()
	{
		boost::asio::async_read(socket, boost::asio::buffer(this->read_msg.data(), chat_message::header_length),
			[self = shared_from_this()](boost::system::error_code ec, std::size_t /*length*/) {
				if (!ec && self->read_msg.decode_header()) {
					self->do_read_body();
				}
				else {
					self->room.leave(self->shared_from_this());
				}
			});
	}

	void do_read_body()
	{
		boost::asio::async_read(socket, boost::asio::buffer(this->read_msg.body(), this->read_msg.body_length()),
			[self = shared_from_this()](boost::system::error_code ec, std::size_t /*length*/) {
				if (!ec) {
					self->room.deliver(self->read_msg);
					self->do_read_header();
				}
				else {
					self->room.leave(self->shared_from_this());
				}
			});
	}

	void do_write()
	{
		boost::asio::async_write(socket, boost::asio::buffer(this->write_msgs.front().data(), this->write_msgs.front().length()),
			[self = shared_from_this()](boost::system::error_code ec, std::size_t /*length*/) {
				if (!ec) {
					self->write_msgs.pop_front();
					if (!self->write_msgs.empty()) {
						self->do_write();
					}
				}
				else {
					self->room.leave(self->shared_from_this());
				}
			});
	}

	tcp::socket socket;
	chat_room& room;
	chat_message read_msg;
	chat_message_queue write_msgs;
};

//----------------------------------------------------------------------

class chat_server
{
public:
	chat_server(boost::asio::io_context& io_context,
		const tcp::endpoint& endpoint)
		: acceptor(io_context, endpoint)
	{
		this->do_accept();
	}

private:
	void do_accept()
	{
		this->acceptor.async_accept([this](boost::system::error_code ec, tcp::socket socket) {
			if (!ec) {
				std::make_shared<chat_session>(std::move(socket), room)->start();
			}

			this->do_accept();
		});
	}

	tcp::acceptor acceptor;
	chat_room room;
};

//----------------------------------------------------------------------

int main(int argc, char* argv[])
{
	try {
		boost::asio::io_context io_context;
		std::list<chat_server> servers;
		tcp::endpoint endpoint(tcp::v4(), 80);
		servers.emplace_back(io_context, endpoint);

		io_context.run();
	}
	catch (std::exception& e) {
		std::cerr << "Exception: " << e.what() << "\n";
	}

	return 0;
}