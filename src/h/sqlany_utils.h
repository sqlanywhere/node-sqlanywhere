// ***************************************************************************
// Copyright (c) 2017 SAP SE or an SAP affiliate company. All rights reserved.
// ***************************************************************************
#ifndef SQLANY_UTILS_H
#define SQLANY_UTILS_H

#include <atomic>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <string>
#include <string.h>
#include <sstream>
#include <stdint.h>
#include <vector>
#include <napi.h>
#include "sacapidll.h"
#include "sacapi.h"

#include "errors.h"
#include "connection.h"
#include "stmt.h"

extern SQLAnywhereInterface api;
// Highest DBCAPI version the loaded library supports; valid while api is
// initialized.
extern std::atomic<unsigned> api_max_ver;
extern unsigned openConnections;
// Guards api and openConnections. Lock it before any Connection::conn_mutex.
extern std::mutex api_mutex;

bool cleanAPI(); // Finalizes the API and frees up resources; caller holds api_mutex
void getErrorMsg( a_sqlany_connection *conn, std::string &str );
void getErrorMsg( int code, std::string &str );
[[noreturn]] void throwError( Napi::Env env, int code );

// Passes an error found before an operation could start to callback, or
// throws it when there is no callback.
Napi::Value reportError( Napi::Env env, int code, Napi::Value callback );

std::string HashToString( Napi::Object obj );

// Bind parameter values converted from a JavaScript array. Owns the buffers
// that the entries of params point to.
class BindParams {
  public:
    std::vector<a_sqlany_bind_param>	params;
    unsigned				num_rows = 1;
    // Whether a column of the rows mixes NULL and other values, which
    // DBCAPI can't bind in one multi-row execution
    bool				mixed_nulls = false;

    // Returns false if arg holds a value that can't be bound.
    bool parse( Napi::Array arg );

    // Adds NULL values for the parameters past the parsed ones, up to
    // num_params. Called once, after parse().
    void addNulls( unsigned num_params );

    // The value of parameter i for an execution that starts at row first.
    a_sqlany_data_value rowValue( unsigned i, unsigned first ) const;

  private:
    struct Column {
	int				int_val;
	std::vector<double>		nums;
	std::vector<std::vector<char>>	data;
	std::vector<char *>		ptrs;
	std::vector<size_t>		lens;
	std::vector<sacapi_bool>	nulls;
    };
    std::vector<std::unique_ptr<Column>>	columns;

    bool parseWide( Napi::Array rows );
    Column *addColumn( a_sqlany_bind_param &param );
};

// A result set fetched into native memory, so that it can be read on a
// worker thread and converted to JavaScript on the main thread.
class ResultSet {
  public:
    // Fetches all rows of sqlany_stmt. On failure sets error_msg and returns
    // false.
    bool fetch( a_sqlany_connection *	conn,
		a_sqlany_stmt *		sqlany_stmt,
		std::string &		error_msg );

    // Sets the total number of rows affected by a statement without a result
    // set that was executed in several parts.
    void setAffectedRows( int rows ) { rows_affected = rows > 0 ? rows : -1; }

    // Number of affected rows for DML, an array of row objects for queries,
    // undefined otherwise.
    Napi::Value toJS( Napi::Env env ) const;

  private:
    struct Cell {
	enum Kind : unsigned char { Null, Number, String, Binary } kind;
	double		num;
	std::string	bytes;
    };

    int				rows_affected = -1;
    std::vector<std::string>	col_names;
    size_t			num_rows = 0;
    std::vector<Cell>		cells;
};

// One database operation. execute() does the blocking DBCAPI work and may run
// on a libuv worker thread, so it must not touch JavaScript. result() runs on
// the main thread afterwards, only if execute() succeeded.
class Task {
  public:
    virtual ~Task() {}
    virtual void execute() = 0;
    virtual Napi::Value result( Napi::Env env ) { return env.Undefined(); }

    bool		err = false;
    std::string		error_msg;

  protected:
    void fail( int code ) {
	err = true;
	getErrorMsg( code, error_msg );
    }
    void fail( a_sqlany_connection *conn ) {
	err = true;
	getErrorMsg( conn, error_msg );
    }
};

// Runs task synchronously and returns its result (or throws its error) when
// callback is not a function. Otherwise runs it on the libuv thread pool and
// calls callback( err, result ) afterwards; info.This() and the objects in
// keep_alive can't be garbage collected until then.
Napi::Value runTask( const Napi::CallbackInfo &		info,
		     std::unique_ptr<Task>		task,
		     Napi::Value			callback,
		     std::initializer_list<Napi::Object> keep_alive = {} );

#endif // SQLANY_UTILS_H
