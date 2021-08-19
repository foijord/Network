#pragma once

#include <boost/asio.hpp>

#include <string>
#include <vector>

class Message {
public:
	Message() = default;

	Message(const std::string& message)
	{
		*this << message;
	}

	std::vector<uint8_t> content;

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

	void operator << (const std::string& data)
	{
		uint32_t size = this->header_size() + data.size();
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
};
