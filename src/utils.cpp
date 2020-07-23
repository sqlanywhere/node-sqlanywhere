// ***************************************************************************
// Copyright (c) 2018 SAP SE or an SAP affiliate company. All rights reserved.
// ***************************************************************************
#include "nodever_cover.h"
#include "sqlany_utils.h"
#include "nan.h"

#if NODE_MAJOR_VERSION >= 12

using namespace v8;
using namespace node;

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

v8::Local<v8::String> GetUtf8String(v8::Isolate *isolate, std::string msg) {
	auto ret = String::NewFromUtf8(isolate, msg.c_str());
#if NODE_MAJOR_VERSION == 14
	return ret.ToLocalChecked();
#else
	return ret;
#endif
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

void throwError( a_sqlany_connection *conn )
/******************************************/
{
    Isolate *isolate = Isolate::GetCurrent();
    std::string message;
    getErrorMsg( conn, message );
    isolate->ThrowException(
	Exception::Error( GetUtf8String(isolate, message.c_str() )) );
}

void throwError( int code )
/*************************/
{
    Isolate *isolate = Isolate::GetCurrent();
    std::string message;

    getErrorMsg( code, message );
    isolate->ThrowException(
	Exception::Error(GetUtf8String(isolate, message.c_str())) );
}

void callBack( std::string *		str,
	       Persistent<Function> &	callback,
	       Local<Value> &		Result,
	       bool			callback_required )
/*********************************************************/
{
    Isolate *isolate = Isolate::GetCurrent();
    HandleScope scope( isolate );
    Local<Function> local_callback = Local<Function>::New( isolate, callback );

    // If string is NULL, then there is no error
    if( callback_required ) {
	if( !local_callback->IsFunction() ) {
	    throwError( JS_ERR_INVALID_ARGUMENTS );
	    return;
	}

	Local<Value> Err;
	if( str == NULL ) {
	    Err = Local<Value>::New( isolate, Undefined( isolate ) );

	} else {
	    Err = Exception::Error(GetUtf8String(isolate, str->c_str()));
	}

	int argc = 2;
	Local<Value> argv[2] = { Err, Result };

        TryCatch try_catch( isolate );
        Nan::Callback *cb = new Nan::Callback( local_callback );
	cb->Call( argc, argv );
	if( try_catch.HasCaught()) {
	    node::FatalException( isolate, try_catch );
	}
    } else {
	if( str != NULL ) {
	    isolate->ThrowException(
		Exception::Error(GetUtf8String(isolate, str->c_str())));
	}
    }
}

void callBack( std::string *		str,
	       Persistent<Function> &	callback,
	       Persistent<Value> &	Result,
	       bool			callback_required )
/*********************************************************/
{
    Isolate *isolate = Isolate::GetCurrent();
    HandleScope scope( isolate );
    Local<Value> local_result = Local<Value>::New( isolate, Result );

    callBack( str, callback, local_result, callback_required );
}

void callBack( std::string *		str,
	       const Local<Value> &	arg,
	       Local<Value> &		Result,
	       bool			callback_required )
/*********************************************************/
{
    Isolate *isolate = Isolate::GetCurrent();
    HandleScope scope( isolate );

    // If string is NULL, then there is no error
    if( callback_required ) {
	if( !arg->IsFunction() ) {
	    throwError( JS_ERR_INVALID_ARGUMENTS );
	    return;
	}
	Local<Function> callback = Local<Function>::Cast(arg);

	Local<Value> Err;
	if( str == NULL ) {
	    Err = Local<Value>::New( isolate, Undefined( isolate ) );

	} else {
	    Err = Exception::Error(GetUtf8String(isolate, str->c_str()));
	}

	int argc = 2;
	Local<Value> argv[2] = { Err,  Result };

        TryCatch try_catch( isolate );
        Nan::Callback *cb = new Nan::Callback( callback );
        cb->Call( argc, argv );
	if( try_catch.HasCaught()) {
	    node::FatalException( isolate, try_catch );
	}

    } else {
	if( str != NULL ) {
	    isolate->ThrowException(
		Exception::Error(GetUtf8String(isolate, str->c_str())));
	}
    }
}

static bool getWideBindParameters( std::vector<ExecuteData *>		&execData,
	Local<Value>			arg,
				   std::vector<a_sqlany_bind_param> &	params,
				   unsigned				&num_rows )
/*********************************************************************************/
{
	Isolate* isolate = Isolate::GetCurrent();
	Local<Context> context = isolate->GetCurrentContext();
	Local<Array>	rows = Local<Array>::Cast( arg );
   num_rows = rows->Length();

	Local<Array>	row0 = Local<Array>::Cast( rows->Get(context, 0).ToLocalChecked() );
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
	Local<Array>	row = Local<Array>::Cast( rows->Get(context, r).ToLocalChecked() );
	for( c = 0; c < num_cols; c++ ) {
		Local<Value> val0 = row0->Get(context, c).ToLocalChecked();
		Local<Value> val = row->Get(context, c).ToLocalChecked();

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

	if( row0->Get(context, c).ToLocalChecked()->IsInt32() || row0->Get(context, c).ToLocalChecked()->IsNumber() ) {
	   param.value.type	= A_DOUBLE;
	   param.value.buffer	= (char *)( param_double );

	} else if( row0->Get(context, c).ToLocalChecked()->IsString() ) {
	   param.value.type	= A_STRING;
	   param.value.buffer	= (char *)char_arr;
	   param.value.length	= len;
	   param.value.is_address = true;

	} else if( Buffer::HasInstance( row0->Get(context, c).ToLocalChecked() ) ) {
	   param.value.type	= A_BINARY;
	   param.value.buffer	= (char *)char_arr;
	   param.value.length	= len;
	   param.value.is_address = true;

	} else if( row0->Get(context, c).ToLocalChecked()->IsNull() ) {

	} else{
	   return false;
	}

	for( unsigned int r = 0; r < num_rows; r++ ) {
		Local<Array>	bind_params = Local<Array>::Cast( rows->Get(context, r).ToLocalChecked());

	   is_null[r] = false;
	   if( bind_params->Get(context, c).ToLocalChecked()->IsInt32() || bind_params->Get(context, c).ToLocalChecked()->IsNumber() ) {
		param_double[r] = bind_params->Get(context, c).ToLocalChecked()->NumberValue(context).ToChecked();

	   } else if( bind_params->Get(context, c).ToLocalChecked()->IsString() ) {
		Nan::Utf8String paramValue( bind_params->Get(context, c).ToLocalChecked() );
		const char* param_string = (*paramValue);
		len[r] = (size_t)paramValue.length();
		char *param_char = new char[len[r] + 1];
		char_arr[r] = param_char;
		memcpy( param_char, param_string, len[r] + 1 );

	   } else if( Buffer::HasInstance( bind_params->Get(context, c).ToLocalChecked()) ) {
		len[r] = Buffer::Length( bind_params->Get(context, c).ToLocalChecked());
		char *param_char = new char[len[r]];
		char_arr[r] = param_char;
		memcpy( param_char, Buffer::Data( bind_params->Get(context, c).ToLocalChecked()), len[r] );

	   } else if( bind_params->Get(context, c).ToLocalChecked()->IsNull() ) {
		is_null[r] = true;
	   }
	}

	params.push_back( param );
   }

    return true;
}

bool getBindParameters( std::vector<ExecuteData *>		&execData,
	Local<Value> 				arg,
			std::vector<a_sqlany_bind_param> &	params,
			unsigned				&num_rows )
/*************************************************************************/
{

	Isolate* isolate = Isolate::GetCurrent();
	Local<Context> context = isolate->GetCurrentContext();
	Local<Array>		bind_params = Local<Array>::Cast( arg );

    if( bind_params->Length() == 0 ) {
	// if an empty array was passed in, we still need ExecuteData
	ExecuteData *ex = new ExecuteData;
	execData.push_back( ex );
	return true;
    }

    if( bind_params->Get(context, 0).ToLocalChecked()->IsArray() ) {
	return getWideBindParameters( execData, arg, params, num_rows );
    }
    num_rows = 1;

    ExecuteData *ex = new ExecuteData;
    execData.push_back( ex );

    for( unsigned int i = 0; i < bind_params->Length(); i++ ) {
	a_sqlany_bind_param 	param;
	memset( &param, 0, sizeof( param ) );

	if( bind_params->Get(context, i).ToLocalChecked()->IsInt32() ) {
	    int *param_int = new int;
	    *param_int = bind_params->Get(context, i).ToLocalChecked()->Int32Value(context).ToChecked();
	    ex->addInt( param_int );
	    param.value.buffer = (char *)( param_int );
	    param.value.type   = A_VAL32;

	} else if( bind_params->Get(context, i).ToLocalChecked()->IsNumber() ) {
	    double *param_double = new double;
	    *param_double = bind_params->Get(context, i).ToLocalChecked()->NumberValue(context).ToChecked(); // Remove Round off Error
	    ex->addNum( param_double );
	    param.value.buffer = (char *)( param_double );
	    param.value.type   = A_DOUBLE;

	} else if( bind_params->Get(context, i).ToLocalChecked()->IsString() ) {
		Nan::Utf8String paramValue( bind_params->Get(context, i).ToLocalChecked() );
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

	} else if( Buffer::HasInstance( bind_params->Get(context, i).ToLocalChecked() ) ) {
	    size_t *len = new size_t;
	    *len = Buffer::Length( bind_params->Get(context, i).ToLocalChecked() );
	    char **char_arr = new char *;
	    char *param_char = new char[*len];
	    *char_arr = param_char;

	    memcpy( param_char, Buffer::Data( bind_params->Get(context, i).ToLocalChecked() ), *len );
	    ex->addStrings( char_arr, len );

	    param.value.type = A_BINARY;
	    param.value.buffer = param_char;
	    param.value.length = len;
	    param.value.buffer_size = sizeof( param_char );

	} else if( bind_params->Get(context, i).ToLocalChecked()->IsNull() ) {
	    param.value.type = A_STRING;
	    sacapi_bool *is_null = new sacapi_bool;
	    param.value.is_null = is_null;
	    ex->addNull( is_null );
	    is_null[0] = true;

	} else{
	    return false;
	}

	params.push_back( param );
    }

    return true;
}

bool getResultSet( Persistent<Value> &			Result,
		   int &				rows_affected,
		   std::vector<char *> &		colNames,
		   ExecuteData *			execData,
		   std::vector<a_sqlany_data_type> &	col_types )
/*****************************************************************/
{
    Isolate *isolate = Isolate::GetCurrent();
	Local<Context> context = isolate->GetCurrentContext();
    HandleScope scope( isolate );
    int 	num_rows = 0;
    size_t	num_cols = colNames.size();

    if( rows_affected >= 0 ) {
	Result.Reset( isolate, Integer::New( isolate, rows_affected ) );
	return true;
    }

    if( num_cols > 0 ) {
	size_t count = 0;
	size_t count_int = 0, count_num = 0, count_string = 0;
	Local<Array> ResultSet = Array::New( isolate );
	while( count_int < execData->intSize() ||
	       count_num < execData->numSize() ||
	       count_string < execData->stringSize() ) {
	    Local<Object> curr_row = Object::New( isolate );
	    num_rows++;
	    for( size_t i = 0; i < num_cols; i++ ) {
		switch( col_types[count] ) {
		    case A_INVALID_TYPE:
			curr_row->Set(context, GetUtf8String(isolate, colNames[i]),
				       Null( isolate ) );
			break;

		    case A_VAL32:
		    case A_VAL16:
		    case A_UVAL16:
		    case A_VAL8:
		    case A_UVAL8:
			if( execData->intIsNull( count_int ) ) {
			    curr_row->Set(context, GetUtf8String(isolate, colNames[i]),
					   Null( isolate ) );
			} else {
			    curr_row->Set(context, GetUtf8String(isolate, colNames[i]),
					   Integer::New( isolate, execData->getInt( count_int ) ) );
			}
			count_int++;
			break;

		    case A_UVAL32:
		    case A_UVAL64:
		    case A_VAL64:
		    case A_DOUBLE:
			if( execData->numIsNull( count_num ) ) {
			    curr_row->Set(context, GetUtf8String(isolate, colNames[i]),
					   Null( isolate ) );
			} else {
			    curr_row->Set(context, GetUtf8String(isolate, colNames[i]),
					   Number::New( isolate, execData->getNum( count_num ) ) );
			}
			count_num++;
			break;

		    case A_BINARY:
			if( execData->stringIsNull( count_string ) ) {
			    curr_row->Set(context, GetUtf8String(isolate, colNames[i]),
					   Null( isolate ) );
			} else {
			    MaybeLocal<Object> mbuf = node::Buffer::Copy(
				isolate, execData->getString( count_string ),
				execData->getLen( count_string ) );
			    Local<Object> buf = mbuf.ToLocalChecked();
			    curr_row->Set(context, GetUtf8String(isolate, colNames[i]),
					   buf );
			}
			count_string++;
			break;

		    case A_STRING:
			if( execData->stringIsNull( count_string ) ) {
			    curr_row->Set(context, GetUtf8String(isolate, colNames[i]),
					   Null( isolate ) );
			} else {
			    curr_row->Set(context, GetUtf8String(isolate, colNames[i]),
					   String::NewFromUtf8( isolate,
								execData->getString( count_string ),
								NewStringType::kNormal,
								(int)execData->getLen( count_string ) ).ToLocalChecked()
				);
			}
			count_string++;
			break;

		    default:
			return false;
		}
		count++;
	    }
	    ResultSet->Set(context, num_rows - 1, curr_row );
	}
	Result.Reset( isolate, ResultSet );
    } else {
	Result.Reset( isolate, Local<Value>::New( isolate,
						  Undefined( isolate ) ) );
    }

    return true;
}

bool fetchResultSet( a_sqlany_stmt *			sqlany_stmt,
		     int &				rows_affected,
		     std::vector<char *> &		colNames,
		     ExecuteData *			execData,
		     std::vector<a_sqlany_data_type> &	col_types )
/*****************************************************************/
{

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

// Generic Baton and Callback (After) Function
// Use this if the function does not have any return values and
// Does not take any parameters.
// Create custom Baton and Callback (After) functions otherwise

void Connection::noParamAfter( uv_work_t *req )
/*********************************************/
{
    Isolate *isolate = Isolate::GetCurrent();
    HandleScope	scope(isolate);
    noParamBaton *baton = static_cast<noParamBaton*>(req->data);
    Local<Value> undef = Local<Value>::New( isolate, Undefined( isolate ) );

    if( baton->err ) {
	callBack( &( baton->error_msg ), baton->callback, undef,
		  baton->callback_required );
	return;
    }

    callBack( NULL, baton->callback, undef, baton->callback_required );

    delete baton;
    delete req;
}


// Stmt Object Functions
StmtObject::StmtObject()
/**********************/
{
    connection = NULL;
    sqlany_stmt = NULL;
}

StmtObject::~StmtObject()
/***********************/
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

void StmtObject::Init( Isolate *isolate )
/***************************************/
{
    HandleScope	scope(isolate);
	Local<Context> context = isolate->GetCurrentContext();
    // Prepare constructor template
    Local<FunctionTemplate> tpl = FunctionTemplate::New( isolate, New );
	tpl->SetClassName(GetUtf8String(isolate, "StmtObject"));
    tpl->InstanceTemplate()->SetInternalFieldCount( 1 );

    // Prototype
    NODE_SET_PROTOTYPE_METHOD( tpl, "exec", exec );
    NODE_SET_PROTOTYPE_METHOD( tpl, "drop", drop );
    NODE_SET_PROTOTYPE_METHOD( tpl, "getMoreResults", getMoreResults );
    constructor.Reset( isolate, tpl->GetFunction(context).ToLocalChecked() );
}

void StmtObject::New( const FunctionCallbackInfo<Value> &args )
/*************************************************************/
{
    StmtObject* obj = new StmtObject();

    obj->Wrap( args.This() );
    args.GetReturnValue().Set( args.This() );
}

void StmtObject::NewInstance( const FunctionCallbackInfo<Value> &args )
/*********************************************************************/
{
	Isolate* isolate = Isolate::GetCurrent();
	Local<Context> context = isolate->GetCurrentContext();
    Persistent<Object> obj;
    CreateNewInstance( args, obj );
    args.GetReturnValue().Set( obj.Get(isolate) );
}

void StmtObject::CreateNewInstance( const FunctionCallbackInfo<Value> &	args,
				    Persistent<Object> &		obj )
/***************************************************************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope	scope(isolate);
    const unsigned argc = 1;
	Local<Value> argv[argc] = { args[0] };
    Local<Function>cons = Local<Function>::New( isolate, constructor );
#if NODE_MAJOR_VERSION >= 10
    Local<Context> env = isolate->GetCurrentContext();
    MaybeLocal<Object> mlObj = cons->NewInstance( env, argc, argv );
    Local<Object> instance = mlObj.ToLocalChecked();

    obj.Reset( isolate, instance );
#else
    obj.Reset( isolate, cons->NewInstance( argc, argv ) );
#endif
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

void HashToString( Local<Object> obj, Persistent<String> &ret )
/*************************************************************/
{
    Isolate *isolate = Isolate::GetCurrent();
    HandleScope	scope(isolate);
	Local<Context> context = isolate->GetCurrentContext();
    MaybeLocal<Array> props = obj->GetOwnPropertyNames(context);
    int length = props.ToLocalChecked()->Length();
    std::string params = "";
    bool	first = true;
    for( int i = 0; i < length; i++ ) {
	Local<String> key = props.ToLocalChecked()->Get(context, i).ToLocalChecked().As<String>();
	Local<String> val = obj->Get(context, key).ToLocalChecked().As<String>();
	String::Utf8Value key_utf8( isolate, key );
	String::Utf8Value val_utf8( isolate, val );
	if( !first ) {
	    params += ";";
	}
	first = false;
	params += std::string(*key_utf8);
	params += "=";
	params += std::string(*val_utf8);
    }
	ret.Reset(isolate, GetUtf8String(isolate, params.c_str()));
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

Connection::Connection( const FunctionCallbackInfo<Value> &args )
/***************************************************************/
{
    Isolate *isolate = args.GetIsolate();
	Local<Context> context = isolate->GetCurrentContext();
    HandleScope scope( isolate );
    uv_mutex_init(&conn_mutex);
    conn = NULL;

    if( args.Length() > 1 ) {
	throwError( JS_ERR_INVALID_ARGUMENTS );
	return;
    }

    if( args.Length() == 1 ) {
	//CheckArgType( args[0] );
	if( args[0]->IsString() ) {
	    MaybeLocal<String> str = args[0]->ToString(context);
	    int string_len = str.ToLocalChecked()->Utf8Length(isolate);
	    char *buf = new char[string_len+1];
	    str.ToLocalChecked()->WriteUtf8(isolate, buf );
	    _arg.Reset( isolate, GetUtf8String(isolate, buf));
	    delete [] buf;
	} else if( args[0]->IsObject() ) {
		HashToString(args[0]->ToObject(context).ToLocalChecked(), _arg);
	} else if( !args[0]->IsUndefined() && !args[0]->IsNull() ) {
	    throwError( JS_ERR_INVALID_ARGUMENTS );
	} else {
	    _arg.Reset( isolate, GetUtf8String(isolate, ""));
	}
    } else {
	_arg.Reset( isolate, GetUtf8String(isolate, ""));
    }
}


Connection::~Connection()
/***********************/
{
    scoped_lock api_lock( api_mutex );
    scoped_lock lock( conn_mutex );

    _arg.Reset();
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

void Connection::Init( Isolate *isolate )
/***************************************/
{
    HandleScope scope( isolate );
	Local<Context> context = isolate->GetCurrentContext();
    // Prepare constructor template
    Local<FunctionTemplate> tpl = FunctionTemplate::New( isolate, New );
    tpl->SetClassName(GetUtf8String(isolate, "Connection"));
    tpl->InstanceTemplate()->SetInternalFieldCount( 1 );

    // Prototype
    NODE_SET_PROTOTYPE_METHOD( tpl, "exec", exec );
    NODE_SET_PROTOTYPE_METHOD( tpl, "prepare", prepare );
    NODE_SET_PROTOTYPE_METHOD( tpl, "connect", connect );
    NODE_SET_PROTOTYPE_METHOD( tpl, "disconnect", disconnect );
    NODE_SET_PROTOTYPE_METHOD( tpl, "close", disconnect );
    NODE_SET_PROTOTYPE_METHOD( tpl, "commit", commit );
    NODE_SET_PROTOTYPE_METHOD( tpl, "rollback", rollback );
    NODE_SET_PROTOTYPE_METHOD( tpl, "connected", connected );

    constructor.Reset( isolate, tpl->GetFunction(context).ToLocalChecked() );
}

void Connection::New( const FunctionCallbackInfo<Value> &args )
/*************************************************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope scope( isolate );

    if( args.IsConstructCall() ) {
	Connection *obj = new Connection( args );
	obj->Wrap( args.This() );
	args.GetReturnValue().Set( args.This() );
    } else {
	const int argc = 1;
	Local<Value> argv[argc] = { args[0] };
	Local<Function> cons = Local<Function>::New( isolate, constructor );
#if NODE_MAJOR_VERSION >= 10
        Local<Context> env = isolate->GetCurrentContext();
        MaybeLocal<Object> mlObj = cons->NewInstance( env, argc, argv );
        const Local<Object> obj = mlObj.ToLocalChecked();
	args.GetReturnValue().Set( obj );
#else
	args.GetReturnValue().Set( cons->NewInstance( argc, argv ) );
#endif
    }
}

void Connection::NewInstance( const FunctionCallbackInfo<Value> &args )
/*********************************************************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope scope( isolate );
    const unsigned argc = 1;
	Local<Value> argv[argc] = { args[0] };
    Local<Function> cons = Local<Function>::New( isolate, constructor );
#if NODE_MAJOR_VERSION >= 10
    Local<Context> env = isolate->GetCurrentContext();
    MaybeLocal<Object> mlObj = cons->NewInstance( env, argc, argv );
    Local<Object> instance = mlObj.ToLocalChecked();
#else
    Local<Object> instance = cons->NewInstance( argc, argv );
#endif
    args.GetReturnValue().Set( instance );
}

#endif //NODE_MAJOR_VERSION >= 12
