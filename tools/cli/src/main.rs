use clap::{Parser, Subcommand};
use std::path::Path;

mod commands;
mod engine;
mod table;
mod tui;

use commands::{
    backup::{execute_backup, execute_restore},
    config::execute_config,
    database::{execute_database_create, execute_database_delete, execute_database_list},
    init::execute_init,
    inspect::execute_inspect,
    organization::execute_organization_create,
    query::execute_query,
    server::{execute_start, execute_stop, execute_status},
    user::execute_user_create,
};
use engine::CeebaseClient;

#[derive(Parser)]
#[command(name = "ceebase")]
#[command(author = "MackenzieCampbelAndross <mackenzieandross@gmail.com>")]
#[command(version = "1.0.0")]
#[command(about = "Official CeeBase Database CLI and Interactive TUI", long_about = None)]
struct Cli {
    #[command(subcommand)]
    command: Option<Commands>,

    /// Default database file or server target
    #[arg(short, long, global = true, default_value = "ceebase.cbdb")]
    database: String,

    /// Force plain ASCII output without Unicode borders
    #[arg(long, global = true)]
    plain: bool,
}

#[derive(Subcommand)]
enum Commands {
    /// Initialize a fresh CeeBase database kernel file
    Init {
        #[arg(default_value = "ceebase.cbdb")]
        path: String,
    },
    /// Start the CeeBase server
    Start {
        #[arg(long, default_value_t = 8080)]
        port: u16,
        #[arg(long, default_value = "127.0.0.1")]
        host: String,
        #[arg(long)]
        daemon: bool,
    },
    /// Stop the running CeeBase server
    Stop {
        #[arg(long, default_value_t = 8080)]
        port: u16,
    },
    /// Inspect server or database file status
    Status {
        #[arg(default_value = "ceebase.cbdb")]
        target: String,
    },
    /// Connect to a database or server in interactive TUI mode
    Connect {
        #[arg(default_value = "ceebase.cbdb")]
        target: String,
    },
    /// Execute a SQL statement non-interactively
    Query {
        sql: String,
        #[arg(short, long)]
        database: Option<String>,
    },
    /// Manage databases
    Database {
        #[command(subcommand)]
        cmd: DatabaseCommands,
    },
    /// Manage users
    User {
        #[command(subcommand)]
        cmd: UserCommands,
    },
    /// Manage organizations
    Organization {
        #[command(subcommand)]
        cmd: OrganizationCommands,
    },
    /// Create a database backup snapshot
    Backup {
        #[arg(short, long)]
        database: Option<String>,
        #[arg(short, long)]
        out: Option<String>,
    },
    /// Restore a database from backup
    Restore {
        backup_file: String,
        #[arg(short, long)]
        database: Option<String>,
    },
    /// View or update configuration settings
    Config {
        #[arg(long)]
        get: Option<String>,
        #[arg(long)]
        set: Option<String>,
    },
    /// Deep inspection of low-level 4 KB slotted storage pages
    Inspect {
        #[arg(default_value = "ceebase.cbdb")]
        database: String,
    },
}

#[derive(Subcommand)]
enum DatabaseCommands {
    Create { name: String },
    List,
    Delete { name: String },
}

#[derive(Subcommand)]
enum UserCommands {
    Create {
        email: String,
        #[arg(short, long, default_value = "admin")]
        role: String,
    },
}

#[derive(Subcommand)]
enum OrganizationCommands {
    Create { name: String },
}

fn main() {
    let cli = Cli::parse();

    let res = match cli.command {
        Some(Commands::Init { path }) => execute_init(&path),
        Some(Commands::Start { port, host, daemon }) => execute_start(port, &host, daemon),
        Some(Commands::Stop { port }) => execute_stop(port),
        Some(Commands::Status { target }) => execute_status(&target),
        Some(Commands::Connect { target }) => {
            // Ensure target exists if local
            if !target.starts_with("http") && !Path::new(&target).exists() {
                let _ = execute_init(&target);
            }
            match CeebaseClient::open(&target) {
                Ok(client) => tui::run_tui(client).map_err(|e| e.to_string()),
                Err(e) => Err(e),
            }
        }
        Some(Commands::Query { sql, database }) => {
            let target = database.unwrap_or(cli.database);
            execute_query(&target, &sql, cli.plain)
        }
        Some(Commands::Database { cmd }) => match cmd {
            DatabaseCommands::Create { name } => execute_database_create(&name),
            DatabaseCommands::List => execute_database_list(),
            DatabaseCommands::Delete { name } => execute_database_delete(&name),
        },
        Some(Commands::User { cmd }) => match cmd {
            UserCommands::Create { email, role } => execute_user_create(&email, &role),
        },
        Some(Commands::Organization { cmd }) => match cmd {
            OrganizationCommands::Create { name } => execute_organization_create(&name),
        },
        Some(Commands::Backup { database, out }) => {
            let db = database.unwrap_or(cli.database);
            execute_backup(&db, out.as_deref())
        }
        Some(Commands::Restore { backup_file, database }) => {
            execute_restore(&backup_file, database.as_deref())
        }
        Some(Commands::Config { get, set }) => execute_config(get.as_deref(), set.as_deref()),
        Some(Commands::Inspect { database }) => execute_inspect(&database),
        None => {
            // Default: bare `ceebase` -> launch interactive TUI
            let target = cli.database;
            if !target.starts_with("http") && !Path::new(&target).exists() {
                let _ = execute_init(&target);
            }
            match CeebaseClient::open(&target) {
                Ok(client) => tui::run_tui(client).map_err(|e| e.to_string()),
                Err(e) => Err(e),
            }
        }
    };

    if let Err(e) = res {
        eprintln!("Error: {}", e);
        std::process::exit(1);
    }
}
