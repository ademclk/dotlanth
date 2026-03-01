#![forbid(unsafe_code)]

use dot_db::{DotDb, RunId};

pub(crate) fn run(run_id: &str) -> Result<(), String> {
    let run_id = parse_run_id(run_id)?;

    let mut db = DotDb::open_default().map_err(|error| format!("failed to open DotDB: {error}"))?;
    let logs = db
        .run_logs(run_id)
        .map_err(|error| format!("failed to read logs for run {run_id}: {error}"))?;

    for entry in logs {
        println!("{} INFO {}", entry.created_at_ms, entry.line);
    }

    Ok(())
}

fn parse_run_id(raw: &str) -> Result<RunId, String> {
    let trimmed = raw.trim();
    if trimmed.is_empty() {
        return Err("run id must not be empty".to_owned());
    }

    let value: i64 = trimmed
        .parse()
        .map_err(|_| format!("invalid run id `{trimmed}` (expected integer)"))?;
    if value <= 0 {
        return Err(format!(
            "invalid run id `{trimmed}` (expected positive integer)"
        ));
    }

    Ok(RunId::new(value))
}

#[cfg(test)]
mod tests {
    use super::parse_run_id;

    #[test]
    fn parse_run_id_rejects_non_positive() {
        assert!(parse_run_id("0").is_err());
        assert!(parse_run_id("-1").is_err());
    }

    #[test]
    fn parse_run_id_accepts_positive_integer() {
        let id = parse_run_id("42").expect("run id should parse");
        assert_eq!(id.as_i64(), 42);
    }
}
