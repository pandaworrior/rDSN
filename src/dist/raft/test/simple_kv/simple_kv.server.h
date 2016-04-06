/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2015 Microsoft Corporation
 * 
 * -=- Robust Distributed System Nucleus (rDSN) -=- 
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
# pragma once

# include "simple_kv.code.definition.h"
# include <iostream>

namespace dsn { namespace replication { namespace test { 
class simple_kv_service 
    : public serverlet<simple_kv_service>
{
public:
    simple_kv_service()
        : ::dsn::serverlet<simple_kv_service>("simple_kv")
    {
    }

    virtual ~simple_kv_service()
    {
    }

protected:
    // all service handlers to be implemented further
    // RPC_SIMPLE_KV_SIMPLE_KV_READ 
    virtual void on_read(const std::string& key, ::dsn::rpc_replier<std::string>& reply)
    {
        std::cout << "... exec RPC_SIMPLE_KV_SIMPLE_KV_READ ... (not implemented) " << std::endl;
        std::string resp;
        reply(resp);
    }
    // RPC_SIMPLE_KV_SIMPLE_KV_WRITE 
    virtual void on_write(const kv_pair& pr, ::dsn::rpc_replier<int32_t>& reply)
    {
        std::cout << "... exec RPC_SIMPLE_KV_SIMPLE_KV_WRITE ... (not implemented) " << std::endl;
        int32_t resp;
        reply(resp);
    }
    // RPC_SIMPLE_KV_SIMPLE_KV_APPEND 
    virtual void on_append(const kv_pair& pr, ::dsn::rpc_replier<int32_t>& reply)
    {
        std::cout << "... exec RPC_SIMPLE_KV_SIMPLE_KV_APPEND ... (not implemented) " << std::endl;
        int32_t resp;
        reply(resp);
    }
    
public:
    void open_service(dsn_gpid gpid)
    {
        this->register_async_rpc_handler(RPC_SIMPLE_KV_SIMPLE_KV_READ, "read", &simple_kv_service::on_read, gpid);
        this->register_async_rpc_handler(RPC_SIMPLE_KV_SIMPLE_KV_WRITE, "write", &simple_kv_service::on_write, gpid);
        this->register_async_rpc_handler(RPC_SIMPLE_KV_SIMPLE_KV_APPEND, "append", &simple_kv_service::on_append, gpid);
    }

    void close_service(dsn_gpid gpid)
    {
        this->unregister_rpc_handler(RPC_SIMPLE_KV_SIMPLE_KV_READ, gpid);
        this->unregister_rpc_handler(RPC_SIMPLE_KV_SIMPLE_KV_WRITE, gpid);
        this->unregister_rpc_handler(RPC_SIMPLE_KV_SIMPLE_KV_APPEND, gpid);
    }
};

} } } 