#!/bin/env bash

cd /book/
make html


# fix issues that github has for displaying the pages for me
cd build/html/

# copy the files over
mkdir /output/spimulator-examples
cp -r * /output/spimulator-examples/
# see if this fixes github issue with unscores in
# filenames created by sphinx
touch /output/spimulator-examples/.nojekyll
