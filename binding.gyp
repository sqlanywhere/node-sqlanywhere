{
  "targets": [
    {
      "target_name": "sqlanywhere",
      "defines": [ '_SACAPI_VERSION=5', 'DRIVER_NAME=sqlanywhere' ],
      "sources": [ "src/sqlanywhere.cpp",
		   "src/utils.cpp",
		   "src/sacapidll.cpp", ],

      "include_dirs": [
        "src/h",
        "<!(node -e \"require('nan')\")",
      ],
      
      'configurations': {
	'Release': {
	  'msvs_settings': {
            'VCCLCompilerTool': {
              'ExceptionHandling': 1
            }
	  }
	}
      }	
    }
  ]
}
