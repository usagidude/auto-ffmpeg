#!/bin/bash
g++-13 -std=gnu++20 -static-libstdc++ -static-libgcc -Wall -O2 -D_GNU_SOURCE os.cpp auto_ffmpeg.cpp -lpthread -luuid -o auto-ffmpeg