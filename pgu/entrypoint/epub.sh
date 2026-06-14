#!/bin/env bash

# build the EPUB
cd /pgu/docs
make epub


# copy the output to the host OS
mkdir -p /output/pgu/epub/
cp -r build/epub/* /output/pgu/epub/
