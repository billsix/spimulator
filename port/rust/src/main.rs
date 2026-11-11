//! The `spimulator-rust` command: see [`spimulator::cli`].

fn main() {
    let status: i32 = spimulator::cli::main();
    std::process::exit(status);
}
