#!/bin/env bash

# build the PDF
cd /pgu/docs
make latexpdf

# copy the output to the host OS
mkdir -p /output/pgu/pdf/
cp -r build/latex/*pdf /output/pgu/pdf/
