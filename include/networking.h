#pragma once

#include <unordered_map>
#include <typeindex>

#include "asio.hpp"

#include <google/protobuf/message.h>

#include "dir_sync.pb.h"

using std::unordered_map;
using std::type_index;

using asio::ip::tcp;

using google::protobuf::Message;

using dir_sync::FileTree;
using dir_sync::FileRequest;
using dir_sync::FileResponse;
using dir_sync::DirectoryRequest;
using dir_sync::SanityCheck;
using dir_sync::ProtocolSeparator;
using dir_sync::MinimalFileMetadata;

enum class MessageType {
	FileTree = 1,
	FileRequest = 2,
	FileResponse = 3,
	DirectoryRequest = 4,
	SanityCheck = 5,
	ProtocolSeparator = 6,
	MinimalFileMetadata = 7
};

const unordered_map<type_index, MessageType> proto_type_mapping {
	{typeid(FileTree), MessageType::FileTree},
	{typeid(FileRequest), MessageType::FileRequest},
	{typeid(FileResponse), MessageType::FileResponse},
	{typeid(DirectoryRequest), MessageType::DirectoryRequest},
	{typeid(SanityCheck), MessageType::SanityCheck},
	{typeid(ProtocolSeparator), MessageType::ProtocolSeparator},
	{typeid(MinimalFileMetadata), MessageType::MinimalFileMetadata}
};

const unsigned int U_INT_8_SIZE{sizeof(u_int8_t)};
const unsigned int U_INT_64_SIZE{sizeof(u_int64_t)};

const int SEND_OK{0};

const int PROTO_TYPE_STAGE_END{-1};
const int PROTO_TYPE_OK{0};
const int PROTO_TYPE_WRONG{1};


const int ASIO_ERROR{2};

int send_proto(tcp::socket& sock, Message& message);

int recv_proto(tcp::socket& sock, Message& message);