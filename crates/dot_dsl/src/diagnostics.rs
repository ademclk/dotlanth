use std::fmt;
use std::path::PathBuf;

/// Source span in 1-based coordinates.
///
/// `column` and `length` are measured in UTF-8 bytes on the line.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Span {
    pub line: usize,
    pub column: usize,
    pub length: usize,
}

impl Span {
    pub const fn new(line: usize, column: usize, length: usize) -> Self {
        Self {
            line,
            column,
            length,
        }
    }
}

/// Validation or parse diagnostic with a stable semantic path.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Diagnostic {
    pub semantic_path: String,
    pub span: Span,
    pub message: String,
}

impl Diagnostic {
    pub fn new(semantic_path: impl Into<String>, span: Span, message: impl Into<String>) -> Self {
        Self {
            semantic_path: semantic_path.into(),
            span,
            message: message.into(),
        }
    }
}

#[derive(Debug)]
pub enum LoadError {
    Io {
        path: PathBuf,
        source: std::io::Error,
    },
    Diagnostics {
        path: PathBuf,
        diagnostics: Vec<Diagnostic>,
    },
}

impl LoadError {
    pub fn diagnostics(path: impl Into<PathBuf>, diagnostics: Vec<Diagnostic>) -> Self {
        Self::Diagnostics {
            path: path.into(),
            diagnostics,
        }
    }

    pub fn as_diagnostics(&self) -> Option<&[Diagnostic]> {
        match self {
            Self::Diagnostics { diagnostics, .. } => Some(diagnostics),
            Self::Io { .. } => None,
        }
    }
}

impl fmt::Display for LoadError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Io { path, source } => {
                write!(f, "failed to read {}: {source}", path.display())
            }
            Self::Diagnostics { path, diagnostics } => {
                for (index, diagnostic) in diagnostics.iter().enumerate() {
                    if index > 0 {
                        writeln!(f)?;
                    }
                    write!(
                        f,
                        "{}:{}:{}:{} {} | {}",
                        path.display(),
                        diagnostic.span.line,
                        diagnostic.span.column,
                        diagnostic.span.length,
                        diagnostic.semantic_path,
                        diagnostic.message
                    )?;
                }
                Ok(())
            }
        }
    }
}

impl std::error::Error for LoadError {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        match self {
            Self::Io { source, .. } => Some(source),
            Self::Diagnostics { .. } => None,
        }
    }
}
