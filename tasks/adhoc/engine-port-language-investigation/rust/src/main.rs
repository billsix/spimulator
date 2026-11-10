//! Rust port spike: same output as ref.c. Fixed-width types carry the
//! semantics; wrapping/checked ops make every overflow decision explicit.
use std::time::Instant;

fn p(name: &str, v: u32) {
    println!("{name:<22} 0x{v:08x}");
}

fn div_s(hi: &mut u32, lo: &mut u32, a: i32, b: i32) {
    if b != 0 && !(a == i32::MIN && b == -1) {
        *lo = (a / b) as u32; // Rust `/` truncates toward zero, like C
        *hi = (a % b) as u32;
    }
}

fn run_loop() -> u32 {
    let prog: [u32; 7] = [0x3c09002d, 0x3529c6c0, 0x24080000, 0x01094021, 0x2529ffff, 0x1520fffd, 0x0000000c];
    let mut r = [0u32; 32];
    let mut pc: u32 = 0;
    loop {
        let w = prog[(pc / 4) as usize];
        let (op, rs, rt, rd) = (w >> 26, ((w >> 21) & 31) as usize, ((w >> 16) & 31) as usize, ((w >> 11) & 31) as usize);
        let simm = (w as u16) as i16 as i32;
        let mut npc = pc.wrapping_add(4);
        match op {
            0x00 => match w & 63 {
                0x21 => r[rd] = r[rs].wrapping_add(r[rt]),
                0x0c => return r[8],
                _ => {}
            },
            0x0f => r[rt] = (w & 0xffff) << 16,
            0x0d => r[rt] = r[rs] | (w & 0xffff),
            0x09 => r[rt] = r[rs].wrapping_add(simm as u32),
            0x05 => {
                if r[rs] != r[rt] {
                    npc = pc.wrapping_add(4).wrapping_add((simm * 4) as u32);
                }
            }
            _ => return 0xdeadbeef,
        }
        r[0] = 0;
        pc = npc;
    }
}

fn main() {
    p("addu_wrap", 0x7fff_ffffu32.wrapping_add(1));
    p("add_overflow_traps", 0x7fff_ffffi32.checked_add(1).is_none() as u32);
    p("sra", (0x8000_0000u32 as i32 >> 4) as u32);
    p("srl", 0x8000_0000u32 >> 4);
    p("sllv_shift33", 1u32 << (33 & 0x1f));
    let m = -2i64 * 3;
    p("mult_-2x3_hi", (m >> 32) as u32);
    p("mult_-2x3_lo", m as u32);
    let mu = 0xffff_ffffu64 * 0xffff_ffffu64;
    p("multu_max_hi", (mu >> 32) as u32);
    p("multu_max_lo", mu as u32);
    let (mut hi, mut lo) = (0x1111_1111u32, 0x1111_1111u32);
    div_s(&mut hi, &mut lo, -7, 2);
    p("div_-7/2_lo", lo);
    p("div_-7/2_hi", hi);
    (hi, lo) = (0x1111_1111, 0x1111_1111);
    div_s(&mut hi, &mut lo, 5, 0);
    p("div_by_zero_lo", lo);
    (hi, lo) = (0x1111_1111, 0x1111_1111);
    div_s(&mut hi, &mut lo, i32::MIN, -1);
    p("div_min/-1_lo", lo);
    let byte: u8 = 0x80;
    p("lb_0x80", byte as i8 as i32 as u32);
    p("lbu_0x80", byte as u32);
    p("slt_-1<1", ((0xffff_ffffu32 as i32) < 1) as u32);
    p("sltu_-1<1", (0xffff_ffffu32 < 1) as u32);
    p("mtc1_bits_1.0f", f32::from_bits(0x3f80_0000).to_bits());
    let db = 1.0f64.to_bits();
    p("double_1.0_hi", (db >> 32) as u32);
    p("double_1.0_lo", db as u32);
    let total = 0.1f32 + 0.2f32;
    p("add.s_0.1+0.2_bits", total.to_bits());
    // `as` saturates (NaN -> 0) where C is UB; -2.7 truncates to -2 either way.
    p("cvt.w.s_-2.7", (-2.7f32) as i32 as u32);
    p("round.w.s_-2.7_spim", ((-2.7f32) as f64 + 0.5) as i32 as u32);
    println!("print_float(0.1f+0.2f) {:.8}", total as f64);
    // Rust has no %g: format the 18 significant digits by hand for this value.
    println!("print_double(0.1+0.2)  {}", fmt_g18(0.1f64 + 0.2f64));
    let t0 = Instant::now();
    let r = run_loop();
    let secs = t0.elapsed().as_secs_f64();
    p("loop_sum_1..3000000", r);
    eprintln!("loop seconds: {secs:.3}");
}

/// `%.18g` for a value in [1e-4, 1e18): 18 significant digits, trailing zeros stripped.
fn fmt_g18(x: f64) -> String {
    let exp = x.abs().log10().floor() as i32;
    let decimals = (17 - exp).max(0) as usize;
    let s = format!("{x:.decimals$}");
    if s.contains('.') { s.trim_end_matches('0').trim_end_matches('.').to_string() } else { s }
}
