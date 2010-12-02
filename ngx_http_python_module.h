
/*
 * Author: Jakub Dornak (jakub.dornak@misli.cz)
 */

#ifndef _NGX_HTTP_PYTHON_MODULE_H_
#define _NGX_HTTP_PYTHON_MODULE_H_

#include <xmlrpc.h>
#include <python3.1/Python.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

typedef struct {
    ngx_str_t   path;
} ngx_http_python_main_conf_t;

typedef struct {
    ngx_uint_t  handler;
    ngx_str_t   module;
    ngx_str_t   xmlrpc_dispatcher;
    ngx_str_t   soap_dispatcher;
    PyObject   *pModule;
    PyObject   *pXmlrpcDispatcher;
    PyObject   *pSoapDispatcher;
} ngx_http_python_loc_conf_t;

#define NGX_HTTP_PYTHON_HANDLER_XMLRPC          1
#define NGX_HTTP_PYTHON_HANDLER_SOAP            2

static void *ngx_http_python_create_main_conf(ngx_conf_t *cf);
static void *ngx_http_python_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_python_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child);

static ngx_int_t ngx_http_python_init(ngx_conf_t *cf);
static ngx_int_t ngx_http_python_init_worker(ngx_cycle_t *cycle);
void             ngx_http_python_exit_worker(ngx_cycle_t *cycle);

static void      ngx_http_python_xmlrpc_handler(ngx_http_request_t *r);

#endif
