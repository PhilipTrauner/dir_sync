#include "shared.h"

#include <string>
#include <iostream>
#include <mutex>
#include <ctime>
#include <vector>
#include <fstream>

#include <unistd.h>
#include <sys/stat.h> 
#include <sys/mman.h>
#include <fcntl.h>
#include <ftw.h>
#include <utime.h>

#include <openssl/sha.h>

#include "asio.hpp"
#include "spdlog/spdlog.h"
#include "fmt/format.h"

#include "networking.h"

#include "dir_sync.pb.h"

using std::cout;
using std::endl;

using std::string;
using std::size_t;

using std::time;

using std::mutex;
using std::lock_guard;

using std::vector;

using std::fstream;
using std::ifstream;
using std::ofstream;
using std::ios;
using std::ios_base;

using asio::ip::tcp;

using spdlog::stdout_color_mt;

using fmt::format;

using dir_sync::DirectoryMetadata;
using dir_sync::FileMetadata;
using dir_sync::FileTree;
using dir_sync::SanityCheck;
using dir_sync::FileResponse;
using dir_sync::MinimalFileMetadata;

#define TRY(fn) error_code_ = fn; if (error_code_ > 0) { return error_code_; }

// Due to the API of nftw() it is necessary to define the actual
// file collection at the global scope.
mutex mtx;
FileTree file_tree;

auto shared_console{stdout_color_mt("shared")};

string sha512_hash_file(int fd, long long size) {
	unsigned char hash_result[SHA512_DIGEST_LENGTH];

	void* file_buffer{mmap(0, size, PROT_READ, 
		MAP_SHARED, fd, 0)};

	SHA512((unsigned char*) file_buffer, size, hash_result);
	munmap(file_buffer, size); 

	string result{};
	char buffer [2];

	for (int i = 0; i < SHA512_DIGEST_LENGTH; i++) {
		sprintf(buffer, "%02x", hash_result[i]);
		result += buffer;
	}

	return result;
}

int process_file(const char* path, const struct stat* stats, int flag, struct FTW *) {
	if (flag == FTW_D || flag == FTW_F) {
		string relative_path{string(path)};

		// Exclude paths like '.', './'
		if (relative_path.length() > 2) {
			relative_path = relative_path.substr(2, relative_path.length());

			switch (flag) {
				// Directory
				case FTW_D: 
				{
					DirectoryMetadata* directory_metadata = file_tree.add_directories();

					directory_metadata->set_relative_path(relative_path);

					break;
				}
				// File
				case FTW_F:
				{
					int fd{open(path, O_RDONLY)};
					if (fd < 0) {
						shared_console->warn("Could not open '{0}'. Skipping...", path);
					} else {
						FileMetadata* file_metadata = file_tree.add_files();

						file_metadata->set_relative_path(relative_path);
						file_metadata->set_size(stats->st_size);
						file_metadata->set_mtime(stats->st_mtime);
						file_metadata->set_hash(sha512_hash_file(fd, stats->st_size));			
					}
					close(fd);

					break;
				}
			}			
		}	

	}
	return 0;
}


FileTree get_file_tree(string path) {
	lock_guard<mutex> guard{mtx};
	file_tree.Clear();

	nftw(path.c_str(), process_file, MAX_FDS, 0);

	return file_tree;
}

SanityCheck run_sanity_check() {
	SanityCheck sanity_check;

	sanity_check.set_time(time(nullptr));

	return sanity_check;
}

bool is_sane(SanityCheck& sanity_check, unsigned short allowed_timeshift) {
	long long sane_time = time(nullptr);
	long long possibly_insane_time = sanity_check.time();

	return (sane_time - allowed_timeshift) < possibly_insane_time && 
		(sane_time + allowed_timeshift) > possibly_insane_time;
}

string repr_chdir_error(int error_code) {
	switch (error_code) {
		case EACCES:
			return "No permission to access supplied directory.";
		case ENOENT:
			return "Directory does not exist.";
		case ENOTDIR:
			return "Supplied path is not a directory.";
		default:
			return format("Unknown error: {0}", error_code);
	}
}

void print_string_vector(vector<string> vec, bool verbose) {
	if (verbose) {
		for (const auto& s : vec) {
			cout << s << endl;
		}
	}
}

bool set_mtime(time_t mtime, string path) {
	struct stat old_stat;
	struct utimbuf new_times;

	if (stat(path.c_str(), &old_stat) < 0) {
		return false;
	}

	new_times.actime = old_stat.st_atime; /* keep atime unchanged */
	new_times.modtime = mtime;    /* set mtime to current time */
	
	if (utime(path.c_str(), &new_times) < 0) {
		return false;
	}

	shared_console->debug("Successfully set mtime of '{}' to '{}'",
		path, mtime);
 
	return true;
}

int send_file(tcp::socket& sock, string relative_path) {
	int error_code_{};

	MinimalFileMetadata file_metadata;
	file_metadata.set_relative_path(relative_path);
	
	struct stat stat_;
	time_t mtime{};


	if (stat(relative_path.c_str(), &stat_) < 0) {
		return SEND_MTIME_ERR; // can't determine mtime
	}

	mtime = stat_.st_mtime;

	file_metadata.set_mtime(mtime);

	TRY(send_proto(sock, file_metadata));

	FileResponse file_response;

	char byte;
	unsigned long part_id{0};
	string part{};

	ifstream file_stream(relative_path, ios_base::binary);
	while (file_stream) {
		// Read current byte
		file_stream.get(byte);

		// Append byte to buffer
		part += byte;
		// Increment part id
		part_id++;

		if (part_id == PART_SIZE) {
			// Dump buffer into protobuf
			file_response.set_part(part);

			TRY(send_proto(sock, file_response));

			// Clear buffer
			part.clear();
			// Reset part id
			part_id = 0;
		}
	}
	// Dump excess bytes from buffer
	file_response.set_part(part);
	TRY(send_proto(sock, file_response));

	// Protocol separator indicates file end
	TRY(send_protocol_separator(sock));

	return FILE_SEND_OK;
}

int send_directory(tcp::socket& sock, string relative_path) {
	int error_code_{};

	DirectoryRequest dir_response;
	dir_response.set_relative_path(relative_path);

	TRY(send_proto(sock, dir_response));

	return DIRECTORY_SEND_OK;
}

int recv_files(tcp::socket& sock) {
	MinimalFileMetadata file_metadata;
	FileResponse file_response;
	
	int meta_recv_error_code{};
	int file_recv_error_code{};

	while (true) {
		meta_recv_error_code = recv_proto(sock, file_metadata);

		if (meta_recv_error_code == PROTO_TYPE_OK) {
			shared_console->debug("Receiving file '{}'", 
				file_metadata.relative_path());

			bool all_parts_received{false};
			ofstream file_stream;

			file_stream.open(file_metadata.relative_path(), ios::out | ios::app);

			while (!all_parts_received) {
				file_recv_error_code = recv_proto(sock, file_response);
				
				if (file_recv_error_code == PROTO_TYPE_STAGE_END) {
					all_parts_received = true;
				} else if (file_recv_error_code == PROTO_TYPE_OK) {
					file_stream << file_response.part();
				} else {
					return file_recv_error_code;
				}
			}
			file_stream.close();
			if (!set_mtime(file_metadata.mtime(), file_metadata.relative_path())) {
				shared_console->warn("Could not set mtime for '{}'.", 
					file_metadata.relative_path());
			}
		} else if (meta_recv_error_code == PROTO_TYPE_STAGE_END) {
			break;
		} else {
			return meta_recv_error_code;
		}
	}

	return FILE_RECV_OK;
}

int send_protocol_separator(tcp::socket& sock) {
	int error_code_{};

	ProtocolSeparator protocol_stage_complete;
	TRY(send_proto(sock, protocol_stage_complete));

	return PROTO_SEPARATOR_SEND_OK;
}