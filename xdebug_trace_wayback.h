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
#ifndef XDEBUG_TRACE_WAYBACK_H
#define XDEBUG_TRACE_WAYBACK_H

#include "xdebug_tracing.h"

#include <inttypes.h>

typedef struct _xdebug_trace_wayback_context
{
	FILE *trace_file;
	char *trace_filename;

	xdebug_hash *string_table;
	uint16_t current_string_page_nr;
	uint16_t current_string_string_nr;
} xdebug_trace_wayback_context;

extern xdebug_trace_handler_t xdebug_trace_handler_wayback;
#endif
