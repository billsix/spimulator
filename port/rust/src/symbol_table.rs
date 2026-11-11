//! The symbol table: labels, their addresses, and the forward references
//! waiting for them (`symbol-table.c`).
//!
//! When the assembler meets `beq $t0, $t1, done` before `done:` is
//! defined, it cannot fill in the branch offset yet.  It records a *use*
//! of `done` and finishes the instruction later, when the label is
//! defined.  Data can wait the same way (`.word done`).
//!
//! The C engine keeps labels in a hash table of 8191 chains.  The order of
//! the chains is visible: `print_symbols` and the "undefined symbols"
//! message list labels in table order.  So the port keeps the same hash
//! function and the same chains, newest label first in each chain.

use crate::instruction::{InstRef, Instruction};
use crate::machine::Machine;
use crate::memory::DATA_BOT;

/// An index into [`SymbolTable::labels`].  Labels are never freed while a
/// program is loaded (instructions keep referring to them), so an index
/// stays valid until the machine is reinitialized.
pub type LabelId = usize;

/// The number of hash chains (`LABEL_HASH_TABLE_SIZE`).
const HASH_TABLE_SIZE: usize = 8191;

/// What a forward reference is waiting to patch.
#[derive(Clone, Debug)]
pub enum UseSite {
    /// The `width` bytes of data at the use's address hold the label's
    /// address plus `offset` (`.word label+4`).
    Data {
        /// The constant added to the label's address.
        offset: i32,
        /// How many bytes were stored: 1, 2 or 4.
        width: i32,
    },
    /// An instruction whose immediate or target field needs the label.
    /// It is shared with the text segment, so patching it here patches
    /// the instruction in memory.
    Inst(InstRef),
}

/// One forward reference (`label_use`).
#[derive(Clone, Debug)]
pub struct LabelUse {
    /// What to patch.
    pub site: UseSite,
    /// The address of the instruction or datum.
    pub addr: u32,
}

/// A label (`struct label`).
#[derive(Clone, Debug, Default)]
pub struct Label {
    /// The label's name.
    pub name: String,
    /// Its address, or 0 while it is undefined.  For a constant
    /// (`X = 5`), its value.
    pub addr: i64,
    /// Declared with `.globl`: visible to the files loaded after this one.
    pub global: bool,
    /// Placed in the small data area addressed off `$gp` (`.extern`).
    pub gp: bool,
    /// A constant (`X = 5`), not an address.
    pub constant: bool,
    /// Forward references waiting for the label, most recent last.
    pub uses: Vec<LabelUse>,
}

impl Label {
    /// True once the label has an address (`SYMBOL_IS_DEFINED`).
    pub fn is_defined(&self) -> bool {
        self.addr != 0
    }
}

/// The table of labels.
#[derive(Clone, Debug)]
pub struct SymbolTable {
    /// Every label created since the table was last cleared.
    pub labels: Vec<Label>,
    /// The hash chains; each chain lists its labels newest first.
    chains: Vec<Vec<LabelId>>,
    /// The labels of the file being assembled that are not global, newest
    /// first (`local_labels`); they are removed when the file ends.
    local_labels: Vec<LabelId>,
}

impl Default for SymbolTable {
    fn default() -> Self {
        SymbolTable {
            labels: Vec::new(),
            chains: vec![Vec::new(); HASH_TABLE_SIZE],
            local_labels: Vec::new(),
        }
    }
}

/// The C engine's hash of a label name (`get_hash`).
///
/// It starts from the length and folds in each character, multiplying by
/// 613, keeps 30 bits, and takes the remainder by the table size.  A
/// character is first sign-extended, as C's `(unsigned)(char)` does.
///
/// ```
/// use spimulator::symbol_table::hash_name;
/// assert_eq!(hash_name("a"), 710); // 1 * 613 + 97
/// assert_eq!(hash_name("main"), 6536);
/// ```
pub fn hash_name(name: &str) -> usize {
    let bytes: &[u8] = name.as_bytes();
    let mut hash: u32 = bytes.len() as u32;
    for byte in bytes {
        let sign_extended: u32 = i32::from(*byte as i8) as u32;
        hash = hash.wrapping_mul(613).wrapping_add(sign_extended);
    }
    hash &= (1 << 30) - 1;
    (hash % HASH_TABLE_SIZE as u32) as usize
}

impl SymbolTable {
    /// Forget every label (`initialize_symbol_table`).
    pub fn clear(&mut self) {
        *self = SymbolTable::default();
    }

    /// The label named `name`, if the table has one (`label_is_defined`,
    /// which, despite its name, does not check that it is defined).
    pub fn find(&self, name: &str) -> Option<LabelId> {
        self.chains[hash_name(name)]
            .iter()
            .copied()
            .find(|id: &LabelId| self.labels[*id].name == name)
    }

    /// The label named `name`, created undefined if it is new
    /// (`lookup_label`).  A new label goes at the head of its chain.
    pub fn lookup(&mut self, name: &str) -> LabelId {
        if let Some(id) = self.find(name) {
            return id;
        }
        let id: LabelId = self.labels.len();
        self.labels.push(Label {
            name: name.to_string(),
            ..Label::default()
        });
        self.chains[hash_name(name)].insert(0, id);
        id
    }

    /// The label with index `id`.
    pub fn label(&self, id: LabelId) -> &Label {
        &self.labels[id]
    }

    /// The label with index `id`, to change.
    pub fn label_mut(&mut self, id: LabelId) -> &mut Label {
        &mut self.labels[id]
    }

    /// Every label in table order: chain by chain, newest first in each.
    pub fn in_table_order(&self) -> Vec<LabelId> {
        self.chains.iter().flatten().copied().collect()
    }
}

impl Machine {
    /// Define label `name` at `address` (`record_label`).  If
    /// `resolve_uses`, patch the forward references waiting for it now.
    pub fn record_label(&mut self, name: &str, address: u32, resolve_uses: bool) -> LabelId {
        let id: LabelId = self.symbols.lookup(name);
        if !self.symbols.label(id).gp {
            if self.symbols.label(id).addr != 0 {
                self.parse_error("Label is defined for the second time");
                return id;
            }
            self.symbols.label_mut(id).addr = i64::from(address);
        }
        let global: bool = self.symbols.label(id).global;
        let label_name: String = self.symbols.label(id).name.clone();
        self.listing_label_def(address, &label_name, global);
        if resolve_uses {
            self.resolve_label_uses(id);
        }
        if !self.symbols.label(id).global {
            self.symbols.local_labels.insert(0, id);
        }
        id
    }

    /// Make label `name` global (`.globl`), creating it if need be.
    pub fn make_label_global(&mut self, name: &str) -> LabelId {
        let id: LabelId = self.symbols.lookup(name);
        self.symbols.label_mut(id).global = true;
        id
    }

    /// Record that `instruction`, about to be stored, uses the undefined
    /// label `id` (`record_inst_uses_symbol`).  An instruction assembled
    /// into the data segment is stored as a word right away, so the use
    /// keeps its own copy to encode again once the label is known.
    pub fn record_inst_uses_symbol(&mut self, instruction: &InstRef, id: LabelId) {
        let lab_use: LabelUse = if self.asm.data_dir {
            let copy: Instruction = instruction.borrow().clone();
            LabelUse {
                site: UseSite::Inst(copy.into_ref()),
                addr: self.current_data_pc(),
            }
        } else {
            LabelUse {
                site: UseSite::Inst(instruction.clone()),
                addr: self.current_text_pc(),
            }
        };
        let addr: u32 = lab_use.addr;
        self.symbols.label_mut(id).uses.push(lab_use);
        let name: String = self.symbols.label(id).name.clone();
        self.listing_forward_ref(addr, &name);
    }

    /// Record that the `width`-byte datum at `location` holds the undefined
    /// label `id` plus `offset` (`record_data_uses_symbol`).
    pub fn record_data_uses_symbol(&mut self, location: u32, id: LabelId, offset: i32, width: i32) {
        self.symbols.label_mut(id).uses.push(LabelUse {
            site: UseSite::Data { offset, width },
            addr: location,
        });
        let name: String = self.symbols.label(id).name.clone();
        self.listing_forward_ref(location, &name);
    }

    /// Patch every forward reference to the newly defined label `id`
    /// (`resolve_label_uses`), most recent first.
    pub fn resolve_label_uses(&mut self, id: LabelId) {
        let uses: Vec<LabelUse> = std::mem::take(&mut self.symbols.label_mut(id).uses);
        let label_addr: i64 = self.symbols.label(id).addr;
        for lab_use in uses.into_iter().rev() {
            match &lab_use.site {
                UseSite::Data { offset, width } => {
                    let value: i32 = (label_addr as i32).wrapping_add(*offset);
                    self.write_data_value(lab_use.addr, value, *width);
                }
                UseSite::Inst(instruction) => {
                    self.resolve_a_label_sub(instruction, lab_use.addr);
                    if lab_use.addr >= DATA_BOT && lab_use.addr < self.mem.stack_bot {
                        let encoding: i32 = instruction.borrow().encode();
                        self.mem_write_word(lab_use.addr, encoding);
                    }
                }
            }
            let name: String = self.symbols.label(id).name.clone();
            let value: i32 = self.symbols.label(id).addr as i32;
            self.listing_forward_resolved(lab_use.addr, &name, value);
        }
    }

    /// Store the low `width` bytes of `value` at `at`, little-endian, one
    /// byte at a time (`write_data_value`; `at` need not be aligned).
    fn write_data_value(&mut self, at: u32, value: i32, width: i32) {
        for i in 0..width {
            let byte: i32 = ((value as u32 >> (8 * i)) & 0xff) as i32;
            self.mem_write_byte(at.wrapping_add(i as u32), byte);
        }
    }

    /// Remove the labels of the file just assembled that are not global,
    /// so the next file cannot see them (`flush_local_labels`).  With
    /// `warn_undefined`, warn about the ones never defined.
    pub fn flush_local_labels(&mut self, warn_undefined: bool) {
        let locals: Vec<LabelId> = std::mem::take(&mut self.symbols.local_labels);
        for id in locals {
            let name: String = self.symbols.label(id).name.clone();
            let Some(entry) = self.symbols.find(&name) else {
                continue;
            };
            let chain: &mut Vec<LabelId> = &mut self.symbols.chains[hash_name(&name)];
            if let Some(position) = chain.iter().position(|x: &LabelId| *x == entry) {
                chain.remove(position);
                let label: &Label = self.symbols.label(entry);
                if warn_undefined && label.addr == 0 && !label.constant {
                    crate::machine::error(&format!(
                        "Warning: local symbol {} was not defined\n",
                        label.name
                    ));
                }
            }
        }
    }

    /// The address of label `symbol`, or 0 if it is undefined
    /// (`find_symbol_address`).  Like the C engine, this creates the label
    /// if it did not exist.
    pub fn find_symbol_address(&mut self, symbol: &str) -> u32 {
        let id: LabelId = self.symbols.lookup(symbol);
        self.symbols.label(id).addr as u32
    }

    /// Print every label (`print_symbols`), in table order.
    pub fn print_symbols(&self) {
        let mut text: String = String::new();
        for id in self.symbols.in_table_order() {
            let label: &Label = self.symbols.label(id);
            let prefix: &str = if label.global { "g\t" } else { "\t" };
            text.push_str(&format!(
                "{prefix}{} at 0x{:08x}\n",
                label.name, label.addr as u32
            ));
        }
        crate::machine::error(&text);
    }

    /// The names of the undefined labels, one per line, or `None` if every
    /// label is defined (`undefined_symbol_string`).
    pub fn undefined_symbol_string(&self) -> Option<String> {
        let mut text: String = String::new();
        for id in self.symbols.in_table_order() {
            let label: &Label = self.symbols.label(id);
            if label.addr == 0 {
                text.push_str(&label.name);
                text.push('\n');
            }
        }
        if text.is_empty() {
            None
        } else {
            Some(text)
        }
    }
}
