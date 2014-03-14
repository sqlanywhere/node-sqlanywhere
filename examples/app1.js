// ***************************************************************************
// Copyright (c) 2014 SAP AG or an SAP affiliate company. All rights reserved.
// ***************************************************************************
// This sample code is provided AS IS, without warranty or liability of any kind.
//
// You may use, reproduce, modify and distribute this sample code without limitation,
// on the condition that you retain the foregoing copyright notice and disclaimer
// as to the original code.
// ***************************************************************************

// This example uses waterfall.
// npm install async-waterfall

'use strict';

var util = require('util');
var waterfall = require('async-waterfall');
var sqlanywhere = require('sqlanywhere');

var cstr = { Server	: 'demo16',
	     UserID	: 'DBA',
	     Password	: 'sql'
	    };

var client = sqlanywhere.createConnection();

var tasks = [myconn, 
	     mysql1, myexecute, myresults, 
             mysql2, myexecute, myresults, 
	     mycommit, 
	     mydisco];
	     
var sql;	    

waterfall( tasks, done );
console.log( "Async calls underway\n" );

function myconn( cb )
{
    client.connect( cstr, cb );
}

function mysql1( arg, cb )
{
    var fields = ['EmployeeID', 'GivenName', 'Surname'];
    var range = [200,299];
    sql = util.format(
	'SELECT %s FROM Employees ' +
	'WHERE EmployeeID BETWEEN %s', 
	fields.join(','), range.join(' AND ') );
    cb();
}

function mysql2( arg, cb )
{
    var fields = ['EmployeeID', 'GivenName', 'Surname'];
    var range = [300,399];
    sql = util.format(
	'SELECT %s FROM Employees ' +
	'WHERE EmployeeID BETWEEN %s', 
	fields.join(','), range.join(' AND ') );
    cb();
}
 
function myexecute( cb ) 
{
    client.exec( sql, cb );
}

function myresults( rows, cb ) 
{
    console.log( "SQL statement: " + sql );
    console.log( util.inspect(rows, { colors: true } ) );
    console.log( "" );
    client.exec( "COMMIT", cb );
}

function mycommit( arg, cb ) 
{
    client.exec( "COMMIT", cb );
}

function mydisco( rows, cb )
{
    if( rows )
    {
	console.log( "Disco" );
	console.log( util.inspect(rows, { colors: true } ) );
    }
    client.disconnect(cb);
}

function done( err ) 
{
    console.log( "Async done" );
    if( err ) 
    {
	return console.error( err );
    }
}

