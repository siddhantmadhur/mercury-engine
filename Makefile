.PHONY: build

init: 
	cmake -B build . 

build: init
	./sokol-shdc -i src/1-triangle.glsl -o 1-triangle.shader.hh --slang glsl430
	cmake --build build

run: build
	./build/mercury

dependencies:
	wget https://github.com/floooh/sokol-tools-bin/raw/refs/heads/master/bin/linux/sokol-shdc
	chmod +x sokol-shdc
