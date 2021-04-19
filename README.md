# node-sqlanywhere
This is a Node.js driver written for [SAP SQL Anywhere](https://www.sap.com/products/sql-anywhere.html).

[![NPM](https://nodei.co/npm/sqlanywhere.png?compact=true)](https://nodei.co/npm/sqlanywhere/)

## Install
```
npm install sqlanywhere
```
#### Prerequisites
This driver communicates with the native SQL Anywhere libraries, and thus requires
native compilation. Native compilation is managed by [`node-gyp`](https://github.com/TooTallNate/node-gyp/). Please see that project for additional prerequisites including Python 2.7, and C/C++ tool chain.

The official version hosted on NPM includes precompiled libraries for Windows (64-bit).

Versions supported:

<table border="1">
<tr><th>Driver version</th><th>Node.js version</th></tr>
<tr><td>1.0.6</td><td>0.10, 0.12, 4.x, 5.x</td></tr>
<tr><td>1.0.9</td><td>6.x, 7.x</td></tr>
<tr><td>1.0.19</td><td>8.x</td></tr>
<tr><td>1.0.22</td><td>9.x</td></tr>
<tr><td>1.0.23</td><td>10.x</td></tr>
<tr><td>1.0.24<td><b>Only</b> 5.x through 10.x (support for 0.10, 0.12, and 4.x is dropped)</td></tr>
<tr><td>1.0.25<td>5.x through 12.x</td></tr>
</table>

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

As of version 1.0.16, wide inserts, deletes, and updates are possible by passing in an array of arrays, one per row. For example, the following statement inserts three rows rather than just one:

```js
conn.exec("INSERT INTO Test VALUES ( ?, ? )", [ [1, 10], [2, 20], [3, 30] ], function (err, rows) {
  if (err) throw err;
  console.log('Rows:', rows); // should display 3
});
```

When using wide statements, each array must have the same number of elements and the type of the values must be the same in each row.

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
