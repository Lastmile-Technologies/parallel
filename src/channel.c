/*
  +----------------------------------------------------------------------+
  | parallel                                                              |
  +----------------------------------------------------------------------+
  | Copyright (c) Joe Watkins 2019                                       |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: krakjoe                                                      |
  +----------------------------------------------------------------------+
 */
#ifndef HAVE_PARALLEL_CHANNEL
#define HAVE_PARALLEL_CHANNEL

#include "parallel.h"

typedef struct _php_parallel_channels_t {
	php_parallel_monitor_t *monitor;
	HashTable links;
} php_parallel_channels_t;

php_parallel_channels_t php_parallel_channels;

zend_class_entry *php_parallel_channel_ce;
zend_object_handlers php_parallel_channel_handlers;

static zend_always_inline void php_parallel_channels_make(zval *return_value, zend_string *name, zend_bool buffered, zend_long capacity) {
    php_parallel_channel_t *channel;
    
    object_init_ex(return_value, php_parallel_channel_ce);
    
    channel = php_parallel_channel_from(return_value);
    channel->link = php_parallel_link_init(name, buffered, capacity);

    zend_hash_add_ptr(
		&php_parallel_channels.links,
		php_parallel_link_name(channel->link), 
		php_parallel_link_copy(channel->link));
}

static zend_always_inline void php_parallel_channels_open(zval *return_value, php_parallel_link_t *link) {
    php_parallel_channel_t *channel;
    
    object_init_ex(return_value, php_parallel_channel_ce);

	channel = php_parallel_channel_from(return_value);
    channel->link = php_parallel_link_copy(link);
}

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(php_parallel_channel_make_arginfo, 0, 1, \\parallel\\Channel, 0)
    ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, capacity, IS_LONG, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(Channel, make)
{
	zend_string *name = NULL;
	zend_bool    buffered = 0;
    zend_long    capacity = -1;
    
    if (ZEND_NUM_ARGS() == 1) {
        ZEND_PARSE_PARAMETERS_START(1, 1)
	        Z_PARAM_STR(name)
        ZEND_PARSE_PARAMETERS_END();
    } else {
        ZEND_PARSE_PARAMETERS_START(2, 2)
	        Z_PARAM_STR(name)
	        Z_PARAM_LONG(capacity)
        ZEND_PARSE_PARAMETERS_END();
        
        if (capacity < -1 || capacity == 0) {
            php_parallel_invalid_arguments(
                "capacity may be -1 for unlimited, or a positive integer");
            return;
        }
        
        buffered = 1;
    }

	php_parallel_monitor_lock(php_parallel_channels.monitor);

	if (zend_hash_exists(&php_parallel_channels.links, name)) {
		php_parallel_exception_ex(
		    php_parallel_channel_error_existence_ce,
			"channel named %s already exists", 
			ZSTR_VAL(name));
	} else {
		php_parallel_channels_make(return_value, name, buffered, capacity);
	}
	
	php_parallel_monitor_unlock(php_parallel_channels.monitor);
}

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(php_parallel_channel_open_arginfo, 0, 1, \\parallel\\Channel, 0)
    ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(Channel, open)
{
	zend_string *name = NULL;
	php_parallel_link_t *link;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STR(name)
    ZEND_PARSE_PARAMETERS_END();

	php_parallel_monitor_lock(php_parallel_channels.monitor);

	if (!(link = zend_hash_find_ptr(&php_parallel_channels.links, name))) {
		php_parallel_exception_ex(
		    php_parallel_channel_error_existence_ce,
		    "channel named %s not found",
		    ZSTR_VAL(name));
	} else {
		php_parallel_channels_open(return_value, link);
	}

	php_parallel_monitor_unlock(php_parallel_channels.monitor);
}

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(php_parallel_channel_send_arginfo, 0, 1, IS_VOID, 0)
    ZEND_ARG_INFO(0, value)
ZEND_END_ARG_INFO()

PHP_METHOD(Channel, send)
{
    php_parallel_channel_t *channel = php_parallel_channel_from(getThis());
    zval *value;
    
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_ZVAL(value)
    ZEND_PARSE_PARAMETERS_END();
    
    if (Z_TYPE_P(value) == IS_OBJECT || Z_TYPE_P(value) == IS_NULL) {
        php_parallel_exception_ex(
            php_parallel_channel_error_illegal_value_ce,
            "value of type %s is illegal",
            zend_get_type_by_const(Z_TYPE_P(value)));
        return;
    }
    
    if (php_parallel_link_closed(channel->link) ||
        !php_parallel_link_send(channel->link, value)) {
        php_parallel_exception_ex(
            php_parallel_channel_error_closed_ce,
            "channel(%s) closed", 
            ZSTR_VAL(php_parallel_link_name(channel->link)));
        return;
    }
}

ZEND_BEGIN_ARG_INFO_EX(php_parallel_channel_recv_arginfo, 0, 0, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(Channel, recv)
{
    php_parallel_channel_t *channel = php_parallel_channel_from(getThis());
    
    PARALLEL_PARAMETERS_NONE(return);
    
    if (php_parallel_link_closed(channel->link) ||
        !php_parallel_link_recv(channel->link, return_value)) {
        php_parallel_exception_ex(
            php_parallel_channel_error_closed_ce,
            "channel(%s) closed", 
            ZSTR_VAL(php_parallel_link_name(channel->link)));
        return;
    }
}

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(php_parallel_channel_close_arginfo, 0, 0, IS_VOID, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(Channel, close)
{
    php_parallel_channel_t *channel = php_parallel_channel_from(getThis());

    PARALLEL_PARAMETERS_NONE(return);

	if (!php_parallel_link_close(channel->link)) {
        php_parallel_exception_ex(
            php_parallel_channel_error_closed_ce,
            "channel(%s) already closed", 
            ZSTR_VAL(php_parallel_link_name(channel->link)));		
    }
    
    php_parallel_monitor_lock(php_parallel_channels.monitor);
    zend_hash_del(
        &php_parallel_channels.links, 
            php_parallel_link_name(channel->link));
	php_parallel_monitor_unlock(php_parallel_channels.monitor);
}

PHP_METHOD(Channel, __toString)
{
	php_parallel_channel_t *channel = 
		php_parallel_channel_from(getThis());

	RETURN_STR_COPY(php_parallel_link_name(channel->link));
}

zend_function_entry php_parallel_channel_methods[] = {
	PHP_ME(Channel, make, php_parallel_channel_make_arginfo, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
	PHP_ME(Channel, open, php_parallel_channel_open_arginfo, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
	PHP_ME(Channel, send, php_parallel_channel_send_arginfo, ZEND_ACC_PUBLIC)
	PHP_ME(Channel, recv, php_parallel_channel_recv_arginfo, ZEND_ACC_PUBLIC)
	PHP_ME(Channel, close, php_parallel_channel_close_arginfo, ZEND_ACC_PUBLIC)
	PHP_ME(Channel, __toString, NULL, ZEND_ACC_PUBLIC)
	PHP_FE_END
};

zend_object* php_parallel_channel_create(zend_class_entry *type) {
	php_parallel_channel_t *channel = ecalloc(1, 
			sizeof(php_parallel_channel_t) + zend_object_properties_size(type));

	zend_object_std_init(&channel->std, type);

	channel->std.handlers = &php_parallel_channel_handlers;
    
	return &channel->std;
}

void php_parallel_channel_destroy(zend_object *o) {
	php_parallel_channel_t *channel = 
		php_parallel_channel_fetch(o);
	
    php_parallel_link_destroy(channel->link);
	
	zend_object_std_dtor(o);
}

void php_parallel_channels_link_destroy(zval *zv) {
    php_parallel_link_t *link = Z_PTR_P(zv);
    
    php_parallel_link_destroy(link);
}

void php_parallel_channel_startup() {
	zend_class_entry ce;

	memcpy(
	    &php_parallel_channel_handlers, 
	    php_parallel_standard_handlers(), 
	    sizeof(zend_object_handlers));

	php_parallel_channel_handlers.offset = XtOffsetOf(php_parallel_channel_t, std);
	php_parallel_channel_handlers.free_obj = php_parallel_channel_destroy;

	INIT_NS_CLASS_ENTRY(ce, "parallel", "Channel", php_parallel_channel_methods);

	php_parallel_channel_ce = zend_register_internal_class(&ce);
	php_parallel_channel_ce->create_object = php_parallel_channel_create;
	php_parallel_channel_ce->ce_flags |= ZEND_ACC_FINAL;
	
	zend_declare_class_constant_long(php_parallel_channel_ce, ZEND_STRL("Infinite"), -1);

	php_parallel_channels.monitor = php_parallel_monitor_create();

	zend_hash_init(
		&php_parallel_channels.links, 
		32,
		NULL,
		php_parallel_channels_link_destroy, 1);
}

void php_parallel_channel_shutdown() {
	php_parallel_monitor_destroy(
		php_parallel_channels.monitor);
	zend_hash_destroy(&php_parallel_channels.links);	
}

#endif
