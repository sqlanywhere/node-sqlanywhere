#node-sqlanywhere
This is a Node.js driver written for [SAP SQL Anywhere](http://www.sap.com/pc/tech/database/software/sybase-sql-anywhere/index.html).
##Install

```
npm install sqlanywhere
```

##Getting Started

```js
var sqlanywhere = require('sqlanywhere');

var conn = sqlanywhere.createConnection();

var conn_params = {
  Server  : 'demo16',
  UserId  : 'DBA',
  Password: 'sql'
};


conn.connect(conn_params, function() {
  conn.exec('SELECT Name, Description FROM Products WHERE id = ?', [301], function (err, result) {
    if (err) throw err;

    console.log('Name: ', result[0].Name, ', Description: ', result[0].Description);
    // output --> Name: Tee Shirt, Description: V-neck
  })
});
```

##Establish a database connection
###Connecting
A database connection object is created by calling `createConnection`.  The connection is established by calling the connection object's `connect` method, and passing in an object representing connection parameters. The object can contain most valid [connection properties](http://dcx.sybase.com/index.html#sa160/en/dbadmin/da-conparm.html).

#####Example: Connecting over TCP/IP
```js
conn.connect({
  Host    : 'localhost:2638'
  UserId  : 'DBA',
  Password: 'sql'
});
```

#####Example: Auto-starting a database on first connection
```js
conn.connect({
  DatabaseFile: 'demo.db',
  AutoStart: 'YES',
  UserId: 'DBA',
  Password: 'sql',
});
```

###Disconnecting

```js
conn.disconnect(function(err) {
  if (err) throw err;
  console.log('Disconnected');
});
```
##Direct Statement Execution
Direct statement execution is the simplest way to execute SQL statements. The only input parameter is the SQL command to be executed. Generally we return the statement execution results using callbacks. The type of returned result depends on the kind of statement.

####DDL Statement

In the case of a successful DDL Statement nothing is returned.

```js
conn.exec('CREATE TABLE Test (id INTEGER PRIMARY KEY DEFAULT AUTOINCREMENT, msg LONG VARCHAR)', function (err, result) {
  if (err) throw err;
  console.log('Table Test created!');
});
```

####DML Statement

In the case of a DML Statement the number of `affectedRows` is returned.

```js
conn.exec("INSERT INTO Test(msg) SELECT 'Hello,' || row_num FROM sa_rowgenerator(1, 10)", function (err, affectedRows) {
  if (err) throw err;
  console.log('Number of affected rows:', affectedRows);
  conn.commit();
});
```

####Query

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

##Prepared Statement Execution
####Prepare a Statement
The connection returns a `statement` object which can be executed multiple times.
```js
conn.prepare('SELECT * FROM Test WHERE id = ?', function (err, stmt){
  if (err) throw err;
  // do something with the statement
});
```

####Execute a Statement
The execution of a prepared statement is similar to the direct statement execution. The first parameter of `exec` function is an array with positional parameters.
```js
stmt.exec([16], function(err, rows) {
  if (err) throw err;
  console.log("Rows: ", rows);
});
```

####Drop Statement
```js
stmt.drop(function(err) {
  if (err) throw err;
});
```

##Transaction Handling
__Transactions are  not automatically commited.__ Executing a statement implicitly starts a new transaction that must be explicitly committed, or rolled back. 

####Commit a Transaction

```js
conn.commit(function(err) {
  if (err) throw err;
  console.log('Transaction commited.');
});
```

####Rollback a Transaction
```js
conn.rollback(function(err) {
  if (err) throw err;
  console.log('Transaction rolled back.');
});
```

##Resources
+ [SAP SQL Anywhere Documentation](http://dcx.sybase.com/)
+ [SAP SQL Anywhere Developer Q&A Forum](http://sqlanywhere-forum.sybase.com/)

