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
            std::unique_ptr<slave_failure_detector_with_multimaster> _fd;
            
            struct layer1_app_info
            {
                ::dsn::replication::partition_configuration configuration;
                dsn_handle_t app_proc_handle;
                std::string working_dir;
                std::string package_dir;
                uint16_t working_port;

                layer1_app_info(const ::dsn::replication::configuration_update_request & proposal)
                {
                    configuration = proposal.config;
                    app_proc_handle = nullptr;
                }
            };

            ::dsn::service::zrwlock_nr _lock;
            std::unordered_map<::dsn::replication::global_partition_id, std::unique_ptr<layer1_app_info>> _apps;
            std::atomic<bool> _online;

            std::string _working_dir;
            std::string _package_dir_on_daemon;
            rpc_address _package_server;
            std::string _package_dir_on_package_server;

            task_ptr _app_check_timer;

        private:
            void on_master_connected();
            void on_master_disconnected();

            void on_add_app(const ::dsn::replication::configuration_update_request& proposal);
            void on_remove_app(const ::dsn::replication::configuration_update_request& proposal);

            error_code create_app(layer1_app_info* app);
            error_code kill_app(layer1_app_info* app);

            void check_apps();
        };
    }
}