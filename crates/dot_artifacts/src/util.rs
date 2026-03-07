use crate::error::BundleWriterError;
use sha2::{Digest, Sha256};
use std::fs;
use std::io::Read;
use std::path::{Path, PathBuf};
use std::time::{SystemTime, UNIX_EPOCH};

pub(crate) fn create_dir_all(path: &Path, action: &'static str) -> Result<(), BundleWriterError> {
    fs::create_dir_all(path).map_err(|source| BundleWriterError::Io {
        action,
        path: path.to_path_buf(),
        source,
    })
}

pub(crate) fn write_bytes(
    path: &Path,
    action: &'static str,
    bytes: &[u8],
) -> Result<(), BundleWriterError> {
    if let Some(parent) = path.parent() {
        create_dir_all(parent, "create artifact parent directory")?;
    }
    fs::write(path, bytes).map_err(|source| BundleWriterError::Io {
        action,
        path: path.to_path_buf(),
        source,
    })
}

pub(crate) fn build_staging_dir(bundle_dir: &Path, attempt: u32) -> PathBuf {
    let parent = bundle_dir
        .parent()
        .map(Path::to_path_buf)
        .unwrap_or_else(|| PathBuf::from("."));
    let base_name = bundle_dir
        .file_name()
        .and_then(|name| name.to_str())
        .unwrap_or("bundle");
    let nonce = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|duration| duration.as_nanos())
        .unwrap_or_default();

    if attempt == 0 {
        parent.join(format!(
            ".{base_name}.staging-{}-{nonce}",
            std::process::id()
        ))
    } else {
        parent.join(format!(
            ".{base_name}.staging-{}-{nonce}-{attempt}",
            std::process::id()
        ))
    }
}

pub(crate) fn now_ms() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|duration| duration.as_millis() as u64)
        .unwrap_or_default()
}

pub(crate) fn sha256_hex(bytes: &[u8]) -> String {
    let digest = Sha256::digest(bytes);
    digest_to_hex(digest)
}

pub(crate) fn sha256_hex_file(
    path: &Path,
    action: &'static str,
) -> Result<(String, u64), BundleWriterError> {
    let mut file = fs::File::open(path).map_err(|source| BundleWriterError::Io {
        action,
        path: path.to_path_buf(),
        source,
    })?;
    let mut hasher = Sha256::new();
    let mut buffer = [0_u8; 8 * 1024];
    let mut total_bytes = 0_u64;

    loop {
        let read = file
            .read(&mut buffer)
            .map_err(|source| BundleWriterError::Io {
                action,
                path: path.to_path_buf(),
                source,
            })?;
        if read == 0 {
            break;
        }
        hasher.update(&buffer[..read]);
        total_bytes += read as u64;
    }

    Ok((digest_to_hex(hasher.finalize()), total_bytes))
}

fn digest_to_hex(digest: impl AsRef<[u8]>) -> String {
    let digest = digest.as_ref();
    let mut out = String::with_capacity(digest.len() * 2);
    for &byte in digest {
        out.push(hex_digit((byte >> 4) & 0x0f));
        out.push(hex_digit(byte & 0x0f));
    }
    out
}

const fn hex_digit(value: u8) -> char {
    match value {
        0..=9 => (b'0' + value) as char,
        10..=15 => (b'a' + (value - 10)) as char,
        _ => '0',
    }
}
