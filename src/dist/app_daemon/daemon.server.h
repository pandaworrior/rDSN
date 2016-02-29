# pragma once

# include <dsn/dist/replication.h>
# include <dsn/service_api_cpp.h>
# include <dsn/dist/failure_detector_multimaster.h>

namespace dsn
{
    namespace dist
    {
        class daemon_s_service
            : public ::dsn::serverlet<daemon_s_service>
        {
        public:
            daemon_s_service();
            ~daemon_s_service();

            void open_service();
            void close_service();

            void on_config_proposal(const ::dsn::replication::configuration_update_request& proposal);
            
        private:
            void on_master_connected();
            void on_master_disconnected();

            void on_add_app(const::dsn::replication::configuration_update_request& proposal);
            void on_remove_app(const::dsn::replication::configuration_update_request& proposal);

        private:
            std::unique_ptr<slave_failure_detector_with_multimaster> _fd;
            
            struct layer1_app_info
            {
                ::dsn::replication::partition_configuration configuration;
                dsn_handle_t app_proc_handle;
                std::string working_dir;
                uint16_t working_port;
            };

            ::dsn::service::zrwlock_nr _lock;
            std::unordered_map<uint64_t, std::unique_ptr<layer1_app_info>> _apps;

            std::string _working_dir;
        };
    }
}