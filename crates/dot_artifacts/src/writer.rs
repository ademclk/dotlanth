use crate::error::BundleWriterError;
use crate::schema::{
    BundleManifestV1, BundleSection, DEFAULT_INPUT_UNAVAILABLE_MESSAGE,
    DEFAULT_SECTION_UNAVAILABLE_MESSAGE, DEFAULT_UNAVAILABLE_CODE, ENTRY_DOT_FILE,
    INPUT_ENTRY_DOT_KEY, INPUTS_DIR, MANIFEST_FILE, SectionErrorMarker, SectionStatus, TRACE_FILE,
};
use crate::util::{build_staging_dir, create_dir_all, sha256_hex, sha256_hex_file, write_bytes};
use serde::Serialize;
use std::fs;
use std::path::{Path, PathBuf};

pub struct BundleWriter {
    bundle_dir: PathBuf,
    staging_dir: PathBuf,
    manifest: BundleManifestV1,
    finalized: bool,
}

impl BundleWriter {
    pub fn new(
        bundle_dir: impl AsRef<Path>,
        run_id: impl Into<String>,
    ) -> Result<Self, BundleWriterError> {
        let bundle_dir = bundle_dir.as_ref().to_path_buf();
        if bundle_dir.exists() {
            return Err(BundleWriterError::BundleAlreadyExists { path: bundle_dir });
        }

        let parent = bundle_dir
            .parent()
            .map(Path::to_path_buf)
            .unwrap_or_else(|| PathBuf::from("."));
        create_dir_all(&parent, "create bundle parent directory")?;

        let mut staging_dir = None;
        let mut last_attempt = build_staging_dir(&bundle_dir, 0);
        for attempt in 0..64 {
            let candidate = build_staging_dir(&bundle_dir, attempt);
            last_attempt = candidate.clone();
            match fs::create_dir(&candidate) {
                Ok(()) => {
                    staging_dir = Some(candidate);
                    break;
                }
                Err(source) if source.kind() == std::io::ErrorKind::AlreadyExists => continue,
                Err(source) => {
                    return Err(BundleWriterError::Io {
                        action: "create bundle staging directory",
                        path: candidate,
                        source,
                    });
                }
            }
        }
        let staging_dir = staging_dir.ok_or_else(|| BundleWriterError::Io {
            action: "create bundle staging directory",
            path: last_attempt,
            source: std::io::Error::new(
                std::io::ErrorKind::AlreadyExists,
                "failed to allocate unique staging directory",
            ),
        })?;

        create_dir_all(
            &staging_dir.join(INPUTS_DIR),
            "create bundle inputs directory",
        )?;

        let mut writer = Self {
            bundle_dir,
            staging_dir,
            manifest: BundleManifestV1::new(run_id),
            finalized: false,
        };
        writer.write_default_placeholders()?;
        Ok(writer)
    }

    pub fn bundle_dir(&self) -> &Path {
        &self.bundle_dir
    }

    pub fn staging_dir(&self) -> &Path {
        &self.staging_dir
    }

    pub fn manifest(&self) -> &BundleManifestV1 {
        &self.manifest
    }

    pub fn is_finalized(&self) -> bool {
        self.finalized
    }

    pub fn snapshot_entry_dot_from_path(
        &mut self,
        source: impl AsRef<Path>,
    ) -> Result<(), BundleWriterError> {
        self.ensure_open()?;
        let source = source.as_ref();
        let bytes = fs::read(source).map_err(|source_error| BundleWriterError::Io {
            action: "read entry dot input file",
            path: source.to_path_buf(),
            source: source_error,
        })?;
        self.snapshot_entry_dot_bytes(&bytes)
    }

    pub fn snapshot_entry_dot_bytes(&mut self, bytes: &[u8]) -> Result<(), BundleWriterError> {
        self.ensure_open()?;
        write_bytes(
            &self.staging_path(ENTRY_DOT_FILE),
            "write entry dot snapshot",
            bytes,
        )?;

        let entry = self.manifest.input_entry_dot_mut().ok_or_else(|| {
            BundleWriterError::MissingManifestInput {
                key: INPUT_ENTRY_DOT_KEY.to_owned(),
            }
        })?;
        entry.status = SectionStatus::Ok;
        entry.sha256 = Some(sha256_hex(bytes));
        entry.bytes = Some(bytes.len() as u64);
        entry.error = None;

        Ok(())
    }

    pub fn write_trace_jsonl<I, S>(&mut self, lines: I) -> Result<(), BundleWriterError>
    where
        I: IntoIterator<Item = S>,
        S: AsRef<str>,
    {
        self.ensure_open()?;
        let path = self.staging_path(TRACE_FILE);
        if let Some(parent) = path.parent() {
            create_dir_all(parent, "create artifact parent directory")?;
        }
        let mut bytes = Vec::new();
        for line in lines {
            bytes.extend_from_slice(line.as_ref().as_bytes());
            bytes.push(b'\n');
        }
        write_bytes(&path, "write trace artifact file", &bytes)?;

        self.mark_section_written(BundleSection::Trace, &bytes)
    }

    pub fn commit_existing_trace_jsonl(&mut self) -> Result<(), BundleWriterError> {
        self.ensure_open()?;
        self.mark_existing_section_file(BundleSection::Trace)
    }

    pub fn write_state_diff_json<T: Serialize>(
        &mut self,
        value: &T,
    ) -> Result<(), BundleWriterError> {
        self.ensure_open()?;
        self.write_json_section(BundleSection::StateDiff, value)
    }

    pub fn write_capability_report_json<T: Serialize>(
        &mut self,
        value: &T,
    ) -> Result<(), BundleWriterError> {
        self.ensure_open()?;
        self.write_json_section(BundleSection::CapabilityReport, value)
    }

    pub fn mark_section_unavailable(
        &mut self,
        section: BundleSection,
        code: impl Into<String>,
        message: impl Into<String>,
    ) -> Result<(), BundleWriterError> {
        self.ensure_open()?;
        self.set_section_marker(section, SectionStatus::Unavailable, code, message)?;
        self.write_section_marker_file(section)
    }

    pub fn mark_section_error(
        &mut self,
        section: BundleSection,
        code: impl Into<String>,
        message: impl Into<String>,
    ) -> Result<(), BundleWriterError> {
        self.ensure_open()?;
        self.set_section_marker(section, SectionStatus::Error, code, message)?;
        self.write_section_marker_file(section)
    }

    pub fn finalize(&mut self) -> Result<(), BundleWriterError> {
        self.ensure_open()?;
        self.ensure_required_files_present()?;

        let manifest_bytes = self
            .manifest
            .serialize_pretty_json()
            .map_err(|source| BundleWriterError::ManifestSerialize { source })?;
        write_bytes(
            &self.staging_path(MANIFEST_FILE),
            "write manifest.json",
            &manifest_bytes,
        )?;

        fs::rename(&self.staging_dir, &self.bundle_dir).map_err(|source| {
            BundleWriterError::Io {
                action: "atomically finalize bundle directory",
                path: self.bundle_dir.clone(),
                source,
            }
        })?;
        self.finalized = true;
        Ok(())
    }

    fn write_default_placeholders(&mut self) -> Result<(), BundleWriterError> {
        write_bytes(
            &self.staging_path(ENTRY_DOT_FILE),
            "write default entry dot placeholder",
            b"# unavailable: entry.dot snapshot not captured\n",
        )?;

        for section in BundleSection::ALL {
            self.write_section_marker_file(section)?;
        }
        Ok(())
    }

    fn ensure_open(&self) -> Result<(), BundleWriterError> {
        if self.finalized {
            Err(BundleWriterError::AlreadyFinalized)
        } else {
            Ok(())
        }
    }

    fn ensure_required_files_present(&mut self) -> Result<(), BundleWriterError> {
        let entry_path = self.staging_path(ENTRY_DOT_FILE);
        if !entry_path.is_file() {
            write_bytes(
                &entry_path,
                "write missing entry dot placeholder",
                b"# unavailable: entry.dot snapshot not captured\n",
            )?;

            let entry = self.manifest.input_entry_dot_mut().ok_or_else(|| {
                BundleWriterError::MissingManifestInput {
                    key: INPUT_ENTRY_DOT_KEY.to_owned(),
                }
            })?;
            entry.status = SectionStatus::Unavailable;
            entry.sha256 = None;
            entry.bytes = None;
            if entry.error.is_none() {
                entry.error = Some(SectionErrorMarker::new(
                    DEFAULT_UNAVAILABLE_CODE,
                    DEFAULT_INPUT_UNAVAILABLE_MESSAGE,
                ));
            }
        }

        for section in BundleSection::ALL {
            let path = self.staging_path(section.path());
            if path.is_file() {
                continue;
            }

            let section_manifest = self
                .manifest
                .section_mut(section)
                .ok_or(BundleWriterError::MissingManifestSection { section })?;
            if section_manifest.status == SectionStatus::Ok {
                section_manifest.status = SectionStatus::Unavailable;
            }
            section_manifest.sha256 = None;
            section_manifest.bytes = None;
            if section_manifest.error.is_none() {
                section_manifest.error = Some(SectionErrorMarker::new(
                    DEFAULT_UNAVAILABLE_CODE,
                    DEFAULT_SECTION_UNAVAILABLE_MESSAGE,
                ));
            }

            self.write_section_marker_file(section)?;
        }

        Ok(())
    }

    fn set_section_marker(
        &mut self,
        section: BundleSection,
        status: SectionStatus,
        code: impl Into<String>,
        message: impl Into<String>,
    ) -> Result<(), BundleWriterError> {
        let section_manifest = self
            .manifest
            .section_mut(section)
            .ok_or(BundleWriterError::MissingManifestSection { section })?;

        section_manifest.status = status;
        section_manifest.sha256 = None;
        section_manifest.bytes = None;
        section_manifest.error = Some(SectionErrorMarker::new(code, message));
        Ok(())
    }

    fn mark_section_written(
        &mut self,
        section: BundleSection,
        bytes: &[u8],
    ) -> Result<(), BundleWriterError> {
        let section_manifest = self
            .manifest
            .section_mut(section)
            .ok_or(BundleWriterError::MissingManifestSection { section })?;
        section_manifest.status = SectionStatus::Ok;
        section_manifest.sha256 = Some(sha256_hex(bytes));
        section_manifest.bytes = Some(bytes.len() as u64);
        section_manifest.error = None;
        Ok(())
    }

    fn mark_existing_section_file(
        &mut self,
        section: BundleSection,
    ) -> Result<(), BundleWriterError> {
        let path = self.staging_path(section.path());
        let (sha256, bytes) = sha256_hex_file(&path, "read artifact section for hashing")?;
        let section_manifest = self
            .manifest
            .section_mut(section)
            .ok_or(BundleWriterError::MissingManifestSection { section })?;
        section_manifest.status = SectionStatus::Ok;
        section_manifest.sha256 = Some(sha256);
        section_manifest.bytes = Some(bytes);
        section_manifest.error = None;
        Ok(())
    }

    fn write_json_section<T: Serialize>(
        &mut self,
        section: BundleSection,
        value: &T,
    ) -> Result<(), BundleWriterError> {
        let mut bytes = serde_json::to_vec_pretty(value)
            .map_err(|source| BundleWriterError::ManifestSerialize { source })?;
        bytes.push(b'\n');
        write_bytes(
            &self.staging_path(section.path()),
            "write json artifact section",
            &bytes,
        )?;
        self.mark_section_written(section, &bytes)
    }

    fn write_section_marker_file(
        &mut self,
        section: BundleSection,
    ) -> Result<(), BundleWriterError> {
        let section_manifest = self
            .manifest
            .sections
            .get(section.key())
            .ok_or(BundleWriterError::MissingManifestSection { section })?;

        let marker = SectionFileMarker {
            section: section.key(),
            status: section_manifest.status,
            error: section_manifest.error.as_ref(),
        };

        if section == BundleSection::Trace {
            let mut line = serde_json::to_string(&marker)
                .map_err(|source| BundleWriterError::ManifestSerialize { source })?;
            line.push('\n');
            write_bytes(
                &self.staging_path(section.path()),
                "write trace marker section",
                line.as_bytes(),
            )
        } else {
            let mut bytes = serde_json::to_vec_pretty(&marker)
                .map_err(|source| BundleWriterError::ManifestSerialize { source })?;
            bytes.push(b'\n');
            write_bytes(
                &self.staging_path(section.path()),
                "write json marker section",
                &bytes,
            )
        }
    }

    fn staging_path(&self, relative_path: &str) -> PathBuf {
        self.staging_dir.join(relative_path)
    }
}

#[derive(Serialize)]
struct SectionFileMarker<'a> {
    section: &'a str,
    status: SectionStatus,
    #[serde(skip_serializing_if = "Option::is_none")]
    error: Option<&'a SectionErrorMarker>,
}

impl Drop for BundleWriter {
    fn drop(&mut self) {
        if !self.finalized {
            let _ = fs::remove_dir_all(&self.staging_dir);
        }
    }
}
