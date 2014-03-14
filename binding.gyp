{
  "targets": [
    {
      "target_name": "sqlanywhere",
      "defines": [ '_SACAPI_VERSION=2', 'DRIVER_NAME=sqlanywhere' ],
      "sources": [ "src/sqlanywhere.cpp",
		   "src/utils.cpp", 
		   "src/sacapidll.cpp", ],

      "include_dirs": [ "src/h", ],
      
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