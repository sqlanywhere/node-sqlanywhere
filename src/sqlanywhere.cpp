// ***************************************************************************
// Copyright (c) 2017 SAP SE or an SAP affiliate company. All rights reserved.
// ***************************************************************************
#include <stdint.h>
#include "sqlany_utils.h"

SQLAnywhereInterface api;
std::atomic<unsigned> api_max_ver( 0 );
unsigned openConnections = 0;
std::mutex api_mutex;

// Execute and fetch

class ExecuteTask : public Task {
  public:
    // stmt_obj is NULL when executing sql directly on the connection
    ExecuteTask( Connection *obj, StmtObject *stmt_obj )
	: obj( obj ), stmt_obj( stmt_obj ) {}

    // Returns 0 or the error code to report.
    int setBindParameters( Napi::Value arg ) {
	if( !arg.IsArray() ) {
	    return 0;
	}
	if( !binds.parse( arg.As<Napi::Array>() ) ) {
	    return JS_ERR_BINDING_PARAMETERS;
	}
	if( binds.num_rows > 1 && api_max_ver < SQLANY_API_VERSION_4 ) {
	    // can't support wide inserts with older dbcapi
	    return JS_ERR_NO_WIDE_STATEMENTS;
	}
	return 0;
    }

    void execute() override {
	std::lock_guard<std::mutex> lock( obj->conn_mutex );
	a_sqlany_connection *conn = obj->conn;

	if( conn == NULL ) {
	    fail( JS_ERR_NOT_CONNECTED );
	    return;
	}

	if( stmt_obj != NULL ) {
	    a_sqlany_stmt *sqlany_stmt = stmt_obj->sqlany_stmt;
	    if( sqlany_stmt == NULL ) {
		fail( JS_ERR_INVALID_OBJECT );
		return;
	    }
	    run( conn, sqlany_stmt );
	    return;
	}

	if( sql.length() == 0 ) {
	    fail( JS_ERR_INVALID_OBJECT );
	    return;
	}
	a_sqlany_stmt *sqlany_stmt = api.sqlany_prepare( conn, sql.c_str() );
	if( sqlany_stmt == NULL ) {
	    fail( conn );
	    return;
	}
	run( conn, sqlany_stmt );
	api.sqlany_free_stmt( sqlany_stmt );
    }

    Napi::Value result( Napi::Env env ) override {
	return results.toJS( env );
    }

    std::string		sql;

  protected:
    Connection		*obj;
    StmtObject		*stmt_obj;
    BindParams		binds;
    ResultSet		results;

  private:
    void run( a_sqlany_connection *conn, a_sqlany_stmt *sqlany_stmt ) {
	// The server takes a parameter left unbound as NULL, but then keeps
	// it NULL in later executions on the connection, even of other
	// statements, and can crash. So missing values are bound as NULL.
	// This also binds every input parameter again, as statements are not
	// reset and would otherwise keep bindings to freed buffers.
	int num_params = api.sqlany_num_params( sqlany_stmt );
	if( num_params > 0 ) {
	    binds.addNulls( num_params );
	}

	// DBCAPI fails to execute several rows of a statement with a result set
	bool wide = binds.num_rows > 1 && api.sqlany_num_cols( sqlany_stmt ) == 0;
	if( wide && binds.mixed_nulls ) {
	    runRowByRow( conn, sqlany_stmt );
	    return;
	}
	// Even when it fails, so that sqlany_stmt only ever runs single rows
	if( binds.num_rows > 1 && stmt_obj != NULL ) {
	    runWide( conn );
	    return;
	}
	if( executeRows( conn, sqlany_stmt, 0, binds.num_rows ) &&
	    !results.fetch( conn, sqlany_stmt, error_msg ) ) {
	    err = true;
	}
    }

    // Executes all rows on the wide handle of the prepared statement.
    void runWide( a_sqlany_connection *conn ) {
	a_sqlany_stmt *wide_stmt = stmt_obj->wide_stmt;
	if( wide_stmt == NULL ) {
	    wide_stmt = api.sqlany_prepare( conn, stmt_obj->sql.c_str() );
	    if( wide_stmt == NULL ) {
		fail( conn );
		return;
	    }
	    stmt_obj->wide_stmt = wide_stmt;
	}

	// The batch size goes first, so that DBCAPI never takes the new
	// parameters for as many rows as an earlier execution had
	if( !api.sqlany_set_batch_size( wide_stmt, binds.num_rows ) ||
	    !bindParams( wide_stmt, 0 ) ) {
	    fail( conn );
	    return;
	}
	if( !api.sqlany_execute( wide_stmt ) ) {
	    fail( conn );
	    return;
	}
	if( !results.fetch( conn, wide_stmt, error_msg ) ) {
	    err = true;
	}
    }

    // DBCAPI binds NULL with a different type than other values, which the
    // server rejects in a multi-row execution (error -700), so the rows are
    // executed one at a time. Rolling back to the savepoint undoes the
    // earlier rows if one fails, as a failing multi-row execution is
    // undone. A commit by auto_commit or unchained mode releases the
    // savepoint, so errors about it not existing are ignored.
    void runRowByRow( a_sqlany_connection *conn, a_sqlany_stmt *sqlany_stmt ) {
	if( !api.sqlany_execute_immediate( conn, "SAVEPOINT" ) ) {
	    fail( conn );
	    return;
	}
	int affected = 0;
	for( unsigned row = 0; row < binds.num_rows; row++ ) {
	    if( !executeRows( conn, sqlany_stmt, row, 1 ) ) {
		api.sqlany_execute_immediate( conn, "ROLLBACK TO SAVEPOINT" );
		return;
	    }
	    int rows = api.sqlany_affected_rows( sqlany_stmt );
	    if( rows > 0 ) {
		affected += rows;
	    }
	}
	api.sqlany_execute_immediate( conn, "RELEASE SAVEPOINT" );
	results.setAffectedRows( affected );
    }

    // Executes the statement with num_rows rows of bind parameters from row
    // first on. On failure sets the error and returns false. Several rows are
    // only executed on a statement that is used once.
    bool executeRows( a_sqlany_connection *conn, a_sqlany_stmt *sqlany_stmt,
		      unsigned first, unsigned num_rows ) {
	// The statement is not reset: after sqlany_reset(), DBCAPI describes
	// the IN parameters of a CALL that also has OUT parameters as OUT
	// ones, so their values would be lost or crash it
	if( !bindParams( sqlany_stmt, first ) ) {
	    fail( conn );
	    return false;
	}

	if( num_rows > 1 && !api.sqlany_set_batch_size( sqlany_stmt, num_rows ) ) {
	    fail( conn );
	    return false;
	}

	if( !api.sqlany_execute( sqlany_stmt ) ) {
	    fail( conn );
	    return false;
	}
	return true;
    }

    // Binds the parameters of the rows from row first on. Returns false on
    // failure, leaving the error with the connection.
    bool bindParams( a_sqlany_stmt *sqlany_stmt, unsigned first ) {
	for( unsigned int i = 0; i < binds.params.size(); i++ ) {
	    a_sqlany_bind_param 	param;

	    if( !api.sqlany_describe_bind_param( sqlany_stmt, i, &param ) ) {
		return false;
	    }

	    // Output of procedures is not returned, and DBCAPI would write it
	    // into the buffers of the values. So an OUT parameter is never
	    // bound, ignoring its value, and an INOUT one only passes its value
	    // in. Never binding it also keeps a statement that is not reset
	    // from writing into the buffers of an earlier execution.
	    if( param.direction == DD_OUTPUT ) {
		continue;
	    }
	    param.direction = DD_INPUT;
	    param.value = binds.rowValue( i, first );

	    if( !api.sqlany_bind_param( sqlany_stmt, i, &param ) ) {
		return false;
	    }
	}
	return true;
    }
};

class GetMoreResultsTask : public ExecuteTask {
  public:
    GetMoreResultsTask( Connection *obj, StmtObject *stmt_obj )
	: ExecuteTask( obj, stmt_obj ) {}

    void execute() override {
	std::lock_guard<std::mutex> lock( obj->conn_mutex );
	a_sqlany_connection *conn = obj->conn;

	if( conn == NULL ) {
	    fail( JS_ERR_NOT_CONNECTED );
	    return;
	}

	a_sqlany_stmt *sqlany_stmt = stmt_obj->sqlany_stmt;
	if( sqlany_stmt == NULL ) {
	    fail( JS_ERR_INVALID_OBJECT );
	    return;
	}

	if( !api.sqlany_get_next_result( sqlany_stmt ) ) {
	    char buffer[SACAPI_ERROR_SIZE];
	    if( api.sqlany_error( conn, buffer, sizeof(buffer) ) != 105 ) {
		// 105 means "procedure has completed" - i.e. no more result
		// sets. Just return undefined, not an error
		fail( conn );
	    }
	    return;
	}

	if( !results.fetch( conn, sqlany_stmt, error_msg ) ) {
	    err = true;
	}
    }
};

// Checks the arguments of Statement::exec() and Statement::getMoreResults().
// Returns the connection object to keep alive while the statement runs.
static Napi::Object checkStmt( Napi::Env env, StmtObject *stmt, Napi::Value callback,
			       bool &ok )
/*************************************************************************************/
{
    ok = false;
    if( stmt->connection == NULL || stmt->connection->conn == NULL ||
	stmt->sqlany_stmt == NULL ) {
	reportError( env, JS_ERR_INVALID_OBJECT, callback );
	return Napi::Object();
    }
    // Empty if the connection object has been garbage collected but not freed yet
    Napi::Object conn_obj = stmt->connection->Value();
    if( conn_obj.IsEmpty() ) {
	reportError( env, JS_ERR_INVALID_OBJECT, callback );
	return Napi::Object();
    }
    ok = true;
    return conn_obj;
}

Napi::Value StmtObject::exec( const Napi::CallbackInfo &info )
/************************************************************/
{
    Napi::Env env = info.Env();
    size_t num_args = info.Length();
    Napi::Value params = env.Undefined();
    Napi::Value callback = env.Undefined();

    if( num_args == 0 ) {

    } else if( num_args == 1 && info[0].IsArray() ) {
	params = info[0];

    } else if( num_args == 1 && info[0].IsFunction() ) {
	callback = info[0];

    } else if( num_args == 2 && info[0].IsArray() && info[1].IsFunction() ) {
	params = info[0];
	callback = info[1];

    } else {
	throwError( env, JS_ERR_INVALID_ARGUMENTS );
    }

    bool ok;
    Napi::Object conn_obj = checkStmt( env, this, callback, ok );
    if( !ok ) {
	return env.Undefined();
    }

    std::unique_ptr<ExecuteTask> task( new ExecuteTask( connection, this ) );
    int code = task->setBindParameters( params );
    if( code != 0 ) {
	return reportError( env, code, callback );
    }
    return runTask( info, std::move( task ), callback, { conn_obj } );
}

Napi::Value StmtObject::getMoreResults( const Napi::CallbackInfo &info )
/**********************************************************************/
{
    Napi::Env env = info.Env();
    size_t num_args = info.Length();
    Napi::Value callback = env.Undefined();

    if( num_args == 0 ) {

    } else if( num_args == 1 && info[0].IsFunction() ) {
	callback = info[0];

    } else {
	throwError( env, JS_ERR_INVALID_ARGUMENTS );
    }

    bool ok;
    Napi::Object conn_obj = checkStmt( env, this, callback, ok );
    if( !ok ) {
	return env.Undefined();
    }

    std::unique_ptr<Task> task( new GetMoreResultsTask( connection, this ) );
    return runTask( info, std::move( task ), callback, { conn_obj } );
}

Napi::Value Connection::exec( const Napi::CallbackInfo &info )
/************************************************************/
{
    Napi::Env env = info.Env();
    size_t num_args = info.Length();
    Napi::Value params = env.Undefined();
    Napi::Value callback = env.Undefined();

    if( num_args >= 1 && info[0].IsString() ) {
	if( num_args == 1 ) {

	} else if( num_args == 2 && info[1].IsArray() ) {
	    params = info[1];

	} else if( num_args == 2 && info[1].IsFunction() ) {
	    callback = info[1];

	} else if( num_args == 3 && info[1].IsArray() && info[2].IsFunction() ) {
	    params = info[1];
	    callback = info[2];

	} else {
	    throwError( env, JS_ERR_INVALID_ARGUMENTS );
	}
    } else {
	throwError( env, JS_ERR_INVALID_ARGUMENTS );
    }

    if( conn == NULL ) {
	return reportError( env, JS_ERR_INVALID_OBJECT, callback );
    }

    std::unique_ptr<ExecuteTask> task( new ExecuteTask( this, NULL ) );
    task->sql = info[0].As<Napi::String>().Utf8Value();
    int code = task->setBindParameters( params );
    if( code != 0 ) {
	return reportError( env, code, callback );
    }
    return runTask( info, std::move( task ), callback );
}

// Prepare

class PrepareTask : public Task {
  public:
    PrepareTask( Connection *obj, StmtObject *stmt_obj )
	: obj( obj ), stmt_obj( stmt_obj ) {}

    void execute() override {
	std::lock_guard<std::mutex> lock( obj->conn_mutex );
	a_sqlany_connection *conn = obj->conn;

	if( conn == NULL ) {
	    fail( JS_ERR_INVALID_OBJECT );
	    return;
	}

	a_sqlany_stmt *sqlany_stmt = api.sqlany_prepare( conn, stmt_obj->sql.c_str() );
	if( sqlany_stmt == NULL ) {
	    fail( conn );
	    return;
	}
	stmt_obj->sqlany_stmt = sqlany_stmt;
    }

    Napi::Value result( Napi::Env env ) override {
	return stmt_obj->Value();
    }

  private:
    Connection		*obj;
    StmtObject		*stmt_obj;
};

Napi::Value Connection::prepare( const Napi::CallbackInfo &info )
/***************************************************************/
{
    Napi::Env env = info.Env();
    Napi::Value callback = env.Undefined();

    if( info.Length() == 1 && info[0].IsString() ) {
	// do nothing
    } else if( info.Length() == 2 && info[0].IsString() && info[1].IsFunction() ) {
	callback = info[1];
    } else {
	throwError( env, JS_ERR_INVALID_ARGUMENTS );
    }

    if( conn == NULL ) {
	return reportError( env, JS_ERR_NOT_CONNECTED, callback );
    }

    Napi::Object stmt = StmtObject::NewInstance( env );
    StmtObject *obj = StmtObject::Unwrap( stmt );
    obj->connection = this;
    obj->sql = info[0].As<Napi::String>().Utf8Value();
    {
	std::lock_guard<std::mutex> lock( conn_mutex );
	statements.push_back( obj );
    }

    std::unique_ptr<Task> task( new PrepareTask( this, obj ) );
    return runTask( info, std::move( task ), callback, { stmt } );
}

// Connect and disconnect

// Loads and initializes DBCAPI unless that is done already; caller holds
// api_mutex.
static bool initAPI()
/*******************/
{
    if( api.initialized ) {
	return true;
    }
    if( !sqlany_initialize_interface( &api, NULL ) ) {
	return false;
    }

    unsigned max_ver = 0;
    if( !api.sqlany_init( "Node.js", SQLANY_API_VERSION_4, &max_ver ) ) {
	// As long as the version is >= 2, we're OK. We just have to disable
	// wide inserts
	if( max_ver < SQLANY_API_VERSION_2 ||
	    !api.sqlany_init( "Node.js", max_ver, &max_ver ) ) {
	    sqlany_finalize_interface( &api );
	    return false;
	}
    }
    api_max_ver = max_ver;
    return true;
}

class ConnectTask : public Task {
  public:
    ConnectTask( Connection *obj ) : obj( obj ) {}

    void execute() override {
	std::lock_guard<std::mutex> api_lock( api_mutex );
	std::lock_guard<std::mutex> lock( obj->conn_mutex );

	if( obj->conn != NULL ) {
	    fail( JS_ERR_CONNECTION_ALREADY_EXISTS );
	    return;
	}

	if( !initAPI() ) {
	    fail( JS_ERR_INITIALIZING_DBCAPI );
	    return;
	}

	a_sqlany_connection *conn;
	if( !sqlca_connection ) {
	    conn = api.sqlany_new_connection();
	    if( conn == NULL ) {
		fail( JS_ERR_GENERAL_ERROR );
		cleanAPI();
		return;
	    }
	    if( !api.sqlany_connect( conn, conn_string.c_str() ) ) {
		fail( conn );
		api.sqlany_free_connection( conn );
		cleanAPI();
		return;
	    }

	} else {
	    conn = api.sqlany_make_connection( sqlca );
	    if( conn == NULL ) {
		fail( JS_ERR_GENERAL_ERROR );
		cleanAPI();
		return;
	    }
	}

	obj->sqlca_connection = sqlca_connection;
	obj->conn = conn;
	openConnections++;
    }

    bool		sqlca_connection = false;
    std::string		conn_string;
    void		*sqlca = NULL;

  private:
    Connection		*obj;
};

Napi::Value Connection::connect( const Napi::CallbackInfo &info )
/***************************************************************/
{
    Napi::Env env = info.Env();
    size_t num_args = info.Length();
    Napi::Value arg = env.Undefined();
    Napi::Value callback = env.Undefined();

    if( num_args == 1 && info[0].IsFunction() ) {
	callback = info[0];

    } else if( num_args == 1 ) {
	arg = info[0];

    } else if( num_args == 2 && info[1].IsFunction() &&
	       ( info[0].IsNumber() || info[0].IsString() || info[0].IsObject() ) ) {
	arg = info[0];
	callback = info[1];

    } else if( num_args > 1 ) {
	throwError( env, JS_ERR_INVALID_ARGUMENTS );
    }

    std::unique_ptr<ConnectTask> task( new ConnectTask( this ) );

    if( arg.IsNumber() ) {
	// A DBCAPI connection handle from the JavaScript External Environment
	task->sqlca_connection = true;
	task->sqlca = (void *)(intptr_t)arg.As<Napi::Number>().Int64Value();

    } else {
	std::string &conn_string = task->conn_string;
	conn_string = conn_arg;
	if( arg.IsString() ) {
	    conn_string.append( ";" );
	    conn_string.append( arg.As<Napi::String>().Utf8Value() );
	} else if( arg.IsObject() ) {
	    conn_string.append( ";" );
	    conn_string.append( HashToString( arg.As<Napi::Object>() ) );
	} else if( !arg.IsUndefined() && !arg.IsNull() ) {
	    conn_string.append( ";" );
	    conn_string.append( arg.ToString().Utf8Value() );
	}
	conn_string.append( ";CHARSET='UTF-8'" );
    }

    return runTask( info, std::move( task ), callback );
}

// Returns the callback of a method that only takes an optional callback.
static Napi::Value getOnlyCallback( const Napi::CallbackInfo &info )
/******************************************************************/
{
    if( info.Length() == 0 ) {
	return info.Env().Undefined();
    }
    if( info.Length() == 1 && info[0].IsFunction() ) {
	return info[0];
    }
    throwError( info.Env(), JS_ERR_INVALID_ARGUMENTS );
}

class DisconnectTask : public Task {
  public:
    DisconnectTask( Connection *obj ) : obj( obj ) {}

    void execute() override {
	std::lock_guard<std::mutex> api_lock( api_mutex );
	std::lock_guard<std::mutex> lock( obj->conn_mutex );

	// Disconnecting when not connected is not an error
	if( obj->conn == NULL ) {
	    return;
	}

	obj->cleanupStmts();

	if( !obj->sqlca_connection ) {
	    api.sqlany_disconnect( obj->conn );
	}
	// Must free the connection object or there will be a memory leak
	api.sqlany_free_connection( obj->conn );
	obj->conn = NULL;
	openConnections--;

	if( openConnections <= 0 ) {
	    openConnections = 0;
	    cleanAPI();
	}
    }

  private:
    Connection		*obj;
};

Napi::Value Connection::disconnect( const Napi::CallbackInfo &info )
/******************************************************************/
{
    Napi::Value callback = getOnlyCallback( info );
    std::unique_ptr<Task> task( new DisconnectTask( this ) );
    return runTask( info, std::move( task ), callback );
}

class EndTransactionTask : public Task {
  public:
    EndTransactionTask( Connection *obj, bool commit )
	: obj( obj ), commit( commit ) {}

    void execute() override {
	std::lock_guard<std::mutex> lock( obj->conn_mutex );
	a_sqlany_connection *conn = obj->conn;

	if( conn == NULL ) {
	    fail( JS_ERR_NOT_CONNECTED );
	    return;
	}

	if( !( commit ? api.sqlany_commit( conn ) : api.sqlany_rollback( conn ) ) ) {
	    fail( conn );
	}
    }

  private:
    Connection		*obj;
    bool		commit;
};

Napi::Value Connection::commit( const Napi::CallbackInfo &info )
/**************************************************************/
{
    Napi::Value callback = getOnlyCallback( info );
    std::unique_ptr<Task> task( new EndTransactionTask( this, true ) );
    return runTask( info, std::move( task ), callback );
}

Napi::Value Connection::rollback( const Napi::CallbackInfo &info )
/****************************************************************/
{
    Napi::Value callback = getOnlyCallback( info );
    std::unique_ptr<Task> task( new EndTransactionTask( this, false ) );
    return runTask( info, std::move( task ), callback );
}

Napi::Value Connection::connected( const Napi::CallbackInfo &info )
/*****************************************************************/
{
    return Napi::Boolean::New( info.Env(), conn != NULL );
}

// Drop

class DropTask : public Task {
  public:
    DropTask( Connection *obj, StmtObject *stmt_obj )
	: obj( obj ), stmt_obj( stmt_obj ) {}

    void execute() override {
	// obj is NULL if the statement is dropped already
	if( obj == NULL ) {
	    return;
	}
	std::lock_guard<std::mutex> lock( obj->conn_mutex );
	stmt_obj->cleanup();
	obj->removeStmt( stmt_obj );
    }

    Napi::Value result( Napi::Env env ) override {
	// The statement is registered with its connection as long as
	// connection is set
	if( obj != NULL ) {
	    stmt_obj->connection = NULL;
	}
	return env.Undefined();
    }

  private:
    Connection		*obj;
    StmtObject		*stmt_obj;
};

Napi::Value StmtObject::drop( const Napi::CallbackInfo &info )
/************************************************************/
{
    Napi::Value callback = getOnlyCallback( info );
    Connection *obj = connection;

    // Keep the connection object alive until the statement is freed
    Napi::Object conn_obj;
    if( obj != NULL ) {
	conn_obj = obj->Value();
	if( conn_obj.IsEmpty() ) {
	    // The connection object is about to free the statement
	    obj = NULL;
	}
    }

    std::unique_ptr<Task> task( new DropTask( obj, this ) );
    if( obj == NULL ) {
	return runTask( info, std::move( task ), callback );
    }
    return runTask( info, std::move( task ), callback, { conn_obj } );
}
