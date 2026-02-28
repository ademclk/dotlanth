#![forbid(unsafe_code)]

mod diagnostics;
mod model;
mod parser;
mod validate;

use std::path::{Path, PathBuf};

pub use diagnostics::{Diagnostic, LoadError, Span};
pub use model::{Api, Document, Metadata, Response, Route, Server, Spanned};

/// Loads a `dot` file from disk, parses it, and runs semantic validation.
pub fn load_and_validate(path: impl AsRef<Path>) -> Result<Document, LoadError> {
    let path = path.as_ref();
    let source = std::fs::read_to_string(path).map_err(|source| LoadError::Io {
        path: path.to_path_buf(),
        source,
    })?;
    parse_and_validate(&source, path)
}

/// Parses and validates a `dot` document from a source string.
pub fn parse_and_validate(
    source: &str,
    source_path: impl Into<PathBuf>,
) -> Result<Document, LoadError> {
    let source_path = source_path.into();
    let document = parser::parse(source, &source_path)?;
    validate::validate(&document, &source_path)?;
    Ok(document)
}
