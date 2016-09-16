// ***************************************************************************
// Copyright (c) 2016 SAP SE or an SAP affiliate company. All rights reserved.
// ***************************************************************************
#include "nodever_cover.h"
#include "sqlany_utils.h"

#if !v010

using namespace v8;

SQLAnywhereInterface api;
unsigned openConnections = 0;
uv_mutex_t api_mutex;

struct executeBaton {
    Persistent<Function>		callback;
    bool 				err;
    std::string 			error_msg;
    bool 				callback_required;
    
    Connection 				*obj;
    StmtObject				*stmt_obj;

    bool				free_stmt;
    std::string				stmt;
    std::vector<char*> 			string_vals;
    std::vector<double*> 		num_vals;
    std::vector<int*> 			int_vals;
    std::vector<size_t*> 		string_len;
    std::vector<a_sqlany_bind_param> 	params;
    
    std::vector<char*> 			colNames;
    int 				rows_affected;
    std::vector<a_sqlany_data_type> 	col_types;

    executeBaton() {
	err = false;
	callback_required = false;
	obj = NULL;
	stmt_obj = NULL;
	rows_affected = -1;
	free_stmt = false;
    }

    ~executeBaton() {
	obj = NULL;
	if( stmt_obj != NULL && free_stmt ) {
	    delete stmt_obj;
	    stmt_obj = NULL;
	}
	CLEAN_STRINGS( string_vals );
	CLEAN_STRINGS( colNames );
	CLEAN_NUMS( num_vals );
	CLEAN_NUMS( int_vals );
	CLEAN_NUMS( string_len );
	col_types.clear();
	callback.Reset();

	for( size_t i = 0; i < params.size(); i++ ) {
	    if( params[i].value.is_null != NULL ) {
		delete params[i].value.is_null;
		params[i].value.is_null = NULL;
	    }
        }
	params.clear();
    }
};

static bool fillResult( executeBaton *baton, Persistent<Value> &ResultSet )
/*************************************************************************/
{
    Isolate *isolate = Isolate::GetCurrent();
    HandleScope scope( isolate );
    Local<Value> undef = Local<Value>::New( isolate, Undefined( isolate ) );

    if( baton->err ) {
	callBack( &( baton->error_msg ), baton->callback, undef,
		  baton->callback_required );
	return false;
    }

    if( !getResultSet( ResultSet, baton->rows_affected, baton->colNames,
		       baton->string_vals, baton->num_vals, baton->int_vals,
		       baton->string_len, baton->col_types ) ) {
	getErrorMsg( JS_ERR_RESULTSET, baton->error_msg );
	callBack( &( baton->error_msg ), baton->callback, undef,
		  baton->callback_required );
	return false;
    }
    if( baton->callback_required ) {
	callBack( NULL, baton->callback, ResultSet, baton->callback_required );
    }
    return true;
}

void executeWork( uv_work_t *req )
/********************************/
{
    executeBaton *baton = static_cast<executeBaton*>(req->data);
    scoped_lock lock( baton->obj->conn_mutex );

    if( baton->obj->conn == NULL ) {
	baton->err = true;
	getErrorMsg( JS_ERR_NOT_CONNECTED, baton->error_msg );
	return;
    }
    
   
    a_sqlany_stmt *sqlany_stmt = NULL;
    if( baton->stmt_obj == NULL ) {
	baton->stmt_obj = new StmtObject();
	baton->stmt_obj->connection = baton->obj;
	baton->obj->statements.push_back( baton->stmt_obj );
    } else {
	sqlany_stmt = baton->stmt_obj->sqlany_stmt;
    }

    if( sqlany_stmt == NULL && baton->stmt.length() > 0 ) {
	sqlany_stmt = api.sqlany_prepare( baton->obj->conn,
					  baton->stmt.c_str() );
	if( sqlany_stmt == NULL ) {
	    baton->err = true;
	    getErrorMsg( baton->obj->conn, baton->error_msg );
	    return;
	}
	baton->stmt_obj->sqlany_stmt = sqlany_stmt;
	
    } else if( sqlany_stmt == NULL ) {
	baton->err = true;
	getErrorMsg( JS_ERR_INVALID_OBJECT, baton->error_msg );
	return;
    }
    
    if( !api.sqlany_reset( sqlany_stmt ) ) {
	baton->err = true;
	getErrorMsg( baton->obj->conn, baton->error_msg );
	return;
    }

    for( unsigned int i = 0; i < baton->params.size(); i++ ) {
	a_sqlany_bind_param 	param;
	
	if( !api.sqlany_describe_bind_param( sqlany_stmt, i, &param ) ) {
	    baton->err = true;
	    getErrorMsg( baton->obj->conn, baton->error_msg );
	    return;
	}
	
	param.value.type = baton->params[i].value.type;
	param.value.buffer = baton->params[i].value.buffer;
	
	if( param.value.type == A_STRING || param.value.type == A_BINARY ) {
	    param.value.length = baton->params[i].value.length;
	    param.value.buffer_size = baton->params[i].value.buffer_size;
	}
	
	if( baton->params[i].value.is_null != NULL ) {
	    param.value.is_null = baton->params[i].value.is_null;
	}
	
	if( !api.sqlany_bind_param( sqlany_stmt, i, &param ) ) {
	    baton->err = true;
	    getErrorMsg( baton->obj->conn, baton->error_msg );
	    return;
	}
    }
    
    sacapi_bool success_execute = api.sqlany_execute( sqlany_stmt );
    CLEAN_STRINGS( baton->string_vals );
    CLEAN_NUMS( baton->int_vals );
    CLEAN_NUMS( baton->num_vals );
    baton->string_len.clear();
    
    if( !success_execute ) {
	baton->err = true;
	getErrorMsg( baton->obj->conn, baton->error_msg );
	return;
    }
    
    if( !fetchResultSet( sqlany_stmt, baton->rows_affected, baton->colNames,
			 baton->string_vals, baton->num_vals, baton->int_vals,
			 baton->string_len, baton->col_types ) ) {
	baton->err = true;
	getErrorMsg( baton->obj->conn, baton->error_msg );
	return;
    }
}

void executeAfter( uv_work_t *req )
/*********************************/
{
    Isolate *isolate = Isolate::GetCurrent();
    HandleScope scope( isolate );
    executeBaton *baton = static_cast<executeBaton*>( req->data );
    Persistent<Value> ResultSet;
    fillResult( baton, ResultSet );
    ResultSet.Reset();

    delete baton;
    delete req;
}

NODE_API_FUNC( StmtObject::exec )
/*******************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope scope( isolate );
    StmtObject *obj = ObjectWrap::Unwrap<StmtObject>( args.This() );
    int num_args = args.Length();
    bool callback_required = false, bind_required = false;
    int cbfunc_arg = -1;
    Local<Value> undef = Local<Value>::New( isolate, Undefined( isolate ) );
    
    if( num_args == 0 ) {
	
    } else if( num_args == 1 && args[0]->IsArray() ) {
	bind_required = true;

    } else if( num_args == 1 && args[0]->IsFunction() ) {
	callback_required = true;
	cbfunc_arg = 0;

     } else if( num_args == 2 && args[0]->IsArray() && args[1]->IsFunction() ) {
	callback_required = true;
	bind_required = true;
	cbfunc_arg = 1;

    } else {
	throwError( JS_ERR_INVALID_ARGUMENTS );
	args.GetReturnValue().SetUndefined();
	return;
    }
    
    if( obj == NULL || obj->connection == NULL || obj->connection->conn == NULL ||
	obj->sqlany_stmt == NULL ) {
	std::string error_msg;
	getErrorMsg( JS_ERR_INVALID_OBJECT, error_msg );
	callBack( &( error_msg ), args[cbfunc_arg], undef, callback_required );
	args.GetReturnValue().SetUndefined();
	return;
    }
    
    executeBaton *baton = new executeBaton();
    baton->obj = obj->connection;
    baton->stmt_obj = obj;
    baton->free_stmt = false;
    baton->callback_required = callback_required;
    
    if( bind_required ) {
	if( !getBindParameters( baton->string_vals, baton->num_vals, 
				baton->int_vals, baton->string_len, args[0],
				baton->params ) ) {
	    std::string error_msg;
	    getErrorMsg( JS_ERR_BINDING_PARAMETERS, error_msg );
	    callBack( &( error_msg ), args[cbfunc_arg], undef, callback_required );
	    args.GetReturnValue().SetUndefined();
	    return;
	}
    }

    uv_work_t *req = new uv_work_t();
    req->data = baton;
    
    if( callback_required ) {
	Local<Function> callback = Local<Function>::Cast(args[cbfunc_arg]);
	baton->callback.Reset( isolate, callback );
	
	int status;
	status = uv_queue_work( uv_default_loop(), req, executeWork,
				(uv_after_work_cb)executeAfter );
	assert(status == 0);
	
	args.GetReturnValue().SetUndefined();
	return;
    }
    
    Persistent<Value> ResultSet;
    
    executeWork( req );
    bool success = fillResult( baton, ResultSet );
    delete baton;
    delete req;
    
    if( !success ) {
	args.GetReturnValue().SetUndefined();
	return;
    }
    args.GetReturnValue().Set( ResultSet );
    ResultSet.Reset();
}


NODE_API_FUNC( Connection::exec )
/*******************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope scope( isolate );
    Local<Value> undef = Local<Value>::New( isolate, Undefined( isolate ) );

    int num_args = args.Length();
    bool callback_required = false, bind_required = false;
    int cbfunc_arg = 0;
    if( args[0]->IsString() ) {
	if( num_args == 1 ) {

	} else if( num_args == 2 && args[1]->IsArray() ) {
	    bind_required = true;
	
	} else if( num_args == 2 && args[1]->IsFunction() ) {
	    callback_required = true;
	    cbfunc_arg = 1;
	    
	} else if( num_args == 3 && args[1]->IsArray() && args[2]->IsFunction() ) {
	    callback_required = true;
	    bind_required = true;
	    cbfunc_arg = 2;
	
	} else {
	    throwError( JS_ERR_INVALID_ARGUMENTS );
	    args.GetReturnValue().SetUndefined();
	    return;
	}
    } else {
        throwError( JS_ERR_INVALID_ARGUMENTS );
	args.GetReturnValue().SetUndefined();
	return;
    }

    Connection *obj = ObjectWrap::Unwrap<Connection>( args.This() );
    
    if( obj == NULL || obj->conn == NULL ) {
	std::string error_msg;
	getErrorMsg( JS_ERR_INVALID_OBJECT, error_msg );
	callBack( &( error_msg ), args[cbfunc_arg], undef, callback_required );
	args.GetReturnValue().SetUndefined();
	return;
    }
    
    String::Utf8Value 		param0( args[0]->ToString() );
    
    executeBaton *baton = new executeBaton();
    baton->obj = obj;
    baton->callback_required = callback_required;
    baton->free_stmt = true;
    baton->stmt_obj = NULL;
    baton->stmt = std::string(*param0);
    
    if( bind_required ) {
	if( !getBindParameters( baton->string_vals, baton->num_vals,
				baton->int_vals, baton->string_len, args[1],
				baton->params ) ) {
	    std::string error_msg;
	    getErrorMsg( JS_ERR_BINDING_PARAMETERS, error_msg );
	    callBack( &( error_msg ), args[cbfunc_arg], undef, callback_required );
	    args.GetReturnValue().SetUndefined();
	    return;
	}
    }
    
    uv_work_t *req = new uv_work_t();
    req->data = baton;
    
    if( callback_required ) {
	Local<Function> callback = Local<Function>::Cast(args[cbfunc_arg]);
	baton->callback.Reset( isolate, callback );
	int status;
	status = uv_queue_work( uv_default_loop(), req, executeWork,
				(uv_after_work_cb)executeAfter );
	assert(status == 0);
	
	args.GetReturnValue().SetUndefined();
	return;
    }
    
    Persistent<Value> ResultSet;

    executeWork( req );
    bool success = fillResult( baton, ResultSet );

    delete baton;
    delete req;
    
    if( !success ) {
	args.GetReturnValue().SetUndefined();
	return;
    }
    Local<Value> local_result = Local<Value>::New( isolate, ResultSet );
    args.GetReturnValue().Set( local_result );
    ResultSet.Reset();
}

struct prepareBaton {
    Persistent<Function> 	callback;
    bool 			err;
    std::string 		error_msg;
    bool 			callback_required;
    
    StmtObject 			*obj;
    std::string 		stmt;
    Persistent<Value> 		StmtObj;
    
    prepareBaton() {
	err = false;
	callback_required = false;
	obj = NULL;
    }
    
    ~prepareBaton() {
	obj = NULL;
	callback.Reset();
	StmtObj.Reset();
    }
};

void Connection::prepareWork( uv_work_t *req ) 
/*********************************************/
{
    prepareBaton *baton = static_cast<prepareBaton*>(req->data);
    if( baton->obj == NULL || baton->obj->connection == NULL || 
	baton->obj->connection->conn == NULL ) {
	baton->err = true;
	getErrorMsg( JS_ERR_INVALID_OBJECT, baton->error_msg );
	return;
    }
    
    scoped_lock lock( baton->obj->connection->conn_mutex );

    baton->obj->sqlany_stmt = api.sqlany_prepare( baton->obj->connection->conn,
						  baton->stmt.c_str() );
    
    if( baton->obj->sqlany_stmt == NULL ) {
	baton->err = true;
	getErrorMsg( baton->obj->connection->conn, baton->error_msg );
	return;
    }
}

void Connection::prepareAfter( uv_work_t *req ) 
/**********************************************/
{
    Isolate *isolate = Isolate::GetCurrent();
    HandleScope scope( isolate );
    prepareBaton *baton = static_cast<prepareBaton*>(req->data);
    Local<Value> undef = Local<Value>::New( isolate, Undefined( isolate ) );
    
    if( baton->err ) {
	callBack( &( baton->error_msg ), baton->callback, undef,
		  baton->callback_required );
	delete baton;
	delete req;
	return;
    }
    
    if( baton->callback_required ) {
	Local<Value> StmtObj = Local<Value>::New( isolate, baton->StmtObj );
	callBack( NULL, baton->callback, StmtObj,  baton->callback_required );
	baton->StmtObj.Reset();
    }
    
    delete baton;
    delete req;
}

NODE_API_FUNC( Connection::prepare )
/**********************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope scope( isolate );
    bool callback_required = false;
    int cbfunc_arg = -1;
    Local<Value> undef = Local<Value>::New( isolate, Undefined( isolate ) );

    if( args.Length() == 1 && args[0]->IsString() ) {
	// do nothing
    } else if( args.Length() == 2 && args[0]->IsString() && args[1]->IsFunction() ) {
	callback_required = true;
	cbfunc_arg = 1;
    } else {
	throwError( JS_ERR_INVALID_ARGUMENTS );
	args.GetReturnValue().SetUndefined();
	return;
    }
        
    Connection *db = ObjectWrap::Unwrap<Connection>( args.This() );
    
    if( db == NULL || db->conn == NULL ) {
	std::string error_msg;
	getErrorMsg( JS_ERR_NOT_CONNECTED, error_msg );
	callBack( &( error_msg ), args[cbfunc_arg], undef, callback_required );
	args.GetReturnValue().SetUndefined();
	return;
    }

    Persistent<Object> p_stmt;
    StmtObject::CreateNewInstance( args, p_stmt );
    Local<Object> l_stmt = Local<Object>::New( isolate, p_stmt );
    StmtObject *obj = ObjectWrap::Unwrap<StmtObject>( l_stmt );
    obj->connection = db;
    {
        scoped_lock	lock( db->conn_mutex );
	db->statements.push_back( obj );
    }

    if( obj == NULL ) {
        std::string error_msg;
	getErrorMsg( JS_ERR_GENERAL_ERROR, error_msg );
	callBack( &( error_msg ), args[cbfunc_arg], undef, callback_required );
	args.GetReturnValue().SetUndefined();
	p_stmt.Reset();
	return;
    }
    
    String::Utf8Value 		param0( args[0]->ToString() );
    
    prepareBaton *baton = new prepareBaton();
    baton->obj = obj;
    baton->callback_required = callback_required;    
    baton->stmt =  std::string(*param0);
    
    uv_work_t *req = new uv_work_t();
    req->data = baton;
    
    if( callback_required ) {
	Local<Function> callback = Local<Function>::Cast(args[cbfunc_arg]);
	baton->callback.Reset( isolate, callback );
	baton->StmtObj.Reset( isolate, p_stmt );
	int status;
	status = uv_queue_work( uv_default_loop(), req, prepareWork,
				(uv_after_work_cb)prepareAfter );
	assert(status == 0);
	
	args.GetReturnValue().SetUndefined();
	p_stmt.Reset();
	return;
    }
    
    prepareWork( req );
    bool err = baton->err;
    prepareAfter( req );
    
    if( err ) {
	args.GetReturnValue().SetUndefined();
	return;
    }
    args.GetReturnValue().Set( p_stmt );
    p_stmt.Reset();
}


// Connect and disconnect
// Connect Function
struct connectBaton {
    Persistent<Function> 	callback;
    bool 			err;
    std::string 		error_msg;
    bool 			callback_required;
    
    Connection 			*obj;
    bool 			sqlca_connection;
    std::string 		conn_string;
    void 			*sqlca;
    
    connectBaton() {
	obj = NULL;
	sqlca = NULL;
	sqlca_connection = false;
	err = false;
	callback_required = false;
    }
    
    ~connectBaton() {
	obj = NULL;
	sqlca = NULL;
	callback.Reset();
    }
    
};

void Connection::connectWork( uv_work_t *req ) 
/*********************************************/
{
    connectBaton *baton = static_cast<connectBaton*>(req->data);
    scoped_lock api_lock( api_mutex );
    scoped_lock lock( baton->obj->conn_mutex );
    
    if( baton->obj->conn != NULL ) {
	baton->err = true;
	getErrorMsg( JS_ERR_CONNECTION_ALREADY_EXISTS, baton->error_msg );
	return;
    }
    
    if( api.initialized == false) {
    
	if( !sqlany_initialize_interface( &api, NULL ) ) {
	    baton->err = true;
	    getErrorMsg( JS_ERR_INITIALIZING_DBCAPI, baton->error_msg );
	    return;
	}
    
	if( !api.sqlany_init( "Node.js", SQLANY_API_VERSION_2,
			      &(baton->obj->max_api_ver) )) {
            baton->err = true;
	    getErrorMsg( JS_ERR_INITIALIZING_DBCAPI, baton->error_msg );
	    return;
	}
    }
    
    if( !baton->sqlca_connection ) {
	baton->obj->conn = api.sqlany_new_connection();
	if( !api.sqlany_connect( baton->obj->conn, baton->conn_string.c_str() ) ) {
	    getErrorMsg( baton->obj->conn, baton->error_msg );
	    baton->err = true;
	    api.sqlany_free_connection( baton->obj->conn );
	    baton->obj->conn = NULL;
	    cleanAPI();
	    return;
	}
	
    } else {
	baton->obj->conn = api.sqlany_make_connection( baton->sqlca );
	if( baton->obj->conn == NULL ) {
	    getErrorMsg( baton->obj->conn, baton->error_msg );
	    cleanAPI();
	    return;
	}
    } 
    
    baton->obj->sqlca_connection = baton->sqlca_connection;
    openConnections++;
}

void Connection::connectAfter( uv_work_t *req ) 
/**********************************************/
{
    Isolate *isolate = Isolate::GetCurrent();
    HandleScope scope( isolate );
    connectBaton *baton = static_cast<connectBaton*>(req->data);
    Local<Value> undef = Local<Value>::New( isolate, Undefined( isolate ) );
    
    if( baton->err ) {
	callBack( &( baton->error_msg ), baton->callback, undef,
		  baton->callback_required );
	delete baton;
	delete req;
	return;
    }
    
    callBack( NULL, baton->callback, undef, baton->callback_required );
    
    delete baton;
    delete req;
}

NODE_API_FUNC( Connection::connect )
/**********************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope scope( isolate );
    int		num_args = args.Length();
    Connection *obj;
    obj = ObjectWrap::Unwrap<Connection>( args.This() );     
    bool	sqlca_connection = false;
    bool	callback_required = false;
    int		cbfunc_arg = -1;
    bool	arg_is_string = true;
    bool	arg_is_object = false;

    if( num_args == 0 ) {
	arg_is_string = false;

    } else if( num_args == 1 && args[0]->IsFunction() ) {
	callback_required = true;
	cbfunc_arg = 0;
	arg_is_string = false;

    } else if( num_args == 1 && args[0]->IsNumber() ){
	sqlca_connection = true;
	
    } else if( num_args == 1 && args[0]->IsString() ) {
	sqlca_connection = false;
    
    } else if( num_args == 1 && args[0]->IsObject() ) {
	sqlca_connection = false;
	arg_is_string = false;
	arg_is_object = true;
    
    } else if( num_args == 2 && args[0]->IsNumber() && args[1]->IsFunction() ) {
	sqlca_connection = true;
	callback_required = true;
	cbfunc_arg = 1;
    
    } else if( num_args == 2 && args[0]->IsString() && args[1]->IsFunction() ) {
	sqlca_connection = false;
	callback_required = true;
	cbfunc_arg = 1;
    
    } else if( num_args == 2 && args[0]->IsObject() && args[1]->IsFunction() ) {
	sqlca_connection = false;
	callback_required = true;
	cbfunc_arg = 1;
	arg_is_string = false;
	arg_is_object = true;

    } else if( num_args > 1 ) {
        throwError( JS_ERR_INVALID_ARGUMENTS );
	args.GetReturnValue().SetUndefined();
	return;
    }
    
    connectBaton *baton = new connectBaton();
    baton->obj = obj;
    baton->callback_required = callback_required;

    baton->sqlca_connection = sqlca_connection;
    
    if( sqlca_connection ) {
	baton->sqlca = (void *)(long)args[0]->NumberValue();
	
    } else {
	Local<String> localArg = Local<String>::New( isolate, obj->_arg );
	if( localArg->Length() > 0 ) {
	    String::Utf8Value param0( localArg );
	    baton->conn_string = std::string(*param0);
	} else {
	    baton->conn_string = std::string();
	}
	if( arg_is_string ) {
	    String::Utf8Value param0( args[0]->ToString() );
	    baton->conn_string.append( ";" );
	    baton->conn_string.append(*param0);
	} else if( arg_is_object ) {
	    Persistent<String> arg_string;
	    HashToString( args[0]->ToObject(), arg_string );
	    Local<String> local_arg_string = 
		Local<String>::New( isolate, arg_string );
	    String::Utf8Value param0( local_arg_string );
	    baton->conn_string.append( ";" );
	    baton->conn_string.append(*param0);
	    arg_string.Reset();
	}
	baton->conn_string.append( ";CHARSET='UTF-8'" );
    }
    
    uv_work_t *req = new uv_work_t();
    req->data = baton;
    
    if( callback_required ) {
	Local<Function> callback = Local<Function>::Cast(args[cbfunc_arg]);
	baton->callback.Reset( isolate, callback );

	int status;
	status = uv_queue_work( uv_default_loop(), req, connectWork,
				(uv_after_work_cb)connectAfter );
	assert(status == 0);
	args.GetReturnValue().SetUndefined();
	return;
    }
    
    connectWork( req );
    connectAfter( req );
    args.GetReturnValue().SetUndefined();
    return;
}

// Disconnect Function
void Connection::disconnectWork( uv_work_t *req ) 
/************************************************/
{
    noParamBaton *baton = static_cast<noParamBaton*>(req->data);
    scoped_lock api_lock(api_mutex );
    scoped_lock lock( baton->obj->conn_mutex );
    
    if( baton->obj->conn == NULL ) {
	getErrorMsg( JS_ERR_NOT_CONNECTED, baton->error_msg );
	return;
    }

    baton->obj->cleanupStmts();

    if( !baton->obj->sqlca_connection ) {
	api.sqlany_disconnect( baton->obj->conn );
    }
    // Must free the connection object or there will be a memory leak 
    api.sqlany_free_connection( baton->obj->conn );
    baton->obj->conn = NULL;
    openConnections--;

    if( openConnections <= 0 ) {	
	openConnections = 0;
	cleanAPI();
    }
    
    return;
}

NODE_API_FUNC( Connection::disconnect )
/*************************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope scope( isolate );
    int num_args = args.Length();
    bool callback_required = false;
    int cbfunc_arg = -1;
    
    if( num_args == 0 ) {
	
    } else if( num_args == 1 && args[0]->IsFunction() ) {
	callback_required = true;
	cbfunc_arg = 0;
	
    } else {
	throwError( JS_ERR_INVALID_ARGUMENTS );
	args.GetReturnValue().SetUndefined();
	return;
    }
    
    Connection *obj = ObjectWrap::Unwrap<Connection>( args.This() );
    noParamBaton *baton = new noParamBaton();
    
    baton->callback_required = callback_required;
    baton->obj = obj;
    
    uv_work_t *req = new uv_work_t();
    req->data = baton;
    
    if( callback_required ) {
	Local<Function> callback = Local<Function>::Cast(args[cbfunc_arg]);
	baton->callback.Reset( isolate, callback );
	
	int status;
	status = uv_queue_work( uv_default_loop(), req, disconnectWork,
				(uv_after_work_cb)noParamAfter );
	assert(status == 0);
	
	args.GetReturnValue().SetUndefined();
	return;
    }
    
    disconnectWork( req );
    noParamAfter( req );
    args.GetReturnValue().SetUndefined();
    return;
}

void Connection::commitWork( uv_work_t *req ) 
/********************************************/
{
    noParamBaton *baton = static_cast<noParamBaton*>(req->data);
    scoped_lock lock( baton->obj->conn_mutex );
    
    if( baton->obj->conn == NULL ) {
	baton->err = true;
	getErrorMsg( JS_ERR_NOT_CONNECTED, baton->error_msg );
	return;
    }

    if( !api.sqlany_commit( baton->obj->conn ) ) {
	baton->err = true;
	getErrorMsg( baton->obj->conn, baton->error_msg );
	return;
    }
}

NODE_API_FUNC( Connection::commit )
/*********************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope scope( isolate );
    int num_args = args.Length();
    bool callback_required = false;
    int cbfunc_arg = -1;
    
    if( num_args == 0 ) {
	
    } else if( num_args == 1 && args[0]->IsFunction() ) {
	callback_required = true;
	cbfunc_arg = 0;
	
    } else {
	throwError( JS_ERR_INVALID_ARGUMENTS );
	args.GetReturnValue().SetUndefined();
	return;
    }
    
    Connection *obj = ObjectWrap::Unwrap<Connection>( args.This() );
    
    noParamBaton *baton = new noParamBaton();
    baton->obj = obj;
    baton->callback_required = callback_required;
    
    uv_work_t *req = new uv_work_t();
    req->data = baton;
    
    if( callback_required ) {
	Local<Function> callback = Local<Function>::Cast(args[cbfunc_arg]);
	baton->callback.Reset( isolate, callback );
	
	int status;
	status = uv_queue_work( uv_default_loop(), req, commitWork, 
				(uv_after_work_cb)noParamAfter );
	assert(status == 0);
	
	args.GetReturnValue().SetUndefined();
	return;
    }
    
    commitWork( req );
    noParamAfter( req );
    args.GetReturnValue().SetUndefined();
    return;
}

void Connection::rollbackWork( uv_work_t *req ) 
/**********************************************/
{
    noParamBaton *baton = static_cast<noParamBaton*>(req->data);
    scoped_lock lock( baton->obj->conn_mutex );

    if( baton->obj->conn == NULL ) {
	baton->err = true;
	getErrorMsg( JS_ERR_NOT_CONNECTED, baton->error_msg );
	return;
    }

    if( !api.sqlany_rollback( baton->obj->conn ) ) {
	baton->err = true;
	getErrorMsg( baton->obj->conn, baton->error_msg );
	return;
    }
}

NODE_API_FUNC( Connection::rollback )
/***********************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope scope( isolate );
    int num_args = args.Length();
    bool callback_required = false;
    int cbfunc_arg = -1;
    
    if( num_args == 0 ) {
	
    } else if( num_args == 1 && args[0]->IsFunction() ) {
	callback_required = true;
	cbfunc_arg = 0;
	
    } else {
	throwError( JS_ERR_INVALID_ARGUMENTS );
	args.GetReturnValue().SetUndefined();
	return;
    }
    
    Connection *obj = ObjectWrap::Unwrap<Connection>( args.This() );

    noParamBaton *baton = new noParamBaton();
    baton->obj = obj;
    baton->callback_required = callback_required;
    
    uv_work_t *req = new uv_work_t();
    req->data = baton;
    
    if( callback_required ) {
	Local<Function> callback = Local<Function>::Cast(args[cbfunc_arg]);
	baton->callback.Reset( isolate, callback );
	
	int status;
	status = uv_queue_work( uv_default_loop(), req, rollbackWork,
				(uv_after_work_cb)noParamAfter );
	assert(status == 0);
	
	args.GetReturnValue().SetUndefined();
	return;
    }
    
    rollbackWork( req );
    noParamAfter( req );
    args.GetReturnValue().SetUndefined();
    return;
}

struct dropBaton {
    Persistent<Function> 	callback;
    bool 			err;
    std::string 		error_msg;
    bool 			callback_required;
    
    StmtObject 			*obj;  
    
    dropBaton() {
	err = false;
	callback_required = false;
	obj = NULL;
    }
    
    ~dropBaton() {
	obj = NULL;
	callback.Reset();
    }
};

void StmtObject::dropAfter( uv_work_t *req ) 
/*******************************************/
{
    Isolate *isolate = Isolate::GetCurrent();
    HandleScope scope( isolate );
    dropBaton *baton = static_cast<dropBaton*>(req->data);
    Local<Value> undef = Local<Value>::New( isolate, Undefined( isolate ) );
    
    if( baton->err ) {
	callBack( &( baton->error_msg ), baton->callback, undef,
		  baton->callback_required );
	delete baton;
	delete req;
	return;
    }
    
    callBack( NULL, baton->callback, undef, baton->callback_required );
    
    delete baton;
    delete req;
}

void StmtObject::dropWork( uv_work_t *req ) 
/******************************************/
{
    dropBaton *baton = static_cast<dropBaton*>(req->data);
    scoped_lock connlock( baton->obj->connection->conn_mutex );
    
    baton->obj->cleanup();
    baton->obj->removeConnection();
}

NODE_API_FUNC( StmtObject::drop )
/*******************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope scope( isolate );
    int num_args = args.Length();
    bool callback_required = false;
    int cbfunc_arg = -1;
    
    if( num_args == 0 ) {
	
    } else if( num_args == 1 && args[0]->IsFunction() ) {
	callback_required = true;
	cbfunc_arg = 0;
	
    } else {
	throwError( JS_ERR_INVALID_ARGUMENTS );
	args.GetReturnValue().SetUndefined();
	return;
    }
    
    StmtObject *obj = ObjectWrap::Unwrap<StmtObject>( args.This() );

    dropBaton *baton = new dropBaton();
    baton->obj = obj;
    baton->callback_required = callback_required;
    
    uv_work_t *req = new uv_work_t();
    req->data = baton;
    
    if( callback_required ) {
	Local<Function> callback = Local<Function>::Cast(args[cbfunc_arg]);
	baton->callback.Reset( isolate, callback );
	
	int status;
	status = uv_queue_work( uv_default_loop(), req, dropWork,
				(uv_after_work_cb)dropAfter );
	assert(status == 0);
	
	args.GetReturnValue().SetUndefined();
	return;
    }
    
    dropWork( req );
    dropAfter( req );
    args.GetReturnValue().SetUndefined();
    return;
}

void init( Local<Object> exports )
/********************************/
{
    uv_mutex_init(&api_mutex);
#if v012
    Isolate *isolate = Isolate::GetCurrent();
#else
    Isolate *isolate = exports->GetIsolate();
#endif
    StmtObject::Init( isolate );
    Connection::Init( isolate );
    NODE_SET_METHOD( exports, "createConnection", Connection::NewInstance );
}

NODE_MODULE( DRIVER_NAME, init )

#endif // !v010
