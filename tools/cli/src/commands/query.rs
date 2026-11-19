use crate::engine::CeebaseClient;
use crate::table::{render_table, TableOptions};

pub fn execute_query(target: &str, sql: &str, plain: bool) -> Result<(), String> {
    let client = CeebaseClient::open(target)?;
    let result = client.query(sql);
    let options = TableOptions {
        plain,
        ..Default::default()
    };
    print!("{}", render_table(&result, &options));
    if !result.success {
        std::process::exit(1);
    }
    Ok(())
}
