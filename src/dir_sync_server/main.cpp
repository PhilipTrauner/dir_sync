#include <iostream>
#include <vector>
#include <string>
#include <tuple>
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

using std::vector;
using std::tuple;

using std::get;

using std::ifstream;

using asio::io_context;
using asio::ip::tcp;
using asio::ip::address;
using asio::error_code;

using spdlog::stdout_color_mt;
using spdlog::set_level;
using spdlog::level::level_enum;

using dir_sync::FileMetadata;
using dir_sync::DirectoryMetadata;

using nlohmann::json;

using namespace clipp;

typedef tuple<vector<string>, vector<string>> file_tree_file_diff;
typedef vector<string> file_tree_dir_diff;

auto console{stdout_color_mt("server")};

file_tree_file_diff diff_file_tree_files(
	FileTree& master_tree, FileTree& slave_tree) {

	vector<string> file_sends;
	vector<string> file_requests;

	string cur_master_file_path;	
	string cur_slave_file_path;	

	for (const FileMetadata& master_file : master_tree.files()) {
		bool in_slave{false};
		cur_master_file_path = master_file.relative_path();

		for (const FileMetadata& slave_file : slave_tree.files()) {
			cur_slave_file_path = slave_file.relative_path();

			if (slave_file.relative_path() == master_file.relative_path()) {
				in_slave = true;

				if (slave_file.hash() != master_file.hash()) {
					if (slave_file.mtime() < master_file.mtime())  {
						// Send file
						file_sends.push_back(cur_slave_file_path);
					} else if (slave_file.mtime() > master_file.mtime()) {
						// Request file
						file_requests.push_back(cur_slave_file_path);
					}
				}
			}
		}

		if (!in_slave) {
			// Send file
			file_sends.push_back(cur_master_file_path);
		}
	}

	// Request files that are in slave tree but not in master tree
	for (const FileMetadata& slave_file : slave_tree.files()) {
		bool in_master{false};
		cur_slave_file_path = slave_file.relative_path();

		for (const FileMetadata& master_file : master_tree.files()) {
			if (slave_file.relative_path() == master_file.relative_path()) {
				in_master = true;
			}
		}

		if (!in_master) {
			// Request file
			file_requests.push_back(cur_slave_file_path);
		}
	}

	return {file_sends, file_requests};
}

file_tree_dir_diff diff_file_tree_directories(
	FileTree& master_tree, FileTree& slave_tree) {

	vector<string> folder_requests;

	string cur_master_dir_path;

	for (const DirectoryMetadata& master_dir : master_tree.directories()) {
		bool in_slave{false};
		cur_master_dir_path = master_dir.relative_path();

		for (const DirectoryMetadata& slave_dir : slave_tree.directories()) {
			if (slave_dir.relative_path() == master_dir.relative_path()) {
				in_slave = true;
			}
		}

		if (!in_slave) {
			// Request folder creation
			folder_requests.push_back(cur_master_dir_path);
		}
	}

	return folder_requests;
}

int send_file_request(tcp::socket& sock, string relative_path) {
	FileRequest file_request;
	file_request.set_relative_path(relative_path);

	return send_proto(sock, file_request);
}


int main(int argc, char* argv[]) {
	GOOGLE_PROTOBUF_VERIFY_VERSION;

	string directory;
	string config{""};

	unsigned short port{default_port};
	bool strict{false};
	bool verbose{false};
	

	auto cli = (
		value("directory", directory),
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
			
			io_context ctx;
			error_code error_code_;

			tcp::acceptor acceptor{ctx, tcp::endpoint{tcp::v4(), port}};

			while (true) {
				tcp::socket sock{acceptor.accept(error_code_)};
				tcp::endpoint endpoint{sock.remote_endpoint()};

				string ip_address{endpoint.address().to_string()};
				unsigned short port{endpoint.port()};

				if (error_code_) {
					console->error("Connection with '{}:{}' could not be "
						"established. ({})", ip_address, port, error_code_.message());
					sock.close();
				} else {
					console->info("Connection established ({}:{}).", ip_address, 
						port);						

					SanityCheck sanity_check;

					if (recv_proto(sock, sanity_check) == PROTO_TYPE_OK) {
						if (!is_sane(sanity_check)) {
							console->error("Sanity check failed, "
								"prepare for unforseen consequences.");
						} else {
							console->debug("Client is sane.");
						}

						FileTree master_file_tree{get_file_tree(".")};
						FileTree slave_file_tree;

						if (recv_proto(sock, slave_file_tree) == PROTO_TYPE_OK) {
							console->debug("File tree received.");

							file_tree_file_diff file_tree_file_diff_{
								diff_file_tree_files(
									master_file_tree, slave_file_tree)};

							file_tree_dir_diff file_tree_dir_diff_server{
								diff_file_tree_directories(
									master_file_tree, slave_file_tree)};

							file_tree_dir_diff file_tree_dir_diff_client{
								diff_file_tree_directories(
									slave_file_tree, master_file_tree)};	

							console->debug("Files slated for send: ");
							print_string_vector(get<0>(file_tree_file_diff_), verbose);

							console->debug("File requests: ");
							print_string_vector(get<1>(file_tree_file_diff_), verbose);

							console->debug("Directory creation requests: ");
							print_string_vector(file_tree_dir_diff_server, verbose);

							console->debug("Creating directories: ");
							print_string_vector(file_tree_dir_diff_client, verbose);

							// Create folders that exist on client but not on server

							for (const string& dir_path : file_tree_dir_diff_client) {
								if (mkdir(dir_path.c_str(), 
									S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) > 0) {
									console->warn("Could not create directory '{}'.", 
										dir_path);
								}
							}

							// Request folder creation of folders that are missing on the client

							for (const string& dir_path : file_tree_dir_diff_server) {
								if (send_directory(sock, dir_path) > 0) {
									console->warn("Could not request directory creation "
										"for '{}'.", dir_path);
								}
							}

							send_protocol_separator(sock);

							// Send files that are missing on the client

							for (const string& file_path : get<0>(file_tree_file_diff_)) {
								if (send_file(sock, file_path) > 0) {
									console->warn("Could not send file "
										"'{}'.", file_path);
								}
							}

							send_protocol_separator(sock);

							// Request files that are missing on the server

							for (const string& file_path : get<1>(file_tree_file_diff_)) {
								if (send_file_request(sock, file_path) > 0) {
									console->warn("Could not request file "
										"'{}'.", file_path);									
								}
							}

							send_protocol_separator(sock);

							// Receive files

							if (recv_files(sock) > 0) {
								console->warn("Could not receive all requested files.");
							}

							sock.close();
						}
					} else {
						console->error("Unexpected message type ({}:{}). "
							"Closing connection.", ip_address, port);

						sock.close();
					}
				}		
			}

		} else {
			console->error(repr_chdir_error(errno));
			return errno;
		}
	}

	return 0;
}