var db = null;
var exec = require('child_process').exec;
exec( "rm ./build/Release/nodesa.node", function( error, out, err ) {
    try {
	console.log( "Looking for binaries..." );
    
	// Clean up and try to load modules
	db = require( "./lib/index" );
	db.createConnection(); // If this step succeeds binaries can be found
	console.log( "Binaries found! Install Complete!" );
    
    } catch( err ) {
	try {
	    console.log( "Trying to build binaries" );
	
	    var config = require('child_process').exec;
	    config( "node-gyp configure", function( error, out ) {
		if( error ) {
		    console.log( error, out );
		    console.log( "Error when executing node-gyp configure" );
		    process.exit( -1 );
		}
		var build = require('child_process').exec;
		build( "node-gyp build", function( error, out ) {
		    if( error ) {
			console.log( "Error when executing node-gyp configure" );
			process.exit( -1 );
		    }
		    db = require( "./lib/index" );
		    conn = db.createConnection();
		    console.log( "Built Binaries!" );
		    console.log( "Install Complete!" );
	
		});
	    });
	} catch( err ) {
	    console.log( "Error Building Binaries. Make sure node-gyp is installed and in the PATH")
            throw new Error( "Could not build binaries" );
	    process.exit( -1 );
	}
    }
});
