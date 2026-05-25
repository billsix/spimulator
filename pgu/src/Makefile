all: \
     conversion-program \
     factorial \
     exit \
     gnome-example-c

gnome-example-c: gnome-example-c.c
	gcc -Wall $$(pkg-config --cflags gtk4) gnome-example-c.c -o gnome-example-c $$(pkg-config --libs gtk4)

# add-year: add-year.s
# 	clang -m32 -o add-year add-year.s -nostdlib

conversion-program: conversion-program.s \
		    count-chars.s \
                    integer-to-string.s \
                    write-newline.s \
                    linux.s
	clang -m32 -o conversion-program conversion-program.s count-chars.s integer-to-string.s write-newline.s -nostdlib

factorial: factorial.s
	clang -m32 -o factorial factorial.s -nostdlib


exit: exit.s
	clang -m32 -o exit exit.s -nostdlib

# C ports of every assembly file in this directory live in c/ and
# are built by c/Makefile.  See c/README.md.
