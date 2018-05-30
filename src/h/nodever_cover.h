// ***************************************************************************
// Copyright (c) 2018 SAP SE or an SAP affiliate company. All rights reserved.
// ***************************************************************************
#include <node_version.h>

#if NODE_MAJOR_VERSION == 0 && NODE_MINOR_VERSION == 10
#define v010	1
#define v012	0
#elif NODE_MAJOR_VERSION == 0 && NODE_MINOR_VERSION == 12
#define v010	0
#define v012	1
#else
#define v010	0
#define v012	0
#endif

#if v010
#define NODE_API_FUNC( name ) Handle<Value> name ( const Arguments &args )
#else
#define NODE_API_FUNC( name ) void name ( const FunctionCallbackInfo<Value> &args )
#endif
