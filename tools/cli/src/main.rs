use clap::{Parser, Subcommand};
use std::path::Path;

mod commands;
mod engine;
mod table;
mod tui;

use commands::{
    auth_cmd::{execute_auth_providers, execute_auth_users},
    backup::{execute_backup, execute_restore},
    config::execute_config,
    database::{execute_database_create, execute_database_delete, execute_database_list},
    db::{
        execute_db_connect, execute_db_dump, execute_db_pull, execute_db_push, execute_db_reset,
        execute_db_restore, execute_db_shell,
    },
    doctor::{execute_deploy, execute_doctor, execute_logs},
    functions_cmd::{execute_functions_deploy, execute_functions_invoke, execute_functions_logs},
    init::execute_init,
    inspect::execute_inspect,
    login::{execute_login, execute_logout},
    migration::{
        execute_migration_create, execute_migration_down, execute_migration_status,
        execute_migration_up,
    },
    organization::execute_organization_create,
    projects::{
        execute_projects_create, execute_projects_delete, execute_projects_link,
        execute_projects_list,
    },
    query::execute_query,
    secrets_cmd::{execute_secrets_list, execute_secrets_set},
    server::{execute_start, execute_status, execute_stop},
    storage_cmd::{execute_storage_buckets, execute_storage_download, execute_storage_upload},
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
    /// Authenticate CLI against CeeBase Platform
    Login {
        #[arg(short, long)]
        email: Option<String>,
        #[arg(short, long)]
        password: Option<String>,
    },
    /// Log out and clear saved credentials
    Logout,

    /// Initialize a fresh CeeBase database kernel file
    Init {
        #[arg(default_value = "ceebase.cbdb")]
        path: String,
    },

    /// Manage projects on CeeBase Platform
    Projects {
        #[command(subcommand)]
        cmd: ProjectsCommands,
    },

    /// Manage database operations, connections, and schemas
    Db {
        #[command(subcommand)]
        cmd: DbCommands,
    },

    /// Manage database migrations
    Migration {
        #[command(subcommand)]
        cmd: MigrationCommands,
    },

    /// Manage authentication and identity
    Auth {
        #[command(subcommand)]
        cmd: AuthCommands,
    },

    /// Manage object storage buckets and files
    Storage {
        #[command(subcommand)]
        cmd: StorageCommands,
    },

    /// Manage serverless functions and jobs
    Functions {
        #[command(subcommand)]
        cmd: FunctionsCommands,
    },

    /// Manage project secrets
    Secrets {
        #[command(subcommand)]
        cmd: SecretsCommands,
    },

    /// Fetch and display platform logs
    Logs,

    /// Deploy project to production
    Deploy,

    /// Diagnose system health and dependencies
    Doctor,

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
enum ProjectsCommands {
    /// List all projects
    List,
    /// Create a new project
    Create {
        name: String,
        #[arg(short, long)]
        org: Option<String>,
    },
    /// Delete a project
    Delete { id: String },
    /// Link current directory to a project
    Link { id: String },
}

#[derive(Subcommand)]
enum DbCommands {
    /// Connect to database in interactive TUI mode
    Connect { target: Option<String> },
    /// Open interactive SQL shell
    Shell { target: Option<String> },
    /// Pull remote schema definitions
    Pull,
    /// Push local migrations to remote database
    Push,
    /// Reset database to empty clean state
    Reset { target: Option<String> },
    /// Dump database schema and data to SQL file
    Dump {
        target: Option<String>,
        #[arg(short, long)]
        out: Option<String>,
    },
    /// Restore database from SQL dump file
    Restore {
        dump_file: String,
        #[arg(short, long)]
        target: Option<String>,
    },
}

#[derive(Subcommand)]
enum MigrationCommands {
    /// Create a new SQL migration file
    Create { name: String },
    /// Apply pending migrations
    Up,
    /// Rollback the latest migration
    Down,
    /// Show migration status
    Status,
}

#[derive(Subcommand)]
enum AuthCommands {
    /// List application users
    Users,
    /// List configured authentication providers
    Providers,
}

#[derive(Subcommand)]
enum StorageCommands {
    /// List storage buckets
    Buckets,
    /// Upload a file to a bucket
    Upload {
        bucket: String,
        file: String,
        #[arg(short, long)]
        dest: Option<String>,
    },
    /// Download a file from a bucket
    Download {
        bucket: String,
        remote: String,
        #[arg(short, long)]
        out: Option<String>,
    },
}

#[derive(Subcommand)]
enum FunctionsCommands {
    /// Deploy a serverless function
    Deploy { name: String, file: String },
    /// Invoke a function
    Invoke {
        name: String,
        #[arg(short, long)]
        payload: Option<String>,
    },
    /// View function invocation logs
    Logs { name: String },
}

#[derive(Subcommand)]
enum SecretsCommands {
    /// Set a secret value
    Set { key: String, value: String },
    /// List configured secrets
    List,
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
        // Platform commands
        Some(Commands::Login { email, password }) => {
            execute_login(email.as_deref(), password.as_deref())
        }
        Some(Commands::Logout) => execute_logout(),
        Some(Commands::Projects { cmd }) => match cmd {
            ProjectsCommands::List => execute_projects_list(),
            ProjectsCommands::Create { name, org } => {
                execute_projects_create(&name, org.as_deref())
            }
            ProjectsCommands::Delete { id } => execute_projects_delete(&id),
            ProjectsCommands::Link { id } => execute_projects_link(&id),
        },
        Some(Commands::Db { cmd }) => match cmd {
            DbCommands::Connect { target } => execute_db_connect(target.as_deref()),
            DbCommands::Shell { target } => execute_db_shell(target.as_deref()),
            DbCommands::Pull => execute_db_pull(),
            DbCommands::Push => execute_db_push(),
            DbCommands::Reset { target } => execute_db_reset(target.as_deref()),
            DbCommands::Dump { target, out } => {
                execute_db_dump(target.as_deref(), out.as_deref())
            }
            DbCommands::Restore { dump_file, target } => {
                execute_db_restore(&dump_file, target.as_deref())
            }
        },
        Some(Commands::Migration { cmd }) => match cmd {
            MigrationCommands::Create { name } => execute_migration_create(&name),
            MigrationCommands::Up => execute_migration_up(),
            MigrationCommands::Down => execute_migration_down(),
            MigrationCommands::Status => execute_migration_status(),
        },
        Some(Commands::Auth { cmd }) => match cmd {
            AuthCommands::Users => execute_auth_users(),
            AuthCommands::Providers => execute_auth_providers(),
        },
        Some(Commands::Storage { cmd }) => match cmd {
            StorageCommands::Buckets => execute_storage_buckets(),
            StorageCommands::Upload { bucket, file, dest } => {
                execute_storage_upload(&bucket, &file, dest.as_deref())
            }
            StorageCommands::Download { bucket, remote, out } => {
                execute_storage_download(&bucket, &remote, out.as_deref())
            }
        },
        Some(Commands::Functions { cmd }) => match cmd {
            FunctionsCommands::Deploy { name, file } => execute_functions_deploy(&name, &file),
            FunctionsCommands::Invoke { name, payload } => {
                execute_functions_invoke(&name, payload.as_deref())
            }
            FunctionsCommands::Logs { name } => execute_functions_logs(&name),
        },
        Some(Commands::Secrets { cmd }) => match cmd {
            SecretsCommands::Set { key, value } => execute_secrets_set(&key, &value),
            SecretsCommands::List => execute_secrets_list(),
        },
        Some(Commands::Logs) => execute_logs(),
        Some(Commands::Deploy) => execute_deploy(),
        Some(Commands::Doctor) => execute_doctor(),

        // Engine & Server commands
        Some(Commands::Init { path }) => execute_init(&path),
        Some(Commands::Start { port, host, daemon }) => execute_start(port, &host, daemon),
        Some(Commands::Stop { port }) => execute_stop(port),
        Some(Commands::Status { target }) => execute_status(&target),
        Some(Commands::Connect { target }) => {
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
