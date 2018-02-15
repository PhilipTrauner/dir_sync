#include <iostream>
#include <fstream>

#include <unistd.h>
#include <errno.h>
#include <sys/stat.h> 

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wconversion"

#include "asio.hpp"
#include "spdlog/spdlog.h"
#include "fmt/format.h"
#include "json.hpp"
#include "clipp.h"

#pragma GCC diagnostic pop

#include "dir_sync.pb.h"

#include "shared.h"
#include "constants.h"
#include "networking.h"

using std::cout;

using std::ifstream;

using asio::io_context;
using asio::ip::tcp;
using asio::ip::address;
using asio::error_code;

using spdlog::stdout_color_mt;
using spdlog::set_level;
using spdlog::level::level_enum;

using json = nlohmann::json;

using namespace clipp;

auto console{stdout_color_mt("client")};

int process_file_requests(tcp::socket& sock) {
	vector<string> file_paths;

	FileRequest file_request;

	while (true) {
		if (recv_proto(sock, file_request) == PROTO_TYPE_OK) {
			console->debug("File '{}' requested", 
				file_request.relative_path());

			file_paths.push_back(file_request.relative_path());

		} else {
			break;
		}
	}

	int recv_error_code{};

	for (const string& file_path : file_paths) {
		recv_error_code = send_file(sock, file_path);

		if (recv_error_code < 0) {
			return recv_error_code;
		}
	}

	return 0;
}

int recv_directories(tcp::socket& sock) {
	DirectoryRequest dir_response;

	int recv_error_code{};

	while (true) {
		recv_error_code = recv_proto(sock, dir_response);

		if (recv_error_code != PROTO_TYPE_OK) {
			return recv_error_code;
		} else {
			console->debug("Creating directory '{}'", 
				dir_response.relative_path());

			if (mkdir(dir_response.relative_path().c_str(), 
				S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) > 0) {
				console->warn("Could not create directory '{}'", 
					dir_response.relative_path());
			}
		}
	}

	return 0;
}

int main(int argc, char* argv[]) {
	GOOGLE_PROTOBUF_VERIFY_VERSION;

	string directory;
	string address;
	string config;

	unsigned short port{default_port};
	bool strict{false};
	bool verbose{false};

	auto cli = (
		value("directory", directory),
		value("address", address),
		option("--strict").set(strict).doc(
			"compare hashes and file metadata"),
		option("--verbose").set(verbose).doc(
			"log additional debug info"),		
		(option("-p", "--port").doc(
			"provide alternative port") & value("port", port)),
		(option("-c", "--config").doc(
			"override command line parameters with config") & value(
			"config", config))
	);

	if (!parse(argc, argv, cli)) {
		cout << make_man_page(cli, argv[0]);
	} else {
		if (config != "") {
			ifstream config_file(config);

			if (config_file.good()) {
				json j;
				config_file >> j;

				strict = j.value("strict", strict);
				verbose = j.value("verbose", verbose);
				port = j.value("port", port);

				console->info("Config file '{}' applied.", config);
			} else {
				console->warn("Config file '{}' could not be applied.", 
					config);
			}
		}	

		if (verbose) {
			set_level(level_enum::debug);
		}

		if (chdir(directory.c_str()) == 0) {

			io_context io_context;
			error_code error_code_;

			tcp::socket sock{io_context};
			tcp::endpoint endpoint(address::from_string(address, error_code_), 
				port);

			sock.connect(endpoint, error_code_);
			
			if (!error_code_) {
				SanityCheck sanity_check = run_sanity_check();

				if (send_proto(sock, sanity_check) > 0) {
					console->error("Could not send SanityCheck.");
				}

				FileTree file_tree{get_file_tree(".")};

				if (send_proto(sock, file_tree) > 0) {
					console->error("Could not transfer FileTree.");
				}

				if (recv_directories(sock) > 0) {
					console->error("Could not create requested directories.");
				}

				console->debug("Received directories");

				if (recv_files(sock) > 0) {
					console->error("Could not receive missing files.");
				}

				console->debug("Received files");

				if (process_file_requests(sock)) {
					console->error("Could not process file requests.");
				}

				console->debug("Processed file requests");

				send_protocol_separator(sock);

				sock.close();
			} else {
				console->error("Could not connect to server");
				return CONNECTION_COULD_NOT_BE_ESTABLISHED;					
			}
		} else {
			console->error(repr_chdir_error(errno));
			return errno;
		}
	}
	return 0;
}