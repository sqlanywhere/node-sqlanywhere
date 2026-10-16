{
  "targets": [
    {
      "target_name": "sqlanywhere",
      "defines": [ "_SACAPI_VERSION=5", "NAPI_VERSION=8" ],
      "sources": [ "src/sqlanywhere.cpp",
                   "src/utils.cpp",
                   "src/sacapidll.cpp" ],
      "include_dirs": [ "src/h" ],
      "dependencies": [
        "<!(node -p \"require('node-addon-api').targets\"):node_addon_api_except"
      ],
      "xcode_settings": {
        "MACOSX_DEPLOYMENT_TARGET": "11.0"
      }
    }
  ]
}
