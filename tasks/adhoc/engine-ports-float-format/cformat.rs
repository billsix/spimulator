// glibc-compatible %.<p>f, %.<p>g and %#.<p>g for f64, built on Rust's own
// exactly rounded `{:.N}` and `{:.Ne}` formatting.
use std::io::{self, BufRead, Write};

fn special(x: f64) -> Option<String> {
    if x.is_nan() {
        Some(if x.is_sign_negative() { "-nan" } else { "nan" }.to_string())
    } else if x.is_infinite() {
        Some(if x < 0.0 { "-inf" } else { "inf" }.to_string())
    } else {
        None
    }
}

fn format_f(p: usize, x: f64) -> String {
    special(x).unwrap_or_else(|| format!("{:.*}", p, x))
}

fn strip_zeros(s: &str) -> String {
    if !s.contains('.') {
        return s.to_string();
    }
    s.trim_end_matches('0').trim_end_matches('.').to_string()
}

fn format_g(alt: bool, p0: usize, x: f64) -> String {
    if let Some(s) = special(x) {
        return s;
    }
    let p: usize = p0.max(1);
    // C's rule: X is the exponent %e would print at precision P - 1.
    let e_form: String = format!("{:.*e}", p - 1, x);
    let (mantissa, exp_str) = e_form.split_once('e').unwrap();
    let exponent: i32 = exp_str.parse().unwrap();
    let body: String = if exponent < -4 || exponent >= p as i32 {
        let m: String = if alt {
            if mantissa.contains('.') { mantissa.to_string() } else { format!("{mantissa}.") }
        } else {
            strip_zeros(mantissa)
        };
        let sign: char = if exponent < 0 { '-' } else { '+' };
        format!("{m}e{sign}{:02}", exponent.abs())
    } else {
        let decimals: usize = (p as i32 - 1 - exponent) as usize;
        let fixed: String = format!("{:.*}", decimals, x);
        if alt {
            if fixed.contains('.') { fixed } else { format!("{fixed}.") }
        } else {
            strip_zeros(&fixed)
        }
    };
    body
}

fn main() {
    let stdin = io::stdin();
    let mut out = io::BufWriter::new(io::stdout().lock());
    for line in stdin.lock().lines() {
        let bits: u64 = u64::from_str_radix(line.unwrap().trim(), 16).unwrap();
        let d: f64 = f64::from_bits(bits);
        writeln!(
            out,
            "{} {} {} {} [{:<13}]",
            format_g(false, 18, d),
            format_f(8, d),
            format_g(false, 17, d),
            format_g(false, 6, d),
            format_g(true, 6, d)
        )
        .unwrap();
    }
}
