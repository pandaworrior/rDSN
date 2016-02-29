# pragma once

# include "daemon.server.h"

using namespace ::dsn::replication;

namespace dsn
{
    namespace dist
    {
        daemon_s_service::daemon_s_service() 
            : ::dsn::serverlet<daemon_s_service>("daemon_s")
        {
        }

        daemon_s_service::~daemon_s_service()
        {
        }

        void daemon_s_service::on_config_proposal(const ::dsn::replication::configuration_update_request& proposal)
        {
            dassert(proposal.is_stateful == false, 
                "stateful replication not supported by daemon, please using a different layer2 handler");

            switch (proposal.type)
            {
            case CT_ADD_SECONDARY:
            case CT_ADD_SECONDARY_FOR_LB:
                on_add_app(proposal);
                break;

            case CT_REMOVE:          
                on_remove_app(proposal);
                break;

            default:
                dwarn("not supported configuration type %s received", enum_to_string(proposal.type));
                break;
            }
        }

        static std::vector<rpc_address> read_meta_servers()
        {
            std::vector<rpc_address> meta_servers;

            const char* server_ss[10];
            int capacity = 10, need_count;
            need_count = dsn_config_get_all_keys("cluster.meta_servers", server_ss, &capacity);
            dassert(need_count <= capacity, "too many meta servers specified");

            std::ostringstream oss;
            for (int i = 0; i < capacity; i++)
            {
                std::string s(server_ss[i]);
                // name:port
                auto pos1 = s.find_first_of(':');
                if (pos1 != std::string::npos)
                {
                    ::dsn::rpc_address ep(s.substr(0, pos1).c_str(), atoi(s.substr(pos1 + 1).c_str()));
                    meta_servers.push_back(ep);
                    oss << "[" << ep.to_string() << "] ";
                }
            }
            ddebug("read meta servers from config: %s", oss.str().c_str());

            return std::move(meta_servers);
        }

        void daemon_s_service::open_service()
        {
            auto meta_servers = std::move(read_meta_servers());
            _fd.reset(new slave_failure_detector_with_multimaster(meta_servers, 
                [=]() { this->on_master_disconnected(); },
                [=]() { this->on_master_connected(); }
                ));

            this->register_rpc_handler(RPC_CONFIG_PROPOSAL, "config_proposal", &daemon_s_service::on_config_proposal);

            auto err = _fd->start(
                5,
                3,
                20,
                25
                );

            dassert(ERR_OK == err, "failure detector start failed, err = %s", err.to_string());

        }

        void daemon_s_service::close_service()
        {
            _fd->stop();
            this->unregister_rpc_handler(RPC_CONFIG_PROPOSAL);
        }

        void daemon_s_service::on_master_connected()
        {
        }
        
        void daemon_s_service::on_master_disconnected()
        {
        }

        void daemon_s_service::on_add_app(const::dsn::replication::configuration_update_request & proposal)
        {

        }

        void daemon_s_service::on_remove_app(const::dsn::replication::configuration_update_request & proposal)
        {

        }
    }
}