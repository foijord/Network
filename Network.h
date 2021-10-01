
#include <Scheme/Scheme.h>

#include <boost/asio.hpp>

#include <deque>
#include <memory>
#include <iostream>
#include <exception>


class Message {
public:
	Message() = default;

	Message(const std::string& message)
	{
		*this << message;
	}

	uint32_t header_size()
	{
		return sizeof(uint32_t);
	}

	uint32_t size()
	{
		uint32_t size;
		std::memcpy(&size, this->content.data(), sizeof(size));
		return size;
	}

	uint32_t body_size()
	{
		return this->size() - this->header_size();
	}

	boost::asio::mutable_buffer header()
	{
		return boost::asio::buffer(this->content.data(), this->header_size());
	}

	boost::asio::mutable_buffer body()
	{
		return boost::asio::buffer(this->content.data() + this->header_size(), this->body_size());
	}

	boost::asio::mutable_buffer data()
	{
		return boost::asio::buffer(this->content.data(), this->size());
	}

	std::string to_string()
	{
		std::string s;
		*this >> s;
		return s;
	}

	void operator << (const std::string& data)
	{
		uint32_t size = static_cast<uint16_t>(this->header_size() + data.size());
		this->content.resize(size);
		std::memcpy(this->content.data(), &size, sizeof(size));
		std::memcpy(this->content.data() + sizeof(size), data.data(), data.size());
	}

	void operator >> (std::string& data)
	{
		data.resize(this->content.size() - this->header_size());
		std::memcpy(data.data(), this->content.data() + this->header_size(), data.size());
	}

	friend std::ostream& operator << (std::ostream& os, Message& self)
	{
		for (size_t i = self.header_size(); i < self.content.size(); i++) {
			os << self.content[i];
		}
		return os;
	}

	std::vector<uint8_t> content;
};


class Connection : public std::enable_shared_from_this<Connection> {
public:
	Connection(
		boost::asio::ip::tcp::socket socket,
		std::function<void(Message message)> on_message) :
		socket(std::move(socket)),
		on_message(on_message)
	{}

	void async_read_header()
	{
		this->message.content.resize(this->message.header_size());
		boost::asio::async_read(this->socket, this->message.header(),
			[self = this->shared_from_this()](boost::system::error_code ec, size_t) {
			if (!ec) {
				self->async_read_body();
			}
		});
	}

	void async_read_body()
	{
		this->message.content.resize(this->message.size());
		boost::asio::async_read(this->socket, this->message.body(),
			[self = this->shared_from_this()](boost::system::error_code ec, size_t) {
			if (!ec) {
				self->on_message(self->message);
				self->async_read_header();
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
			[self = this->shared_from_this()](boost::system::error_code ec, size_t) {
			if (!ec) {
				self->write_queue.pop_front();
				if (!self->write_queue.empty()) {
					self->do_write();
				}
			}
		});
	}

	Message message;
	std::deque<Message> write_queue;
	boost::asio::ip::tcp::socket socket;
	std::function<void(Message message)> on_message;
};


class Acceptor : public std::enable_shared_from_this<Acceptor> {
public:
	Acceptor(
		boost::asio::io_context& io_context,
		boost::asio::ip::tcp::endpoint endpoint,
		std::function<void(boost::asio::ip::tcp::socket socket)> on_accept) :
		acceptor(io_context, endpoint),
		on_accept(on_accept)
	{}

	void async_accept()
	{
		this->acceptor.async_accept(
			[self = this->shared_from_this()](boost::system::error_code ec, boost::asio::ip::tcp::socket socket) {
			std::cout << ec.message() << std::endl;
			if (!ec) {
				self->on_accept(std::move(socket));
			}
			self->async_accept();
		});
	}

private:
	boost::asio::ip::tcp::acceptor acceptor;
	std::function<void(boost::asio::ip::tcp::socket socket)> on_accept;
};


class Connector : public std::enable_shared_from_this<Connector> {
public:
	Connector(
		boost::asio::io_context& io_context,
		boost::asio::ip::tcp::endpoint endpoint,
		std::function<void(boost::asio::ip::tcp::socket socket)> on_connect) :
		socket(io_context),
		endpoint(endpoint),
		on_connect(on_connect)
	{}

	void async_connect()
	{
		this->socket.async_connect(this->endpoint,
			[self = this->shared_from_this()](boost::system::error_code ec) {
			std::cout << ec.message() << std::endl;
			if (!ec) {
				self->on_connect(std::move(self->socket));
			}
			else {
				self->async_connect();
			}
		});
	}

private:
	boost::asio::ip::tcp::socket socket;
	boost::asio::ip::tcp::endpoint endpoint;
	std::function<void(boost::asio::ip::tcp::socket socket)> on_connect;
};


class Network {
public:
	Network(
		boost::asio::io_context& io_context,
		std::shared_ptr<scm::Env> env) :
		io_context(io_context),
		env(env)
	{
		scm::fun_ptr print = [this](const scm::List& lst) {
			std::cout << lst.front() << std::endl;
			return true;
		};

		scm::fun_ptr write = [this](const scm::List& lst) {
			for (auto& weak_ptr : this->connections) {
				if (auto connection = weak_ptr.lock()) {
					connection->write(scm::print(lst.front()));
				}
			}
			return true;
		};

		this->on_connect = [this](boost::asio::ip::tcp::socket socket)
		{
			auto connection = std::make_shared<Connection>(
				std::move(socket),
				[this](Message message) {
					this->eval(message.to_string());
				});

			connection->async_read_header();
			this->connections.push_back(connection);
			return true;
		};

		scm::fun_ptr accept = [this](const scm::List& lst)
		{
			if (lst.size() != 1 || lst[0].type() != typeid(boost::asio::ip::tcp::endpoint)) {
				throw std::invalid_argument("accept: invalid argument");
			}
			auto endpoint = std::any_cast<boost::asio::ip::tcp::endpoint>(lst[0]);
			auto acceptor = std::make_shared<Acceptor>(this->io_context, endpoint, this->on_connect);
			acceptor->async_accept();
			return true;
		};

		scm::fun_ptr connect = [this](const scm::List& lst)
		{
			if (lst.size() != 1 || lst[0].type() != typeid(boost::asio::ip::tcp::endpoint)) {
				throw std::invalid_argument("connect: invalid argument");
			}
			auto endpoint = std::any_cast<boost::asio::ip::tcp::endpoint>(lst[0]);
			auto connector = std::make_shared<Connector>(this->io_context, endpoint, this->on_connect);
			connector->async_connect();
			return true;
		};

		scm::fun_ptr endpoint = [this](const scm::List& lst)
		{
			if (lst.size() != 2 || lst[0].type() != typeid(scm::String) || lst[1].type() != typeid(scm::Number)) {
				throw std::invalid_argument("endpoint: invalid argument");
			}
			auto addr = std::any_cast<scm::String>(lst[0]);
			auto port = std::any_cast<scm::Number>(lst[1]);
			auto address = boost::asio::ip::address::from_string(addr);
			return boost::asio::ip::tcp::endpoint(address, static_cast<unsigned short>(port));
		};

		this->env->outer = std::make_shared<scm::Env>(
			std::unordered_map<std::string, std::any>{
				{ "write", write },
				{ "print", print },
				{ "accept", accept },
				{ "connect", connect },
				{ "endpoint", endpoint },
		});
	}

	void eval(const std::string& expression)
	{
		std::cout << scm::eval(scm::read(expression), this->env) << std::endl;
	}

private:
	std::shared_ptr<scm::Env> env;
	boost::asio::io_context& io_context;
	std::list<std::weak_ptr<Connection>> connections;
	std::function<void(boost::asio::ip::tcp::socket socket)> on_connect;
};
