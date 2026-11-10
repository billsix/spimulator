//! C `printf` number formatting, byte-identical to glibc.
//!
//! The C engine prints floating-point numbers with `printf`: `%.8f` for
//! `print_float`, `%.18g` for `print_double`, `%g` for the REPL's `print
//! $f0`, `%#-13.6g` in the register dump and `%.17g` in `-print-ast-json`.
//! Rust's `{}` formatting picks different digits for these, so the port
//! rebuilds C's rules on top of Rust's exactly rounded `{:.N}` (fixed) and
//! `{:.Ne}` (scientific) formatting.  Both round the exact binary value of
//! the number to the nearest decimal, ties to even, which is what glibc
//! does; what is left to write by hand is C's choice between the two
//! notations and its spelling of infinities and NaNs.
//!
//! These functions were checked against glibc on about 158,000 values
//! (`tests/cformat_vs_c.rs` repeats that check whenever a C compiler is
//! available).

/// C's spelling of the values that have no digits: `inf`, `-inf`, `nan`,
/// `-nan` (glibc prints the sign of a NaN).
fn special(x: f64) -> Option<String> {
    if x.is_nan() {
        let text: &str = if x.is_sign_negative() { "-nan" } else { "nan" };
        Some(text.to_string())
    } else if x.is_infinite() {
        let text: &str = if x < 0.0 { "-inf" } else { "inf" };
        Some(text.to_string())
    } else {
        None
    }
}

/// `printf("%.<precision>f", x)`: fixed notation with `precision` digits
/// after the decimal point.
///
/// ```
/// use spimulator::cformat::format_f;
/// assert_eq!(format_f(8, 0.1f32 as f64), "0.10000000");
/// assert_eq!(format_f(8, (0.1f32 + 0.2f32) as f64), "0.30000001");
/// assert_eq!(format_f(2, -0.0), "-0.00");
/// assert_eq!(format_f(8, f64::NAN), "nan");
/// ```
pub fn format_f(precision: usize, x: f64) -> String {
    special(x).unwrap_or_else(|| format!("{:.*}", precision, x))
}

/// Remove the trailing zeros of a fraction, and then the decimal point if
/// nothing is left after it (`%g` does this unless the `#` flag is given).
fn strip_zeros(text: &str) -> String {
    if !text.contains('.') {
        return text.to_string();
    }
    text.trim_end_matches('0').trim_end_matches('.').to_string()
}

/// `printf("%.<precision>g", x)`, or `%#.<precision>g` when `alternate`.
///
/// C's rule: let X be the exponent that `%e` would print at precision
/// `precision - 1`.  If X is below -4 or at least `precision`, use
/// scientific notation; otherwise use fixed notation with `precision - 1 -
/// X` decimals.  Then, unless `#` was given, drop trailing zeros.
///
/// ```
/// use spimulator::cformat::format_g;
/// assert_eq!(format_g(false, 18, 0.1 + 0.2), "0.300000000000000044");
/// assert_eq!(format_g(false, 6, 1e-5), "1e-05");
/// assert_eq!(format_g(false, 6, 100000.0), "100000");
/// assert_eq!(format_g(false, 6, 1000000.0), "1e+06");
/// assert_eq!(format_g(true, 6, 0.0), "0.00000");
/// assert_eq!(format_g(false, 6, -f64::NAN), "-nan");
/// ```
pub fn format_g(alternate: bool, precision: usize, x: f64) -> String {
    if let Some(text) = special(x) {
        return text;
    }
    let precision: usize = precision.max(1);
    let e_form: String = format!("{:.*e}", precision - 1, x);
    let (mantissa, exponent_text): (&str, &str) = e_form
        .split_once('e')
        .expect("Rust's {:e} formatting always writes an exponent");
    let exponent: i32 = exponent_text
        .parse()
        .expect("Rust's {:e} exponent is a decimal integer");
    let precision_i32: i32 = i32::try_from(precision).unwrap_or(i32::MAX);
    if exponent < -4 || exponent >= precision_i32 {
        let digits: String = if alternate {
            if mantissa.contains('.') {
                mantissa.to_string()
            } else {
                format!("{mantissa}.")
            }
        } else {
            strip_zeros(mantissa)
        };
        let sign: char = if exponent < 0 { '-' } else { '+' };
        format!("{digits}e{sign}{:02}", exponent.abs())
    } else {
        let decimals: usize = usize::try_from(precision_i32 - 1 - exponent)
            .expect("the fixed branch has exponent < precision");
        let fixed: String = format!("{:.*}", decimals, x);
        if alternate {
            if fixed.contains('.') {
                fixed
            } else {
                format!("{fixed}.")
            }
        } else {
            strip_zeros(&fixed)
        }
    }
}

/// `printf("%g", x)`: six significant digits.
///
/// ```
/// use spimulator::cformat::format_g6;
/// assert_eq!(format_g6(3.14159265), "3.14159");
/// ```
pub fn format_g6(x: f64) -> String {
    format_g(false, 6, x)
}

/// `printf("%#-13.6g", x)`: the register dump's column, six significant
/// digits with trailing zeros kept, left-justified in 13 characters.
///
/// ```
/// use spimulator::cformat::format_register_column;
/// assert_eq!(format_register_column(0.0), "0.00000      ");
/// assert_eq!(format_register_column(1.5), "1.50000      ");
/// ```
pub fn format_register_column(x: f64) -> String {
    format!("{:<13}", format_g(true, 6, x))
}
