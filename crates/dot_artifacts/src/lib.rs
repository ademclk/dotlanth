#![forbid(unsafe_code)]

mod error;
mod schema;
mod util;
mod writer;

pub use crate::error::BundleWriterError;
pub use crate::schema::{
    BUNDLE_SCHEMA_VERSION_V1, BundleManifestV1, BundleSection, CAPABILITY_REPORT_FILE,
    DETERMINISM_REPORT_FILE, DeterminismAuditSummaryManifest, DeterminismBudgetSummaryManifest,
    DeterminismEligibilityManifest, DeterminismViolationSummaryManifest, ENTRY_DOT_FILE,
    INPUTS_DIR, InputSnapshotManifest, MANIFEST_FILE, REQUIRED_FILE_PATHS,
    ReplayProofArtifactSurfaceManifest, ReplayProofCanonicalSurfaceManifest,
    ReplayProofDeterminismSurfaceManifest, ReplayProofManifest, ReplayProofTraceSurfaceManifest,
    STATE_DIFF_FILE, SectionErrorMarker, SectionManifest, SectionStatus, TRACE_FILE,
};
pub use crate::writer::BundleWriter;

#[cfg(test)]
mod tests;
