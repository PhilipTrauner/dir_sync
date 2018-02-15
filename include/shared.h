#pragma once

#include <string>
#include <vector>

#include "asio.hpp"

#include "dir_sync.pb.h"

using std::string;
using std::vector;

using asio::ip::tcp;

using dir_sync::FileTree;
using dir_sync::SanityCheck;

// Max amount of file descriptors nftw() is allowed to use
const unsigned short MAX_FDS{15};
const unsigned int PART_SIZE{65536};

const int FILE_RECV_OK{0};
const int FILE_SEND_OK{0};
const int DIRECTORY_SEND_OK{0};
const int PROTO_SEPARATOR_SEND_OK{0};
const int SEND_MTIME_ERR{3};

string sha512_hash_file(int fd, long long size);

FileTree get_file_tree(string path);

SanityCheck run_sanity_check();

bool is_sane(SanityCheck& sanity_check, unsigned short allow_timeshifted=5);

string repr_chdir_error(int error_code);

void print_string_vector(vector<string> vec, bool verbose=false);

int send_file(tcp::socket& sock, string relative_path);

// 'Send' means request in this context
// Little counter-intuitive but naming things is hard 
int send_directory(tcp::socket& sock, string relative_path);

int recv_files(tcp::socket& sock);

int send_protocol_separator(tcp::socket& sock);