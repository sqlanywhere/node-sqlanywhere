var os = require("os");
var fs = require("fs");
var path = require("path");

var db = null;
var driver_file = "sqlanywhere.node";
var nodeVersion = detectCurrentNodeVersion();

function detectCurrentNodeVersion() {
    var nodeVersion;
    if (process.version.indexOf("v0.10") === 0) {
        nodeVersion = "0_10";
    } else if (process.version.indexOf("v0.8") === 0) {
        nodeVersion = "0_8";
    }
    return nodeVersion;
}

function attemptToLoadPreviouslyCompiledBindings() {
    var defaultCompilationTarget = path.join(__dirname, "..", "build", "Release", driver_file);
    try {
        db = require(defaultCompilationTarget);
        console.log("Found binaries in:" + defaultCompilationTarget);
    } catch (err) {
        throw new Error("Could not load modules for Platform: '" + process.platform + "', Process Arch: '"
        + process.arch + "', and Version: '" + process.version + "'");
    }
}

if (nodeVersion) {
    if (os.platform() === 'win32') {
        // Default Windows bindings (dll's) names are hardcoded internally as "sqlanywhere_v0_10",
        driver_file = "sqlanywhere_v" + nodeVersion + ".node";
    }
    var source = path.join(__dirname, "..", "prebuild", os.platform(), os.arch(), nodeVersion, driver_file);

    if (fs.existsSync(source)) {
        console.log("Attempting to load pre-compiled bindings from: " + source + "...");
        try {
            db = require(source);
            console.log("Success");
        } catch (err) {
            console.log(err);
        }
    }
}

if (!db) {
    attemptToLoadPreviouslyCompiledBindings();
}

module.exports = db;
