#![forbid(unsafe_code)]

use dot_db::DotDb;

pub(crate) fn run(run_id: &str) -> Result<(), String> {
    let run_id = parse_run_id(run_id)?;

    let mut db = DotDb::open_default().map_err(|error| format!("failed to open DotDB: {error}"))?;
    let logs = db
        .run_logs(&run_id)
        .map_err(|error| format!("failed to read logs for run {run_id}: {error}"))?;

    for entry in logs {
        println!("{} INFO {}", entry.created_at_ms, entry.line);
    }

    Ok(())
}

fn parse_run_id(raw: &str) -> Result<String, String> {
    let trimmed = raw.trim();
    if trimmed.is_empty() {
        return Err("run id must not be empty".to_owned());
    }

    Ok(trimmed.to_owned())
}

#[cfg(test)]
mod tests {
    use super::parse_run_id;

    #[test]
    fn parse_run_id_rejects_blank_values() {
        assert!(parse_run_id("").is_err());
        assert!(parse_run_id("   ").is_err());
    }

    #[test]
    fn parse_run_id_accepts_external_string() {
        let id = parse_run_id("run_123").expect("run id should parse");
        assert_eq!(id, "run_123");
    }
}
