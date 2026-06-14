#!/bin/env bash

# build the HTML and PDF
cd /pgu/docs
# build the HTML
make html
# build PDF
make latexpdf

# copy the output to the host OS
mkdir -p /output/pgu/
cp -r build/html/* /output/pgu/
cp -r build/latex/*pdf /output/pgu/
