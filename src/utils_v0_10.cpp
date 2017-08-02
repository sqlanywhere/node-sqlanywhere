// ***************************************************************************
// Copyright (c) 2017 SAP SE or an SAP affiliate company. All rights reserved.
// ***************************************************************************
#include "nodever_cover.h"

#if v010

#include "sqlany_utils.h"

using namespace v8;
using namespace node;

void getErrorMsg( int code, std::string &str ) 
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

void throwError( a_sqlany_connection *conn ) 
{
    std::string message;
    getErrorMsg( conn, message );
    ThrowException( Exception::Error( String::New(  message.c_str() ) ) );
}

void throwError( int code ) 
{
    std::string message;
    getErrorMsg( code, message );
    ThrowException( Exception::Error( String::New(  message.c_str() ) ) );
}

void callBack( std::string *str, Persistent<Function> callback, Local<Value> Result, bool callback_required ) 
{
    // If string is NULL, then there is no error
    if( callback_required ) {
	if( !callback->IsFunction() ) {
	    throwError( JS_ERR_INVALID_ARGUMENTS );
	    return;
	}
	
	Local<Value> Err;
	if( str == NULL ) {
	    Err = Local<Value>::New( Undefined() );
	
	} else {
	    Err = Exception::Error( String::New( str->c_str() ) );
	}
	
	int argc = 2;
	Local<Value> argv[2] = { Err,  Result };
	
	TryCatch try_catch;
	callback->Call( Context::GetCurrent()->Global(), argc, argv );
	if( try_catch.HasCaught()) {
	    node::FatalException( try_catch );
	}
	callback.Dispose();
	
    } else {
	if( str != NULL ) {
	    ThrowException( Exception::Error( String::New( str->c_str() ) ) );
	}
    }
    return;
}

void callBack( std::string *str, Local<Value> arg, Local<Value> Result, bool callback_required ) 
{
    // If string is NULL, then there is no error
    if( callback_required ) {
	if( !arg->IsFunction() ) {
	    throwError( JS_ERR_INVALID_ARGUMENTS );
	    return;
	}
	Local<Function> callback = Local<Function>::Cast(arg);
	
	Local<Value> Err;
	if( str == NULL ) {
	    Err = Local<Value>::New( Undefined() );
	
	} else {
	    Err = Exception::Error( String::New( str->c_str() ) );
	}
	
	int argc = 2;
	Local<Value> argv[2] = { Err,  Result };
	
	TryCatch try_catch;
	callback->Call( Context::GetCurrent()->Global(), argc, argv );
	if( try_catch.HasCaught()) {
	    node::FatalException( try_catch );
	}
	
    } else {
	if( str != NULL ) {
	    ThrowException( Exception::Error( String::New( str->c_str() ) ) );
	}
    }
    return;
}

static bool getWideBindParameters( std::vector<ExecuteData *>		&execData,
				   Handle<Value>			arg,
				   std::vector<a_sqlany_bind_param> &	params,
				   unsigned				&num_rows )
/*********************************************************************************/
{
    Handle<Array>	rows = Handle<Array>::Cast( arg );
    num_rows = rows->Length();

    Handle<Array>	row0 = Handle<Array>::Cast( rows->Get(0) );
    unsigned		num_cols = row0->Length();
    unsigned		c;
    
    if( num_cols == 0 ) {
	// if an empty array was passed in, we still need ExecuteData
	ExecuteData *ex = new ExecuteData;
	execData.push_back( ex );
	return true;
    }

    // Make sure that each array in the list has the same number and types
    // of values
    for( unsigned int r = 1; r < num_rows; r++ ) {
	Handle<Array>	row = Handle<Array>::Cast( rows->Get(r) );
	for( c = 0; c < num_cols; c++ ) {
	    Handle<Value> val0 = row0->Get(c);
	    Handle<Value> val = row->Get(c);

	    if( ( val0->IsInt32() || val0->IsNumber() ) &&
		( !val->IsInt32() && !val->IsNumber() && !val->IsNull() ) ) {
		return false;
	    }
	    if( val0->IsString() &&
		!val->IsString() && !val->IsNull() ) {
		return false;
	    }
	    if( Buffer::HasInstance( val0 ) &&
		!Buffer::HasInstance( val ) && !val->IsNull() ) {
		return false;
	    }
	}
    }

    for( c = 0; c < num_cols; c++ ) {
	a_sqlany_bind_param 	param;
	memset( &param, 0, sizeof( param ) );

	ExecuteData *ex = new ExecuteData;
	execData.push_back( ex );

	double *	param_double = new double[num_rows];
	ex->addNum( param_double );
	char **		char_arr = new char *[num_rows];
	size_t *	len = new size_t[num_rows];
	ex->addStrings( char_arr, len );
	sacapi_bool *	is_null = new sacapi_bool[num_rows];
	ex->addNull( is_null );
	param.value.is_null	= is_null;
	param.value.is_address	= false;

	if( row0->Get(c)->IsInt32() || row0->Get(c)->IsNumber() ) {
	    param.value.type	= A_DOUBLE;
	    param.value.buffer	= (char *)( param_double );

	} else if( row0->Get(c)->IsString() ) {
	    param.value.type	= A_STRING;
	    param.value.buffer	= (char *)char_arr;
	    param.value.length	= len;
	    param.value.is_address = true;

	} else if( Buffer::HasInstance( row0->Get(c) ) ) {
	    param.value.type	= A_BINARY;
	    param.value.buffer	= (char *)char_arr;
	    param.value.length	= len;
	    param.value.is_address = true;
	    
	} else if( row0->Get(c)->IsNull() ) {

	} else{
	    return false;
	}

	for( unsigned int r = 0; r < num_rows; r++ ) {
	    Handle<Array>	bind_params = Handle<Array>::Cast( rows->Get(r) );
	
	    is_null[r] = false;
	    if( bind_params->Get(c)->IsInt32() || bind_params->Get(c)->IsNumber() ) {
		param_double[r] = bind_params->Get(c)->NumberValue();
	
	    } else if( bind_params->Get(c)->IsString() ) {
		String::Utf8Value paramValue( bind_params->Get(c)->ToString() );
		const char* param_string = (*paramValue);
		len[r] = (size_t)paramValue.length();
		char *param_char = new char[len[r] + 1];
		char_arr[r] = param_char;
		memcpy( param_char, param_string, len[r] + 1 );
		
	    } else if( Buffer::HasInstance( bind_params->Get(c) ) ) {
		len[r] = Buffer::Length( bind_params->Get(c) );
		char *param_char = new char[len[r]];
		char_arr[r] = param_char;
		memcpy( param_char, Buffer::Data( bind_params->Get(c) ), len[r] );

	    } else if( bind_params->Get(c)->IsNull() ) {
		is_null[r] = true;
	    }
	}
    
	params.push_back( param );
    }

    return true;	   
}

bool getBindParameters( std::vector<ExecuteData *>		&execData
			, Handle<Value>				arg
			, std::vector<a_sqlany_bind_param> 	&params
			, unsigned				&num_rows )
/*************************************************************************/
{
		   
    Handle<Array>		bind_params = Handle<Array>::Cast( arg );

    if( bind_params->Length() == 0 ) {
	// if an empty array was passed in, we still need ExecuteData
	ExecuteData *ex = new ExecuteData;
	execData.push_back( ex );
	return true;
    }
    
    if( bind_params->Get(0)->IsArray() ) {
	return getWideBindParameters( execData, arg, params, num_rows );
    }
    num_rows = 1;

    ExecuteData *ex = new ExecuteData;
    execData.push_back( ex );

    for( unsigned int i = 0; i < bind_params->Length(); i++ ) {
	a_sqlany_bind_param 	param;
	memset( &param, 0, sizeof( param ) );

	if( bind_params->Get(i)->IsInt32() ) {
	    int *param_int = new int;
	    *param_int = bind_params->Get(i)->Int32Value();
	    ex->addInt( param_int );
	    param.value.buffer = (char *)( param_int );
	    param.value.type   = A_VAL32;
	    
	} else if( bind_params->Get(i)->IsNumber() ) {
	    double *param_double = new double;
	    *param_double = bind_params->Get(i)->NumberValue(); // Remove Round off Error
	    ex->addNum( param_double );
	    param.value.buffer = (char *)( param_double );
	    param.value.type   = A_DOUBLE;
	
	} else if( bind_params->Get(i)->IsString() ) {
	    String::Utf8Value paramValue( bind_params->Get(i)->ToString() );
	    const char* param_string = (*paramValue);
	    size_t *len = new size_t;
	    *len = (size_t)paramValue.length();
	    
	    char **char_arr = new char *;
	    char *param_char = new char[*len + 1];
	    *char_arr = param_char;

	    memcpy( param_char, param_string, ( *len ) + 1 );
	    ex->addStrings( char_arr, len );
	    
	    param.value.type = A_STRING;
	    param.value.buffer = param_char;
	    param.value.length = len;
	    param.value.buffer_size = *len + 1;
	    
	} else if( Buffer::HasInstance( bind_params->Get(i) ) ) {
	    size_t *len = new size_t;
	    *len = Buffer::Length( bind_params->Get(i) );
	    char **char_arr = new char *;
	    char *param_char = new char[*len];
	    *char_arr = param_char;

	    memcpy( param_char, Buffer::Data( bind_params->Get(i) ), *len );
	    ex->addStrings( char_arr, len );
	    
	    param.value.type = A_BINARY;
	    param.value.buffer = param_char;
	    param.value.length = len;
	    param.value.buffer_size = sizeof( param_char );
	    
	} else if( bind_params->Get(i)->IsNull() ) {
	    param.value.type = A_STRING;
	    sacapi_bool *is_null = new sacapi_bool;
	    param.value.is_null = is_null;
	    ex->addNull( is_null );
	    *is_null = true;
	    
	} else{
	    return false;
	}
	    
	params.push_back( param );
    }

    return true;	   
}

bool getResultSet( Local<Value> 			&Result   
		 , int 					&rows_affected
		 , std::vector<char *> 			&colNames
		 , ExecuteData				*execData
		 , std::vector<a_sqlany_data_type> 	&col_types ) {
		   
    int 	num_rows = 0;
    size_t	num_cols = colNames.size();
    
    if( rows_affected >= 0 ) {
	Result = Integer::New( rows_affected );
	return true;
    }
    
    if( num_cols > 0 ) {
	size_t count = 0;
	size_t count_int = 0, count_num = 0, count_string = 0;
	Result = Array::New();
	while( count_int < execData->intSize() ||
	       count_num < execData->numSize() ||
	       count_string < execData->stringSize() ) {
	    Local<Array> ResultSet = Local<Array>::Cast( Result );
	    Local<Object> curr_row = Object::New();
	    num_rows++;
	    for( size_t i = 0; i < num_cols; i++ ) {

		switch( col_types[count] ) {
		    case A_INVALID_TYPE:
			curr_row->Set( String::NewSymbol( colNames[i] ), Null() );
			break;

		    case A_VAL32:
		    case A_VAL16:
		    case A_UVAL16:
		    case A_VAL8:
		    case A_UVAL8:
			if( execData->intIsNull( count_int ) ) {
			    curr_row->Set( String::NewSymbol( colNames[i] ), Null() );
			} else {
			    curr_row->Set( String::NewSymbol( colNames[i] ),
					   Integer::New( execData->getInt( count_int ) ) );
			}
			count_int++;
			break;
			
		    case A_UVAL32:
		    case A_UVAL64:
		    case A_VAL64:
		    case A_DOUBLE:
			if( execData->numIsNull( count_num ) ) {
			    curr_row->Set( String::NewSymbol( colNames[i] ), Null() );
			} else {
			    curr_row->Set( String::NewSymbol( colNames[i] ), Number::New( execData->getNum( count_num ) ) );
			}
			count_num++;
			break;
			
		    case A_BINARY:
			if( execData->stringIsNull( count_string ) ) {
			    curr_row->Set( String::NewSymbol( colNames[i] ), Null() );
			} else {
			    Buffer *bp = Buffer::New( execData->getLen( count_string ) );
			    memcpy( Buffer::Data(bp), execData->getString( count_string ), execData->getLen( count_string ) );
			    curr_row->Set( String::NewSymbol( colNames[i] ), bp->handle_ ) ;
			}
			count_string++;
			break;
			
		    case A_STRING:		    
			if( execData->stringIsNull( count_string ) ) {
			    curr_row->Set( String::NewSymbol( colNames[i] ), Null() );
			} else {
			    curr_row->Set( String::NewSymbol( colNames[i] ),
					   String::New( execData->getString( count_string ), (int)execData->getLen( count_string ) ) );
			}
			count_string++;
			break;
			
                    default:
			return false;
			break;
		}
		ResultSet->Set( num_rows - 1, curr_row );
		count++;
	    }
	}
    } else {
    
	Result = Local<Value>::New( Undefined() );
    }
    
    return true;
}

bool fetchResultSet( a_sqlany_stmt 			*sqlany_stmt
		   , int 				&rows_affected
		   , std::vector<char *> 		&colNames
		   , ExecuteData			*execData
		   , std::vector<a_sqlany_data_type> 	&col_types ) {
    
    a_sqlany_data_value		value;
    int				num_cols = 0;
    
    rows_affected = api.sqlany_affected_rows( sqlany_stmt );
    num_cols = api.sqlany_num_cols( sqlany_stmt );
    
    
    if( rows_affected > 0 && num_cols < 1 ) {
        return true;
    }
    
    rows_affected = -1;
    if( num_cols > 0 ) {
	
	for( int i = 0; i < num_cols; i++ ) {
	    a_sqlany_column_info info;
	    api.sqlany_get_column_info( sqlany_stmt, i, &info );
	    size_t size = strlen( info.name ) + 1;
	    char *name = new char[ size ];
	    memcpy( name, info.name, size );
	    colNames.push_back( name );
	}
	
	int count_string = 0, count_num = 0, count_int = 0;
	while( api.sqlany_fetch_next( sqlany_stmt ) ) {

	    for( int i = 0; i < num_cols; i++ ) {

		if( !api.sqlany_get_column( sqlany_stmt, i, &value ) ) {
		    return false;
		    break;
		}
	
		if( *(value.is_null) ) {
		    col_types.push_back( A_INVALID_TYPE );
		    continue;
		}
		
		switch( value.type ) {
		    case A_BINARY:
		    {
			size_t *size = new size_t;
			*size = *(value.length);
			char *val = new char[ *size ];
			memcpy( val, value.buffer, *size );
			execData->addString( val, size );
			count_string++;
			break;
		    }
		    
		    case A_STRING:
		    {
			size_t *size = new size_t;
			*size = (size_t)( (int)*(value.length) );
			char *val = new char[ *size ];
			memcpy( val, (char *)value.buffer, *size );
			execData->addString( val, size );
			count_string++;
			break;
		    }
			
		    case A_VAL64:
		    {
			double *val = new double;
			*val = (double)*(long long *)value.buffer;
			execData->addNum( val );
			count_num++;
			break;
		    }
			
		    case A_UVAL64:
		    {
			double *val = new double;
			*val = (double)*(unsigned long long *)value.buffer;
			execData->addNum( val );
			count_num++;
			break;
		    }
			
		    case A_VAL32:
		    {
			int *val = new int;
			*val = *(int*)value.buffer;
			execData->addInt( val );
			count_int++;
			break;
		    }

		    case A_UVAL32:
		    {
			double *val = new double;
			*val = (double)*(unsigned int*)value.buffer;
			execData->addNum( val );
			count_num++;
			break;
		    }
			
		    case A_VAL16:
		    {
			int *val = new int;
			*val = (int)*(short*)value.buffer;
			execData->addInt( val );
			count_int++;
			break;
		    }

		    case A_UVAL16:
		    {
			int *val = new int;
			*val = (int)*(unsigned short*)value.buffer;
			execData->addInt( val );
			count_int++;
			break;
		    }
			
		    case A_VAL8:
		    {
			int *val = new int;
			*val = (int)*(char *)value.buffer;
			execData->addInt( val );
			count_int++;
			break;
		    }
			
		    case A_UVAL8:
		    {
			int *val = new int;
			*val = (int)*(unsigned char *)value.buffer;
			execData->addInt( val );
			count_int++;
			break;
		    }
			
		    case A_DOUBLE:
		    {
			double *val = new double;
			*val = (double)*(double *)value.buffer;
			execData->addNum( val );
			count_num++;
			break;
		    }
			
                    default:
			return false;
		}
		col_types.push_back( value.type );
	    }
	}
    }
    
    return true;
}

bool cleanAPI() 
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

// Generic Baton and Callback (After) Function
// Use this if the function does not have any return values and
// Does not take any parameters. 
// Create custom Baton and Callback (After) functions otherwise

void Connection::noParamAfter( uv_work_t *req ) 
{
    noParamBaton *baton = static_cast<noParamBaton*>(req->data);
    
    if( baton->err ) {
	callBack( &( baton->error_msg ), baton->callback, Local<Value>::New( Undefined() ), baton->callback_required );
	return;
    }
    
    callBack( NULL, baton->callback, Local<Value>::New( Undefined() ),  baton->callback_required );
    
    delete baton;
    delete req;
}


// Stmt Object Functions
StmtObject::StmtObject() 
{
    connection = NULL;
    sqlany_stmt = NULL;
}
StmtObject::~StmtObject() 
{
    uv_mutex_t *mutex = NULL;
    if( connection != NULL ) {
	mutex = &connection->conn_mutex;
	uv_mutex_lock( mutex );
    }

    cleanup();
    removeConnection();

    if( mutex != NULL ) {
	uv_mutex_unlock( mutex );
    }
}

Persistent<Function> StmtObject::constructor;

void StmtObject::Init() 
{
    // Prepare constructor template
    Local<FunctionTemplate> tpl = FunctionTemplate::New( New );
    tpl->SetClassName( String::NewSymbol( "StmtObject" ) );
    tpl->InstanceTemplate()->SetInternalFieldCount( 1 );
    // Prototype
    tpl->PrototypeTemplate()->Set( String::NewSymbol( "exec" ),
	FunctionTemplate::New( exec )->GetFunction() );
    tpl->PrototypeTemplate()->Set( String::NewSymbol( "drop" ),
	FunctionTemplate::New( drop )->GetFunction() );
    tpl->PrototypeTemplate()->Set( String::NewSymbol( "getMoreResults" ),
	FunctionTemplate::New( getMoreResults )->GetFunction() );
    
    constructor = Persistent<Function>::New( tpl->GetFunction() );
}

Handle<Value> StmtObject::New( const Arguments &args ) 
{
    HandleScope scope;
    StmtObject* obj = new StmtObject();

    obj->Wrap( args.This() );
    return args.This();
}

Handle<Value> StmtObject::NewInstance( const Arguments &args ) 
{
    HandleScope scope;

    const unsigned argc = 1;
    Handle<Value> argv[argc] = { args[0] };
    Local<Object> instance = constructor->NewInstance( argc, argv );

    return scope.Close( instance );
}


void StmtObject::cleanup( void )
/******************************/
{
    if( sqlany_stmt != NULL ) {
	api.sqlany_free_stmt( sqlany_stmt );
	sqlany_stmt = NULL;
    }
}

void StmtObject::removeConnection( void )
/***************************************/
{
    if( connection != NULL ) {
	connection->removeStmt( this );
    	connection = NULL;
    }
}

// Connection Functions

Handle<Value> CreateConnection( const Arguments &args ) 
{
    HandleScope scope;
    return scope.Close( Connection::NewInstance( args ) );
}

Persistent<String> HashToString( Local<Object> obj )
/**************************************************/
{
    Local<Array> props = obj->GetOwnPropertyNames();
    int length = props->Length();
    std::string params = "";
    bool	first = true;
    for( int i = 0; i < length; i++ ) {
	Local<String> key = props->Get(i).As<String>();
	Local<String> val = obj->Get(key).As<String>();
	String::Utf8Value key_utf8( key );
	String::Utf8Value val_utf8( val );
	if( !first ) {
	    params += ";";
	}
	first = false;
	params += std::string(*key_utf8);
	params += "=";
	params += std::string(*val_utf8);
    }
    return Persistent<String>::New( String::New((const char *)params.c_str()) );
}

#if 0
// Handy function for determining what type an object is.
static void CheckArgType( Local<Value> &obj )
/*******************************************/
{
    static const char *type = NULL;
    if( obj->IsArray() ) {
	type = "Array";
    } else if( obj->IsBoolean() ) {
	type = "Boolean";
    } else if( obj->IsBooleanObject() ) {
	type = "BooleanObject";
    } else if( obj->IsDate() ) {
	type = "Date";
    } else if( obj->IsExternal() ) {
	type = "External";
    } else if( obj->IsFunction() ) {
	type = "Function";
    } else if( obj->IsInt32() ) {
	type = "Int32";
    } else if( obj->IsNativeError() ) {
	type = "NativeError";
    } else if( obj->IsNull() ) {
	type = "Null";
    } else if( obj->IsNumber() ) {
	type = "Number";
    } else if( obj->IsNumberObject() ) {
	type = "NumberObject";
    } else if( obj->IsObject() ) {
	type = "Object";
    } else if( obj->IsRegExp() ) {
	type = "RegExp";
    } else if( obj->IsString() ) {
	type = "String";
    } else if( obj->IsStringObject() ) {
	type = "StringObject";
    } else if( obj->IsUint32() ) {
	type = "Uint32";
    } else if( obj->IsUndefined() ) {
	type = "Undefined";
    } else {
	type = "Unknown";
    }
}
#endif

Connection::Connection( const Arguments &args )
/*********************************************/
{
    uv_mutex_init(&conn_mutex);
    conn = NULL;

    if( args.Length() > 1 ) {
	throwError( JS_ERR_INVALID_ARGUMENTS );
	return;
    }

    if( args.Length() == 1 ) {
	//CheckArgType( args[0] );
	if( args[0]->IsString() ) {
	    Local<String> str = args[0]->ToString();
	    int string_len = str->Utf8Length();
	    char *buf = new char[string_len+1];
	    str->WriteUtf8( buf );
	    _arg = Persistent<String>::New( String::New(buf, string_len) );
	    delete [] buf;
	} else if( args[0]->IsObject() ) {
	    _arg = HashToString( args[0]->ToObject() );
	} else if( !args[0]->IsUndefined() && !args[0]->IsNull() ) {
	    throwError( JS_ERR_INVALID_ARGUMENTS );
	} else {
	    _arg = Persistent<String>::New( String::New("") );
	}
    } else {
	_arg = Persistent<String>::New( String::New("") );
    }
}


Connection::~Connection()
/***********************/
{
    scoped_lock api_lock( api_mutex );
    scoped_lock lock( conn_mutex );

    _arg.Dispose();
    cleanupStmts();
    if( conn != NULL ) {
	api.sqlany_disconnect( conn );
	api.sqlany_free_connection( conn );
	conn = NULL;
	openConnections--;
    }
    
    cleanAPI();
};

void Connection::cleanupStmts( void )
/***********************************/
{
    std::vector<void*>::iterator findit;
    for( findit = statements.begin(); findit != statements.end(); findit++ ) {
	StmtObject *s = reinterpret_cast<StmtObject*>(*findit);
	s->cleanup();
    }
}

void Connection::removeStmt( class StmtObject *stmt )
/***************************************************/
{
    // caller must get mutex
    std::vector<void*>::iterator findit;
    for( findit = statements.begin(); findit != statements.end(); findit++ ) {
	if( *findit == reinterpret_cast<void*>(stmt) ) {
	    statements.erase( findit );
	    return;
	}
    }
}

Persistent<Function> Connection::constructor;

void Connection::Init()
/*********************/
{
    // Prepare constructor template
    Local<FunctionTemplate> tpl = FunctionTemplate::New( New );
    tpl->SetClassName( String::NewSymbol( "Connection" ) );
    tpl->InstanceTemplate()->SetInternalFieldCount( 1 );
    
    // Prototype
    tpl->PrototypeTemplate()->Set( String::NewSymbol( "exec" ),
	FunctionTemplate::New( exec )->GetFunction() );
    tpl->PrototypeTemplate()->Set( String::NewSymbol( "prepare" ),
	FunctionTemplate::New( prepare )->GetFunction() );
    tpl->PrototypeTemplate()->Set( String::NewSymbol( "connect" ),
	FunctionTemplate::New( connect )->GetFunction() );
    tpl->PrototypeTemplate()->Set( String::NewSymbol( "disconnect" ),
	FunctionTemplate::New( disconnect )->GetFunction() );
    tpl->PrototypeTemplate()->Set( String::NewSymbol( "close" ),
	FunctionTemplate::New( disconnect )->GetFunction() );
    tpl->PrototypeTemplate()->Set( String::NewSymbol( "commit" ),
	FunctionTemplate::New( commit )->GetFunction() );
    tpl->PrototypeTemplate()->Set( String::NewSymbol( "rollback" ),
	FunctionTemplate::New( rollback )->GetFunction() );
    tpl->PrototypeTemplate()->Set( String::NewSymbol( "connected" ),
	FunctionTemplate::New( connected )->GetFunction() );
    constructor = Persistent<Function>::New( tpl->GetFunction() );
}

Handle<Value> Connection::New( const Arguments &args ) 
{
    HandleScope scope;
    
    Connection *obj = new Connection( args );
    obj->Wrap( args.This() );

    return args.This();
}

Handle<Value> Connection::NewInstance( const Arguments &args ) 
{
    HandleScope scope;

    const unsigned argc = 1;
    Handle<Value> argv[argc] = { args[0] };
    Local<Object> instance = constructor->NewInstance( argc, argv );

    return scope.Close( instance );
}

#endif // v010
