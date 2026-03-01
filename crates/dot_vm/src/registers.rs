#![forbid(unsafe_code)]

use crate::{Value, VmError};

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct Reg(pub u16);

impl Reg {
    pub const fn as_usize(self) -> usize {
        self.0 as usize
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct RegisterFile {
    registers: Vec<Value>,
}

impl RegisterFile {
    pub fn new(register_count: usize) -> Self {
        Self {
            registers: vec![Value::Unit; register_count],
        }
    }

    pub fn len(&self) -> usize {
        self.registers.len()
    }

    pub fn is_empty(&self) -> bool {
        self.registers.is_empty()
    }

    pub fn get(&self, reg: Reg) -> Result<&Value, VmError> {
        self.registers
            .get(reg.as_usize())
            .ok_or(VmError::RegisterOutOfBounds {
                reg,
                register_count: self.registers.len(),
            })
    }

    pub fn set(&mut self, reg: Reg, value: Value) -> Result<(), VmError> {
        let register_count = self.registers.len();
        let slot = self
            .registers
            .get_mut(reg.as_usize())
            .ok_or(VmError::RegisterOutOfBounds {
                reg,
                register_count,
            })?;
        *slot = value;
        Ok(())
    }
}
