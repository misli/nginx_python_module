
/*
 * Author: Jakub Dornak (jakub.dornak@misli.cz)
 */


#include "ngx_http_python_module.h"


static ngx_conf_enum_t  ngx_http_python_handlers[] = {
    { ngx_string("xmlrpc"), NGX_HTTP_PYTHON_HANDLER_XMLRPC },
    { ngx_string("soap"),   NGX_HTTP_PYTHON_HANDLER_SOAP },
    { ngx_null_string, 0 }
};


static ngx_command_t  ngx_http_python_commands[] = {

    { ngx_string("python_path"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_http_python_main_conf_t, path),
      NULL },

    { ngx_string("python_handler"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_enum_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_python_loc_conf_t, handler),
      &ngx_http_python_handlers },

    { ngx_string("python_module"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_python_loc_conf_t, module),
      NULL },

    { ngx_string("python_xmlrpc_dispatcher"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_python_loc_conf_t, xmlrpc_dispatcher),
      NULL },

    { ngx_string("python_soap_dispatcher"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_python_loc_conf_t, soap_dispatcher),
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_http_python_module_ctx = {
    NULL,                                  /* preconfiguration */
    ngx_http_python_init,                  /* postconfiguration */

    ngx_http_python_create_main_conf,      /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    ngx_http_python_create_loc_conf,       /* create location configuration */
    ngx_http_python_merge_loc_conf         /* merge location configuration */
};


ngx_module_t  ngx_http_python_module = {
    NGX_MODULE_V1,
    &ngx_http_python_module_ctx,           /* module context */
    ngx_http_python_commands,              /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    ngx_http_python_init_worker,           /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    ngx_http_python_exit_worker,           /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_int_t
ngx_http_python_handler(ngx_http_request_t *r)
{
    ngx_int_t                    rc;
    ngx_http_python_loc_conf_t  *conf;

    conf = ngx_http_get_module_loc_conf(r, ngx_http_python_module);

    if (!conf->handler) {
        return NGX_DECLINED;
    }

    if (conf->pModule == NULL) {
        conf->pModule = PyImport_ImportModule((char *)conf->module.data);
        if (conf->pModule == NULL) { 
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Failed to import module: %s", conf->module.data);
            return NGX_ERROR;
        }
    }

    switch (conf->handler) {
        case NGX_HTTP_PYTHON_HANDLER_XMLRPC:
            if (conf->pXmlrpcDispatcher == NULL) {
                conf->pXmlrpcDispatcher = PyObject_GetAttrString(conf->pModule, (char *)conf->xmlrpc_dispatcher.data);
                if (conf->pXmlrpcDispatcher == NULL) {
                    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Failed to import xmlrpc dispatcher '%s' from module '%s'", conf->xmlrpc_dispatcher.data, conf->module.data);
                    return NGX_ERROR;
                }

                if (!PyObject_HasAttrString(conf->pXmlrpcDispatcher, "_marshaled_dispatch")) {
                    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Object '%s' is not a valid XmlRPC dispatcher", conf->xmlrpc_dispatcher.data);
                    Py_CLEAR(conf->pXmlrpcDispatcher);
                    return NGX_ERROR;
                }
            }

            switch (r->method) {
                case NGX_HTTP_HEAD:
                    rc = ngx_http_discard_request_body(r);
                    if (rc != NGX_OK) {
                        return rc;
                    }
                    r->headers_out.status = NGX_HTTP_OK;
                    return ngx_http_send_header(r);

                case NGX_HTTP_POST:
                    r->request_body_in_single_buf = 1;
                    rc = ngx_http_read_client_request_body(r, ngx_http_python_xmlrpc_handler);
                    if (rc >= NGX_HTTP_SPECIAL_RESPONSE) {
                        return rc;
                    }
                    return NGX_DONE;

                default:
                    return NGX_HTTP_NOT_ALLOWED;
            }

        default:
            return NGX_DECLINED;
    }
}



static void
ngx_http_python_xmlrpc_handler(ngx_http_request_t *r)
{
    ngx_int_t                    rc;
    ngx_buf_t                   *buf;
    ngx_chain_t                  out;
    ngx_str_t                    content_type = ngx_string("text/xml");
    u_char                      *req;
    int                          req_size;
    char                        *content;
    size_t                       content_length, size;
    PyObject                    *pResponse;
    ngx_http_python_loc_conf_t  *conf;

    conf = ngx_http_get_module_loc_conf(r, ngx_http_python_module);

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "ngx_http_python_xmlrpc_handler");

    buf  = r->request_body->bufs->next ? r->request_body->bufs->next->buf : r->request_body->bufs->buf;
    if (buf->in_file) {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "file buffer");
        req_size = buf->file_last - buf->file_pos;
        req      = ngx_palloc(r->pool, req_size + 1);
        if (req == NULL) {
            ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
            return;
        }
        size     = ngx_read_file(buf->file, req, req_size, buf->file_pos);
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "read %d bytes from file", size);
    } else {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "memory buffer");
        req_size = buf->last - buf->pos;
        req      = buf->pos;
    }
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "req_size = %d", req_size);

    pResponse = PyObject_CallMethod(conf->pXmlrpcDispatcher, "_marshaled_dispatch", "(y#)", (char *)req, req_size);
    if (pResponse == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Failed to call dispatcher method");
        ngx_http_finalize_request(r, NGX_HTTP_BAD_REQUEST);
        return;
    }

    content_length = PyBytes_GET_SIZE(pResponse);
    content        = PyBytes_AS_STRING(pResponse);

    out.next = NULL;
    out.buf = ngx_create_temp_buf(r->pool, content_length);
    if (out.buf == NULL) {
        Py_DECREF(pResponse);
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }

    out.buf->last = ngx_cpymem(out.buf->last, content, content_length);
    out.buf->last_buf = 1;

    Py_DECREF(pResponse);

    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_type = content_type;
    r->headers_out.content_length_n = content_length;

    rc = ngx_http_send_header(r);

    if (rc == NGX_OK) {
        rc = ngx_http_output_filter(r, &out);
    }

    ngx_http_finalize_request(r, rc);
}



static void *
ngx_http_python_create_main_conf(ngx_conf_t *cf)
{
    return ngx_pcalloc(cf->pool, sizeof(ngx_http_python_main_conf_t));
}

static void *
ngx_http_python_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_python_loc_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_python_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    /*
     * set by ngx_pcalloc():
     *
     *     conf->module             = { 0, NULL };
     *     conf->xmlrpc_dispatcher  = { 0, NULL };
     *     conf->soap_dispatcher    = { 0, NULL };
     *     conf->pModule            = NULL;
     *     conf->pXmlrpcDispatcher  = NULL;
     *     conf->pSoapDispatcher    = NULL;
     */

    conf->handler = NGX_CONF_UNSET_UINT;

    return (void *)conf;
}


static char *
ngx_http_python_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_python_loc_conf_t  *prev = parent;
    ngx_http_python_loc_conf_t  *conf = child;

    ngx_conf_merge_uint_value(conf->handler,
                              prev->handler, 0);

    ngx_conf_merge_str_value(conf->module,
                             prev->module, "");

    ngx_conf_merge_str_value(conf->xmlrpc_dispatcher,
                             prev->xmlrpc_dispatcher, "");

    ngx_conf_merge_str_value(conf->soap_dispatcher,
                             prev->soap_dispatcher, "");

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_http_python_init(ngx_conf_t *cf)
{
    ngx_http_handler_pt        *h;
    ngx_http_core_main_conf_t  *cmcf;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_CONTENT_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    *h = ngx_http_python_handler;

    return NGX_OK;
}


static ngx_int_t
ngx_http_python_init_worker(ngx_cycle_t *cycle)
{
    ngx_http_python_main_conf_t  *conf;
    ngx_str_t                     assign = ngx_string("import sys\nsys.path = ");
    ngx_str_t                     code;
    u_char                       *last;

    conf = ngx_http_cycle_get_module_main_conf(cycle, ngx_http_python_module);

    ngx_log_error(NGX_LOG_INFO,  &(cycle->new_log), 0, "Initializing Python interpreter");
    Py_Initialize();

    if (conf->path.len) {
        ngx_log_error(NGX_LOG_INFO,  &(cycle->new_log), 0, "Setting up sys.path = %s", conf->path.data);
        code.len  = assign.len + conf->path.len;
        code.data = ngx_alloc(code.len + 1, &(cycle->new_log));
        last      = code.data;
        last      = ngx_cpymem(last, assign.data,     assign.len);
        last      = ngx_cpymem(last, conf->path.data, conf->path.len);
        *last     = '\0';

        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, &(cycle->new_log), 0, "PyRun_SimpleString(\"%s\")", code.data);
        PyRun_SimpleString((char *)code.data);
    }

    return NGX_OK;
}


void
ngx_http_python_exit_worker(ngx_cycle_t *cycle)
{
    Py_Finalize();
}


