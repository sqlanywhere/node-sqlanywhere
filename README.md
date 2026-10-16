# node-sqlanywhere
This is a Node.js driver written for [SAP SQL Anywhere](http://www.sap.com/pc/tech/database/software/sybase-sql-anywhere/index.html).

[![NPM](https://nodei.co/npm/sqlanywhere.png?compact=true)](https://nodei.co/npm/sqlanywhere/)

## Install
```
npm install sqlanywhere
```
#### Prerequisites
This driver communicates with the native SQL Anywhere libraries, and thus requires
native compilation. Native compilation is managed by [`node-gyp`](https://github.com/nodejs/node-gyp/). Please see that project for additional prerequisites including Python 3, and C/C++ tool chain.

The module is compiled by the package's install script. Newer npm versions ask you to approve dependency install scripts; allow this one with `npm install-scripts approve sqlanywhere`.

Since version 2.0.0 the driver is built on [Node-API](https://nodejs.org/api/n-api.html), so a compiled module keeps working with newer Node.js versions without being rebuilt.

At run time the driver loads the SQL Anywhere client library `libdbcapi_r.so` (Linux), `libdbcapi.dylib` (macOS) or `dbcapi.dll` (Windows). Run `sa_config.sh` from the SQL Anywhere installation (or set `LD_LIBRARY_PATH` / `DYLD_LIBRARY_PATH` yourself) so that it can be found. The library must match the architecture of the `node` binary: SQL Anywhere 17 for macOS is x86_64 only, so on Apple silicon use an x64 build of Node.js (it runs under Rosetta).

Versions supported:

<table border="1">
<tr><th>Driver version</th><th>Node.js version</th></tr>
<tr><td>1.0.6</td><td>0.10, 0.12, 4.x, 5.x</td></tr>
<tr><td>1.0.9</td><td>6.x, 7.x</td></tr>
<tr><td>1.0.19</td><td>8.x</td></tr>
<tr><td>1.0.22</td><td>9.x</td></tr>
<tr><td>2.0.0</td><td>18 and later</td></tr>
</table>

#### Running the tests
The tests need a running SQL Anywhere server:
```
dbsrv17 -n node_test -x "tcpip(port=2638)" demo.db
SQLANY_TEST_CONN="Host=localhost:2638;ServerName=node_test;UID=DBA;PWD=sql" node --test test/*.test.js
```
On macOS run `node --test` directly rather than `npm test`: npm starts scripts with `/bin/sh`, which drops `DYLD_LIBRARY_PATH`.

## Getting Started

```js
var sqlanywhere = require('sqlanywhere');

var conn = sqlanywhere.createConnection();

var conn_params = {
  Server  : 'demo16',
  UserId  : 'DBA',
  Password: 'sql'
};


conn.connect(conn_params, function(err) {
  if (err) throw err;
  conn.exec('SELECT Name, Description FROM Products WHERE id = ?', [301], function (err, result) {
    if (err) throw err;

    console.log('Name: ', result[0].Name, ', Description: ', result[0].Description);
    // output --> Name: Tee Shirt, Description: V-neck
    conn.disconnect();
  })
});
```

## Establish a database connection
### Connecting
A database connection object is created by calling `createConnection`.  The connection is established by calling the connection object's `connect` method, and passing in an object representing connection parameters. The object can contain most valid [connection properties](http://dcx.sybase.com/index.html#sa160/en/dbadmin/da-conparm.html).

##### Example: Connecting over TCP/IP
```js
conn.connect({
  Host    : 'localhost:2638'
  UserId  : 'DBA',
  Password: 'sql'
});
```

##### Example: Auto-starting a database on first connection
```js
conn.connect({
  DatabaseFile: 'demo.db',
  AutoStart: 'YES',
  UserId: 'DBA',
  Password: 'sql',
});
```

### Disconnecting

The `disconnect()` function closes the connection. As of version 1.0.16, you can also use `close()`.

```js
conn.disconnect(function(err) {
  if (err) throw err;
  console.log('Disconnected');
});
```

### Determining whether you are connected
The connected() method was added in version 1.0.16.
```js
var conn = sqlanywhere.createConnection();
var connected = conn.connected(); // connected === false
conn.connect({ ... } );
connected = conn.connected(); // connected === true
conn.disconnect();
connected = conn.connected(); // connected === false
```

## Direct Statement Execution
Direct statement execution is the simplest way to execute SQL statements. The inputs are the SQL command to be executed, and an optional array of positional arguments. The result is returned using callbacks. The type of returned result depends on the kind of statement.

#### DDL Statement

In the case of a successful DDL Statement nothing is returned.

```js
conn.exec('CREATE TABLE Test (id INTEGER PRIMARY KEY DEFAULT AUTOINCREMENT, msg LONG VARCHAR)', function (err, result) {
  if (err) throw err;
  console.log('Table Test created!');
});
```

#### DML Statement

In the case of a DML Statement the number of `affectedRows` is returned.

```js
conn.exec("INSERT INTO Test(msg) SELECT 'Hello,' || row_num FROM sa_rowgenerator(1, 10)", function (err, affectedRows) {
  if (err) throw err;
  console.log('Number of affected rows:', affectedRows);
  conn.commit();
});
```

#### Query

The `exec` function is a convenient way to completely retrieve the result of a query. In this case all selected rows are fetched and returned in the callback. 

```js
conn.exec("SELECT * FROM Test WHERE id < 5", function (err, rows) {
  if (err) throw err;
  console.log('Rows:', rows);
});
```

Values in the query can be substitued with JavaScript variables by using `?` placeholders in the query, and passing an array of positional arguments.

```js
conn.exec("SELECT * FROM Test WHERE id BETWEEN ? AND ?", [5, 8], function (err, rows) {
  if (err) throw err;
  console.log('Rows:', rows);
});
```

A placeholder without a value is bound as NULL, like a `null` value: `conn.exec("INSERT INTO Test VALUES ( ?, ? )", [1])` inserts NULL into the second column, and so does a statement executed without any values. Earlier versions left such parameters unbound; the server took them as NULL too, but then also inserted NULL for them in later INSERT statements on the connection that did have values, and could crash. Passing more values than there are placeholders is an error.

Output parameters of procedures are not returned. A placeholder for an `OUT` parameter is never bound, so a value passed for it is ignored, and an `INOUT` parameter only passes its value into the procedure: `conn.exec("CALL MyProc( ?, ? )", [1, null])` calls `MyProc( IN a INT, OUT b INT )` with `a = 1`. Earlier versions crashed when such a placeholder had a value, and otherwise could call the procedure with NULL for its `IN` parameters. A `CALL` can't be executed for several rows at once, and a procedure that has `OUT` parameters and also returns a result set fails with error -182.

As of version 1.0.16, wide inserts, deletes, and updates are possible by passing in an array of arrays, one per row. For example, the following statement inserts three rows rather than just one:

```js
conn.exec("INSERT INTO Test VALUES ( ?, ? )", [ [1, 10], [2, 20], [3, 30] ], function (err, rows) {
  if (err) throw err;
  console.log('Rows:', rows); // should display 3
});
```

When using wide statements, each array must have the same number of elements and the type of the values must be the same in each row, except that any value can be NULL. DBCAPI cannot send a column that mixes NULL and other values in one execution, so such a statement is executed one row at a time, which is slower. If a row fails, the rows before it are rolled back to a savepoint, just as a failing wide statement inserts nothing; with `auto_commit` on or `chained` off every row is committed, so they stay.

## Prepared Statement Execution
#### Prepare a Statement
The connection returns a `statement` object which can be executed multiple times.
```js
conn.prepare('SELECT * FROM Test WHERE id = ?', function (err, stmt){
  if (err) throw err;
  // do something with the statement
});
```

#### Execute a Statement
The execution of a prepared statement is similar to the direct statement execution. The first parameter of `exec` function is an array with positional parameters. With version 1.0.16, wide statements (except SELECT) are supported here as well.

```js
stmt.exec([16], function(err, rows) {
  if (err) throw err;
  console.log("Rows: ", rows);
});
```

A prepared statement that is executed with several rows at once is prepared a second time on the server for those executions, so it counts twice towards the limit of prepared statements per connection (the `max_statement_count` option).

#### Fetching multiple result sets
As of version 1.0.16, you can prepare and execute a batch containing multiple select statements. To do this, you would prepare the multiple select statements and use `stmt.exec()` to fetch the first result set. To fetch the next result set, call `stmt.getMoreResults()`. getMoreResults takes an optional callback function (which takes the same arguments as `exec`), making it asynchronous. The `getMoreResults()` function returns `undefined` (or passes it to the callback function) after the last result set.

A simple synchronous example is below.
```js
stmt = conn.prepare( 'select 1 as a from dummy; select 2 as b, 3 as c from dummy' );
rs = stmt.exec();
// rs == [ { a: 1 } ]
rs = stmt.getMoreResults();
// rs = [ { b: 2, c: 3 } ]
stmt.drop();
```

#### Drop Statement
```js
stmt.drop(function(err) {
  if (err) throw err;
});
```

## Transaction Handling
__Transactions are  not automatically commited.__ Executing a statement implicitly starts a new transaction that must be explicitly committed, or rolled back. 

#### Commit a Transaction

```js
conn.commit(function(err) {
  if (err) throw err;
  console.log('Transaction commited.');
});
```

#### Rollback a Transaction
```js
conn.rollback(function(err) {
  if (err) throw err;
  console.log('Transaction rolled back.');
});
```

## Resources
+ [SAP SQL Anywhere Documentation](http://dcx.sap.com/)
+ [SAP SQL Anywhere Developer Q&A Forum](http://sqlanywhere-forum.sap.com/)
