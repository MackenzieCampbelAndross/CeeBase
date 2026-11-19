#ifndef CEEBASE_C_H
#define CEEBASE_C_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) || defined(__CYGWIN__)
  #if defined(CEEBASE_BUILD_DLL)
    #define CEEBASE_API __declspec(dllexport)
  #elif defined(CEEBASE_USE_DLL)
    #define CEEBASE_API __declspec(dllimport)
  #else
    #define CEEBASE_API
  #endif
#else
  #define CEEBASE_API __attribute__((visibility("default")))
#endif

// Return error codes
#define CEEBASE_OK 0
#define CEEBASE_ERROR 1
#define CEEBASE_ROW 100
#define CEEBASE_DONE 101

// Data types
#define CEEBASE_TYPE_INTEGER 1
#define CEEBASE_TYPE_BIGINT 2
#define CEEBASE_TYPE_FLOAT 3
#define CEEBASE_TYPE_DOUBLE 4
#define CEEBASE_TYPE_BOOLEAN 5
#define CEEBASE_TYPE_TEXT 6
#define CEEBASE_TYPE_NULL 7

typedef struct ceebase_db ceebase_db;
typedef struct ceebase_stmt ceebase_stmt;

// Lifecycle
CEEBASE_API int ceebase_open(const char* filename, ceebase_db** db);
CEEBASE_API int ceebase_close(ceebase_db* db);
CEEBASE_API const char* ceebase_errmsg(ceebase_db* db);

// Execution
CEEBASE_API int ceebase_exec(ceebase_db* db, const char* sql, char** errmsg);
CEEBASE_API void ceebase_free_errmsg(char* errmsg);

// Prepared statement / cursor workflow
CEEBASE_API int ceebase_prepare(ceebase_db* db, const char* sql, ceebase_stmt** stmt);
CEEBASE_API int ceebase_step(ceebase_stmt* stmt);
CEEBASE_API int ceebase_reset(ceebase_stmt* stmt);
CEEBASE_API int ceebase_finalize(ceebase_stmt* stmt);

// Parameter binding (1-based index)
CEEBASE_API int ceebase_bind_int(ceebase_stmt* stmt, int param_idx, int32_t val);
CEEBASE_API int ceebase_bind_int64(ceebase_stmt* stmt, int param_idx, int64_t val);
CEEBASE_API int ceebase_bind_double(ceebase_stmt* stmt, int param_idx, double val);
CEEBASE_API int ceebase_bind_text(ceebase_stmt* stmt, int param_idx, const char* val, int len);
CEEBASE_API int ceebase_bind_null(ceebase_stmt* stmt, int param_idx);
CEEBASE_API int ceebase_clear_bindings(ceebase_stmt* stmt);

// Column accessors
CEEBASE_API int ceebase_column_count(ceebase_stmt* stmt);
CEEBASE_API const char* ceebase_column_name(ceebase_stmt* stmt, int col_idx);
CEEBASE_API int ceebase_column_type(ceebase_stmt* stmt, int col_idx);
CEEBASE_API int ceebase_column_is_null(ceebase_stmt* stmt, int col_idx);
CEEBASE_API int32_t ceebase_column_int(ceebase_stmt* stmt, int col_idx);
CEEBASE_API int64_t ceebase_column_int64(ceebase_stmt* stmt, int col_idx);
CEEBASE_API double ceebase_column_double(ceebase_stmt* stmt, int col_idx);
CEEBASE_API int ceebase_column_boolean(ceebase_stmt* stmt, int col_idx);
CEEBASE_API const char* ceebase_column_text(ceebase_stmt* stmt, int col_idx);

// Diagnostics
CEEBASE_API int ceebase_changes(ceebase_db* db);
CEEBASE_API const char* ceebase_version(void);

#ifdef __cplusplus
}
#endif

#endif // CEEBASE_C_H
