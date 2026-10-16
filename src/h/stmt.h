// ***************************************************************************
// Copyright (c) 2021 SAP SE or an SAP affiliate company. All rights reserved.
// ***************************************************************************

/** Represents prepared statement
 * @class Statement
 *
 * The Statement object is for SQL statements that will be executed multiple
 * times.
 * @see Connection::prepare
 */
class StmtObject : public Napi::ObjectWrap<StmtObject>
{
  public:
    /// @internal
    static Napi::Function Init( Napi::Env env );

    /// @internal
    static Napi::Object NewInstance( Napi::Env env );

    /// @internal
    StmtObject( const Napi::CallbackInfo &info );
    /// @internal
    ~StmtObject();
    /// @internal
    void cleanup( void );

  private:

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
    Napi::Value exec( const Napi::CallbackInfo &info );
    
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
    Napi::Value drop( const Napi::CallbackInfo &info );

    /** Gets the next result set of a multi-result-set query.
     *
     * This method can be either synchronous or asynchronous depending on
     * whether or not a callback function is specified.
     * The callback function is of the form:
     *
     * <p><pre>
     * function( err, res )
     * {
     *
     * };
     * </pre></p>
     *
     * The following synchronous example shows how to use the getMoreResults method
     * on a prepared statement.
     *
     * <p><pre>
     * var sqlanywhere = require( 'sqlanywhere' );
     * var client = sqlanywhere.createConnection();
     * client.connect( "ServerName=demo17;UID=DBA;PWD=sql" )
     * stmt = client.prepare( "SELECT * FROM Customers WHERE ID = ?; SELECT * FROM Customers where ID = ?" );
     * result = stmt.exec( [200, 300] );
     * console.log( result ); // first query
     * result = stmt.getMoreResults();
     * console.log( result ); // second query
     * stmt.drop();
     * client.disconnect();
     * </pre></p>
     *
     * @fn Statement::getMoreResults( Function callback )
     *
     * @param callback The optional callback function.
     *
     */
    Napi::Value getMoreResults( const Napi::CallbackInfo &info );

  public:
    /// @internal
    /// NULL once the statement is dropped or its connection object is freed.
    /// Only changed on the main thread.
    Connection				*connection;
    /// @internal
    std::atomic<a_sqlany_stmt *>	sqlany_stmt;
    /// @internal
    /// The SQL text, which wide_stmt is prepared from.
    std::string				sql;
    /// @internal
    /// A second handle of the statement for executions with several rows,
    /// prepared on first use. sqlany_set_batch_size() leaves the SQLDA at
    /// its largest size, which a single-row execution reads in full, and
    /// only sqlany_reset() shrinks it again. But a handle that is reset can
    /// only execute more rows than in any earlier execution (error -182
    /// otherwise), and resetting breaks CALLs with OUT parameters, so no
    /// handle is reset and sqlany_stmt only executes single rows. Only used
    /// while holding the connection mutex.
    a_sqlany_stmt			*wide_stmt;
};
