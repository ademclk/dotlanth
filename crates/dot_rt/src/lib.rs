#![forbid(unsafe_code)]

/// Returns true when the crate is linked and available.
pub fn is_available() -> bool {
    true
}

#[cfg(test)]
mod tests {
    use super::is_available;

    #[test]
    fn smoke() {
        assert!(is_available());
    }
}
