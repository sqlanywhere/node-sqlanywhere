// ***************************************************************************
// Copyright (c) 2017 SAP SE or an SAP affiliate company. All rights reserved.
// ***************************************************************************
#include <cmath>
#include <stdint.h>
#include "sqlany_utils.h"

struct AddonData {
    Napi::FunctionReference	connection_ctor;
    Napi::FunctionReference	stmt_ctor;
};

void getErrorMsg( int code, std::string &str )
/********************************************/
{
    std::ostringstream message;
    message << "Code: ";
    message << code;
    message << " Msg: ";

    switch( code ) {
	case JS_ERR_INVALID_OBJECT:
	    message << "Invalid Object";
	    break;
	case JS_ERR_INVALID_ARGUMENTS:
	    message << "Invalid Arguments";
	    break;
	case JS_ERR_CONNECTION_ALREADY_EXISTS:
	    message << "Already Connected";
	    break;
	case JS_ERR_INITIALIZING_DBCAPI:
	    message << "Can't initialize DBCAPI";
	    break;
	case JS_ERR_NOT_CONNECTED:
	    message << "No Connection Available";
	    break;
	case JS_ERR_BINDING_PARAMETERS:
	    message << "Can not bind parameter(s)";
	    break;
	case JS_ERR_GENERAL_ERROR:
	    message << "An error occurred";
	    break;
	case JS_ERR_RESULTSET:
	    message << "Error making result set Object";
	    break;
	case JS_ERR_NO_WIDE_STATEMENTS:
	    message << "The DBCAPI library must be upgraded to support wide statements";
	    break;
	default:
	    message << "Unknown Error";
    }
    str = message.str();

}

void getErrorMsg( a_sqlany_connection *conn, std::string &str )
/*************************************************************/
{
    char buffer[SACAPI_ERROR_SIZE];
    int rc;
    rc = api.sqlany_error( conn, buffer, sizeof(buffer) );
    std::ostringstream message;
    message << "Code: ";
    message << rc;
    message << " Msg: ";
    message << buffer;
    str = message.str();
}

void throwError( Napi::Env env, int code )
/****************************************/
{
    std::string message;
    getErrorMsg( code, message );
    throw Napi::Error::New( env, message );
}

Napi::Value reportError( Napi::Env env, int code, Napi::Value callback )
/**********************************************************************/
{
    std::string message;
    getErrorMsg( code, message );
    Napi::Error err = Napi::Error::New( env, message );
    if( !callback.IsFunction() ) {
	throw err;
    }
    callback.As<Napi::Function>().Call( env.Global(),
					{ err.Value(), env.Undefined() } );
    return env.Undefined();
}

// Mirrors v8::Value::IsInt32(): integral numbers in int32 range, except -0
static bool getInt32( const Napi::Value &val, int &out )
/******************************************************/
{
    if( !val.IsNumber() ) {
	return false;
    }
    double d = val.As<Napi::Number>().DoubleValue();
    if( !( d >= INT32_MIN && d <= INT32_MAX ) || ( d == 0 && std::signbit( d ) ) ) {
	return false;
    }
    out = (int)d;
    return (double)out == d;
}

static void copyBytes( const Napi::Value &val, std::vector<char> &dest, size_t &len )
/***********************************************************************************/
{
    // dest always gets a terminating NUL so that it is never empty and
    // strings can be bound as C strings
    if( val.IsString() ) {
	std::string str = val.As<Napi::String>().Utf8Value();
	len = str.size();
	dest.assign( str.c_str(), str.c_str() + len + 1 );
    } else {
	Napi::Buffer<char> buf = val.As<Napi::Buffer<char>>();
	len = buf.Length();
	dest.assign( buf.Data(), buf.Data() + len );
	dest.push_back( '\0' );
    }
}

BindParams::Column *BindParams::addColumn( a_sqlany_bind_param &param )
/*********************************************************************/
{
    memset( &param, 0, sizeof( param ) );
    columns.emplace_back( new Column );
    return columns.back().get();
}

bool BindParams::parseWide( Napi::Array rows )
/********************************************/
{
    num_rows = rows.Length();
    std::vector<Napi::Array> row_arrs;
    for( unsigned r = 0; r < num_rows; r++ ) {
	Napi::Value row = rows.Get( r );
	if( !row.IsArray() ) {
	    return false;
	}
	row_arrs.push_back( row.As<Napi::Array>() );
    }
    unsigned num_cols = row_arrs[0].Length();

    for( unsigned c = 0; c < num_cols; c++ ) {
	// The first non-null value decides the type of the column; the other
	// values must be of the same type or null
	std::vector<Napi::Value> vals;
	a_sqlany_data_type type = A_INVALID_TYPE;
	bool has_null = false;
	for( unsigned r = 0; r < num_rows; r++ ) {
	    Napi::Value val = row_arrs[r].Get( c );
	    a_sqlany_data_type val_type;
	    if( val.IsNull() ) {
		val_type = A_INVALID_TYPE;
		has_null = true;
	    } else if( val.IsNumber() ) {
		val_type = A_DOUBLE;
	    } else if( val.IsString() ) {
		val_type = A_STRING;
	    } else if( val.IsBuffer() ) {
		val_type = A_BINARY;
	    } else {
		return false;
	    }
	    if( type == A_INVALID_TYPE ) {
		type = val_type;
	    } else if( val_type != A_INVALID_TYPE && val_type != type ) {
		return false;
	    }
	    vals.push_back( val );
	}
	if( type == A_INVALID_TYPE ) {
	    type = A_STRING;
	} else if( has_null ) {
	    mixed_nulls = true;
	}

	a_sqlany_bind_param param;
	Column *col = addColumn( param );
	col->nulls.resize( num_rows );
	param.value.type = type;
	param.value.is_null = col->nulls.data();

	if( type == A_DOUBLE ) {
	    col->nums.resize( num_rows );
	    for( unsigned r = 0; r < num_rows; r++ ) {
		col->nulls[r] = vals[r].IsNull();
		if( !col->nulls[r] ) {
		    col->nums[r] = vals[r].As<Napi::Number>().DoubleValue();
		}
	    }
	    param.value.buffer = (char *)col->nums.data();
	    param.value.is_address = false;
	} else {
	    col->data.resize( num_rows );
	    col->ptrs.resize( num_rows, NULL );
	    col->lens.resize( num_rows, 0 );
	    for( unsigned r = 0; r < num_rows; r++ ) {
		col->nulls[r] = vals[r].IsNull();
		if( !col->nulls[r] ) {
		    copyBytes( vals[r], col->data[r], col->lens[r] );
		    col->ptrs[r] = col->data[r].data();
		}
	    }
	    param.value.buffer = (char *)col->ptrs.data();
	    param.value.length = col->lens.data();
	    param.value.is_address = true;
	}
	params.push_back( param );
    }
    return true;
}

void BindParams::addNulls( unsigned num_params )
/**********************************************/
{
    while( params.size() < num_params ) {
	// Laid out as a parsed NULL, or as a column of NULLs for several rows
	a_sqlany_bind_param param;
	Column *col = addColumn( param );
	col->nulls.resize( num_rows, true );
	param.value.type = A_STRING;
	param.value.is_null = col->nulls.data();
	if( num_rows > 1 ) {
	    col->ptrs.resize( num_rows, NULL );
	    col->lens.resize( num_rows, 0 );
	    param.value.buffer = (char *)col->ptrs.data();
	    param.value.length = col->lens.data();
	    param.value.is_address = true;
	}
	params.push_back( param );
    }
}

a_sqlany_data_value BindParams::rowValue( unsigned i, unsigned first ) const
/**************************************************************************/
{
    a_sqlany_data_value value = params[i].value;
    if( first > 0 ) {
	// Multi-row binds hold arrays of doubles or of pointers to the data
	value.buffer += first * ( value.is_address ? sizeof( char * ) : sizeof( double ) );
	if( value.length != NULL ) {
	    value.length += first;
	}
	value.is_null += first;
    }
    return value;
}

bool BindParams::parse( Napi::Array arg )
/***************************************/
{
    num_rows = 1;
    if( arg.Length() == 0 ) {
	return true;
    }
    if( arg.Get( (uint32_t)0 ).IsArray() ) {
	return parseWide( arg );
    }

    for( unsigned i = 0; i < arg.Length(); i++ ) {
	Napi::Value val = arg.Get( i );
	a_sqlany_bind_param param;
	Column *col = addColumn( param );

	if( getInt32( val, col->int_val ) ) {
	    param.value.buffer = (char *)&col->int_val;
	    param.value.type   = A_VAL32;

	} else if( val.IsNumber() ) {
	    col->nums.push_back( val.As<Napi::Number>().DoubleValue() );
	    param.value.buffer = (char *)col->nums.data();
	    param.value.type   = A_DOUBLE;

	} else if( val.IsString() || val.IsBuffer() ) {
	    col->data.resize( 1 );
	    col->lens.resize( 1 );
	    copyBytes( val, col->data[0], col->lens[0] );
	    param.value.buffer = col->data[0].data();
	    param.value.length = col->lens.data();
	    if( val.IsString() ) {
		param.value.type = A_STRING;
		param.value.buffer_size = col->lens[0] + 1;
	    } else {
		param.value.type = A_BINARY;
		param.value.buffer_size = col->lens[0];
	    }

	} else if( val.IsNull() ) {
	    col->nulls.push_back( true );
	    param.value.type = A_STRING;
	    param.value.is_null = col->nulls.data();

	} else {
	    return false;
	}

	params.push_back( param );
    }
    return true;
}

bool ResultSet::fetch( a_sqlany_connection *	conn,
		       a_sqlany_stmt *		sqlany_stmt,
		       std::string &		error_msg )
/***********************************************************/
{
    rows_affected = api.sqlany_affected_rows( sqlany_stmt );
    int num_cols = api.sqlany_num_cols( sqlany_stmt );

    if( rows_affected > 0 && num_cols < 1 ) {
	return true;
    }

    rows_affected = -1;
    if( num_cols < 1 ) {
	return true;
    }

    for( int i = 0; i < num_cols; i++ ) {
	a_sqlany_column_info info;
	if( !api.sqlany_get_column_info( sqlany_stmt, i, &info ) ) {
	    getErrorMsg( conn, error_msg );
	    return false;
	}
	col_names.push_back( info.name );
    }

    while( api.sqlany_fetch_next( sqlany_stmt ) ) {
	for( int i = 0; i < num_cols; i++ ) {
	    a_sqlany_data_value value;
	    if( !api.sqlany_get_column( sqlany_stmt, i, &value ) ) {
		getErrorMsg( conn, error_msg );
		return false;
	    }

	    cells.emplace_back();
	    Cell &cell = cells.back();
	    cell.kind = Cell::Number;
	    if( *(value.is_null) ) {
		cell.kind = Cell::Null;
		continue;
	    }

	    switch( value.type ) {
		case A_BINARY:
		    cell.kind = Cell::Binary;
		    cell.bytes.assign( value.buffer, *(value.length) );
		    break;
		case A_STRING:
		    cell.kind = Cell::String;
		    cell.bytes.assign( value.buffer, *(value.length) );
		    break;
		case A_VAL64:
		    cell.num = (double)*(long long *)value.buffer;
		    break;
		case A_UVAL64:
		    cell.num = (double)*(unsigned long long *)value.buffer;
		    break;
		case A_VAL32:
		    cell.num = *(int *)value.buffer;
		    break;
		case A_UVAL32:
		    cell.num = *(unsigned int *)value.buffer;
		    break;
		case A_VAL16:
		    cell.num = *(short *)value.buffer;
		    break;
		case A_UVAL16:
		    cell.num = *(unsigned short *)value.buffer;
		    break;
		case A_VAL8:
		    cell.num = *(signed char *)value.buffer;
		    break;
		case A_UVAL8:
		    cell.num = *(unsigned char *)value.buffer;
		    break;
		case A_DOUBLE:
		    cell.num = *(double *)value.buffer;
		    break;
#if _SACAPI_VERSION+0 >= SQLANY_API_VERSION_5
		case A_FLOAT:
		    cell.num = *(float *)value.buffer;
		    break;
#endif
		default:
		    getErrorMsg( JS_ERR_RESULTSET, error_msg );
		    return false;
	    }
	}
	num_rows++;
    }

    // sqlany_fetch_next() also returns false when fetching a row fails, which
    // must not be mistaken for the end of the result set
    char buffer[SACAPI_ERROR_SIZE];
    if( api.sqlany_error( conn, buffer, sizeof(buffer) ) < 0 ) {
	getErrorMsg( conn, error_msg );
	return false;
    }
    return true;
}

Napi::Value ResultSet::toJS( Napi::Env env ) const
/************************************************/
{
    if( rows_affected >= 0 ) {
	return Napi::Number::New( env, rows_affected );
    }
    if( col_names.empty() ) {
	return env.Undefined();
    }

    size_t num_cols = col_names.size();
    std::vector<Napi::String> keys;
    for( const std::string &name : col_names ) {
	keys.push_back( Napi::String::New( env, name ) );
    }

    Napi::Array rows = Napi::Array::New( env, num_rows );
    for( size_t r = 0; r < num_rows; r++ ) {
	Napi::HandleScope scope( env );
	Napi::Object row = Napi::Object::New( env );
	for( size_t c = 0; c < num_cols; c++ ) {
	    const Cell &cell = cells[r * num_cols + c];
	    Napi::Value val;
	    switch( cell.kind ) {
		case Cell::Null:
		    val = env.Null();
		    break;
		case Cell::Number:
		    val = Napi::Number::New( env, cell.num );
		    break;
		case Cell::String:
		    val = Napi::String::New( env, cell.bytes );
		    break;
		case Cell::Binary:
		    val = Napi::Buffer<char>::Copy( env, cell.bytes.data(),
						    cell.bytes.size() );
		    break;
	    }
	    row.Set( keys[c], val );
	}
	rows.Set( (uint32_t)r, row );
    }
    return rows;
}

bool cleanAPI()
/*************/
{
    if( openConnections == 0 ) {
	if( api.initialized ) {
	    api.sqlany_fini();
	    sqlany_finalize_interface( &api );
	    return true;
	}
    }
    return false;
}

class TaskWorker : public Napi::AsyncWorker
{
  public:
    TaskWorker( Napi::Object receiver, Napi::Function callback, Task *task )
	: Napi::AsyncWorker( receiver, callback, "sqlanywhere" ), task( task ) {}

    void keepAlive( Napi::Object obj ) {
	refs.push_back( Napi::Persistent( obj ) );
    }

  protected:
    void Execute() override {
	task->execute();
    }

    void OnOK() override {
	Napi::Env env = Env();
	Napi::Value err = env.Undefined();
	Napi::Value result = env.Undefined();
	if( task->err ) {
	    err = Napi::Error::New( env, task->error_msg ).Value();
	} else {
	    result = task->result( env );
	}
	try {
	    Callback().Call( env.Global(), { err, result } );
	} catch( const Napi::Error &e ) {
	    // Report it as an uncaught exception, like node::FatalException()
	    // did. Leaving it pending instead would rethrow it from any call
	    // into this addon made by an 'uncaughtException' handler.
	    napi_fatal_exception( env, e.Value() );
	}
    }

  private:
    std::unique_ptr<Task>		task;
    std::vector<Napi::ObjectReference>	refs;
};

Napi::Value runTask( const Napi::CallbackInfo &		info,
		     std::unique_ptr<Task>		task,
		     Napi::Value			callback,
		     std::initializer_list<Napi::Object> keep_alive )
/*******************************************************************/
{
    Napi::Env env = info.Env();
    if( !callback.IsFunction() ) {
	task->execute();
	if( task->err ) {
	    throw Napi::Error::New( env, task->error_msg );
	}
	return task->result( env );
    }

    TaskWorker *worker = new TaskWorker( info.This().As<Napi::Object>(),
					 callback.As<Napi::Function>(),
					 task.release() );
    for( const Napi::Object &obj : keep_alive ) {
	worker->keepAlive( obj );
    }
    worker->Queue();
    return env.Undefined();
}

// Stmt Object Functions
StmtObject::StmtObject( const Napi::CallbackInfo &info )
/******************************************************/
    : Napi::ObjectWrap<StmtObject>( info )
    , connection( NULL )
    , sqlany_stmt( NULL )
    , wide_stmt( NULL )
{
}

StmtObject::~StmtObject()
/***********************/
{
    if( connection != NULL ) {
	std::lock_guard<std::mutex> lock( connection->conn_mutex );
	cleanup();
	connection->removeStmt( this );
    }
}

Napi::Function StmtObject::Init( Napi::Env env )
/**********************************************/
{
    return DefineClass( env, "StmtObject", {
	InstanceMethod<&StmtObject::exec>( "exec" ),
	InstanceMethod<&StmtObject::drop>( "drop" ),
	InstanceMethod<&StmtObject::getMoreResults>( "getMoreResults" ),
    } );
}

Napi::Object StmtObject::NewInstance( Napi::Env env )
/***************************************************/
{
    return env.GetInstanceData<AddonData>()->stmt_ctor.New( {} );
}

void StmtObject::cleanup( void )
/******************************/
{
    // caller must hold the connection mutex
    if( sqlany_stmt != NULL ) {
	api.sqlany_free_stmt( sqlany_stmt );
	sqlany_stmt = NULL;
    }
    if( wide_stmt != NULL ) {
	api.sqlany_free_stmt( wide_stmt );
	wide_stmt = NULL;
    }
}

// Connection Functions

std::string HashToString( Napi::Object obj )
/******************************************/
{
    Napi::Env env = obj.Env();
    napi_value names;
    napi_status status = napi_get_all_property_names(
	env, obj, napi_key_own_only,
	(napi_key_filter)( napi_key_enumerable | napi_key_skip_symbols ),
	napi_key_numbers_to_strings, &names );
    if( status != napi_ok ) {
	throw Napi::Error::New( env );
    }
    Napi::Array keys( env, names );
    std::string params = "";
    for( uint32_t i = 0; i < keys.Length(); i++ ) {
	Napi::Value key = keys.Get( i );
	if( i > 0 ) {
	    params += ";";
	}
	params += key.ToString().Utf8Value();
	params += "=";
	params += obj.Get( key ).ToString().Utf8Value();
    }
    return params;
}

Connection::Connection( const Napi::CallbackInfo &info )
/******************************************************/
    : Napi::ObjectWrap<Connection>( info )
    , conn( NULL )
    , sqlca_connection( false )
{
    if( info.Length() > 1 ) {
	throwError( info.Env(), JS_ERR_INVALID_ARGUMENTS );
    }

    if( info.Length() == 1 ) {
	if( info[0].IsString() ) {
	    conn_arg = info[0].As<Napi::String>().Utf8Value();
	} else if( info[0].IsObject() ) {
	    conn_arg = HashToString( info[0].As<Napi::Object>() );
	} else if( !info[0].IsUndefined() && !info[0].IsNull() ) {
	    throwError( info.Env(), JS_ERR_INVALID_ARGUMENTS );
	}
    }
}

Connection::~Connection()
/***********************/
{
    std::lock_guard<std::mutex> api_lock( api_mutex );
    std::lock_guard<std::mutex> lock( conn_mutex );

    // Statement objects can outlive the connection object
    for( StmtObject *stmt : statements ) {
	stmt->cleanup();
	stmt->connection = NULL;
    }
    statements.clear();

    if( conn != NULL ) {
	if( !sqlca_connection ) {
	    api.sqlany_disconnect( conn );
	}
	api.sqlany_free_connection( conn );
	conn = NULL;
	openConnections--;
    }

    cleanAPI();
}

void Connection::cleanupStmts( void )
/***********************************/
{
    // caller must hold conn_mutex
    for( StmtObject *stmt : statements ) {
	stmt->cleanup();
    }
}

void Connection::removeStmt( class StmtObject *stmt )
/***************************************************/
{
    // caller must hold conn_mutex
    for( auto it = statements.begin(); it != statements.end(); it++ ) {
	if( *it == stmt ) {
	    statements.erase( it );
	    return;
	}
    }
}

Napi::Function Connection::Init( Napi::Env env )
/**********************************************/
{
    return DefineClass( env, "Connection", {
	InstanceMethod<&Connection::exec>( "exec" ),
	InstanceMethod<&Connection::prepare>( "prepare" ),
	InstanceMethod<&Connection::connect>( "connect" ),
	InstanceMethod<&Connection::disconnect>( "disconnect" ),
	InstanceMethod<&Connection::disconnect>( "close" ),
	InstanceMethod<&Connection::commit>( "commit" ),
	InstanceMethod<&Connection::rollback>( "rollback" ),
	InstanceMethod<&Connection::connected>( "connected" ),
    } );
}

static Napi::Value CreateConnection( const Napi::CallbackInfo &info )
/*******************************************************************/
{
    // Only the first argument is passed on, as before
    return info.Env().GetInstanceData<AddonData>()->connection_ctor.New( { info[0] } );
}

static Napi::Object Init( Napi::Env env, Napi::Object exports )
/*************************************************************/
{
    AddonData *data = new AddonData;
    data->stmt_ctor = Napi::Persistent( StmtObject::Init( env ) );
    data->connection_ctor = Napi::Persistent( Connection::Init( env ) );
    env.SetInstanceData( data );

    exports.Set( "createConnection",
		 Napi::Function::New( env, CreateConnection, "createConnection" ) );
    return exports;
}

NODE_API_MODULE( sqlanywhere, Init )
