#!/bin/env bash

[ -d /spimulator ] && cd /spimulator

run-clang-tidy . -fix
