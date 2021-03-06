/*
   +----------------------------------------------------------------------+
   | Xdebug                                                               |
   +----------------------------------------------------------------------+
   | Copyright (c) 2002-2014 Derick Rethans                               |
   +----------------------------------------------------------------------+
   | This source file is subject to version 1.0 of the Xdebug license,    |
   | that is bundled with this package in the file LICENSE, and is        |
   | available at through the world-wide-web at                           |
   | http://xdebug.derickrethans.nl/license.php                           |
   | If you did not receive a copy of the Xdebug license and are unable   |
   | to obtain it through the world-wide-web, please send a note to       |
   | xdebug@derickrethans.nl so we can mail you a copy immediately.       |
   +----------------------------------------------------------------------+
   | Authors: Derick Rethans <derick@xdebug.org>                          |
   +----------------------------------------------------------------------+
 */
#include "xdebug_tracing.h"
#include "xdebug_trace_wayback.h"
#include "xdebug_var.h"
#include "ext/standard/php_string.h"

extern ZEND_DECLARE_MODULE_GLOBALS(xdebug);
extern char* xdebug_dbgp_return_source(char *filename, int begin, int end TSRMLS_DC);

typedef struct wb_file_index_entry {
	uint16_t index;
	char    *filename;
} wb_file_index_entry;

typedef struct wb_type_string_ref {
	uint32_t index;
	char    *string;
} wb_type_string_ref;

typedef struct wb_type_argument {
	char flags;
	wb_type_string_ref variable_name;
	zval *value;
} wb_type_argument;

typedef struct wb_header {
	char marker[2];
	int16_t version;
	int32_t timestamp;
	int64_t padding;
} wb_header;

typedef struct wb_function_entry {
	struct s {
		char marker[1];
		char type;
		int16_t pad;
		int32_t timeindex;
		int32_t memory;
		int32_t file_nr;
		int32_t start_lineno;
		int32_t end_lineno;
		int32_t call_lineno;
		int32_t function_nr;
		int16_t nr_of_args;
	} s;
	wb_type_string_ref classname;
	wb_type_string_ref function_name;
	wb_type_argument arguments[]; 
} wb_function_entry;

void wb_add_inline_string(xdebug_str *str, char *string, int32_t length TSRMLS_DC);

static void xdebug_hash_string_entry_dtor(void *e)
{
	wb_type_string_ref *entry = (wb_type_string_ref*) e;

	xdfree(entry->string);
	xdfree(entry);
}

static void xdebug_hash_file_entry_dtor(void *e)
{
	wb_file_index_entry *entry = (wb_file_index_entry*) e;

	xdfree(entry->filename);
	xdfree(entry);
}

void wb_add_padding(xdebug_str *str)
{
	int i, j;

	i = (str->l % 16);

	for (j = 0; j < (16 - i); j++) {
		xdebug_str_addl(str, "X", 1, 0);
	}
}

void wb_indexed_string_helper(void *str, xdebug_hash_element *he)
{
	wb_type_string_ref *string_index;

	string_index = (wb_type_string_ref*) he->ptr;
	xdebug_str_addl(str, (char*) &(string_index->index), sizeof(uint32_t), 0);
	wb_add_inline_string(str, string_index->string, strlen(string_index->string) TSRMLS_CC);
}

void wb_flush_string_table(xdebug_trace_wayback_context *context TSRMLS_DC)
{
	xdebug_str str = {0, 0, NULL};
	uint16_t nr_of_entries;

	xdebug_str_addl(&str, "$", 1, 0);
	nr_of_entries = context->string_table->size;
	xdebug_str_addl(&str, (char*) &nr_of_entries, sizeof nr_of_entries, 0);

	xdebug_hash_apply(context->string_table, (void *) &str, wb_indexed_string_helper);

	wb_add_padding(&str);
	fwrite(str.d, str.l, 1, context->trace_file);
	fflush(context->trace_file);

	xdfree(str.d);
}

void wb_indexed_file_helper(void *str, xdebug_hash_element *he)
{
	wb_file_index_entry *file_index;

	file_index = (wb_file_index_entry*) he->ptr;
	xdebug_str_addl(str, (char*) &(file_index->index), sizeof(uint16_t), 0);
	wb_add_inline_string(str, file_index->filename, strlen(file_index->filename) TSRMLS_CC);
}

void wb_flush_file_table(xdebug_trace_wayback_context *context TSRMLS_DC)
{
	xdebug_str str = {0, 0, NULL};
	uint16_t nr_of_entries;

	xdebug_str_addl(&str, "F", 1, 0);
	nr_of_entries = context->file_table->size;
	xdebug_str_addl(&str, (char*) &nr_of_entries, sizeof nr_of_entries, 0);

	xdebug_hash_apply(context->file_table, (void *) &str, wb_indexed_file_helper);

	wb_add_padding(&str);
	fwrite(str.d, str.l, 1, context->trace_file);
	fflush(context->trace_file);

	xdfree(str.d);
}

void *xdebug_trace_wayback_init(char *fname, long options TSRMLS_DC)
{
	xdebug_trace_wayback_context *tmp_wayback_context;
	char *used_fname;

	tmp_wayback_context = xdmalloc(sizeof(xdebug_trace_wayback_context));
	tmp_wayback_context->trace_file = xdebug_trace_open_file(fname, options, (char**) &used_fname TSRMLS_CC);
	tmp_wayback_context->trace_filename = used_fname;

	tmp_wayback_context->string_table = xdebug_hash_alloc(1024, (xdebug_hash_dtor) xdebug_hash_string_entry_dtor);
	tmp_wayback_context->current_string_page_nr = 0;
	tmp_wayback_context->current_string_string_nr = 0;

	tmp_wayback_context->file_table = xdebug_hash_alloc(256, (xdebug_hash_dtor) xdebug_hash_file_entry_dtor);
	tmp_wayback_context->current_file_nr = 0;

	return tmp_wayback_context;
}

void xdebug_trace_wayback_deinit(void *ctxt TSRMLS_DC)
{
	xdebug_trace_wayback_context *context = (xdebug_trace_wayback_context*) ctxt;

	wb_flush_string_table(context TSRMLS_DC);
	wb_flush_file_table(context TSRMLS_DC);

	fclose(context->trace_file);
	context->trace_file = NULL;
	xdfree(context->trace_filename);

	xdebug_hash_destroy(context->string_table);

	xdebug_hash_destroy(context->file_table);

	xdfree(context);
}

void xdebug_trace_wayback_write_header(void *ctxt TSRMLS_DC)
{
	xdebug_trace_wayback_context *context = (xdebug_trace_wayback_context*) ctxt;
	wb_header header;

	header.marker[0] = 'W';
	header.marker[1] = 'B';
	header.version = 1;
	header.timestamp = time(NULL);
	header.padding = 0x123456789ABCDEF0;

	fwrite(&header, sizeof header, 1, context->trace_file);
	fflush(context->trace_file);
}

void xdebug_trace_wayback_write_footer(void *ctxt TSRMLS_DC)
{
	xdebug_trace_wayback_context *context = (xdebug_trace_wayback_context*) ctxt;
	char   *str_time;
	double  u_time;
	char   *tmp;

	u_time = xdebug_get_utime();
	tmp = xdebug_sprintf("%10.4f ", u_time - XG(start_time));
	// fprintf(context->trace_file, "%s", tmp);
	xdfree(tmp);
#if HAVE_PHP_MEMORY_USAGE
	// fprintf(context->trace_file, "%10zu", XG_MEMORY_USAGE());
#else
	// fprintf(context->trace_file, "%10u", 0);
#endif
	// fprintf(context->trace_file, "\n");
	str_time = xdebug_get_time();
	// fprintf(context->trace_file, "TRACE END   [%s]\n\n", str_time);
	fflush(context->trace_file);
	xdfree(str_time);
}

char *xdebug_trace_wayback_get_filename(void *ctxt TSRMLS_DC)
{
	xdebug_trace_wayback_context *context = (xdebug_trace_wayback_context*) ctxt;

	return context->trace_filename;
}

void wb_write_file(xdebug_trace_wayback_context *context, uint16_t file_nr, char *filename TSRMLS_DC)
{
	char *file_contents;
	xdebug_str str = {0, 0, NULL};
	uint16_t length;

	file_contents = xdebug_dbgp_return_source(filename, 0, 99999999 TSRMLS_CC);
	length = strlen(file_contents);

	xdebug_str_addl(&str, "C", 1, 0);
	xdebug_str_addl(&str, (char*) &file_nr, sizeof file_nr, 0);
	wb_add_inline_string(&str, file_contents, length TSRMLS_CC);

	wb_add_padding(&str);
	fwrite(str.d, str.l, 1, context->trace_file);
	fflush(context->trace_file);

	xdfree(str.d);
}

uint16_t wb_get_file_nr(xdebug_trace_wayback_context *context, function_stack_entry *fse TSRMLS_DC)
{
	wb_file_index_entry *entry;

	if (xdebug_hash_find(context->file_table, fse->op_array->filename, strlen(fse->op_array->filename), (void *) &entry)) {
		return entry->index;
	} else {
		wb_file_index_entry *new_entry = xdmalloc(sizeof(wb_file_index_entry));

		new_entry->index = context->current_file_nr++;
		new_entry->filename = xdstrdup(fse->op_array->filename);

		xdebug_hash_add(context->file_table, fse->op_array->filename, strlen(fse->op_array->filename), (void*) new_entry);

		wb_write_file(context, new_entry->index, fse->op_array->filename TSRMLS_CC);

		return new_entry->index;
	}
}

void wb_create_string_ref(wb_type_string_ref **ref, xdebug_trace_wayback_context *context, char *string, int length TSRMLS_DC)
{
	if (xdebug_hash_find(context->string_table, string, length, (void*) ref)) {
		return;
	} else {
		*ref = xdmalloc(sizeof(wb_type_string_ref));

		(*ref)->index = context->current_string_string_nr;
		(*ref)->string = xdstrdup(string);

		xdebug_hash_add(context->string_table, string, length, (void*) *ref);
		return;
	}
}

void wb_add_inline_string(xdebug_str *str, char *string, int32_t length TSRMLS_DC)
{
	char type = 1;

	xdebug_str_addl(str, (char*) &type, sizeof type, 0);
	xdebug_str_addl(str, (char*) &length, sizeof length, 0);
	xdebug_str_addl(str, string, length, 0);
	xdebug_str_addl(str, "\0", 1, 0);
}

void wb_add_string_ref(xdebug_str *str, xdebug_trace_wayback_context *context, char *string TSRMLS_DC)
{

	if (string) {
		char type;
		int32_t length;

		length = strlen(string);

		if (length < 16) {
			wb_add_inline_string(str, string, length);
		} else {
			wb_type_string_ref *ref;

			type = 2;
			wb_create_string_ref(&ref, context, string, length TSRMLS_CC);
			xdebug_str_addl(str, (char*) &type, sizeof type, 0);
			xdebug_str_addl(str, (char*) &ref, sizeof ref, 0);
		}
	} else {
		int32_t type = 2;
		xdebug_str_addl(str, (char*) &type, sizeof type, 0);
	}
}

void xdebug_trace_wayback_function_entry(void *ctxt, function_stack_entry *fse, int function_nr TSRMLS_DC)
{
	xdebug_trace_wayback_context *context = (xdebug_trace_wayback_context*) ctxt;
	unsigned int j = 0; /* Counter */
	char *tmp_name;
	xdebug_str str = {0, 0, NULL};

	wb_function_entry function_entry;

	function_entry.s.marker[0] = 'I';
	function_entry.s.type = fse->function.type;
	function_entry.s.timeindex = (int32_t) (((double) fse->time - (double) XG(start_time)) * 1000);
	function_entry.s.memory = fse->memory;
	function_entry.s.file_nr = wb_get_file_nr(context, fse TSRMLS_CC);
	function_entry.s.start_lineno = fse->function.start_lineno;
	function_entry.s.end_lineno = fse->function.end_lineno;
	function_entry.s.call_lineno = fse->lineno;
	function_entry.s.function_nr = function_nr;
	function_entry.s.nr_of_args = XG(collect_params) > 0 ? fse->varc : 0;

	xdebug_str_addl(&str, (char*) &(function_entry.s), sizeof(function_entry.s), 0);
	wb_add_string_ref(&str, context, fse->function.class TSRMLS_CC);
	wb_add_string_ref(&str, context, fse->function.function TSRMLS_CC);

	tmp_name = xdebug_show_fname(fse->function, 0, 0 TSRMLS_CC);

	xdfree(tmp_name);
#if 0
	/* Printing vars */
	if (XG(collect_params) > 0) {
		int variadic_opened = 0;
		int variadic_count  = 0;

		for (j = 0; j < fse->varc; j++) {
			char *tmp_value;

			if (c) {
				xdebug_str_addl(&str, ", ", 2, 0);
			} else {
				c = 1;
			}

			if (fse->var[j].is_variadic && fse->var[j].addr) {
				xdebug_str_add(&str, "...", 0);
				variadic_opened = 1;
			}

			if (fse->var[j].name && XG(collect_params) == 4) {
				xdebug_str_add(&str, xdebug_sprintf("$%s = ", fse->var[j].name), 1);
			}

			if (fse->var[j].is_variadic && fse->var[j].addr) {
				xdebug_str_add(&str, "variadic(", 0);
			}

			if (variadic_opened && XG(collect_params) != 5) {
				xdebug_str_add(&str, xdebug_sprintf("%d => ", variadic_count++), 1);
			}

			switch (XG(collect_params)) {
				case 1: /* synopsis */
				case 2:
					tmp_value = xdebug_get_zval_synopsis(fse->var[j].addr, 0, NULL);
					break;
				case 3: /* full */
				case 4: /* full (with var) */
				default:
					tmp_value = xdebug_get_zval_value(fse->var[j].addr, 0, NULL);
					break;
				case 5: /* serialized */
					tmp_value = xdebug_get_zval_value_serialized(fse->var[j].addr, 0, NULL TSRMLS_CC);
					break;
			}
			if (tmp_value) {
				xdebug_str_add(&str, tmp_value, 1);
			} else {
				xdebug_str_add(&str, "???", 0);
			}
		}

		if (variadic_opened) {
			xdebug_str_add(&str, ")", 0);
		}
	}

	if (fse->include_filename) {
		if (fse->function.type == XFUNC_EVAL) {
			int tmp_len;

			char *escaped;
			escaped = php_addcslashes(fse->include_filename, strlen(fse->include_filename), &tmp_len, 0, "'\\\0..\37", 6 TSRMLS_CC);
			xdebug_str_add(&str, xdebug_sprintf("'%s'", escaped), 1);
			efree(escaped);
		} else {
			xdebug_str_add(&str, fse->include_filename, 0);
		}
	}
#endif
	/* adding padding */
	wb_add_padding(&str);
	fwrite(str.d, str.l, 1, context->trace_file);
	fflush(context->trace_file);

	xdfree(str.d);
}

/* Used for normal return values, and generator return values */
static void xdebug_return_trace_stack_common(xdebug_str *str, function_stack_entry *fse TSRMLS_DC)
{
	unsigned int j = 0; /* Counter */

	xdebug_str_add(str, xdebug_sprintf("%10.4f ", xdebug_get_utime() - XG(start_time)), 1);
#if HAVE_PHP_MEMORY_USAGE
	xdebug_str_add(str, xdebug_sprintf("%10lu ", XG_MEMORY_USAGE()), 1);
#endif

	if (XG(show_mem_delta)) {
		xdebug_str_addl(str, "        ", 8, 0);
	}
	for (j = 0; j < fse->level; j++) {
		xdebug_str_addl(str, "  ", 2, 0);
	}
	xdebug_str_addl(str, " >=> ", 5, 0);
}


void xdebug_trace_wayback_function_return_value(void *ctxt, function_stack_entry *fse, int function_nr, zval *return_value TSRMLS_DC)
{
	xdebug_trace_wayback_context *context = (xdebug_trace_wayback_context*) ctxt;
	xdebug_str str = {0, 0, NULL};
	char      *tmp_value;

	xdebug_return_trace_stack_common(&str, fse TSRMLS_CC);

	tmp_value = xdebug_get_zval_value(return_value, 0, NULL);
	if (tmp_value) {
		xdebug_str_add(&str, tmp_value, 1);
	}
	xdebug_str_addl(&str, "\n", 2, 0);

	// fprintf(context->trace_file, "%s", str.d);
	fflush(context->trace_file);

	xdfree(str.d);
}

#if PHP_VERSION_ID >= 50500
void xdebug_trace_wayback_generator_return_value(void *ctxt, function_stack_entry *fse, int function_nr, zend_generator *generator TSRMLS_DC)
{
	xdebug_trace_wayback_context *context = (xdebug_trace_wayback_context*) ctxt;
	xdebug_str str = {0, 0, NULL};
	char      *tmp_value = NULL;

	/* Generator key */
	tmp_value = xdebug_get_zval_value(generator->key, 0, NULL);
	if (tmp_value) {
		xdebug_return_trace_stack_common(&str, fse TSRMLS_CC);

		xdebug_str_addl(&str, "(", 1, 0);
		xdebug_str_add(&str, tmp_value, 1);
		xdebug_str_addl(&str, " => ", 4, 0);

		tmp_value = xdebug_get_zval_value(generator->value, 0, NULL);
		if (tmp_value) {
			xdebug_str_add(&str, tmp_value, 1);
		}
		xdebug_str_addl(&str, ")", 1, 0);
		xdebug_str_addl(&str, "\n", 2, 0);

		// fprintf(context->trace_file, "%s", str.d);
		fflush(context->trace_file);

		xdfree(str.d);
	}
}
#endif

void xdebug_trace_wayback_assignment(void *ctxt, function_stack_entry *fse, char *full_varname, zval *retval, char *op, char *filename, int lineno TSRMLS_DC)
{
	xdebug_trace_wayback_context *context = (xdebug_trace_wayback_context*) ctxt;
	unsigned int j = 0;
	xdebug_str str = {0, 0, NULL};
	char      *tmp_value;

	xdebug_str_addl(&str, "                    ", 20, 0);
	if (XG(show_mem_delta)) {
		xdebug_str_addl(&str, "        ", 8, 0);
	}
	for (j = 0; j <= fse->level; j++) {
		xdebug_str_addl(&str, "  ", 2, 0);
	}
	xdebug_str_addl(&str, "   => ", 6, 0);

	xdebug_str_add(&str, full_varname, 0);

	if (op[0] != '\0' ) { /* pre/post inc/dec ops are special */
		xdebug_str_add(&str, xdebug_sprintf(" %s ", op), 1);

		tmp_value = xdebug_get_zval_value(retval, 0, NULL);

		if (tmp_value) {
			xdebug_str_add(&str, tmp_value, 1);
		} else {
			xdebug_str_addl(&str, "NULL", 4, 0);
		}
	}
	xdebug_str_add(&str, xdebug_sprintf(" %s:%d\n", filename, lineno), 1);

	// fprintf(context->trace_file, "%s", str.d);
	fflush(context->trace_file);

	xdfree(str.d);
}

xdebug_trace_handler_t xdebug_trace_handler_wayback =
{
	xdebug_trace_wayback_init,
	xdebug_trace_wayback_deinit,
	xdebug_trace_wayback_write_header,
	xdebug_trace_wayback_write_footer,
	xdebug_trace_wayback_get_filename,
	xdebug_trace_wayback_function_entry,
	NULL /*xdebug_trace_wayback_function_exit */,
	xdebug_trace_wayback_function_return_value,
#if PHP_VERSION_ID >= 50500
	xdebug_trace_wayback_generator_return_value,
#endif
	xdebug_trace_wayback_assignment
};
