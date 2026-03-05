use crate::schema::BundleSection;
use std::fmt;
use std::path::PathBuf;

#[derive(Debug)]
pub enum BundleWriterError {
    AlreadyFinalized,
    BundleAlreadyExists {
        path: PathBuf,
    },
    MissingManifestSection {
        section: BundleSection,
    },
    MissingManifestInput {
        key: String,
    },
    Io {
        action: &'static str,
        path: PathBuf,
        source: std::io::Error,
    },
    ManifestSerialize {
        source: serde_json::Error,
    },
}

impl fmt::Display for BundleWriterError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::AlreadyFinalized => f.write_str("bundle writer has already been finalized"),
            Self::BundleAlreadyExists { path } => {
                write!(f, "bundle directory already exists: {}", path.display())
            }
            Self::MissingManifestSection { section } => {
                write!(
                    f,
                    "bundle manifest is missing required section `{}`",
                    section.key()
                )
            }
            Self::MissingManifestInput { key } => {
                write!(f, "bundle manifest is missing required input `{key}`")
            }
            Self::Io {
                action,
                path,
                source,
            } => {
                write!(f, "failed to {action} `{}`: {source}", path.display())
            }
            Self::ManifestSerialize { source } => {
                write!(f, "failed to serialize bundle manifest: {source}")
            }
        }
    }
}

impl std::error::Error for BundleWriterError {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        match self {
            Self::Io { source, .. } => Some(source),
            Self::ManifestSerialize { source } => Some(source),
            _ => None,
        }
    }
}
