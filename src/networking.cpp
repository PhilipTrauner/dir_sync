#include "networking.h"

#include <tuple>

#include "asio.hpp"
#include "spdlog/spdlog.h"
#include "fmt/format.h"

#include <google/protobuf/message.h>

using std::ostream;
using std::istream;

using std::tuple;

using asio::buffer;
using asio::streambuf;
using asio::ip::tcp;
using asio::write;
using asio::error_code;

using spdlog::stdout_color_mt;

using google::protobuf::Message;

auto networking_console = stdout_color_mt("networking");

#define TRY(fn_with_ec) fn_with_ec; if (error_code_) { return ASIO_ERROR; }

// Extracts integral value of enum class variants
template <typename E>
constexpr auto to_underlying(E e) noexcept
{
	return static_cast<u_int8_t>(static_cast<std::underlying_type_t<E>>(e));
}


int send_proto(tcp::socket& sock, Message& message) {
	error_code error_code_;

	u_int8_t message_type{to_underlying(proto_type_mapping.at(typeid(message)))};

	u_int64_t message_size{message.ByteSizeLong()};

	TRY(write(sock, buffer(&message_type, U_INT_8_SIZE), error_code_));
	TRY(write(sock, buffer(&message_size, U_INT_64_SIZE), error_code_));
	
	streambuf buf;
	ostream os(&buf);
	message.SerializeToOstream(&os);

	TRY(write(sock, buf, error_code_));

	return SEND_OK;
}

int recv_proto(tcp::socket& sock, Message& message) {
	error_code error_code_;

	MessageType expected_message_type{proto_type_mapping.at(typeid(message))};

	u_int8_t message_type_raw;
	MessageType message_type;

	u_int64_t message_size;

	TRY(sock.receive(buffer(&message_type_raw, U_INT_8_SIZE), 0, error_code_));

	message_type = static_cast<MessageType>(message_type_raw);

	TRY(sock.receive(buffer(&message_size, U_INT_64_SIZE), 0));

	streambuf buf;
	streambuf::mutable_buffers_type mbt{buf.prepare(message_size)};

	buf.commit(read(sock, mbt, error_code_));

	if (error_code_) {
		return ASIO_ERROR;
	}

	if (message_type == expected_message_type) {
		
		istream is(&buf);
		message.ParseFromIstream(&is);

		return PROTO_TYPE_OK;
	} else if (message_type == MessageType::ProtocolSeparator) {
		return PROTO_TYPE_STAGE_END;
	}

	return PROTO_TYPE_WRONG;
}