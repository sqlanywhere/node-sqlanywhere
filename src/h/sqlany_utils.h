// ***************************************************************************
// Copyright (c) 2017 SAP SE or an SAP affiliate company. All rights reserved.
// ***************************************************************************
#include <iostream>
#include <string>
#include <string.h>
#include <sstream> 
#include <vector>
#include "sacapidll.h"
#include "sacapi.h"

#pragma warning (disable : 4100)
#pragma warning (disable : 4506)
#pragma warning (disable : 4068)
#pragma warning (disable : 4800)
#pragma GCC diagnostic ignored "-Wunused-parameter"

#include "node.h"
#include "v8.h"
#include "node_buffer.h"
#include "node_object_wrap.h"
#include "uv.h"

#pragma GCC diagnostic pop
#pragma warning (default : 4100)
#pragma warning (default : 4506)
#pragma warning (default : 4068)
#pragma warning (default : 4800)

#include "nodever_cover.h"
#include "errors.h"
#include "connection.h"
#include "stmt.h"

using namespace v8;

extern SQLAnywhereInterface api;
extern unsigned openConnections;
extern uv_mutex_t api_mutex;

#define CLEAN_STRINGS( vector )      		\
{                                          	\
    for( size_t i = 0; i < vector.size(); i++ ) { 	\
	delete[] vector[i];			\
    }						\
    vector.clear();				\
}

#define CLEAN_NUMS( vector )      		\
{                                          	\
    for( size_t i = 0; i < vector.size(); i++ ) { 	\
	delete vector[i];			\
    }						\
    vector.clear();				\
}

class scoped_lock
{
    public:
	scoped_lock( uv_mutex_t &mtx ) : _mtx( mtx )
	{
	    uv_mutex_lock( &_mtx );
	}
	~scoped_lock()
	{
	    
	    uv_mutex_unlock( &_mtx );
	}

    private:
	uv_mutex_t &_mtx;

};

bool cleanAPI (); // Finalizes the API and frees up resources
void getErrorMsg( a_sqlany_connection *conn, std::string &str );
void getErrorMsg( int code, std::string &str );
void throwError( a_sqlany_connection *conn );   
void throwError( int code );

#if v010
void callBack( std::string *		str, 
	       Persistent<Function>	callback,
	       Local<Value>		Result,
	       bool			callback_required = true );
void callBack( std::string *		str,
	       Local<Value>		callback,
	       Local<Value>		Result,
	       bool			callback_required = true );
#else
void callBack( std::string *		str,
	       Persistent<Function> &	callback,
	       Local<Value> &		Result,
	       bool			callback_required = true );
void callBack( std::string *		str,
	       const Local<Value> &	callback,
	       Local<Value> &		Result,
	       bool			callback_required = true );
void callBack( std::string *		str,
	       Persistent<Function> &	callback,
	       Persistent<Value> &	Result,
	       bool			callback_required = true );
#endif

bool getBindParameters( std::vector<char*>			&string_params
		      , std::vector<double*>			&num_params
		      , std::vector<int*>			&int_params
		      , std::vector<size_t*>			&string_len
		      , Handle<Value> 				arg
		      , std::vector<a_sqlany_bind_param> 	&params
		      );

#if v010
bool getResultSet( Local<Value> 		&Result   
		 , int 				&rows_affected
		 , std::vector<char *> 		&colNames
		 , std::vector<char*> 		&string_vals
		 , std::vector<double*>		&num_vals
		 , std::vector<int*> 		&int_vals
		 , std::vector<size_t*> 	&string_len
		 , std::vector<a_sqlany_data_type> 	&col_types );
#else
bool getResultSet( Persistent<Value> 		&Result   
		 , int 				&rows_affected
		 , std::vector<char *> 		&colNames
		 , std::vector<char*> 		&string_vals
		 , std::vector<double*>		&num_vals
		 , std::vector<int*> 		&int_vals
		 , std::vector<size_t*> 	&string_len
		 , std::vector<a_sqlany_data_type> 	&col_types );
#endif

bool fetchResultSet( a_sqlany_stmt 			*sqlany_stmt
		   , int 				&rows_affected
		   , std::vector<char *> 		&colNames
		   , std::vector<char*> 		&string_vals
		   , std::vector<double*>		&num_vals
		   , std::vector<int*> 			&int_vals
		   , std::vector<size_t*> 		&string_len
		   , std::vector<a_sqlany_data_type> 	&col_types );

struct noParamBaton {
    Persistent<Function> 	callback;
    bool 			err;
    std::string 		error_msg;
    bool 			callback_required;
    
    Connection *obj;
    
    noParamBaton() {
	obj = NULL;
	err = false;
    }
    
    ~noParamBaton() {
	obj = NULL;
    }
};

void executeAfter( uv_work_t *req );
void executeWork( uv_work_t *req );

#if NODE_MAJOR_VERSION > 0 || NODE_MINOR_VERSION > 10
void HashToString( Local<Object> obj, Persistent<String> &ret );
#else
Persistent<String> HashToString( Local<Object> obj );
Handle<Value> CreateConnection( const Arguments &args );
#endif

