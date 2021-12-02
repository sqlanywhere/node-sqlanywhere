// ***************************************************************************
// Copyright (c) 2021 SAP SE or an SAP affiliate company. All rights reserved.
// ***************************************************************************
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//
// See the License for the specific language governing permissions and
// limitations under the License.
//
// While not a requirement of the license, if you do modify this file, we
// would appreciate hearing about it. Please email
// sqlany_interfaces@sap.com
//
// ***************************************************************************

#ifndef SACAPIDLL_H
#define SACAPIDLL_H

#include "sacapi.h"

/** \file sacapidll.h
 * \brief Header file for stub that can dynamically load the main API DLL.
 * The user will need to include sacapidll.h in their source files and compile in sacapidll.c
 */

#if defined(__cplusplus)
extern "C"
{
#endif
    typedef sacapi_bool (*sqlany_init_func)(const char *app_name, sacapi_u32 api_version, sacapi_u32 *max_version);
    typedef void (*sqlany_fini_func)();
    typedef a_sqlany_connection *(*sqlany_new_connection_func)();
    typedef void (*sqlany_free_connection_func)(a_sqlany_connection *sqlany_conn);
    typedef a_sqlany_connection *(*sqlany_make_connection_func)(void *arg);
    typedef sacapi_bool (*sqlany_connect_func)(a_sqlany_connection *sqlany_conn, const char *str);
    typedef sacapi_bool (*sqlany_disconnect_func)(a_sqlany_connection *sqlany_conn);
    typedef sacapi_bool (*sqlany_execute_immediate_func)(a_sqlany_connection *sqlany_conn, const char *sql);
    typedef a_sqlany_stmt *(*sqlany_prepare_func)(a_sqlany_connection *sqlany_conn, const char *sql_str);
    typedef void (*sqlany_free_stmt_func)(a_sqlany_stmt *sqlany_stmt);
    typedef sacapi_i32 (*sqlany_num_params_func)(a_sqlany_stmt *sqlany_stmt);
    typedef sacapi_bool (*sqlany_describe_bind_param_func)(a_sqlany_stmt *sqlany_stmt, sacapi_u32 index, a_sqlany_bind_param *params);
    typedef sacapi_bool (*sqlany_bind_param_func)(a_sqlany_stmt *sqlany_stmt, sacapi_u32 index, a_sqlany_bind_param *params);
    typedef sacapi_bool (*sqlany_send_param_data_func)(a_sqlany_stmt *sqlany_stmt, sacapi_u32 index, char *buffer, size_t size);
    typedef sacapi_bool (*sqlany_reset_func)(a_sqlany_stmt *sqlany_stmt);
    typedef sacapi_bool (*sqlany_get_bind_param_info_func)(a_sqlany_stmt *sqlany_stmt, sacapi_u32 index, a_sqlany_bind_param_info *info);
    typedef sacapi_bool (*sqlany_execute_func)(a_sqlany_stmt *sqlany_stmt);
    typedef a_sqlany_stmt *(*sqlany_execute_direct_func)(a_sqlany_connection *sqlany_conn, const char *sql_str);
    typedef sacapi_bool (*sqlany_fetch_absolute_func)(a_sqlany_stmt *sqlany_result, sacapi_i32 row_num);
    typedef sacapi_bool (*sqlany_fetch_next_func)(a_sqlany_stmt *sqlany_stmt);
    typedef sacapi_bool (*sqlany_get_next_result_func)(a_sqlany_stmt *sqlany_stmt);
    typedef sacapi_i32 (*sqlany_affected_rows_func)(a_sqlany_stmt *sqlany_stmt);
    typedef sacapi_i32 (*sqlany_num_cols_func)(a_sqlany_stmt *sqlany_stmt);
    typedef sacapi_i32 (*sqlany_num_rows_func)(a_sqlany_stmt *sqlany_stmt);
    typedef sacapi_bool (*sqlany_get_column_func)(a_sqlany_stmt *sqlany_stmt, sacapi_u32 col_index, a_sqlany_data_value *buffer);
    typedef sacapi_i32 (*sqlany_get_data_func)(a_sqlany_stmt *sqlany_stmt, sacapi_u32 col_index, size_t offset, void *buffer, size_t size);
    typedef sacapi_bool (*sqlany_get_data_info_func)(a_sqlany_stmt *sqlany_stmt, sacapi_u32 col_index, a_sqlany_data_info *buffer);
    typedef sacapi_bool (*sqlany_get_column_info_func)(a_sqlany_stmt *sqlany_stmt, sacapi_u32 col_index, a_sqlany_column_info *buffer);
    typedef sacapi_bool (*sqlany_commit_func)(a_sqlany_connection *sqlany_conn);
    typedef sacapi_bool (*sqlany_rollback_func)(a_sqlany_connection *sqlany_conn);
    typedef sacapi_bool (*sqlany_client_version_func)(char *buffer, size_t len);
    typedef sacapi_i32 (*sqlany_error_func)(a_sqlany_connection *sqlany_conn, char *buffer, size_t size);
    typedef size_t (*sqlany_sqlstate_func)(a_sqlany_connection *sqlany_conn, char *buffer, size_t size);
    typedef void (*sqlany_clear_error_func)(a_sqlany_connection *sqlany_conn);
#if _SACAPI_VERSION + 0 >= SQLANY_API_VERSION_2
    typedef a_sqlany_interface_context *(*sqlany_init_ex_func)(const char *app_name, sacapi_u32 api_version, sacapi_u32 *max_version);
    typedef void (*sqlany_fini_ex_func)(a_sqlany_interface_context *context);
    typedef a_sqlany_connection *(*sqlany_new_connection_ex_func)(a_sqlany_interface_context *context);
    typedef a_sqlany_connection *(*sqlany_make_connection_ex_func)(a_sqlany_interface_context *context, void *arg);
    typedef sacapi_bool (*sqlany_client_version_ex_func)(a_sqlany_interface_context *context, char *buffer, size_t len);
    typedef void (*sqlany_cancel_func)(a_sqlany_connection *sqlany_conn);
#endif
#if _SACAPI_VERSION + 0 >= SQLANY_API_VERSION_3
    typedef sacapi_bool (*sqlany_register_callback_func)(a_sqlany_connection *sqlany_conn, a_sqlany_callback_type index, SQLANY_CALLBACK_PARM callback);
#endif
#if _SACAPI_VERSION + 0 >= SQLANY_API_VERSION_4
    typedef sacapi_bool (*sqlany_set_batch_size_func)(a_sqlany_stmt *sqlany_stmt, sacapi_u32 num_rows);
    typedef sacapi_bool (*sqlany_set_param_bind_type_func)(a_sqlany_stmt *sqlany_stmt, size_t row_size);
    typedef sacapi_u32 (*sqlany_get_batch_size_func)(a_sqlany_stmt *sqlany_stmt);
    typedef sacapi_bool (*sqlany_set_rowset_size_func)(a_sqlany_stmt *sqlany_stmt, sacapi_u32 num_rows);
    typedef sacapi_u32 (*sqlany_get_rowset_size_func)(a_sqlany_stmt *sqlany_stmt);
    typedef sacapi_bool (*sqlany_set_column_bind_type_func)(a_sqlany_stmt *sqlany_stmt, size_t row_size);
    typedef sacapi_bool (*sqlany_bind_column_func)(a_sqlany_stmt *sqlany_stmt, sacapi_u32 index, a_sqlany_data_value *value);
    typedef sacapi_bool (*sqlany_clear_column_bindings_func)(a_sqlany_stmt *sqlany_stmt);
    typedef sacapi_i32 (*sqlany_fetched_rows_func)(a_sqlany_stmt *sqlany_stmt);
    typedef sacapi_bool (*sqlany_set_rowset_pos_func)(a_sqlany_stmt *sqlany_stmt, sacapi_u32 row_num);
#endif
#if _SACAPI_VERSION + 0 >= SQLANY_API_VERSION_5
    typedef sacapi_bool (*sqlany_reset_param_data_func)(a_sqlany_stmt *sqlany_stmt, sacapi_u32 index);
    typedef size_t (*sqlany_error_length_func)(a_sqlany_connection *conn);
#endif

#if defined(__cplusplus)
}
#endif

/// @internal
#define function(x) x##_func x

/** The SQL Anywhere C API interface structure.
 *
 * Only one instance of this structure is required in your application environment.  This structure
 * is initialized by the sqlany_initialize_interface method.   It attempts to load the SQL Anywhere C
 * API DLL or shared object dynamically and looks up all the entry points of the DLL.  The fields in
 * the SQLAnywhereInterface structure is populated to point to the corresponding functions in the DLL.
 * \sa sqlany_initialize_interface()
 */
typedef struct SQLAnywhereInterface
{
    /** DLL handle.
     */
    void *dll_handle;

    /** Flag to know if initialized or not.
     */
    int initialized;

    /** Pointer to ::sqlany_init() function.
     */
    function(sqlany_init);

    /** Pointer to ::sqlany_fini() function.
     */
    function(sqlany_fini);

    /** Pointer to ::sqlany_new_connection() function.
     */
    function(sqlany_new_connection);

    /** Pointer to ::sqlany_free_connection() function.
     */
    function(sqlany_free_connection);

    /** Pointer to ::sqlany_make_connection() function.
     */
    function(sqlany_make_connection);

    /** Pointer to ::sqlany_connect() function.
     */
    function(sqlany_connect);

    /** Pointer to ::sqlany_disconnect() function.
     */
    function(sqlany_disconnect);

    /** Pointer to ::sqlany_execute_immediate() function.
     */
    function(sqlany_execute_immediate);

    /** Pointer to ::sqlany_prepare() function.
     */
    function(sqlany_prepare);

    /** Pointer to ::sqlany_free_stmt() function.
     */
    function(sqlany_free_stmt);

    /** Pointer to ::sqlany_num_params() function.
     */
    function(sqlany_num_params);

    /** Pointer to ::sqlany_describe_bind_param() function.
     */
    function(sqlany_describe_bind_param);

    /** Pointer to ::sqlany_bind_param() function.
     */
    function(sqlany_bind_param);

    /** Pointer to ::sqlany_send_param_data() function.
     */
    function(sqlany_send_param_data);

    /** Pointer to ::sqlany_reset() function.
     */
    function(sqlany_reset);

    /** Pointer to ::sqlany_get_bind_param_info() function.
     */
    function(sqlany_get_bind_param_info);

    /** Pointer to ::sqlany_execute() function.
     */
    function(sqlany_execute);

    /** Pointer to ::sqlany_execute_direct() function.
     */
    function(sqlany_execute_direct);

    /** Pointer to ::sqlany_fetch_absolute() function.
     */
    function(sqlany_fetch_absolute);

    /** Pointer to ::sqlany_fetch_next() function.
     */
    function(sqlany_fetch_next);

    /** Pointer to ::sqlany_get_next_result() function.
     */
    function(sqlany_get_next_result);

    /** Pointer to ::sqlany_affected_rows() function.
     */
    function(sqlany_affected_rows);

    /** Pointer to ::sqlany_num_cols() function.
     */
    function(sqlany_num_cols);

    /** Pointer to ::sqlany_num_rows() function.
     */
    function(sqlany_num_rows);

    /** Pointer to ::sqlany_get_column() function.
     */
    function(sqlany_get_column);

    /** Pointer to ::sqlany_get_data() function.
     */
    function(sqlany_get_data);

    /** Pointer to ::sqlany_get_data_info() function.
     */
    function(sqlany_get_data_info);

    /** Pointer to ::sqlany_get_column_info() function.
     */
    function(sqlany_get_column_info);

    /** Pointer to ::sqlany_commit() function.
     */
    function(sqlany_commit);

    /** Pointer to ::sqlany_rollback() function.
     */
    function(sqlany_rollback);

    /** Pointer to ::sqlany_client_version() function.
     */
    function(sqlany_client_version);

    /** Pointer to ::sqlany_error() function.
     */
    function(sqlany_error);

    /** Pointer to ::sqlany_sqlstate() function.
     */
    function(sqlany_sqlstate);

    /** Pointer to ::sqlany_clear_error() function.
     */
    function(sqlany_clear_error);

#if _SACAPI_VERSION + 0 >= SQLANY_API_VERSION_2
    /** Pointer to ::sqlany_init_ex() function.
     */
    function(sqlany_init_ex);

    /** Pointer to ::sqlany_fini_ex() function.
     */
    function(sqlany_fini_ex);

    /** Pointer to ::sqlany_new_connection_ex() function.
     */
    function(sqlany_new_connection_ex);

    /** Pointer to ::sqlany_make_connection_ex() function.
     */
    function(sqlany_make_connection_ex);

    /** Pointer to ::sqlany_client_version_ex() function.
     */
    function(sqlany_client_version_ex);

    /** Pointer to ::sqlany_cancel() function.
     */
    function(sqlany_cancel);
#endif
#if _SACAPI_VERSION + 0 >= SQLANY_API_VERSION_3
    /** Pointer to ::sqlany_register_callback() function.
     */
    function(sqlany_register_callback);
#endif
#if _SACAPI_VERSION + 0 >= SQLANY_API_VERSION_4
    /** Pointer to ::sqlany_set_batch_size() function.
     */
    function(sqlany_set_batch_size);
    /** Pointer to ::sqlany_set_param_bind_type() function.
     */
    function(sqlany_set_param_bind_type);
    /** Pointer to ::sqlany_get_batch_size() function.
     */
    function(sqlany_get_batch_size);
    /** Pointer to ::sqlany_set_rowset_size() function.
     */
    function(sqlany_set_rowset_size);
    /** Pointer to ::sqlany_get_rowset_size() function.
     */
    function(sqlany_get_rowset_size);
    /** Pointer to ::sqlany_set_column_bind_type() function.
     */
    function(sqlany_set_column_bind_type);
    /** Pointer to ::sqlany_bind_column() function.
     */
    function(sqlany_bind_column);
    /** Pointer to ::sqlany_clear_column_bindings() function.
     */
    function(sqlany_clear_column_bindings);
    /** Pointer to ::sqlany_fetched_rows() function.
     */
    function(sqlany_fetched_rows);
    /** Pointer to ::sqlany_set_rowset_pos() function.
     */
    function(sqlany_set_rowset_pos);
#endif

#if _SACAPI_VERSION + 0 >= SQLANY_API_VERSION_5
    /** Pointer to ::sqlany_reset_param_data() function.
     */
    function(sqlany_reset_param_data);
    /** Pointer to ::sqlany_error_length() function.
     */
    function(sqlany_error_length);
#endif

} SQLAnywhereInterface;
#undef function

/** Initializes the SQLAnywhereInterface object and loads the DLL dynamically.
 *
 * Use the following statement to include the function prototype:
 * 
 * <pre>
 * \#include "sacapidll.h"
 * </pre>
 *
 * This function attempts to load the SQL Anywhere C API DLL dynamically and looks up all
 * the entry points of the DLL. The fields in the SQLAnywhereInterface structure are
 * populated to point to the corresponding functions in the DLL. If the optional path argument
 * is NULL, the environment variable SQLANY_API_DLL is checked. If the variable is set,
 * the library attempts to load the DLL specified by the environment variable. If that fails,
 * the interface attempts to load the DLL directly (this relies on the environment being
 * setup correctly).
 *
 * Examples of how the sqlany_initialize_interface method is used can be found in the
 * C API examples in the <dfn>sdk\\dbcapi\\examples</dfn> directory of your SQL 
 * Anywhere installation.
 *
 * \param api An API structure to initialize.
 * \param optional_path_to_dll An optional argument that specifies a path to the SQL Anywhere C API DLL.
 * \return 1 on successful initialization, and 0 on failure.
 */
int sqlany_initialize_interface(SQLAnywhereInterface *api, const char *optional_path_to_dll);

/** Unloads the C API DLL library and resets the SQLAnywhereInterface structure.
 *
 * Use the following statement to include the function prototype:
 * 
 * <pre>
 * \#include "sacapidll.h"
 * </pre>
 *
 * Use this method to finalize and free resources associated with the SQL Anywhere C API DLL.
 *
 * Examples of how the sqlany_finalize_interface method is used can be found in the
 * C API examples in the <dfn>sdk\\dbcapi\\examples</dfn> directory of your SQL 
 * Anywhere installation.
 *
 * \param api An initialized structure to finalize.
 */

void sqlany_finalize_interface(SQLAnywhereInterface *api);

#endif
