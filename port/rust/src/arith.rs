//! Machine arithmetic: the few operations where Rust and C disagree.
//!
//! A MIPS register holds 32 bits, which the instruction reads as a signed
//! (`i32`) or an unsigned (`u32`) number.  Most of the arithmetic is a
//! direct Rust spelling (`wrapping_add` for `addu`, `checked_add` for
//! `add`, which traps on overflow).  This module holds the cases that need
//! more care: converting a floating-point number to an integer, and the
//! 64-bit products of `mult`.

/// Convert a `double` to `int32_t` the way the C engine does on x86-64.
///
/// C leaves the conversion of NaN or of a value outside the `int32_t`
/// range undefined; on x86-64 the compiler emits `cvttsd2si`, which
/// answers `0x80000000` (`i32::MIN`) for all of them.  Rust's `as` would
/// saturate instead (and turn NaN into 0), so the port spells the C
/// engine's observed behavior out.  In range, the conversion truncates
/// toward zero.
///
/// ```
/// use spimulator::arith::f64_to_i32_like_c;
/// assert_eq!(f64_to_i32_like_c(-2.75), -2);
/// assert_eq!(f64_to_i32_like_c(2147483647.9), 2147483647);
/// assert_eq!(f64_to_i32_like_c(2147483648.0), i32::MIN);
/// assert_eq!(f64_to_i32_like_c(-2147483649.0), i32::MIN);
/// assert_eq!(f64_to_i32_like_c(f64::NAN), i32::MIN);
/// ```
pub fn f64_to_i32_like_c(value: f64) -> i32 {
    if value.is_nan() || value >= 2_147_483_648.0 || value <= -2_147_483_649.0 {
        i32::MIN
    } else {
        // In range, so `as` truncates toward zero exactly as C does.
        value as i32
    }
}

/// Convert a C `long` (64 bits) to `int`, keeping the low 32 bits, as the
/// C engine's `(int)` casts do.
///
/// ```
/// use spimulator::arith::low_32_bits;
/// assert_eq!(low_32_bits(0x1_0000_0005), 5);
/// assert_eq!(low_32_bits(-1), -1);
/// ```
pub fn low_32_bits(value: i64) -> i32 {
    value as i32
}

/// The 64-bit product of two signed words, as `mult` leaves it in HI:LO.
///
/// The C engine builds the product from 16-bit halves ("high-school
/// math") because C has no portable 64-bit multiply it trusts; Rust's
/// `i64` gives the same exact product directly.  Returns `(hi, lo)`.
///
/// ```
/// use spimulator::arith::signed_product;
/// assert_eq!(signed_product(-2, 3), (-1, -6));
/// assert_eq!(signed_product(i32::MIN, -1), (0, i32::MIN));
/// ```
pub fn signed_product(a: i32, b: i32) -> (i32, i32) {
    let product: i64 = i64::from(a) * i64::from(b);
    ((product >> 32) as i32, product as i32)
}

/// The 64-bit product of two unsigned words, as `multu` leaves it in HI:LO.
///
/// ```
/// use spimulator::arith::unsigned_product;
/// assert_eq!(unsigned_product(-1, -1), (-2, 1)); // 0xffffffff squared
/// ```
pub fn unsigned_product(a: i32, b: i32) -> (i32, i32) {
    let product: u64 = u64::from(a as u32) * u64::from(b as u32);
    ((product >> 32) as i32, product as i32)
}

/// HI:LO as one 64-bit value (`madd` and `msub` add to it).
pub fn join_hi_lo(hi: i32, lo: i32) -> u64 {
    (u64::from(hi as u32) << 32) | u64::from(lo as u32)
}

/// Split a 64-bit value into `(hi, lo)` words.
pub fn split_hi_lo(value: u64) -> (i32, i32) {
    ((value >> 32) as i32, value as i32)
}

/// Sign-extend the low 16 bits of `x` (the C engine's `sign_ex`).
///
/// ```
/// use spimulator::arith::sign_extend_16;
/// assert_eq!(sign_extend_16(0xffff), -1);
/// assert_eq!(sign_extend_16(0x7fff), 32767);
/// ```
pub fn sign_extend_16(x: i32) -> i32 {
    i32::from(x as i16)
}

/// C's `atoi`: skip white space, read an optional sign and decimal digits,
/// stop at the first other character, 0 if there are no digits.  Like
/// glibc's (which calls `strtol`), a value too large for `long` clamps to
/// the `long` range before the conversion to `int` keeps its low 32 bits.
///
/// ```
/// use spimulator::arith::c_atoi;
/// assert_eq!(c_atoi(b"  42abc"), 42);
/// assert_eq!(c_atoi(b"-7"), -7);
/// assert_eq!(c_atoi(b"x"), 0);
/// assert_eq!(c_atoi(b"4294967297"), 1);
/// ```
pub fn c_atoi(text: &[u8]) -> i32 {
    let mut i: usize = 0;
    while i < text.len() && matches!(text[i], b' ' | b'\t' | b'\n' | b'\r' | 0x0b | 0x0c) {
        i += 1;
    }
    let mut negative: bool = false;
    if i < text.len() && (text[i] == b'-' || text[i] == b'+') {
        negative = text[i] == b'-';
        i += 1;
    }
    let mut value: i128 = 0;
    while i < text.len() && text[i].is_ascii_digit() {
        value = value * 10 + i128::from(text[i] - b'0');
        value = value.min(i128::from(i64::MAX) + 1);
        i += 1;
    }
    let signed: i128 = if negative { -value } else { value };
    let clamped: i64 = signed.clamp(i128::from(i64::MIN), i128::from(i64::MAX)) as i64;
    low_32_bits(clamped)
}

/// C's `atof` (`strtod` on the longest valid prefix, 0.0 if none).
///
/// Accepts what glibc's `strtod` accepts: white space, a sign, decimal
/// digits with an optional point and exponent, `inf`/`infinity`, `nan`
/// (optionally `nan(chars)`), and hexadecimal `0x1.8p3` forms.
///
/// ```
/// use spimulator::arith::c_atof;
/// assert_eq!(c_atof(b"3.25\n"), 3.25);
/// assert_eq!(c_atof(b"1.5e"), 1.5);
/// assert_eq!(c_atof(b"-0x1.8p1"), -3.0);
/// assert_eq!(c_atof(b"abc"), 0.0);
/// assert!(c_atof(b"nan").is_nan());
/// ```
pub fn c_atof(text: &[u8]) -> f64 {
    let mut i: usize = 0;
    while i < text.len() && matches!(text[i], b' ' | b'\t' | b'\n' | b'\r' | 0x0b | 0x0c) {
        i += 1;
    }
    let start: usize = i;
    let mut negative: bool = false;
    if i < text.len() && (text[i] == b'-' || text[i] == b'+') {
        negative = text[i] == b'-';
        i += 1;
    }
    let rest: &[u8] = &text[i..];
    let lower: Vec<u8> = rest.iter().take(8).map(u8::to_ascii_lowercase).collect();
    let magnitude: f64 = if lower.starts_with(b"inf") {
        f64::INFINITY
    } else if lower.starts_with(b"nan") {
        f64::NAN
    } else if lower.starts_with(b"0x")
        && rest.len() > 2
        && (rest[2].is_ascii_hexdigit()
            || (rest[2] == b'.' && rest.len() > 3 && rest[3].is_ascii_hexdigit()))
    {
        parse_hex_float(&rest[2..])
    } else {
        let length: usize = decimal_prefix_length(rest);
        if length == 0 {
            return if start < text.len() && negative {
                -0.0
            } else {
                0.0
            };
        }
        let digits: &str = std::str::from_utf8(&rest[..length]).unwrap_or("0");
        digits.parse::<f64>().unwrap_or(0.0)
    };
    if negative {
        -magnitude
    } else {
        magnitude
    }
}

/// The length of the longest prefix of `text` that is a decimal floating
/// point number without a sign: digits, an optional point and fraction
/// (at least one digit overall), and an exponent only if it has digits.
fn decimal_prefix_length(text: &[u8]) -> usize {
    let mut i: usize = 0;
    let mut digits: usize = 0;
    while i < text.len() && text[i].is_ascii_digit() {
        i += 1;
        digits += 1;
    }
    if i < text.len() && text[i] == b'.' {
        let mut j: usize = i + 1;
        let mut fraction: usize = 0;
        while j < text.len() && text[j].is_ascii_digit() {
            j += 1;
            fraction += 1;
        }
        if digits + fraction > 0 {
            i = j;
            digits += fraction;
        }
    }
    if digits == 0 {
        return 0;
    }
    if i < text.len() && (text[i] == b'e' || text[i] == b'E') {
        let mut j: usize = i + 1;
        if j < text.len() && (text[j] == b'+' || text[j] == b'-') {
            j += 1;
        }
        if j < text.len() && text[j].is_ascii_digit() {
            while j < text.len() && text[j].is_ascii_digit() {
                j += 1;
            }
            i = j;
        }
    }
    i
}

/// Parse the digits of a hexadecimal float after the `0x`: hex digits, an
/// optional point and hex fraction, an optional binary exponent `p±n`.
fn parse_hex_float(text: &[u8]) -> f64 {
    let mut mantissa: f64 = 0.0;
    let mut exponent: i64 = 0;
    let mut i: usize = 0;
    while i < text.len() && text[i].is_ascii_hexdigit() {
        mantissa = mantissa * 16.0 + f64::from(hex_value(text[i]));
        i += 1;
    }
    if i < text.len() && text[i] == b'.' {
        i += 1;
        while i < text.len() && text[i].is_ascii_hexdigit() {
            mantissa = mantissa * 16.0 + f64::from(hex_value(text[i]));
            exponent -= 4;
            i += 1;
        }
    }
    if i < text.len() && (text[i] == b'p' || text[i] == b'P') {
        let mut j: usize = i + 1;
        let mut negative: bool = false;
        if j < text.len() && (text[j] == b'+' || text[j] == b'-') {
            negative = text[j] == b'-';
            j += 1;
        }
        if j < text.len() && text[j].is_ascii_digit() {
            let mut power: i64 = 0;
            while j < text.len() && text[j].is_ascii_digit() {
                power = (power * 10 + i64::from(text[j] - b'0')).min(100_000);
                j += 1;
            }
            exponent += if negative { -power } else { power };
        }
    }
    let exponent_i32: i32 = i32::try_from(exponent.clamp(-100_000, 100_000)).unwrap_or(0);
    mantissa * 2.0_f64.powi(exponent_i32)
}

/// The value of a hexadecimal digit.
fn hex_value(c: u8) -> u32 {
    char::from(c).to_digit(16).unwrap_or(0)
}
