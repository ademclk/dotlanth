#![forbid(unsafe_code)]

use dot_db::DotDb;
use std::path::Path;

pub(crate) fn open_existing_dotdb_in(project_root: &Path) -> Result<DotDb, String> {
    let db_path = DotDb::default_path_in(project_root);
    if !db_path.is_file() {
        return Err(format!(
            "DotDB not found in `{}`; run this command from a Dotlanth project root",
            project_root.display()
        ));
    }

    DotDb::open(&db_path).map_err(|error| format!("failed to open DotDB: {error}"))
}

#[cfg(test)]
mod tests {
    use super::open_existing_dotdb_in;
    use dot_db::DotDb;
    use tempfile::TempDir;

    #[test]
    fn open_existing_dotdb_in_rejects_missing_db_file() {
        let temp = TempDir::new().expect("temp dir must create");
        let err = open_existing_dotdb_in(temp.path()).expect_err("missing db must error");
        assert!(err.contains("DotDB not found"));
        assert!(!DotDb::default_path_in(temp.path()).exists());
    }

    #[test]
    fn open_existing_dotdb_in_opens_existing_db_file() {
        let temp = TempDir::new().expect("temp dir must create");
        DotDb::open_in(temp.path()).expect("db must create");

        let db = open_existing_dotdb_in(temp.path()).expect("db must reopen");
        assert_eq!(db.path(), DotDb::default_path_in(temp.path()).as_path());
    }
}
