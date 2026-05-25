..
   Copyright 2002 Jonathan Bartlett

   Permission is granted to copy, distribute and/or modify this
   document under the terms of the GNU Free Documentation License,
   Version 1.1 or any later version published by the Free Software
   Foundation; with no Invariant Sections, with no Front-Cover Texts,
   and with no Back-Cover Texts.  A copy of the license is included in fdl.xml

.. _syscallap:

Important System Calls
======================

These are some of the more important system calls to use when dealing
with Linux. For most cases, however, it is best to use library functions
rather than direct system calls, because the system calls were designed
to be minimalistic while the library functions were designed to be easy
to program with. For information about the Linux C library, see the
manual at http://www.gnu.org/software/libc/manual/

Remember that %eax holds the system call numbers, and
that the return values and error codes are also stored in %eax.

.. list-table:: Important Linux System Calls
   :header-rows: 1
   :widths: 4 6 14 10 10 30

   * - %eax
     - Name
     - %ebx
     - %ecx
     - %edx
     - Notes
   * - 1
     - ``exit``
     - return value (int)
     -
     -
     - Exits the program
   * - 3
     - ``read``
     - file descriptor
     - buffer start
     - buffer size (int)
     - Reads into the given buffer
   * - 4
     - ``write``
     - file descriptor
     - buffer start
     - buffer size (int)
     - Writes the buffer to the file descriptor
   * - 5
     - ``open``
     - null-terminated file name
     - option list
     - permission mode
     - Opens the given file.  Returns the file descriptor or
       an error number.
   * - 6
     - ``close``
     - file descriptor
     -
     -
     - Closes the give file descriptor
   * - 12
     - ``chdir``
     - null-terminated directory name
     -
     -
     - Changes the current directory of your program.
   * - 19
     - ``lseek``
     - file descriptor
     - offset
     - mode
     - Reposition where you are in the given file.  The mode
       (called the "whence") should be 0 for absolute
       positioning, and 1 for relative positioning.
   * - 20
     - ``getpid``
     -
     -
     -
     - Returns the process ID of the current process.
   * - 39
     - ``mkdir``
     - null-terminated directory name
     - permission mode
     -
     - Creates the given directory.  Assumes all directories
       leading up to it already exist.
   * - 40
     - ``rmdir``
     - null-terminated directory name
     -
     -
     - Removes the given directory.
   * - 41
     - ``dup``
     - file descriptor
     -
     -
     - Returns a new file descriptor that works just like the
       existing file descriptor.
   * - 42
     - ``pipe``
     - pipe array
     -
     -
     - Creates two file descriptors, where writing on one
       produces data to read on the other and vice-versa.
       %ebx is a pointer to two words of storage to hold the
       file descriptors.
   * - 45
     - ``brk``
     - new system break
     -
     -
     - Sets the system break (i.e. - the end of the data
       section).  If the system break is 0, it simply returns
       the current system break.
   * - 54
     - ``ioctl``
     - file descriptor
     - request
     - arguments
     - This is used to set parameters on device files.  Its
       actual usage varies based on the type of file or device
       your descriptor references.

A more complete listing of system calls, along with additional
information is available at http://www.lxhp.in-berlin.de/lhpsyscal.html
You can also get more information about a system call by typing in
``man 2 SYSCALLNAME`` which will return you the information about the
system call from section 2 of the UNIX manual. However, this refers to
the usage of the system call from the C programming language, and may or
may not be directly helpful.

For information on how system calls are implemented on Linux, see the
Linux Kernel 2.4 Internals section on how system calls are implemented
at http://www.faqs.org/docs/kernel_2_4/lki-2.html#ss2.11
