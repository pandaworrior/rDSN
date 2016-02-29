# pragma once
# include "daemon.server.h"

namespace dsn 
{
    namespace dist 
    {
        // server app example
        class daemon_server_app :
            public ::dsn::service_app
        {
        public:
            daemon_server_app() {}

            virtual ::dsn::error_code start(int argc, char** argv)
            {
                _daemon_s_svc.open_service();
                return ::dsn::ERR_OK;
            }

            virtual void stop(bool cleanup = false)
            {
                _daemon_s_svc.close_service();
            }

        private:
            daemon_s_service _daemon_s_svc;
        };
    }
}