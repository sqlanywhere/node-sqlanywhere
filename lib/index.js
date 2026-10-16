// ***************************************************************************
// Copyright (c) 2021 SAP SE or an SAP affiliate company. All rights reserved.
// ***************************************************************************
var db = null;

try {
    // Built by node-gyp on install; N-API keeps it working across Node.js versions
    db = require( "./../build/Release/sqlanywhere.node" );
} catch( err ) {
    throw new Error( "Could not load modules for Platform: '" +
		     process.platform + "', Process Arch: '" + process.arch +
		     "', and Version: '" + process.version + "': " + err.message,
		     { cause: err } );
}
module.exports = db;
