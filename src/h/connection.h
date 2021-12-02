// ***************************************************************************
// Copyright (c) 2021 SAP SE or an SAP affiliate company. All rights reserved.
// ***************************************************************************
using namespace v8;
using namespace node;

#include "nodever_cover.h"

/** Represents the connection to the database.
 * @class Connection
 *
 * The following example uses synchronous calls to create a new connection to
 * the database server, issue a SQL query against the server, display the
 * result set, and then disconnect from the server.
 *
 * <p><pre>
 * var sqlanywhere = require( 'sqlanywhere' );
 * var client = sqlanywhere.createConnection();
 * client.connect( { ServerName: 'demo17', UserID: 'DBA', Password: 'sql' } )
 * console.log('Connected');
 * result = client.exec("SELECT * FROM Customers");
 * console.log( result );
 * client.disconnect()
 * console.log('Disconnected');
 * </pre></p>
 *
 * The following example does essentially the same thing using callbacks
 * to perform asynchronous calls.
 * Error checking is included.
 *
 * <p><pre>
 * var sqlanywhere = require( 'sqlanywhere' );
 * var client = sqlanywhere.createConnection();
 * client.connect( "ServerName=demo17;UID=DBA;PWD=sql",
 *     function( err )
 *     {
 *         if( err )
 *         {
 *             console.error( "Connect error: ", err );
 *         }
 *         else
 *         {
 *             console.log( "Connected" )
 *
 *             client.exec( "SELECT * FROM Customers",
 *                 function( err, rows )
 *                 {
 *                     if( err )
 *                     {
 *                         console.error( "Error: ", err );
 *                     }
 *                     else
 *                     {
 *                         console.log(rows)
 *                     }
 *                 }
 *             );
 *
 *             client.disconnect(
 *                 function( err )
 *                 {
 *                     if( err )
 *                     {
 *                         console.error( "Disconnect error: ", err );
 *                     }
 *                     else
 *                     {
 *                         console.log( "Disconnected" )
 *                     }
 *                 }
 *             );
 *         }
 *     }
 * );
 * </pre></p>
 *
 * The following example also uses callbacks but the functions
 * are not inlined and the code is easier to understand.
 * <p><pre>
 * var sqlanywhere = require( 'sqlanywhere' );
 * var client = sqlanywhere.createConnection();
 * client.connect( "ServerName=demo17;UID=DBA;PWD=sql", async_connect );
 *
 * function async_connect( err )
 * {
 *     if( err )
 *     {
 * 	console.error( "Connect error: ", err );
 *     }
 *     else
 *     {
 * 	console.log( "Connected" )
 *
 * 	client.exec( "SELECT * FROM Customers", async_results );
 * 	
 * 	client.disconnect( async_disco );
 *     }
 * }
 *
 * function async_results( err, rows )
 * {
 *     if( err )
 *     {
 * 	console.error( "Error: ", err );
 *     }
 *     else
 *     {
 * 	console.log(rows)
 *     }
 * }
 *
 * function async_disco( err )
 * {
 *     if( err )
 *     {
 * 	console.error( "Disconnect error: ", err );
 *     }
 *     else
 *     {
 * 	console.log( "Disconnected" )
 *     }
 * }
 * </pre></p>
 *
 * You can also pass connection parameters into the createConnection function,
 * and those parameters are combined with those in the connect() function call
 * to get the connection string used for the connection. You can use a hash of
 * connection parameters or a connection string fragment in either call.
 * <p><pre>
 * var sqlanywhere = require( 'sqlanywhere' );
 * var client = sqlanywhere.createConnection( { uid: 'dba'; pwd: 'sql' } );
 * client.connect( 'server=MyServer;host=localhost' );
 * // the connection string that will be used is 
 * // "uid=dba;pwd=sql;server=MyServer;host=localhost"
 * </pre></p>
 */
class Connection : public ObjectWrap
{
public:
    /// @internal
    static void Init(Isolate *);

    /// @internal
    static NODE_API_FUNC(NewInstance);

private:
    /// @internal
    Connection(const FunctionCallbackInfo<Value> &args);
    /// @internal
    ~Connection();

    /// @internal
    static Persistent<Function> constructor;

    /// @internal
    static void noParamAfter(uv_work_t *req);
    /// @internal
    static void connectAfter(uv_work_t *req);
    /// @internal
    static void connectWork(uv_work_t *req);
    /// @internal
    static NODE_API_FUNC(New);

    /// Connect using an existing connection.
    //
    // This method connects to the database using an existing connection.
    // The DBCAPI connection handle obtained from the JavaScript External
    // environment needs to be passed in as a parameter. The disconnect
    // method should be called before the end of the program to free
    // up resources.
    //
    // @fn Connection::connect( Number DBCAPI_Handle, Function callback )
    //
    // @param DBCAPI_Handle The connection Handle ( type: Number )
    // @param callback The optional callback function. ( type: Function )
    //
    // <p><pre>
    // // In a method written for the JavaScript External Environment
    // var sqlanywhere = require( 'sqlanywhere' );
    // var client = sqlanywhere.createConnection;
    // client.connect( sa_dbcapi_handle, callback );
    // </pre></p>
    //
    // This method can be either synchronous or asynchronous depending on
    // whether or not a callback function is specified.
    // The callback function is of the form:
    //
    // <p><pre>
    // function( err )
    // {
    //
    // };
    // </pre></p>
    //
    // @internal
    ///

    /** Creates a new connection.
     *
     * This method creates a new connection using either a connection string
     * or a hash of connection parameters passed in as a parameter. Before
     * the end of the program, the connection should be disconnected using
     * the disconnect method to free up resources.
     *
     * The CharSet (CS) connection parameter CS=UTF-8 is always appended
     * to the end of the connection string by the driver since it is
     * required that all strings are sent in that encoding.
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
     * The following synchronous example shows how to use the connect method.
     * It is not necessary to specify the CHARSET=UTF-8 connection parameter
     * since it is always added automatically.
     *
     * <p><pre>
     * var sqlanywhere = require( 'sqlanywhere' );
     * var client = sqlanywhere.createConnection();
     * client.connect( "ServerName=demo17;UID=DBA;PWD=sql;CHARSET=UTF-8" );
     * </pre></p>
     *
     * @fn Connection::connect( String conn_string, Function callback )
     *
     * @param conn_string A valid connection string ( type: String )
     * @param callback The optional callback function. ( type: Function )
     *
     * @see Connection::disconnect
     */
    static NODE_API_FUNC(connect);

    /** Closes the current connection.
     *
     * This method closes the current connection and should be
     * called before the program ends to free up resources.
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
     * The following synchronous example shows how to use the disconnect method.
     *
     * <p><pre>
     * var sqlanywhere = require( 'sqlanywhere' );
     * var client = sqlanywhere.createConnection();
     * client.connect( "ServerName=demo17;UID=DBA;PWD=sql" );
     * client.disconnect()
     * </pre></p>
     *
     * @fn Connection::disconnect( Function callback )
     *
     * @param callback The optional callback function. ( type: Function )
     *
     * @see Connection::connect
     */
    static NODE_API_FUNC(disconnect);

    /// @internal
    static void disconnectWork(uv_work_t *req);

    /** Executes the specified SQL statement.
     *
     * This method takes in a SQL statement and an optional array of bind
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
     * The following synchronous example shows how to use the exec method.
     *
     * <p><pre>
     * var sqlanywhere = require( 'sqlanywhere' );
     * var client = sqlanywhere.createConnection();
     * client.connect( "ServerName=demo17;UID=DBA;PWD=sql" );
     * result = client.exec("SELECT * FROM Customers");
     * console.log( result );
     * client.disconnect()
     * </pre></p>
     *
     * The following synchronous example shows how to specify bind parameters.
     *
     * <p><pre>
     * var sqlanywhere = require( 'sqlanywhere' );
     * var client = sqlanywhere.createConnection();
     * client.connect( "ServerName=demo17;UID=DBA;PWD=sql" );
     * result = client.exec(
     *     "SELECT * FROM Customers WHERE ID >=? AND ID <?",
     *     [300, 400] );
     * console.log( result );
     * client.disconnect()
     * </pre></p>
     *
     * @fn Result Connection::exec( String sql, Array params, Function callback )
     *
     * @param sql The SQL statement to be executed. ( type: String )
     * @param params Optional array of bind parameters. ( type: Array )
     * @param callback The optional callback function. ( type: Function )
     *
     * @return If no callback is specified, the result is returned.
     *
     */
    static NODE_API_FUNC(exec);

    /** Prepares the specified SQL statement.
     *
     * This method prepares a SQL statement and returns a Statement object
     * if successful.
     *
     * This method can be either synchronous or asynchronous depending on
     * whether or not a callback function is specified.
     * The callback function is of the form:
     *
     * <p><pre>
     * function( err, Statement )
     * {
     *
     * };
     * </pre></p>
     *
     * The following synchronous example shows how to use the prepare method.
     *
     * <p><pre>
     * var sqlanywhere = require( 'sqlanywhere' );
     * var client = sqlanywhere.createConnection();
     * client.connect( "ServerName=demo17;UID=DBA;PWD=sql" )
     * stmt = client.prepare( "SELECT * FROM Customers WHERE ID >= ? AND ID < ?" );
     * result = stmt.exec( [200, 300] );
     * console.log( result );
     * client.disconnect();
     * </pre></p>
     *
     * @fn Statement Connection::prepare( String sql, Function callback )
     *
     * @param sql The SQL statement to be executed. ( type: String )
     * @param callback The optional callback function. ( type: Function )
     *
     * @return If no callback is specified, a Statement object is returned.
     *
     */
    static NODE_API_FUNC(prepare);

    /// @internal
    static void prepareAfter(uv_work_t *req);
    /// @internal
    static void prepareWork(uv_work_t *req);

    /** Performs a commit on the connection.
     *
     * This method performs a commit on the connection.
     * By default, inserts, updates, and deletes are not committed
     * upon disconnection from the database server.
     *
     * This method can be either synchronous or asynchronous depending on
     * whether or not a callback function is specified.
     * The callback function is of the form:
     *
     * <p><pre>
     * function( err ) {
     *
     * };
     * </pre></p>
     *
     * The following synchronous example shows how to use the commit method.
     *
     * <p><pre>
     * var sqlanywhere = require( 'sqlanywhere' );
     * var client = sqlanywhere.createConnection();
     * client.connect( "ServerName=demo17;UID=DBA;PWD=sql" )
     * stmt = client.prepare(
     *     "INSERT INTO Departments "
     *     + "( DepartmentID, DepartmentName, DepartmentHeadID )"
     *     + "VALUES (?,?,?)" );
     * result = stmt.exec( [600, 'Eastern Sales', 902] );
     * result += stmt.exec( [700, 'Western Sales', 902] );
     * stmt.drop();
     * console.log( "Number of rows added: " + result );
     * result = client.exec( "SELECT * FROM Departments" );
     * console.log( result );
     * client.commit();
     * client.disconnect();	
     * </pre></p>
     *
     * @fn Connection::commit( Function callback )
     *
     * @param callback The optional callback function. ( type: Function )
     *
     */
    static NODE_API_FUNC(commit);

    /// @internal
    static void commitWork(uv_work_t *req);

    /** Performs a rollback on the connection.
     *
     * This method performs a rollback on the connection.
     *
     * This method can be either synchronous or asynchronous depending on
     * whether or not a callback function is specified.
     * The callback function is of the form:
     *
     * <p><pre>
     * function( err ) {
     *
     * };
     * </pre></p>
     *
     * The following synchronous example shows how to use the rollback method.
     *
     * <p><pre>
     * var sqlanywhere = require( 'sqlanywhere' );
     * var client = sqlanywhere.createConnection();
     * client.connect( "ServerName=demo17;UID=DBA;PWD=sql" )
     * stmt = client.prepare(
     *     "INSERT INTO Departments "
     *     + "( DepartmentID, DepartmentName, DepartmentHeadID )"
     *     + "VALUES (?,?,?)" );
     * result = stmt.exec( [600, 'Eastern Sales', 902] );
     * result += stmt.exec( [700, 'Western Sales', 902] );
     * stmt.drop();
     * console.log( "Number of rows added: " + result );
     * result = client.exec( "SELECT * FROM Departments" );
     * console.log( result );
     * client.rollback();
     * client.disconnect();	
     * </pre></p>
     *
     * @fn Connection::rollback( Function callback )
     *
     * @param callback The optional callback function. ( type: Function )
     *
     */
    static NODE_API_FUNC(rollback);

    /// @internal
    static void rollbackWork(uv_work_t *req);

    /** Indicates whether the connection is connected.
     *
     * This synchronous method returns true if the connection is connected and
     * false otherwise.
     *
     * The following example shows how to use this method.
     *
     * <p><pre>
     * var sqlanywhere = require( 'sqlanywhere' );
     * var client = sqlanywhere.createConnection();
     * var connected = client.connected(); // false
     * client.connect( "ServerName=demo17;UID=DBA;PWD=sql" )
     * connected = client.connected(); // true
     * client.disconnect();
     * connected = client.connected(); // false
     * </pre></p>
     *
     * @fn Connection::connected()
     *
     * @return true if the connection is connected, false if not.
     *
     */
    static NODE_API_FUNC(connected);

public:
    /// @internal
    a_sqlany_connection *conn;
    /// @internal
    unsigned int max_api_ver;
    /// @internal
    bool sqlca_connection;
    /// @internal
    uv_mutex_t conn_mutex;
    /// @internal
    Persistent<String> _arg;
    /// @internal
    std::vector<void *> statements;

    /// @internal
    void removeStmt(class StmtObject *stmt);
    /// @internal
    void cleanupStmts(void);
};
