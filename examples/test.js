/**
 * Simple test to check the proper connection to a test database instance.
 *
 * Note that you can check db connection using this command from the command line,
 * within your SQLAnywhere client installation dir:
 *
 * $ dbping -d -c "Host=54.147.208.71:2638;UserId=DBA;Password=sql"
 */
var sqlanywhere = require('..');
var assert = require("assert");

var conn = sqlanywhere.createConnection();

var conn_params = {
    Host: '1.2.3.4:2638',
    UserId: 'DBA',
    Password: 'sql'
};

conn.connect(conn_params, function () {
    console.log('Executing: SELECT Name, Description FROM Products WHERE id = 301');

    conn.exec('SELECT Name, Description FROM Products WHERE id = ?', [301], function (err, result) {
        if (err) throw err;

        var product = result[0];
        console.log('Response: \n' + JSON.stringify(product, null, 2));

        assert.equal(product.Name, 'Tee Shirt');
        assert.equal(product.Description, 'V-neck');

        console.log('Connection was successful and SQL query result was as expected!')
    })
});