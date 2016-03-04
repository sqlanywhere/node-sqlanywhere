// ***************************************************************************
// Copyright (c) 2016 SAP SE or an SAP affiliate company. All rights reserved.
// ***************************************************************************
using namespace v8;

#include "nodever_cover.h"

/** Represents prepared statement
 * @class Statement
 *
 * The Statement object is for SQL statements that will be executed multiple
 * times.
 * @see Connection::prepare
 */
class StmtObject : public node::ObjectWrap
{
  public:
    /// @internal
#if v010
    static void Init();
#else
    static void Init( Isolate * );
#endif
    
    /// @internal
    static NODE_API_FUNC( NewInstance );

    /// @internal
#if !v010
    static void CreateNewInstance( const FunctionCallbackInfo<Value> &args,
				   Persistent<Object> &obj );
#endif

    /// @internal
    StmtObject();
    /// @internal
    ~StmtObject();
	
  private:
    /// @internal
    static Persistent<Function> constructor;
    /// @internal
    static NODE_API_FUNC( New );
	
    /** Executes the prepared SQL statement.
     *
     * This method optionally takes in an array of bind
     * parameters to execute.
     *
     * This method can be either synchronous or asynchronous depending on
     * whether or not a callback function is specified.
     * The callback function is of the form:
     *
     * <p><pre>
     * function( err, result )
     * {
     *
     * };
     * </pre></p>
     *
     * For queries producing result sets, the result set object is returned
     * as the second parameter of the callback.
     * For insert, update and delete statements, the number of rows affected
     * is returned as the second parameter of the callback.
     * For other statements, result is undefined.
     *
     * The following synchronous example shows how to use the exec method
     * on a prepared statement.
     *
     * <p><pre>
     * var sqlanywhere = require( 'sqlanywhere' );
     * var client = sqlanywhere.createConnection();
     * client.connect( "ServerName=demo17;UID=DBA;PWD=sql" )
     * stmt = client.prepare( "SELECT * FROM Customers WHERE ID >= ? AND ID < ?" );
     * result = stmt.exec( [200, 300] );
     * stmt.drop();
     * console.log( result );
     * client.disconnect();
     * </pre></p>
     *
     * @fn result Statement::exec( Array params, Function callback )
     *
     * @param params The optional array of bind parameters.
     * @param callback The optional callback function.
     *
     * @return If no callback is specified, the result is returned.
     *
     */
    static NODE_API_FUNC( exec );
    
    /** Drops the statement.
     *
     * This method drops the prepared statement and frees up resources.
     *
     * This method can be either synchronous or asynchronous depending on
     * whether or not a callback function is specified.
     * The callback function is of the form:
     *
     * <p><pre>
     * function( err )
     * {
     *
     * };
     * </pre></p>
     *
     * The following synchronous example shows how to use the drop method
     * on a prepared statement.
     *
     * <p><pre>
     * var sqlanywhere = require( 'sqlanywhere' );
     * var client = sqlanywhere.createConnection();
     * client.connect( "ServerName=demo17;UID=DBA;PWD=sql" )
     * stmt = client.prepare( "SELECT * FROM Customers WHERE ID >= ? AND ID < ?" );
     * result = stmt.exec( [200, 300] );
     * stmt.drop();
     * console.log( result );
     * client.disconnect();
     * </pre></p>
     *
     * @fn Statement::drop( Function callback )
     *
     * @param callback The optional callback function.
     *
     */
    static NODE_API_FUNC( drop );

    /// @internal
    static void dropAfter( uv_work_t *req );
    /// @internal
    static void dropWork( uv_work_t *req );
    
  public:
    /// @internal
    Connection		*connection;
    /// @internal
    a_sqlany_stmt	*sqlany_stmt;
	
};
