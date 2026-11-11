//! The float formatter against glibc's `printf`, the C engine's formatter.
//!
//! Compiles a five-line C program (`tests/cformat_oracle.c`) that prints
//! each value in the C engine's five float formats, feeds it about 60,000
//! doubles (edge values, random bit patterns, values near decimal
//! boundaries, single-precision values), and compares every line with
//! `spimulator::cformat`.  Skipped, with a message, when no C compiler is
//! installed.

use std::io::Write;
use std::path::PathBuf;
use std::process::{Command, Stdio};

use spimulator::cformat::{format_f, format_g, format_register_column};

/// A small deterministic generator (xorshift64*), so the test needs no
/// crate and checks the same values every run.
struct Xorshift(u64);

impl Xorshift {
    fn next(&mut self) -> u64 {
        self.0 ^= self.0 >> 12;
        self.0 ^= self.0 << 25;
        self.0 ^= self.0 >> 27;
        self.0.wrapping_mul(0x2545_f491_4f6c_dd1d)
    }
}

/// The values to check, as bit patterns.
fn test_values() -> Vec<u64> {
    let special: [f64; 30] = [
        0.0,
        -0.0,
        0.1 + 0.2,
        1.0 / 3.0,
        1.0,
        1e-5,
        1e-4,
        0.0001234,
        123456789012345678.0,
        1e17,
        1e18,
        1e19,
        5e-324,
        2.2250738585072014e-308,
        f64::MAX,
        f64::INFINITY,
        f64::NEG_INFINITY,
        f64::NAN,
        -f64::NAN,
        0.5,
        2.5,
        0.125,
        0.000000005,
        0.000000015,
        0.000000025,
        999999999999999999.0,
        9.5,
        1e-310,
        3.0e10,
        -7.25,
    ];
    let mut values: Vec<u64> = special.iter().map(|v: &f64| v.to_bits()).collect();
    let mut random: Xorshift = Xorshift(0x9e37_79b9_7f4a_7c15);
    for _ in 0..30_000 {
        values.push(random.next());
    }
    for exponent in -30..30 {
        for _ in 0..200 {
            let fraction: f64 = (random.next() >> 11) as f64 / (1u64 << 53) as f64;
            let value: f64 = (0.5 + 9.5 * fraction) * 10f64.powi(exponent);
            values.push(value.to_bits());
        }
    }
    for i in 1..8_000 {
        values.push((f64::from(i) / 8.0).to_bits());
        let single: f32 = f32::from_bits(random.next() as u32);
        values.push(f64::from(single).to_bits());
    }
    values
}

#[test]
fn formats_like_glibc_printf() {
    let manifest: PathBuf = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    let source: PathBuf = manifest.join("tests").join("cformat_oracle.c");
    let binary: PathBuf =
        std::env::temp_dir().join(format!("cformat_oracle_{}", std::process::id()));
    let compiled: std::io::Result<std::process::ExitStatus> = Command::new("cc")
        .arg("-O2")
        .arg("-o")
        .arg(&binary)
        .arg(&source)
        .status();
    match compiled {
        Ok(status) if status.success() => {}
        _ => {
            eprintln!("cformat_vs_c: no working C compiler; skipped");
            return;
        }
    }
    let values: Vec<u64> = test_values();
    let input: String = values.iter().map(|v: &u64| format!("{v:x}\n")).collect();
    // The values go through a file: writing them to a pipe while the
    // oracle fills its output pipe would deadlock.
    let input_path: PathBuf = binary.with_extension("in");
    std::fs::File::create(&input_path)
        .and_then(|mut file: std::fs::File| file.write_all(input.as_bytes()))
        .expect("write the values");
    let output: std::process::Output = Command::new(&binary)
        .stdin(Stdio::from(
            std::fs::File::open(&input_path).expect("reopen the values"),
        ))
        .output()
        .expect("run the C oracle");
    let _ = std::fs::remove_file(&binary);
    let _ = std::fs::remove_file(&input_path);
    let expected: String = String::from_utf8(output.stdout).expect("ASCII output");
    let mut mismatches: usize = 0;
    for (bits, c_line) in values.iter().zip(expected.lines()) {
        let x: f64 = f64::from_bits(*bits);
        let rust_line: String = format!(
            "{} {} {} {} [{}]",
            format_g(false, 18, x),
            format_f(8, x),
            format_g(false, 17, x),
            format_g(false, 6, x),
            format_register_column(x)
        );
        if rust_line != c_line {
            mismatches += 1;
            if mismatches < 10 {
                eprintln!("{bits:016x}: C {c_line:?}, Rust {rust_line:?}");
            }
        }
    }
    assert_eq!(expected.lines().count(), values.len(), "one line per value");
    assert_eq!(mismatches, 0, "lines that differ from glibc");
}
