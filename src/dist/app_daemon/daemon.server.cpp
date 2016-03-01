# pragma once

# include "daemon.server.h"
# include <dsn/cpp/utils.h>

using namespace ::dsn::replication;

namespace dsn
{
    namespace dist
    {
        daemon_s_service::daemon_s_service() 
            : ::dsn::serverlet<daemon_s_service>("daemon_s"), _online(false)
        {
            _working_dir = utils::filesystem::path_combine(dsn_get_current_app_data_dir(), "apps");
            _package_dir_on_daemon = utils::filesystem::path_combine(dsn_get_current_app_data_dir(), "packages");
            _package_server = rpc_address(
                dsn_config_get_value_string("apps.daemon", "package_server_host", "", "the host name of the app store where to download package"),
                (uint16_t)dsn_config_get_value_uint64("apps.daemon", "package_server_port", 26788, "the port of the app store where to download package")
                );
            _package_dir_on_package_server = dsn_config_get_value_string("apps.daemon", "package_dir", "", "the dir on the app store where to download package");

            if (!utils::filesystem::directory_exists(_package_dir_on_daemon))
            {
                utils::filesystem::create_directory(_package_dir_on_daemon);
            }
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


        DEFINE_TASK_CODE(LPC_DAEMON_APPS_CHECK_TIMER, TASK_PRIORITY_COMMON, THREAD_POOL_DEFAULT)

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

            _app_check_timer = tasking::enqueue_timer(
                LPC_DAEMON_APPS_CHECK_TIMER,
                this,
                [this]{ this->check_apps(); },
                std::chrono::milliseconds(30)
                );
        }

        void daemon_s_service::close_service()
        {
            _app_check_timer->cancel(true);
            _fd->stop();
            this->unregister_rpc_handler(RPC_CONFIG_PROPOSAL);
        }

        void daemon_s_service::on_master_connected()
        {
            _online = true;
            dinfo("master is connected");
        }
        
        void daemon_s_service::on_master_disconnected()
        {       
            _online = false;
            {
                ::dsn::service::zauto_read_lock l(_lock);
                for (auto& app : _apps)
                {
                    kill_app(app.second.get());
                }

                _apps.clear();
            }

            dinfo("master is disconnected");
        }

        DEFINE_TASK_CODE_AIO(LPC_DAEMON_DOWNLOAD_PACKAGE, TASK_PRIORITY_COMMON, THREAD_POOL_DEFAULT)

        void daemon_s_service::on_add_app(const ::dsn::replication::configuration_update_request & proposal)
        {
            // check app exists or not
            {
                ::dsn::service::zauto_read_lock l(_lock);
                auto it = _apps.find(proposal.config.gpid);

                // app is running with the same package
                if (it != _apps.end())
                {
                    // proposal's package is older or the same
                    if (proposal.config.package_id <= it->second->configuration.package_id)
                        return;
                    else
                    {
                        kill_app(it->second.get());
                    }
                }   
            }
            
            // TODO: add confliction with the same package
            
            // check and start
            auto app = new layer1_app_info(proposal);
            app->package_dir = utils::filesystem::path_combine(_package_dir_on_daemon, proposal.config.package_id);
            if (utils::filesystem::directory_exists(app->package_dir))
            {                
                create_app(app);
            }

            // download package first if necesary
            else
            {
                utils::filesystem::create_directory(app->package_dir);

                // TODO: better way to download package from app store 
                std::vector<std::string> files{ app->configuration.package_id + ".7z" };

                dinfo("start downloading package %s from %s to %s",
                    app->configuration.package_id.c_str(),
                    _package_server.to_string(),
                    app->package_dir.c_str()
                    );

                file::copy_remote_files(
                    _package_server,
                    _package_dir_on_package_server,
                    files,
                    app->package_dir,
                    true,
                    LPC_DAEMON_DOWNLOAD_PACKAGE,
                    this,
                    [=](error_code err, size_t sz)
                    {
                        std::string command = "7z x " + app->package_dir + '/' + app->configuration.package_id + ".7z -y -o" + app->package_dir;
                        // decompress when completed
                        system(command.c_str());
                        create_app(app);
                    }
                    );
            }
        }

        void daemon_s_service::on_remove_app(const::dsn::replication::configuration_update_request & proposal)
        {


            var req = call.RequestObject;
            PerRoleServices svcs = null;
            _services.TryGetValue(req.ServiceDetail.Name, out svcs);

            if (svcs != null)
            {
                PerRoleService rs = null;
                if (svcs.Services.TryGetValue(req.Partition.Index, out rs))
                {
                    if (req.Partition.ConfigurationVersion >= rs.Partition.ConfigurationVersion)
                    {
                        var r = RemoveServiceOnMetaServer(rs);
                        if (ErrorCode.Success == r || ErrorCode.AppServiceNotFound == r)
                            KillService(svcs, rs);
                        else
                        {
                            Trace.WriteLine("RemoveServiceOnMetaServer failed, code = " + r.ToString());
                        }
                    }
                    else
                    {
                        Trace.WriteLine("Local service version is higher " +
                            rs.Partition.ConfigurationVersion
                            + " vs " +
                            req.Partition.ConfigurationVersion
                            + ", reject service remove for name = '" + req.ServiceDetail.Name + "' when remove it");
                    }
                }
                else
                {
                    Trace.WriteLine("Cannot find services with name = '" + req.ServiceDetail.Name + "', index = " + req.Partition.Index + ", when remove it");
                }
            }

            else
            {
                Trace.WriteLine("Cannot find services with name = '" + req.ServiceDetail.Name + "' when remove it");
            }
        }

        error_code daemon_s_service::create_app(layer1_app_info * app)
        {
            /*
                dsn_handle_t app_proc_handle;
                std::string working_dir;
                std::string package_dir;
                uint16_t working_port;
            */
            
            while (true)
            {
                uint64_t id = dsn_random64(0, 100000000000ULL);
                std::stringstream ss;
                ss << id;
                app->working_dir = utils::filesystem::path_combine(_working_dir, ss.str());

                if (!utils::filesystem::directory_exists(app->working_dir))
                    break;
            }
            
            utils::filesystem::create_directory(app->working_dir);



            int port = (int)RandomGenerator.Random64(1000, ushort.MaxValue);
            for (int i = 0; i < 10; i++)
            {
                try
                {
                    Directory.CreateDirectory(Path.Combine(role.Dir, port.ToString()));
                }
                catch (Exception)
                {
                }

                s.Proc = new Process();
                s.Proc.StartInfo = new ProcessStartInfo(
                    Path.Combine(role.Dir, role.Class.MainExecutableName),
                    role.Class.Arguments.Replace("%port%", port.ToString()).Replace("%name%", role.ServiceInfo.Name)
                    )
                {
                    WorkingDirectory = role.Dir // Path.Combine(role.Dir, port.ToString())
                                                //UseShellExecute = false
                }
                ;

                //s.Proc.StartInfo.EnvironmentVariables.Add("ServicePort", port.ToString());
                //s.Proc.StartInfo.EnvironmentVariables.Add("ServiceMgrPid", Process.GetCurrentProcess().Id.ToString());

                s.Proc.Start();

                s.Proc.WaitForExit(100);

                if (!s.Proc.HasExited)
                {
                    s.Partition.ServicePort = port;

                    Trace.WriteLine("CreateService " + role.ServiceInfo.Name + "." + spi.Index + " successfully with port " + port);
                    return ErrorCode.Success;
                }

                port = (int)RandomGenerator.Random64(1000, ushort.MaxValue);
            }

            s.WorkDir = Path.Combine(role.Dir, port.ToString());
            _job.AddProcess(s.Proc.Handle);

            Trace.WriteLine("CreateService " + role.ServiceInfo.Name + "." + spi.Index + " failed, exit code = " + s.Proc.ExitCode);

            return ErrorCode.ProcessStartFailed;
        }
        
        error_code daemon_s_service::kill_app(layer1_app_info * app)
        {
            Trace.WriteLine("Kill service '" + s.Partition.Name + "'");

            try
            {
                s.Proc.Kill();
            }
            catch (Exception)
            {
            }

            s.Proc.Close();
            s.Proc = null;

            if (svcs != null)
            {
                PerRoleService rs;
                bool r = svcs.Services.TryRemove(s.Partition.Index, out rs);
                Trace.Assert(r);
            }
        }
        
        void daemon_s_service::check_apps()
        {
            List<FailedService> rs = new List<FailedService>();

            if (_online)
            {
                ::dsn::service::zauto_write_lock l(_lock);
                {
                    foreach(var t in _services)
                    {
                        foreach(var s in t.Value.Services)
                        {
                            if (s.Value.Proc != null && s.Value.Proc.HasExited)
                            {
                                ServicePlaceRequest r = new ServicePlaceRequest();
                                r.Partition = new ServicePartitionInfo();
                                s.Value.Partition.CopyTo(r.Partition);
                                r.Partition.ConfigurationVersion++;
                                r.Act = ServicePartitionAction.REMOVE;

                                FailedService fs = new FailedService();
                                fs.Request = r;
                                fs.Role = t.Value;
                                fs.Svc = s.Value;

                                rs.Add(fs);
                            }
                        }
                    }
                }

                foreach(var r in rs)
                {
                    try
                    {
                        _meta.PlaceService(r.Request);
                        ::dsn::service::zauto_write_lock l(_lock);
                        {
                            KillService(r.Role, r.Svc);
                        }
                    }
                    catch (Exception)
                    {
                    }
                }
            }
        }
    }
}