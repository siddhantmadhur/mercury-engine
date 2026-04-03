#!/bin/bash

find pequod/shaders -type f -name "*.glsl" -exec bin/sokol-shdc -i {} -o {}.hh --slang glsl410:metal_macos:spirv_vk:hlsl5 ';'
