#![forbid(unsafe_code)]

use rusqlite::{Connection, OptionalExtension, params};
use std::path::{Path, PathBuf};
use std::time::{SystemTime, UNIX_EPOCH};

const DEFAULT_DB_DIR: &str = ".dotlanth";
const DEFAULT_DB_FILE: &str = "dotdb.sqlite";

const MIGRATIONS: &[&str] = &[r#"
CREATE TABLE IF NOT EXISTS runs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    status TEXT NOT NULL,
    created_at_ms INTEGER NOT NULL,
    finalized_at_ms INTEGER
);

CREATE TABLE IF NOT EXISTS run_logs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    run_id INTEGER NOT NULL,
    created_at_ms INTEGER NOT NULL,
    line TEXT NOT NULL,
    FOREIGN KEY(run_id) REFERENCES runs(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_run_logs_run_id_id
    ON run_logs(run_id, id);

CREATE TABLE IF NOT EXISTS state_kv (
    namespace TEXT NOT NULL,
    key TEXT NOT NULL,
    value BLOB NOT NULL,
    updated_at_ms INTEGER NOT NULL,
    PRIMARY KEY(namespace, key)
);
"#];

/// A local-first SQLite backend for Dotlanth.
#[derive(Debug)]
pub struct DotDb {
    conn: Connection,
    path: PathBuf,
}

impl DotDb {
    /// Returns the default DotDB path within `root`.
    ///
    /// This is currently `${root}/.dotlanth/dotdb.sqlite`.
    pub fn default_path_in(root: impl AsRef<Path>) -> PathBuf {
        root.as_ref().join(DEFAULT_DB_DIR).join(DEFAULT_DB_FILE)
    }

    /// Opens (and creates if missing) the default DotDB within `root`.
    pub fn open_in(root: impl AsRef<Path>) -> Result<Self, DotDbError> {
        Self::open(Self::default_path_in(root))
    }

    /// Opens (and creates if missing) the default DotDB within the current working directory.
    pub fn open_default() -> Result<Self, DotDbError> {
        let cwd = std::env::current_dir().map_err(|source| DotDbError::Io {
            action: "read current working directory",
            path: None,
            source,
        })?;
        Self::open_in(cwd)
    }

    /// Opens (and creates if missing) a DotDB database at the provided path.
    ///
    /// This runs migrations automatically and is safe to call multiple times.
    pub fn open(path: impl AsRef<Path>) -> Result<Self, DotDbError> {
        let path = path.as_ref().to_path_buf();

        ensure_parent_dir_exists(&path)?;

        let conn = Connection::open(&path).map_err(|source| DotDbError::Sql {
            action: "open sqlite connection",
            source,
        })?;

        configure_connection(&conn)?;

        let mut db = Self { conn, path };
        db.migrate()?;
        Ok(db)
    }

    /// Returns the SQLite file path backing this database.
    pub fn path(&self) -> &Path {
        &self.path
    }

    /// Creates a new run and returns its id.
    pub fn create_run(&mut self) -> Result<RunId, DotDbError> {
        let created_at_ms = now_ms();
        self.conn
            .execute(
                "INSERT INTO runs(status, created_at_ms) VALUES (?1, ?2)",
                params![RunStatus::Running.as_str(), created_at_ms],
            )
            .map_err(|source| DotDbError::Sql {
                action: "insert run",
                source,
            })?;
        Ok(RunId(self.conn.last_insert_rowid()))
    }

    /// Finalizes a run, persisting its status and finalization timestamp.
    pub fn finalize_run(&mut self, run_id: RunId, status: RunStatus) -> Result<(), DotDbError> {
        let finalized_at_ms = now_ms();
        let updated = self
            .conn
            .execute(
                "UPDATE runs SET status = ?1, finalized_at_ms = ?2 WHERE id = ?3",
                params![status.as_str(), finalized_at_ms, run_id.0],
            )
            .map_err(|source| DotDbError::Sql {
                action: "finalize run",
                source,
            })?;

        if updated == 0 {
            return Err(DotDbError::RunNotFound { run_id });
        }

        Ok(())
    }

    /// Appends a single log line to a run.
    pub fn append_run_log(&mut self, run_id: RunId, line: &str) -> Result<(), DotDbError> {
        self.append_run_logs(run_id, [line])
    }

    /// Appends multiple log lines to a run in a single transaction.
    pub fn append_run_logs<I, S>(&mut self, run_id: RunId, lines: I) -> Result<(), DotDbError>
    where
        I: IntoIterator<Item = S>,
        S: AsRef<str>,
    {
        let created_at_ms = now_ms();
        let tx = self.conn.transaction().map_err(|source| DotDbError::Sql {
            action: "begin log append transaction",
            source,
        })?;

        for line in lines {
            tx.execute(
                "INSERT INTO run_logs(run_id, created_at_ms, line) VALUES (?1, ?2, ?3)",
                params![run_id.0, created_at_ms, line.as_ref()],
            )
            .map_err(|source| DotDbError::Sql {
                action: "insert run log line",
                source,
            })?;
        }

        tx.commit().map_err(|source| DotDbError::Sql {
            action: "commit log append transaction",
            source,
        })?;

        Ok(())
    }

    /// Returns all log lines for a run, in stable append order.
    pub fn run_logs(&mut self, run_id: RunId) -> Result<Vec<RunLogEntry>, DotDbError> {
        let mut stmt = self
            .conn
            .prepare(
                "SELECT id, created_at_ms, line FROM run_logs WHERE run_id = ?1 ORDER BY id ASC",
            )
            .map_err(|source| DotDbError::Sql {
                action: "prepare run log query",
                source,
            })?;

        let rows = stmt
            .query_map(params![run_id.0], |row| {
                Ok(RunLogEntry {
                    id: row.get(0)?,
                    created_at_ms: row.get(1)?,
                    line: row.get(2)?,
                })
            })
            .map_err(|source| DotDbError::Sql {
                action: "query run logs",
                source,
            })?;

        let mut entries = Vec::new();
        for row in rows {
            entries.push(row.map_err(|source| DotDbError::Sql {
                action: "read run log row",
                source,
            })?);
        }

        Ok(entries)
    }

    /// Sets a state value.
    ///
    /// Values are stored as opaque bytes and addressed by `(namespace, key)`.
    pub fn state_set(
        &mut self,
        namespace: &str,
        key: &str,
        value: &[u8],
    ) -> Result<(), DotDbError> {
        let updated_at_ms = now_ms();
        self.conn
            .execute(
                r#"
INSERT INTO state_kv(namespace, key, value, updated_at_ms)
VALUES (?1, ?2, ?3, ?4)
ON CONFLICT(namespace, key) DO UPDATE SET
    value = excluded.value,
    updated_at_ms = excluded.updated_at_ms
"#,
                params![namespace, key, value, updated_at_ms],
            )
            .map_err(|source| DotDbError::Sql {
                action: "set state kv",
                source,
            })?;
        Ok(())
    }

    /// Gets a state value.
    ///
    /// Returns `Ok(None)` when the key is not present.
    pub fn state_get(&mut self, namespace: &str, key: &str) -> Result<Option<Vec<u8>>, DotDbError> {
        self.conn
            .query_row(
                "SELECT value FROM state_kv WHERE namespace = ?1 AND key = ?2",
                params![namespace, key],
                |row| row.get(0),
            )
            .optional()
            .map_err(|source| DotDbError::Sql {
                action: "get state kv",
                source,
            })
    }

    fn migrate(&mut self) -> Result<(), DotDbError> {
        let current = self.user_version()?;
        let target = u32::try_from(MIGRATIONS.len()).unwrap_or(u32::MAX);

        if current > target {
            return Err(DotDbError::SchemaVersionTooNew {
                current,
                supported: target,
            });
        }

        for (idx, sql) in MIGRATIONS.iter().enumerate() {
            let version = u32::try_from(idx + 1).unwrap_or(u32::MAX);
            if current < version {
                self.apply_migration(version, sql)?;
            }
        }

        Ok(())
    }

    fn user_version(&self) -> Result<u32, DotDbError> {
        self.conn
            .query_row("PRAGMA user_version", [], |row| row.get(0))
            .map_err(|source| DotDbError::Sql {
                action: "read sqlite user_version",
                source,
            })
    }

    fn apply_migration(&mut self, version: u32, sql: &str) -> Result<(), DotDbError> {
        let tx = self.conn.transaction().map_err(|source| DotDbError::Sql {
            action: "begin migration transaction",
            source,
        })?;

        tx.execute_batch(sql).map_err(|source| DotDbError::Sql {
            action: "apply migration",
            source,
        })?;

        tx.pragma_update(None, "user_version", version)
            .map_err(|source| DotDbError::Sql {
                action: "update sqlite user_version",
                source,
            })?;

        tx.commit().map_err(|source| DotDbError::Sql {
            action: "commit migration transaction",
            source,
        })?;

        Ok(())
    }
}

/// A stable id for a persisted run.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct RunId(i64);

impl RunId {
    /// Returns the raw SQLite integer id.
    pub const fn as_i64(self) -> i64 {
        self.0
    }
}

impl std::fmt::Display for RunId {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.0)
    }
}

/// A run status persisted in DotDB.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RunStatus {
    /// Run created but not yet finalized.
    Running,
    /// Run finished successfully.
    Succeeded,
    /// Run finalized with a failure.
    Failed,
}

impl RunStatus {
    fn as_str(self) -> &'static str {
        match self {
            Self::Running => "running",
            Self::Succeeded => "succeeded",
            Self::Failed => "failed",
        }
    }
}

impl std::fmt::Display for RunStatus {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(self.as_str())
    }
}

/// A single log line appended to a run.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct RunLogEntry {
    /// Monotonic id used for stable ordering.
    pub id: i64,
    /// Timestamp (Unix millis) captured at insert time.
    pub created_at_ms: i64,
    /// The log line payload.
    pub line: String,
}

#[derive(Debug)]
pub enum DotDbError {
    Io {
        action: &'static str,
        path: Option<PathBuf>,
        source: std::io::Error,
    },
    Sql {
        action: &'static str,
        source: rusqlite::Error,
    },
    SchemaVersionTooNew {
        current: u32,
        supported: u32,
    },
    RunNotFound {
        run_id: RunId,
    },
}

impl std::fmt::Display for DotDbError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Io { action, path, .. } => {
                if let Some(path) = path {
                    write!(
                        f,
                        "io error while attempting to {action}: `{}`",
                        path.display()
                    )
                } else {
                    write!(f, "io error while attempting to {action}")
                }
            }
            Self::Sql { action, .. } => write!(f, "sqlite error while attempting to {action}"),
            Self::SchemaVersionTooNew { current, supported } => write!(
                f,
                "sqlite schema version {current} is newer than supported {supported}"
            ),
            Self::RunNotFound { run_id } => write!(f, "run not found: {run_id}"),
        }
    }
}

impl std::error::Error for DotDbError {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        match self {
            Self::Io { source, .. } => Some(source),
            Self::Sql { source, .. } => Some(source),
            Self::SchemaVersionTooNew { .. } | Self::RunNotFound { .. } => None,
        }
    }
}

fn ensure_parent_dir_exists(path: &Path) -> Result<(), DotDbError> {
    let Some(parent) = path.parent() else {
        return Ok(());
    };

    if parent.as_os_str().is_empty() {
        return Ok(());
    }

    std::fs::create_dir_all(parent).map_err(|source| DotDbError::Io {
        action: "create database parent directory",
        path: Some(parent.to_path_buf()),
        source,
    })?;

    Ok(())
}

fn configure_connection(conn: &Connection) -> Result<(), DotDbError> {
    conn.execute_batch(
        r#"
PRAGMA foreign_keys = ON;
"#,
    )
    .map_err(|source| DotDbError::Sql {
        action: "configure sqlite connection",
        source,
    })?;
    Ok(())
}

fn now_ms() -> i64 {
    let Ok(delta) = SystemTime::now().duration_since(UNIX_EPOCH) else {
        return 0;
    };

    i64::try_from(delta.as_millis()).unwrap_or(i64::MAX)
}

#[cfg(test)]
mod tests {
    use super::{DotDb, RunStatus};
    use tempfile::TempDir;

    #[test]
    fn open_in_creates_db_and_runs_migrations() {
        let temp = TempDir::new().expect("temp dir must create");
        let mut db = DotDb::open_in(temp.path()).expect("db open must succeed");

        let expected = DotDb::default_path_in(temp.path());
        assert_eq!(db.path(), expected.as_path());
        assert!(expected.exists(), "sqlite file should exist after open");

        let run_id = db.create_run().expect("run must create");
        db.finalize_run(run_id, RunStatus::Succeeded)
            .expect("run must finalize");

        let (status, created_at_ms, finalized_at_ms): (String, i64, Option<i64>) = db
            .conn
            .query_row(
                "SELECT status, created_at_ms, finalized_at_ms FROM runs WHERE id = ?1",
                [run_id.as_i64()],
                |row| Ok((row.get(0)?, row.get(1)?, row.get(2)?)),
            )
            .expect("run row must exist");

        assert_eq!(status, "succeeded");
        assert!(created_at_ms > 0);
        assert!(finalized_at_ms.unwrap_or(0) >= created_at_ms);
    }

    #[test]
    fn run_logs_roundtrip_in_order() {
        let temp = TempDir::new().expect("temp dir must create");
        let mut db = DotDb::open_in(temp.path()).expect("db open must succeed");

        let run_id = db.create_run().expect("run must create");
        db.append_run_logs(run_id, ["first", "second", "third"])
            .expect("append must succeed");

        let logs = db.run_logs(run_id).expect("query must succeed");
        let lines: Vec<_> = logs.into_iter().map(|entry| entry.line).collect();
        assert_eq!(lines, vec!["first", "second", "third"]);
    }

    #[test]
    fn state_kv_set_get_and_overwrite() {
        let temp = TempDir::new().expect("temp dir must create");
        let mut db = DotDb::open_in(temp.path()).expect("db open must succeed");

        assert_eq!(db.state_get("ns", "k").unwrap(), None);

        db.state_set("ns", "k", b"v1").expect("set must succeed");
        assert_eq!(
            db.state_get("ns", "k").unwrap().as_deref(),
            Some(&b"v1"[..])
        );

        db.state_set("ns", "k", b"v2")
            .expect("overwrite must succeed");
        assert_eq!(
            db.state_get("ns", "k").unwrap().as_deref(),
            Some(&b"v2"[..])
        );
    }
}
