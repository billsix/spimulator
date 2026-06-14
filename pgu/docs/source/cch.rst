..
   Copyright 2002 Jonathan Bartlett

   Permission is granted to copy, distribute and/or modify this
   document under the terms of the GNU Free Documentation License,
   Version 1.1 or any later version published by the Free Software
   Foundation; with no Invariant Sections, with no Front-Cover Texts,
   and with no Back-Cover Texts.  A copy of the license is included in fdl.xml


The C Language
==============

In this chapter we will begin to look at our first "real-world"
programming language. Assembly language is the language used at the
machine's level, but most people (including me) find coding in assembly
language too cumbersome for normal use. Many computer languages have
been invented to make the programming task easier. Knowing a wide
variety of languages is useful for many reasons, including

-  Different languages are good for different types of projects

-  Different companies have different standard languages, so knowing
   more languages makes your skills more marketable

-  The more languages you know, the easier it is to pick up new ones

-  Different languages are based on different concepts, which will help
   you to learn different and better ways of doing things

This chapter focuses on the C language.

Compiled and Interpreted Languages
----------------------------------

C is a *compiled* language. When you wrote in assembly language, each
instruction you wrote was translated into exactly one machine
instruction for processing. With compilers, an instruction can translate
into one or hundreds of machine instructions. In fact, depending on how
advanced your compiler is, it might even restructure parts of your code
to make it faster. In assembly language, what you write is what you get.

There are also languages that are *translated* languages. These
languages require that the user run a program called a *translator*
(also called a *run-time environment*) that in turn runs the given
program. These are usually slower than compiled programs, since the
translator has to read and interpret the code as it goes along. However,
in well-made translators, this time can be fairly negligible. There is
also a class of hybrid languages which partially compile a program
before execution into byte-codes, which are only machine readable. The
translator can read the byte-codes much faster than it can read the
regular language, so the reading step only happens once.

There are many reasons to choose one or the other. Compiled programs are
nice, because you don't have to already have a translator installed in
the user's machine. You have to have a compiler for the language, but
the users of your program don't. In a translated language, you have to
be sure that the user has a translator for your program, and that the
computer knows which translator runs your program.

Your First C Program
--------------------

As you may have noticed, I enjoy presenting you with a program first,
and then explaining how it works. So, here is your first program, which
prints "Hello world" to the screen and exits. Type it in, and give it
the name Hello-World.c

.. literalinclude:: ../../src/Hello-World.c
   :language: c
   :linenos:
   :lineno-match:
   :caption: src/Hello-World.c


As you can see, it's a pretty simple program. To compile it, run the
command

::

   gcc -o HelloWorld Hello-World.c

To run the program, do

::

   ./HelloWorld

Let's look at how this program was put together.

Comments in C are started with ``/*`` and ended with ``*/``. Comments
can span multiple lines, but many people prefer to start and end
comments on the same line so they don't get confused.

``#include <stdio.h>`` is the first part of the
program. This is a *preprocessor directive*. C compiling is split into
two stages - the preprocessor and the main compiler. This directive
tells the preprocessor to look for the file ``stdio.h`` and paste it
into your program. So, everything in ``stdio.h`` is now in your program
just as if you typed it there yourself. The angle brackets around the
filename tell the compiler to look in its standard paths for the file
(``/usr/include`` and ``/usr/local/include``, usually). If it was in
quotes, like ``#include "stdio.h"`` it would look in the current
directory for the file. Anyway, ``stdio.h`` contains the declarations
for the standard input and output functions and variables. The next few
lines are simply comments about the program.

Then there is the line ``int main(int argc, char **argv)``. This is the
start of a function. C Functions are declared with their name, arguments
and return type. This declaration says that the functions name is
``main``, it returns an ``int`` (integer), and has two arguments - an
``int`` called ``argc`` and a ``char **`` called ``argv``. You don't
have to worry about where the arguments are positioned on the stack -
the C compiler takes care of that for you. You also don't have to worry
about loading values into and out of registers. The ``main`` function is
a special function - it is the start or all C programs. It always takes
two parameters. The first parameter is the number of arguments given to
this command, and the second parameter is a list of the arguments that
were given.

The next line is a function call. In assembly language, you had to push
the arguments of a function onto the stack, and then call the function.
C takes care of this complexity for you. You simply have to call the
function with the parameters in parenthesis. In this case, we call the
function ``puts``, with a single parameter. This parameter is the

Review
------

Know the Concepts
~~~~~~~~~~~~~~~~~

-

Use the Concepts
~~~~~~~~~~~~~~~~

-

Going Further
~~~~~~~~~~~~~

-
