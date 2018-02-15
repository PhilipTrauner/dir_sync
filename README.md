# dir_sync

> A failed effort to write decent looking C++ code.  
> \- Philip Trauner

Simple networked directory synchronization (please use something like rsync instead).

## Limitations
* Doesn't differentiate between case sensitive and case insensitive file-systems
* No Windows support (lul)
* Probably leaks memory like crazy
* **No** input validation
* If you poke it with a stick it will probably crash

## Building
1. Clone repository  
```bash
git clone https://github.com/PhilipTrauner/dir_sync
cd dir_sync
```

2. Install platform specific dependencies  
	* Debian  
		
	```bash
	sudo apt-get install autoconf automake libtool curl make g++ unzip libssl-dev
	wget https://github.com/google/protobuf/releases/download/v3.5.1/protobuf-cpp-3.5.1.tar.gz
	tar xf protobuf-cpp-3.5.1.tar.gz
	cd protobuf-3.5.1
	./configure
	make
	make check
	sudo make install
	sudo ldconfig

	cd ..
	```

	* macOS  
	```bash
	brew install protobuf openssl cmake
	```

3. Install platform independent dependencies  
```bash
mkdir cpp_clones
cd cpp_clones

git clone https://github.com/nlohmann/json.git --depth=1
git clone https://github.com/gabime/spdlog.git --depth=1
git clone https://github.com/chriskohlhoff/asio.git --depth=1
git clone https://github.com/muellan/clipp.git --depth=1
git clone https://github.com/fmtlib/fmt.git --depth=1
cd fmt
mkdir build
cd build
cmake .. && make

cd ../..

export JSON_INCLUDE_PATH=$(realpath json)/single_include/nlohmann
export FMT_PATH=$(realpath fmt)
export SPDLOG_INCLUDE_PATH=$(realpath spdlog)/include
export CLIPP_INCLUDE_PATH=$(realpath clipp)/include
export ASIO_INCLUDE_PATH=$(realpath asio)/asio/include
```

4. Compiling  
```bash
cd ..
mkdir build
cd build
cmake .. && make
```