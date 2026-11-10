//! The console: the program's input and output, read and written the way
//! the C engine does (the IO functions of `spim.c`).
//!
//! The C engine reads its input straight from file descriptor 0 with
//! `read(2)`, one byte at a time, never through a buffer.  That matters:
//! the REPL's commands, the program's `read_int`, and a program's own
//! `read` syscall on descriptor 0 all take turns on the same input, and
//! none may swallow bytes meant for another.  Rust's `std::io::stdin` is
//! buffered, so the port calls the C library's `read`, `write`, `open`,
//! `close` and `poll` directly; Rust's standard library is linked with the
//! C library anyway, so this needs no extra crate.

use std::ffi::{c_char, c_int, c_short, c_void, CString};

use crate::machine::{error, write_stdout, RunErrorUnwind};

/// The C library functions the console needs (POSIX).
mod sys {
    use std::ffi::{c_char, c_int, c_short, c_void};

    /// One descriptor to watch, for `poll`.
    #[repr(C)]
    pub struct PollFd {
        pub fd: c_int,
        pub events: c_short,
        pub revents: c_short,
    }

    extern "C" {
        pub fn read(fd: c_int, buf: *mut c_void, count: usize) -> isize;
        pub fn write(fd: c_int, buf: *const c_void, count: usize) -> isize;
        pub fn open(path: *const c_char, flags: c_int, ...) -> c_int;
        pub fn close(fd: c_int) -> c_int;
        pub fn poll(fds: *mut PollFd, nfds: u64, timeout: c_int) -> c_int;
        pub fn signal(signum: c_int, handler: usize) -> usize;
        pub fn raise(signum: c_int) -> c_int;
    }
}

/// `poll`'s "data to read" event.
const POLLIN: c_short = 1;

/// `read(fd, buffer)`: the bytes read, 0 at end of input, -1 on error.
pub fn read_fd(fd: i32, buffer: &mut [u8]) -> isize {
    // SAFETY: the pointer and length describe `buffer`, which is valid for
    // writes of `buffer.len()` bytes for the duration of the call.
    unsafe {
        sys::read(
            fd as c_int,
            buffer.as_mut_ptr().cast::<c_void>(),
            buffer.len(),
        )
    }
}

/// `write(fd, buffer)`: the bytes written, or -1.
pub fn write_fd(fd: i32, buffer: &[u8]) -> isize {
    // SAFETY: the pointer and length describe `buffer`, valid for reads.
    unsafe { sys::write(fd as c_int, buffer.as_ptr().cast::<c_void>(), buffer.len()) }
}

/// `open(path, flags, mode)`: a descriptor, or -1.  The flags and mode are
/// the host's values, passed through unchanged as in the C engine.
pub fn open_path(path: &[u8], flags: i32, mode: i32) -> i32 {
    let Ok(path) = CString::new(path.to_vec()) else {
        return -1;
    };
    // SAFETY: `path` is a NUL-terminated string that outlives the call;
    // `open` reads its third (variadic) argument as an int mode.
    unsafe {
        sys::open(
            path.as_ptr().cast::<c_char>(),
            flags as c_int,
            mode as c_int,
        )
    }
}

/// Let a write to a closed pipe end the process, as it ends a C program.
///
/// A Rust program starts with `SIGPIPE` ignored, so a write to a closed
/// pipe fails instead; the engine ignores write failures as the C engine
/// does, and `spimulator-rust -f yes.asm | head -3` would never stop.
pub fn restore_sigpipe() {
    const SIGPIPE: c_int = 13;
    const SIG_DFL: usize = 0;
    // SAFETY: setting a signal's disposition back to the default is
    // always valid; no handler code runs in this process.
    unsafe {
        sys::signal(SIGPIPE, SIG_DFL);
    }
}

/// The signal a C integer division `i32::MIN / -1` dies of on x86.
pub const SIGFPE: i32 = 8;
/// The signal a C null-pointer read dies of.
pub const SIGSEGV: i32 = 11;

/// End the process with `signal`, where the C engine crashes the same
/// way (see "C engine bugs found" in `tasks/engine-port-rust.md`): the
/// port is to behave as the C engine does, and a test sees the signal.
pub fn die_of_signal(signal: i32) -> ! {
    const SIG_DFL: usize = 0;
    // SAFETY: restoring the default disposition is always valid (Rust's
    // runtime installs its own SIGSEGV handler); `raise` then delivers
    // the signal to this thread, which ends the process.
    unsafe {
        sys::signal(signal as c_int, SIG_DFL);
        sys::raise(signal as c_int);
    }
    std::process::abort()
}

/// `close(fd)`: 0, or -1.
pub fn close_fd(fd: i32) -> i32 {
    // SAFETY: closing any integer is defined: an invalid one fails.
    unsafe { sys::close(fd as c_int) }
}

/// Read one byte of input, or `None` at its end.
pub fn read_input_byte() -> Option<u8> {
    let mut byte: [u8; 1] = [0];
    if read_fd(0, &mut byte) <= 0 {
        None
    } else {
        Some(byte[0])
    }
}

/// Read like `fgets`: up to `size - 1` bytes, stopping after a newline,
/// then a NUL if there is room (`read_input`).  Returns the number of
/// bytes read; 0 means end of input.
pub fn read_input(buffer: &mut [u8], size: i32) -> usize {
    let mut room: i32 = size;
    let mut count: usize = 0;
    while room > 1 {
        let Some(byte) = read_input_byte() else {
            break;
        };
        if let Some(slot) = buffer.get_mut(count) {
            *slot = byte;
        }
        room -= 1;
        count += 1;
        if byte == b'\n' {
            break;
        }
    }
    if room > 0 {
        if let Some(slot) = buffer.get_mut(count) {
            *slot = 0;
        }
    }
    count
}

/// Read an integer like `scanf("%ld")` (`read_int_input`): skip white
/// space, an optional sign, digits.  Only the number and the byte after it
/// are taken from the input.  Returns `None` at end of input or if no
/// digit came.
pub fn read_int_input() -> Option<i64> {
    let mut next: Option<u8> = read_input_byte();
    while matches!(next, Some(b' ' | b'\t' | b'\n' | b'\r')) {
        next = read_input_byte();
    }
    let mut sign: i64 = 1;
    if let Some(c @ (b'-' | b'+')) = next {
        if c == b'-' {
            sign = -1;
        }
        next = read_input_byte();
    }
    let mut value: i64 = 0;
    let mut got_digit: bool = false;
    while let Some(c @ b'0'..=b'9') = next {
        got_digit = true;
        value = value.wrapping_mul(10).wrapping_add(i64::from(c - b'0'));
        next = read_input_byte();
    }
    got_digit.then(|| sign.wrapping_mul(value))
}

/// True if a character can be read now without waiting (only with
/// `-mapped_io`; `console_input_available`).
pub fn console_input_available(mapped_io: bool) -> bool {
    if !mapped_io {
        return false;
    }
    let mut descriptor: sys::PollFd = sys::PollFd {
        fd: 0,
        events: POLLIN,
        revents: 0,
    };
    // SAFETY: `descriptor` is one valid `pollfd` for the call; a zero
    // timeout means `poll` only looks.
    let ready: c_int = unsafe { sys::poll(&mut descriptor, 1, 0) };
    ready > 0
}

/// Read one typed character for the memory-mapped receiver
/// (`get_console_char`).  Control-C interrupts the program.
pub fn get_console_char() -> u8 {
    let byte: u8 = read_input_byte().unwrap_or(0);
    if byte == 3 {
        error("\nExecution interrupted\n");
        std::panic::panic_any(RunErrorUnwind);
    }
    byte
}

/// Print one character for the memory-mapped transmitter.
pub fn put_console_char(c: u8) {
    write_stdout(&[c]);
}

/// Read a REPL command line from input, without its newline, the way the
/// C engine's line editor does when the input is not a terminal; `None` at
/// end of input.
pub fn read_command_line() -> Option<Vec<u8>> {
    let mut line: Vec<u8> = Vec::new();
    loop {
        match read_input_byte() {
            None => return if line.is_empty() { None } else { Some(line) },
            Some(b'\n') => return Some(line),
            Some(byte) => line.push(byte),
        }
    }
}
