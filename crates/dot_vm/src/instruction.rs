#![forbid(unsafe_code)]

use crate::{Reg, Value};
use dot_ops::{DeterminismClass, SourceRef, SyscallId, syscall_name, syscall_spec};
use dot_sec::Capability;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Opcode {
    Halt,
    LoadConst,
    Mov,
    Syscall(SyscallId),
}

impl Opcode {
    pub fn metadata(self) -> Option<OpcodeMetadata> {
        match self {
            Self::Halt | Self::LoadConst | Self::Mov => {
                Some(OpcodeMetadata::new(DeterminismClass::Pure, None))
            }
            Self::Syscall(id) => syscall_spec(id).map(OpcodeMetadata::from),
        }
    }
}

impl std::fmt::Display for Opcode {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Halt => f.write_str("halt"),
            Self::LoadConst => f.write_str("load_const"),
            Self::Mov => f.write_str("mov"),
            Self::Syscall(id) => {
                if syscall_spec(*id).is_some() {
                    f.write_str(syscall_name(*id))
                } else {
                    write!(f, "syscall#{}", id.0)
                }
            }
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct OpcodeMetadata {
    classification: DeterminismClass,
    required_capability: Option<Capability>,
}

impl OpcodeMetadata {
    pub const fn new(
        classification: DeterminismClass,
        required_capability: Option<Capability>,
    ) -> Self {
        Self {
            classification,
            required_capability,
        }
    }

    pub const fn classification(self) -> DeterminismClass {
        self.classification
    }

    pub const fn required_capability(self) -> Option<Capability> {
        self.required_capability
    }
}

impl From<dot_ops::SyscallSpec> for OpcodeMetadata {
    fn from(value: dot_ops::SyscallSpec) -> Self {
        Self::new(value.classification(), value.required_capability())
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum StrictDeterminismError {
    MissingClassification {
        opcode: Opcode,
    },
    UnsupportedInStrictMode {
        opcode: Opcode,
        classification: DeterminismClass,
    },
}

impl std::fmt::Display for StrictDeterminismError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::MissingClassification {
                opcode: Opcode::Syscall(id),
            } => write!(
                f,
                "determinism violation: strict mode requires classification for syscall id `{}`",
                id.0
            ),
            Self::MissingClassification { opcode } => write!(
                f,
                "determinism violation: strict mode requires classification for opcode `{opcode}`"
            ),
            Self::UnsupportedInStrictMode {
                opcode: Opcode::Syscall(id),
                ..
            } => write!(
                f,
                "determinism violation: strict mode does not support syscall `{}`",
                syscall_name(*id)
            ),
            Self::UnsupportedInStrictMode {
                opcode,
                classification,
            } => write!(
                f,
                "determinism violation: strict mode does not support opcode `{opcode}` classified as `{}`",
                classification.as_str()
            ),
        }
    }
}

impl std::error::Error for StrictDeterminismError {}

pub fn in_scope_opcodes() -> Vec<Opcode> {
    let mut opcodes = vec![Opcode::Halt, Opcode::LoadConst, Opcode::Mov];
    opcodes.extend(
        dot_ops::registered_syscalls()
            .iter()
            .map(|spec| Opcode::Syscall(spec.id())),
    );
    opcodes
}

pub fn validate_strict_determinism(program: &[Instruction]) -> Result<(), StrictDeterminismError> {
    for instruction in program {
        let opcode = instruction.opcode();
        let Some(metadata) = instruction.determinism_metadata() else {
            return Err(StrictDeterminismError::MissingClassification { opcode });
        };
        if !metadata.classification().supports_strict_mode() {
            return Err(StrictDeterminismError::UnsupportedInStrictMode {
                opcode,
                classification: metadata.classification(),
            });
        }
    }

    Ok(())
}

#[derive(Clone, Debug, PartialEq)]
pub enum Instruction {
    Halt,
    LoadConst {
        dst: Reg,
        value: Value,
    },
    Mov {
        dst: Reg,
        src: Reg,
    },
    Syscall {
        id: SyscallId,
        args: Vec<Reg>,
        results: Vec<Reg>,
        source: Option<SourceRef>,
    },
}

impl Instruction {
    pub const fn opcode(&self) -> Opcode {
        match self {
            Self::Halt => Opcode::Halt,
            Self::LoadConst { .. } => Opcode::LoadConst,
            Self::Mov { .. } => Opcode::Mov,
            Self::Syscall { id, .. } => Opcode::Syscall(*id),
        }
    }

    pub fn determinism_metadata(&self) -> Option<OpcodeMetadata> {
        self.opcode().metadata()
    }
}
