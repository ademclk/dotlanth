use super::*;
use serde_json::{Value, json};
use std::fs;
use tempfile::TempDir;

#[test]
fn manifest_serialization_is_deterministic() {
    let manifest = BundleManifestV1::new_with_created_at("run-123", 42);
    let first = manifest
        .serialize_pretty_json()
        .expect("manifest should serialize");
    let second = manifest
        .serialize_pretty_json()
        .expect("manifest should serialize consistently");

    assert_eq!(first, second);
    let json = String::from_utf8(first).expect("json should be utf8");
    assert!(json.contains("\"schema_version\": \"1\""));
    let cap = json.find("\"capability_report\"").expect("capability key");
    let state = json.find("\"state_diff\"").expect("state key");
    let trace = json.find("\"trace\"").expect("trace key");
    assert!(cap < state);
    assert!(state < trace);
}

#[test]
fn manifest_serialization_records_determinism_mode() {
    let manifest = BundleManifestV1::new_with_created_at("run-123", 42);
    let json = read_json_bytes(
        &manifest
            .serialize_pretty_json()
            .expect("manifest should serialize"),
    );

    assert_eq!(json["determinism"]["mode"], "default");
}

#[test]
fn finalize_creates_required_files_with_explicit_markers() {
    let tmp = TempDir::new().expect("tempdir");
    let bundle = tmp.path().join("bundle-run-1");
    let mut writer = BundleWriter::new(&bundle, "run-1").expect("writer should initialize");
    let staging = writer.staging_dir().to_path_buf();

    writer.finalize().expect("finalize should succeed");
    assert!(writer.is_finalized());
    assert!(!staging.exists());
    assert!(bundle.exists());
    for relative in REQUIRED_FILE_PATHS {
        assert!(
            bundle.join(relative).is_file(),
            "required file missing: {relative}"
        );
    }

    let trace_line = fs::read_to_string(bundle.join(TRACE_FILE)).expect("trace should read");
    let marker: Value = serde_json::from_str(trace_line.lines().next().expect("one marker line"))
        .expect("marker should parse");
    assert_eq!(marker["section"], "trace");
    assert_eq!(marker["status"], "unavailable");

    let state_diff = read_json(&bundle.join(STATE_DIFF_FILE));
    assert_eq!(state_diff["section"], "state_diff");
    assert_eq!(state_diff["status"], "unavailable");

    let capability_report = read_json(&bundle.join(CAPABILITY_REPORT_FILE));
    assert_eq!(capability_report["section"], "capability_report");
    assert_eq!(capability_report["status"], "unavailable");

    let entry_dot = fs::read_to_string(bundle.join(ENTRY_DOT_FILE)).expect("entry dot should read");
    assert!(entry_dot.contains("unavailable"));

    let manifest = read_json(&bundle.join(MANIFEST_FILE));
    assert_eq!(manifest["sections"]["trace"]["status"], "unavailable");
    assert_eq!(manifest["sections"]["state_diff"]["status"], "unavailable");
    assert_eq!(
        manifest["sections"]["capability_report"]["status"],
        "unavailable"
    );
    assert_eq!(manifest["inputs"]["entry_dot"]["status"], "unavailable");
}

#[test]
fn snapshot_capture_hashes_entry_dot_and_updates_manifest() {
    let tmp = TempDir::new().expect("tempdir");
    let source = tmp.path().join("app.dot");
    let content = "dot 0.1\napp \"hello\"\napi \"public\"\n  route GET \"/hello\"\n    respond 200 \"ok\"\n  end\nend\n";
    fs::write(&source, content).expect("source dot should write");

    let bundle = tmp.path().join("bundle-run-2");
    let mut writer = BundleWriter::new(&bundle, "run-2").expect("writer should initialize");
    writer
        .snapshot_entry_dot_from_path(&source)
        .expect("snapshot should succeed");
    writer
        .write_trace_jsonl([r#"{"type":"log","message":"boot"}"#])
        .expect("trace should write");
    writer
        .write_state_diff_json(&json!({"state":"unchanged"}))
        .expect("state diff should write");
    writer
        .write_capability_report_json(&json!({"used":["log"]}))
        .expect("capability report should write");
    writer.finalize().expect("finalize should succeed");

    let snapshotted =
        fs::read_to_string(bundle.join(ENTRY_DOT_FILE)).expect("snapshot should read");
    assert_eq!(snapshotted, content);

    let manifest = read_json(&bundle.join(MANIFEST_FILE));
    assert_eq!(manifest["inputs"]["entry_dot"]["status"], "ok");
    assert_eq!(
        manifest["inputs"]["entry_dot"]["bytes"],
        Value::from(content.len() as u64)
    );
    assert_eq!(
        manifest["inputs"]["entry_dot"]["sha256"],
        Value::String(crate::util::sha256_hex(content.as_bytes()))
    );
    assert_eq!(manifest["sections"]["trace"]["status"], "ok");
    let expected_trace = "{\"type\":\"log\",\"message\":\"boot\"}\n";
    assert_eq!(
        manifest["sections"]["trace"]["bytes"],
        Value::from(expected_trace.len() as u64)
    );
    assert_eq!(
        manifest["sections"]["trace"]["sha256"],
        Value::String(crate::util::sha256_hex(expected_trace.as_bytes()))
    );
    assert_eq!(manifest["sections"]["state_diff"]["status"], "ok");
    assert_eq!(manifest["sections"]["capability_report"]["status"], "ok");
}

#[test]
fn section_error_markers_are_written_to_file_and_manifest() {
    let tmp = TempDir::new().expect("tempdir");
    let bundle = tmp.path().join("bundle-run-3");
    let mut writer = BundleWriter::new(&bundle, "run-3").expect("writer should initialize");

    writer
        .mark_section_error(
            BundleSection::StateDiff,
            "state_capture_failed",
            "state diff capture failed in runtime",
        )
        .expect("marker should write");
    writer.finalize().expect("finalize should succeed");

    let state_diff = read_json(&bundle.join(STATE_DIFF_FILE));
    assert_eq!(state_diff["status"], "error");
    assert_eq!(state_diff["error"]["code"], "state_capture_failed");

    let manifest = read_json(&bundle.join(MANIFEST_FILE));
    assert_eq!(manifest["sections"]["state_diff"]["status"], "error");
    assert_eq!(
        manifest["sections"]["state_diff"]["error"]["code"],
        "state_capture_failed"
    );
}

#[test]
fn finalize_fails_when_called_twice() {
    let tmp = TempDir::new().expect("tempdir");
    let bundle = tmp.path().join("bundle-run-4");
    let mut writer = BundleWriter::new(&bundle, "run-4").expect("writer should initialize");
    writer.finalize().expect("first finalize should succeed");
    let err = writer.finalize().expect_err("second finalize must fail");
    assert!(matches!(err, BundleWriterError::AlreadyFinalized));
}

#[test]
fn finalize_downgrades_manifest_when_section_file_missing() {
    let tmp = TempDir::new().expect("tempdir");
    let bundle = tmp.path().join("bundle-run-missing-trace");
    let mut writer = BundleWriter::new(&bundle, "run-missing").expect("writer should initialize");

    writer
        .write_trace_jsonl([r#"{"type":"log","message":"hello"}"#])
        .expect("trace should write");
    fs::remove_file(writer.staging_dir().join(TRACE_FILE)).expect("trace should delete");

    writer.finalize().expect("finalize should succeed");

    let trace_line = fs::read_to_string(bundle.join(TRACE_FILE)).expect("trace should read");
    let marker: Value = serde_json::from_str(trace_line.lines().next().expect("one marker line"))
        .expect("marker should parse");
    assert_eq!(marker["status"], "unavailable");

    let manifest = read_json(&bundle.join(MANIFEST_FILE));
    assert_eq!(manifest["sections"]["trace"]["status"], "unavailable");
    assert!(manifest["sections"]["trace"]["bytes"].is_null());
    assert!(manifest["sections"]["trace"]["sha256"].is_null());
    assert_eq!(
        manifest["sections"]["trace"]["error"]["code"],
        "unavailable"
    );
}

fn read_json(path: &std::path::Path) -> Value {
    let raw = fs::read(path).expect("json file should read");
    serde_json::from_slice(&raw).expect("json file should parse")
}

fn read_json_bytes(raw: &[u8]) -> Value {
    serde_json::from_slice(raw).expect("json bytes should parse")
}
