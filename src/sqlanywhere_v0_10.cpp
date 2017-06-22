// ***************************************************************************
// Copyright (c) 2017 SAP SE or an SAP affiliate company. All rights reserved.
// ***************************************************************************
#include "nodever_cover.h"

#if v010

#include "sqlany_utils.h"

using namespace v8;

SQLAnywhereInterface api;
unsigned openConnections = 0;
uv_mutex_t api_mutex;

extern Persistent<String> HashToString( Local<Object> obj );

struct executeBaton {
    Persistent<Function> 		callback;
    bool 				err;
    std::string 			error_msg;
    bool 				callback_required;
    unsigned				num_rows;

    Connection 				*obj;
    StmtObject				*stmt_obj;

    bool				free_stmt;
    std::string				stmt;
    std::vector<ExecuteData *>		execData;
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
	num_rows = 0;
    }

    ~executeBaton() {
	obj = NULL;
	if( stmt_obj != NULL && free_stmt ) {
	    delete stmt_obj;
	    stmt_obj = NULL;
	}
	col_types.clear();
	params.clear();
	CLEAN_STRINGS( colNames );
	CLEAN_PTRS( execData );
    }
};

bool fillResult( executeBaton *baton, Local<Value> &ResultSet ) 
{
    
    if( baton->err ) {
	callBack( &( baton->error_msg ), baton->callback, Local<Value>::New( Undefined() ), baton->callback_required );
	return false;
    }
    
    if( !getResultSet( ResultSet, baton->rows_affected, baton->colNames,
		       baton->execData[0], baton->col_types ) ) {
	getErrorMsg( JS_ERR_RESULTSET, baton->error_msg );
	callBack( &( baton->error_msg ), baton->callback , Local<Value>::New( Undefined() ), baton->callback_required );
	return false;
    }
    
    callBack( NULL, baton->callback, ResultSet,  baton->callback_required );
    return true;
}

void executeWork( uv_work_t *req ) 
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
	sqlany_stmt = api.sqlany_prepare( baton->obj->conn, baton->stmt.c_str() );
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
    
    if( !api.sqlany_reset( sqlany_stmt) ) {
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
	
	if( param.value.type == A_INVALID_TYPE &&
	    ( baton->params[i].value.is_null == NULL ||
	      !(*(baton->params[i].value.is_null)) ) ) {
	    param.value.type = baton->params[i].value.type;
	}
	param.value.buffer = baton->params[i].value.buffer;
	param.value.is_address = baton->params[i].value.is_address;
	
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
    
    if( baton->num_rows > 1 ) {
	api.sqlany_set_batch_size( sqlany_stmt, baton->num_rows );
    }
    
    sacapi_bool success_execute = api.sqlany_execute( sqlany_stmt );
    baton->execData[0]->clear();
    
    if( !success_execute ) {
	baton->err = true;
	getErrorMsg( baton->obj->conn, baton->error_msg );
	return;
    }
    
    if( !fetchResultSet( sqlany_stmt, baton->rows_affected, baton->colNames,
			 baton->execData[0], baton->col_types ) ) {
	baton->err = true;
	getErrorMsg( baton->obj->conn, baton->error_msg );
	return;
    }
}

void executeAfter( uv_work_t *req ) 
{
    executeBaton *baton = static_cast<executeBaton*>( req->data );
    Local<Value> ResultSet;

    // We must free the statement here (before we delete the baton) since it will
    // make a request. If we're executing the statement asynchronously, the
    // callback will be spawned (on a different thread) by fillResult. If the
    // callback makes another request to the database, we could have multiple
    // requests on the same connection at the same time, which is not allowed.
    if( baton->stmt_obj != NULL && baton->free_stmt ) {
	delete baton->stmt_obj;
	baton->stmt_obj = NULL;
    }

    fillResult( baton, ResultSet );

    delete baton;
    delete req;
}

Handle<Value> StmtObject::exec( const Arguments &args ) 
{
    HandleScope scope;
    StmtObject *obj = ObjectWrap::Unwrap<StmtObject>( args.This() );
    int num_args = args.Length();
    bool callback_required = false, bind_required = false;
    int cbfunc_arg = -1;
    
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
	return scope.Close( Undefined() );
    }
    
    if( obj == NULL || obj->connection == NULL || obj->connection->conn == NULL ||
	obj->sqlany_stmt == NULL ) {
	std::string error_msg;
	getErrorMsg( JS_ERR_INVALID_OBJECT, error_msg );
	callBack( &( error_msg ), args[cbfunc_arg] , Local<Value>::New( Undefined() ), callback_required );
        return scope.Close( Undefined() );
    }
    
    executeBaton *baton = new executeBaton();
    baton->obj = obj->connection;
    baton->stmt_obj = obj;
    baton->free_stmt = false;
    baton->callback_required = callback_required;
    
    if( bind_required ) {
	if( !getBindParameters( baton->execData, args[0], baton->params,
				baton->num_rows ) ) {
	    delete baton;
	    std::string error_msg;
	    getErrorMsg( JS_ERR_BINDING_PARAMETERS, error_msg );
	    callBack( &( error_msg ), args[cbfunc_arg],
		      Local<Value>::New( Undefined() ), callback_required );
	    return scope.Close( Undefined() );
	}
	if( baton->num_rows > 1 &&
	    baton->obj->max_api_ver < SQLANY_API_VERSION_4 ) {
	    // can't support wide inserts with older dbcapi
	    delete baton;
	    std::string error_msg;
	    getErrorMsg( JS_ERR_NO_WIDE_STATEMENTS, error_msg );
	    callBack( &( error_msg ), args[cbfunc_arg],
		      Local<Value>::New( Undefined() ), callback_required );
	    return scope.Close( Undefined() );
	}
    } else {
	baton->execData.push_back( new ExecuteData );
	baton->num_rows = 1;
    }

    uv_work_t *req = new uv_work_t();
    req->data = baton;
    
    if( callback_required ) {
	Local<Function> callback = Local<Function>::Cast(args[cbfunc_arg]);
	baton->callback = Persistent<Function>::New( callback );
	
	int status;
	status = uv_queue_work( uv_default_loop(), req, executeWork, (uv_after_work_cb)executeAfter );
	assert(status == 0);
	
	return scope.Close( Undefined() );
    }
    
    Local<Value> ResultSet;
    
    executeWork( req );
    bool success = fillResult( baton, ResultSet );
    delete baton;
    delete req;
    
    if( !success ) {
	return scope.Close( Undefined() );
    }
    return scope.Close( ResultSet );
}

void getMoreResultsWork( uv_work_t *req )
/***************************************/
{
    executeBaton *baton = static_cast<executeBaton*>(req->data);
    scoped_lock lock( baton->obj->conn_mutex );

    if( baton->obj->conn == NULL ) {
	baton->err = true;
	getErrorMsg( JS_ERR_NOT_CONNECTED, baton->error_msg );
	return;
    }

    a_sqlany_stmt *stmt = baton->stmt_obj->sqlany_stmt;

    if( stmt == NULL ) {
	baton->err = true;
	getErrorMsg( JS_ERR_INVALID_OBJECT, baton->error_msg );
	return;
    }
    int retval = ( api.sqlany_get_next_result( stmt ) != 0 );
    if( !retval ) {
	char buffer[SACAPI_ERROR_SIZE];
	int rc;
	rc = api.sqlany_error( baton->obj->conn, buffer, sizeof(buffer) );
	if( rc != 105 ) {
	    // rc == 105 means "procedure has completed" - i.e. no more result
	    // sets. Just return null, not an error
	    baton->err = true;
	    getErrorMsg( baton->obj->conn, baton->error_msg );
	}
	return;
    }

    baton->execData[0]->clear();
    if( !fetchResultSet( stmt, baton->rows_affected, baton->colNames,
			 baton->execData[0], baton->col_types ) ) {
	baton->err = true;
	getErrorMsg( baton->obj->conn, baton->error_msg );
	return;
    }
}

void getMoreResultsAfter( uv_work_t *req )
/****************************************/
{
    HandleScope scope;
    executeBaton *baton = static_cast<executeBaton*>( req->data );
    Local<Value> ResultSet;

    // We must free the statement here (before we delete the baton) since it will
    // make a request. If we're executing the statement asynchronously, the
    // callback will be spawned (on a different thread) by fillResult. If the
    // callback makes another request to the database, we could have multiple
    // requests on the same connection at the same time, which is not allowed.
    if( baton->stmt_obj != NULL && baton->free_stmt ) {
	delete baton->stmt_obj;
	baton->stmt_obj = NULL;
    }

    fillResult( baton, ResultSet );

    delete baton;
    delete req;
}

NODE_API_FUNC( StmtObject::getMoreResults )
/*****************************************/
{
    HandleScope scope;
    StmtObject *obj = ObjectWrap::Unwrap<StmtObject>( args.This() );
    int num_args = args.Length();
    bool callback_required = false;
    int cbfunc_arg = -1;
    
    if( num_args == 0 ) {
	
    } else if( num_args == 1 && args[0]->IsFunction() ) {
	callback_required = true;
	cbfunc_arg = 0;

    } else {
	throwError( JS_ERR_INVALID_ARGUMENTS );
	return scope.Close( Undefined() );
    }
    
    if( obj == NULL || obj->connection == NULL || obj->connection->conn == NULL ||
	obj->sqlany_stmt == NULL ) {
	std::string error_msg;
	getErrorMsg( JS_ERR_INVALID_OBJECT, error_msg );
	callBack( &( error_msg ), args[cbfunc_arg],
		  Local<Value>::New( Undefined() ), callback_required );
	return scope.Close( Undefined() );
    }

    executeBaton *baton = new executeBaton;
    baton->obj = obj->connection;
    baton->stmt_obj = obj;
    baton->free_stmt = false;
    baton->callback_required = callback_required;

    baton->execData.push_back( new ExecuteData );
    baton->num_rows = 1;

    uv_work_t *req = new uv_work_t();
    req->data = baton;
    
    if( callback_required ) {
	Local<Function> callback = Local<Function>::Cast(args[cbfunc_arg]);
	baton->callback = Persistent<Function>::New( callback );
	
	int status;
	status = uv_queue_work( uv_default_loop(), req, getMoreResultsWork,
				(uv_after_work_cb)getMoreResultsAfter );
	assert(status == 0);
	
	return scope.Close( Undefined() );
    }
    
    Local<Value> ResultSet;
    
    getMoreResultsWork( req );
    bool success = fillResult( baton, ResultSet );
    delete baton;
    delete req;
    
    if( !success ) {
	return scope.Close( Undefined() );
    }
    return scope.Close( ResultSet );
}

Handle<Value> Connection::exec( const Arguments &args ) 
{
    HandleScope scope;
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
	    return scope.Close( Undefined() );
	}
    } else {
        throwError( JS_ERR_INVALID_ARGUMENTS );
	return scope.Close( Undefined() );
    }

    Connection	*obj = ObjectWrap::Unwrap<Connection>( args.This() );
    
    if( obj == NULL || obj->conn == NULL ) {
	std::string error_msg;
	getErrorMsg( JS_ERR_INVALID_OBJECT, error_msg );
	callBack( &( error_msg ), args[cbfunc_arg] , Local<Value>::New( Undefined() ), callback_required );
        return scope.Close( Undefined() );
    }
    
    String::Utf8Value 		param0( args[0]->ToString() );
    
    executeBaton *baton = new executeBaton();
    baton->obj = obj;
    baton->callback_required = callback_required;
    baton->free_stmt = true;
    baton->stmt_obj = NULL;
    baton->stmt = std::string(*param0);
    
    if( bind_required ) {
	if( !getBindParameters( baton->execData, args[1], baton->params,
				baton->num_rows ) ) {
	    delete baton;
	    std::string error_msg;
	    getErrorMsg( JS_ERR_BINDING_PARAMETERS, error_msg );
	    callBack( &( error_msg ), args[cbfunc_arg],
		      Local<Value>::New( Undefined() ), callback_required );
	    return scope.Close( Undefined() );
	}
	if( baton->num_rows > 1 &&
	    baton->obj->max_api_ver < SQLANY_API_VERSION_4 ) {
	    // can't support wide inserts with older dbcapi
	    delete baton;
	    std::string error_msg;
	    getErrorMsg( JS_ERR_NO_WIDE_STATEMENTS, error_msg );
	    callBack( &( error_msg ), args[cbfunc_arg],
		      Local<Value>::New( Undefined() ), callback_required );
	    return scope.Close( Undefined() );
	}
    } else {
	baton->execData.push_back( new ExecuteData );
	baton->num_rows = 1;
    }
    
    uv_work_t *req = new uv_work_t();
    req->data = baton;
    
    if( callback_required ) {
	Local<Function> callback = Local<Function>::Cast(args[cbfunc_arg]);
	baton->callback = Persistent<Function>::New( callback );
	
	int status;
	status = uv_queue_work( uv_default_loop(), req, executeWork, (uv_after_work_cb)executeAfter );
	assert(status == 0);
	
	return scope.Close( Undefined() );
    }
    
    Local<Value> ResultSet;
    
    executeWork( req );
    bool success = fillResult( baton, ResultSet );

    delete baton;
    delete req;
    
    if( !success ) {
	return scope.Close( Undefined() );
    }
    return scope.Close( ResultSet );
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
    }
};

void Connection::prepareWork( uv_work_t *req ) 
{
    
    prepareBaton *baton = static_cast<prepareBaton*>(req->data);
    if( baton->obj == NULL || baton->obj->connection == NULL || baton->obj->connection->conn == NULL ) {
	baton->err = true;
	getErrorMsg( JS_ERR_INVALID_OBJECT, baton->error_msg );
	return;
    }
    
    scoped_lock lock( baton->obj->connection->conn_mutex );
    
    baton->obj->sqlany_stmt = api.sqlany_prepare( baton->obj->connection->conn, baton->stmt.c_str() );
    
    if( baton->obj->sqlany_stmt == NULL ) {
	baton->err = true;
	getErrorMsg( baton->obj->connection->conn, baton->error_msg );
	return;
    }
}

void Connection::prepareAfter( uv_work_t *req ) 
{
    prepareBaton *baton = static_cast<prepareBaton*>(req->data);
    
    if( baton->err ) {
	callBack( &( baton->error_msg ), baton->callback, Local<Value>::New( Undefined() ), baton->callback_required );
	delete baton;
	delete req;
	return;
    }
    
    if( baton->callback_required ) {
	Local<Value> StmtObj = Local<Value>::New( baton->StmtObj );
	callBack( NULL, baton->callback, StmtObj,  baton->callback_required );
	baton->StmtObj.Dispose();
    }
    
    delete baton;
    delete req;
}

Handle<Value> Connection::prepare( const Arguments &args ) 
{
    HandleScope scope;
    bool callback_required = false;
    int cbfunc_arg = -1;
    if( args.Length() == 1 && args[0]->IsString() ) {
	
    } else if( args.Length() == 2 && args[0]->IsString() && args[1]->IsFunction() ) {
	callback_required = true;
	cbfunc_arg = 1;
	
    } else {
	throwError( JS_ERR_INVALID_ARGUMENTS );
	scope.Close( Undefined() );
    }
        
    Connection *db = ObjectWrap::Unwrap<Connection>( args.This() );
    
    if( db == NULL || db->conn == NULL ) {
	std::string error_msg;
	getErrorMsg( JS_ERR_NOT_CONNECTED, error_msg );
	callBack( &( error_msg ), args[cbfunc_arg] , Local<Value>::New( Undefined() ), callback_required );
        return scope.Close( Undefined() );
    }

    Handle<Value> StmtObj = StmtObject::NewInstance( args );
    StmtObject *obj = node::ObjectWrap::Unwrap<StmtObject>( StmtObj->ToObject() );
    obj->connection = db;

    if( obj == NULL ) {
        std::string error_msg;
	getErrorMsg( JS_ERR_GENERAL_ERROR, error_msg );
	callBack( &( error_msg ), args[cbfunc_arg], Local<Value>::New( Undefined() ), callback_required );
	return scope.Close( Undefined() );
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
	baton->callback = Persistent<Function>::New( callback );
	baton->StmtObj = Persistent<Value>::New( StmtObj );
	
	int status;
	status = uv_queue_work( uv_default_loop(), req, prepareWork, (uv_after_work_cb)prepareAfter );
	assert(status == 0);
	
	return scope.Close( Undefined() );
    }
    
    prepareWork( req );
    bool err = baton->err;
    prepareAfter( req );
    
    if( err ) {
	return scope.Close( Undefined() );
    }
    return scope.Close( StmtObj );
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
    }
    
};

void Connection::connectWork( uv_work_t *req ) 
{

    scoped_lock api_lock( api_mutex );
    connectBaton *baton = static_cast<connectBaton*>(req->data);
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
    
	if( !api.sqlany_init( "Node.js", SQLANY_API_VERSION_4,
			      &(baton->obj->max_api_ver) )) {
	    // As long as the version is >= 2, we're OK. We just have to disable
	    // wide inserts
	    if( baton->obj->max_api_ver >= SQLANY_API_VERSION_2 ) {
		if( !api.sqlany_init( "Node.js", baton->obj->max_api_ver,
				      &(baton->obj->max_api_ver) )) {
		    baton->err = true;
		    getErrorMsg( JS_ERR_INITIALIZING_DBCAPI, baton->error_msg );
		    return;
		}
	    } else {
		baton->err = true;
		getErrorMsg( JS_ERR_INITIALIZING_DBCAPI, baton->error_msg );
		return;
	    }
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
{
    HandleScope scope;
    connectBaton *baton = static_cast<connectBaton*>(req->data);
    
    if( baton->err ) {
	callBack( &( baton->error_msg ), baton->callback, Local<Value>::New( Undefined() ), baton->callback_required );
	delete baton;
	delete req;
	return;
    }
    
    callBack( NULL, baton->callback, Local<Value>::New( Undefined() ),  baton->callback_required );
    
    delete baton;
    delete req;
}

Handle<Value> Connection::connect( const Arguments &args ) 
{
    HandleScope	scope;
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
	return scope.Close( Undefined() );
    }
    
    connectBaton *baton = new connectBaton();
    baton->obj = obj;
    baton->callback_required = callback_required;

    baton->sqlca_connection = sqlca_connection;
    
    if( sqlca_connection ) {
	baton->sqlca = (void *)(long)args[0]->NumberValue();
	
    } else {
	if( obj->_arg->Length() > 0 ) {
	    String::Utf8Value param0( obj->_arg );
	    baton->conn_string = std::string(*param0);
	} else {
	    baton->conn_string = std::string();
	}
	if( arg_is_string ) {
	    String::Utf8Value param0( args[0]->ToString() );
	    baton->conn_string.append( ";" );
	    baton->conn_string.append(*param0);
	} else if( arg_is_object ) {
	    Persistent<String> arg_string = HashToString( args[0]->ToObject() );
	    String::Utf8Value param0( arg_string );
	    baton->conn_string.append( ";" );
	    baton->conn_string.append(*param0);
	    arg_string.Dispose();
	}
	baton->conn_string.append( ";CHARSET='UTF-8'" );
    }
    
    uv_work_t *req = new uv_work_t();
    req->data = baton;
    
    if( callback_required ) {
	Local<Function> callback = Local<Function>::Cast(args[cbfunc_arg]);
	baton->callback = Persistent<Function>::New( callback );
	
	int status;
	status = uv_queue_work( uv_default_loop(), req, connectWork, (uv_after_work_cb)connectAfter );
	assert(status == 0);
	return scope.Close( Undefined() );
    }
    
    connectWork( req );
    connectAfter( req );
    return scope.Close( Undefined() );
}

// Disconnect Function
void Connection::disconnectWork( uv_work_t *req ) 
{
    scoped_lock api_lock( api_mutex );
    noParamBaton *baton = static_cast<noParamBaton*>(req->data);
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

Handle<Value> Connection::disconnect( const Arguments &args ) 
{
    HandleScope scope;
    
    int num_args = args.Length();
    bool callback_required = false;
    int cbfunc_arg = -1;
    
    if( num_args == 0 ) {
	
    } else if( num_args == 1 && args[0]->IsFunction() ) {
	callback_required = true;
	cbfunc_arg = 0;
	
    } else {
	throwError( JS_ERR_INVALID_ARGUMENTS );
	return scope.Close( Undefined() );
    }
    
    Connection *obj = ObjectWrap::Unwrap<Connection>( args.This() );
    noParamBaton *baton = new noParamBaton();
    
    baton->callback_required = callback_required;
    baton->obj = obj;
    
    
    uv_work_t *req = new uv_work_t();
    req->data = baton;
    
    if( callback_required ) {
	Local<Function> callback = Local<Function>::Cast(args[cbfunc_arg]);
	baton->callback = Persistent<Function>::New( callback );
	
	int status;
	status = uv_queue_work( uv_default_loop(), req, disconnectWork, (uv_after_work_cb)noParamAfter );
	assert(status == 0);
	
	return scope.Close( Undefined() );
    }
    
    disconnectWork( req );
    noParamAfter( req );
    return scope.Close( Undefined() );
}

void Connection::commitWork( uv_work_t *req ) 
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

Handle<Value> Connection::commit( const Arguments &args ) 
{
    HandleScope scope;
    
    int num_args = args.Length();
    bool callback_required = false;
    int cbfunc_arg = -1;
    
    if( num_args == 0 ) {
	
    } else if( num_args == 1 && args[0]->IsFunction() ) {
	callback_required = true;
	cbfunc_arg = 0;
	
    } else {
	throwError( JS_ERR_INVALID_ARGUMENTS );
	return scope.Close( Undefined() );
    }
    
    Connection *obj = ObjectWrap::Unwrap<Connection>( args.This() );
    
    noParamBaton *baton = new noParamBaton();
    baton->obj = obj;
    baton->callback_required = callback_required;
    
    uv_work_t *req = new uv_work_t();
    req->data = baton;
    
    if( callback_required ) {
	Local<Function> callback = Local<Function>::Cast(args[cbfunc_arg]);
	baton->callback = Persistent<Function>::New( callback );
	
	int status;
	status = uv_queue_work( uv_default_loop(), req, commitWork, (uv_after_work_cb)noParamAfter );
	assert(status == 0);
	
	return scope.Close( Undefined() );
    }
    
    commitWork( req );
    noParamAfter( req );
    return scope.Close( Undefined() );
}

void Connection::rollbackWork( uv_work_t *req ) 
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

Handle<Value> Connection::rollback( const Arguments &args ) 
{
    HandleScope scope;
    int num_args = args.Length();
    bool callback_required = false;
    int cbfunc_arg = -1;
    
    if( num_args == 0 ) {
	
    } else if( num_args == 1 && args[0]->IsFunction() ) {
	callback_required = true;
	cbfunc_arg = 0;
	
    } else {
	throwError( JS_ERR_INVALID_ARGUMENTS );
	return scope.Close( Undefined() );
    }
    
    Connection *obj = ObjectWrap::Unwrap<Connection>( args.This() );

    noParamBaton *baton = new noParamBaton();
    baton->obj = obj;
    baton->callback_required = callback_required;
    
    uv_work_t *req = new uv_work_t();
    req->data = baton;
    
    if( callback_required ) {
	Local<Function> callback = Local<Function>::Cast(args[cbfunc_arg]);
	baton->callback = Persistent<Function>::New( callback );
	
	int status;
	status = uv_queue_work( uv_default_loop(), req, rollbackWork, (uv_after_work_cb)noParamAfter );
	assert(status == 0);
	
	return scope.Close( Undefined() );
    }
    
    rollbackWork( req );
    noParamAfter( req );
    return scope.Close( Undefined() );
}

Handle<Value> Connection::connected( const Arguments &args )
/**********************************************************/
{
    HandleScope scope;
    Connection *obj = ObjectWrap::Unwrap<Connection>( args.This() );
    Local<Value> ret;
    ret = Integer::New( obj->conn == NULL ? false : true );
    return scope.Close( ret );
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
    }
};

void StmtObject::dropAfter( uv_work_t *req ) 
{
    dropBaton *baton = static_cast<dropBaton*>(req->data);
    
    if( baton->err ) {
	callBack( &( baton->error_msg ), baton->callback, Local<Value>::New( Undefined() ), baton->callback_required );
	delete baton;
	delete req;
	return;
    }
    
    callBack( NULL, baton->callback, Local<Value>::New( Undefined() ),  baton->callback_required );
    
    delete baton;
    delete req;
}

void StmtObject::dropWork( uv_work_t *req ) 
{
    dropBaton *baton = static_cast<dropBaton*>(req->data);
    scoped_lock lock( baton->obj->connection->conn_mutex );
    
    baton->obj->cleanup();
    baton->obj->removeConnection();
}

Handle<Value> StmtObject::drop( const Arguments &args ) 
{
    HandleScope scope;
    int num_args = args.Length();
    bool callback_required = false;
    int cbfunc_arg = -1;
    
    if( num_args == 0 ) {
	
    } else if( num_args == 1 && args[0]->IsFunction() ) {
	callback_required = true;
	cbfunc_arg = 0;
	
    } else {
	throwError( JS_ERR_INVALID_ARGUMENTS );
	return scope.Close( Undefined() );
    }
    
    StmtObject *obj = ObjectWrap::Unwrap<StmtObject>( args.This() );

    dropBaton *baton = new dropBaton();
    baton->obj = obj;
    baton->callback_required = callback_required;
    
    uv_work_t *req = new uv_work_t();
    req->data = baton;
    
    if( callback_required ) {
	Local<Function> callback = Local<Function>::Cast(args[cbfunc_arg]);
	baton->callback = Persistent<Function>::New( callback );
	
	int status;
	status = uv_queue_work( uv_default_loop(), req, dropWork, (uv_after_work_cb)dropAfter );
	assert(status == 0);
	
	return scope.Close( Undefined() );
    }
    
    dropWork( req );
    dropAfter( req );
    return scope.Close( Undefined() );
}

// GLUE
void init( Handle<Object> exports ) {
    uv_mutex_init(&api_mutex);
    StmtObject::Init();
    Connection::Init();
    exports->Set( String::NewSymbol( "createConnection" ),
		  FunctionTemplate::New( CreateConnection )->GetFunction() );

}

#define EXPORT(name) NODE_MODULE(name,init)

EXPORT( DRIVER_NAME )

#endif // v010
