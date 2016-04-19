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

/*
* Description:
*     helper functions of raft in replica object
*
* Revision history:
*     Mar., 2016, @pandaworrior (Cheng Li), first version
*     xxxx-xx-xx, author, fix bug about xxx
*/

# include "raft.h"
# include "replica.h"
#include "mutation.h"
#include "mutation_log.h"
#include "replica_context.h"
#include "replica_stub.h"
#include "replication_app_base.h"

namespace dsn {
	namespace replication {

		void replica::init_raft_heartbeat_monitor_on_follower()
		{
			dassert(_raft->get_raft_role() == PS_SECONDARY, "%s: raft role is %s", 
				name(), enum_to_string(_raft->get_raft_role()));

			CLEANUP_TASK_ALWAYS(_raft->_heartbeat_monitor_task);

			//only follower need to kick off this process
			//create a task to send heart
			_raft->_heartbeat_monitor_task = tasking::enqueue_timer(
				LPC_HEARTBEAT_MONITOR,
				this,
				[this] {monitor_heartbeat(); },
				std::chrono::milliseconds(_raft->get_heartbeat_timeout_ms()),
				gpid_to_hash(get_gpid())
				);
		}

		void replica::monitor_heartbeat()
		{
			dassert(nullptr != _raft->_heartbeat_monitor_task, "");

			ddebug(
				"%s: start monitoring the heartbeat arrival",
				name()
				);

			partition_status current_role_status = _raft->get_raft_role();
			if (current_role_status == PS_SECONDARY)
			{
				//get current time stamp and compare against the last heartbeat receiving time
				uint64_t current_timestamp_milliseconds = dsn_now_ms();
				if (_raft->not_receiving_heartbeat_in_valid_timeout(current_timestamp_milliseconds))
				{
					ddebug("%s: is a follower but has not received heartbeat in a given interval, please starts leader election",
						name());
					// change role to candidate
					// call the update_local_configuration
					update_local_configuration_with_no_ballot_change(PS_POTENTIAL_PRIMARY);
				}
			}
		}

		void replica::disable_raft_heartbeat_monitor_task()
		{
			// clean up heartbeat monitor check
			CLEANUP_TASK_ALWAYS(_raft->_heartbeat_monitor_task);

			ddebug("%s: has canceled its heartbeat monitor task",
				name());
		}

		void replica::disable_raft_leader_election_task()
		{
			CLEANUP_TASK_ALWAYS(_raft->_leader_election_task);
			ddebug("%s: has canceled its leader election task",
				name());
		}

		void replica::install_raft_membership_on_replicas()
		{
			// pacificA piggybacks a replica config to all secondaries, which only
			// specifies the primary address, however, raft requires every replica
			// to the overall membership setting. Therefore, we send a rpc to the 
			// remote peers to let them know the overall membership

			//reset raft membership on leader
			_raft->reset_raft_membership_on_leader(_primary_states.membership);

			// get all secondaries from raft
			std::vector<dsn::rpc_address> raft_mem_seconaries = _raft->get_peers_address(_stub->_primary_address);

			// send to all secondaries a message must reply I think
			for (auto it = raft_mem_seconaries.begin(); it != raft_mem_seconaries.end(); ++it)
			{
				send_raft_membership_update_message(*it, _options->prepare_timeout_ms_for_secondaries);
			}
		}

		void replica::send_raft_membership_update_message(::dsn::rpc_address addr, int timeout_milliseconds)
		{
			ddebug("%s sends raft membership update msg to addr %s",
				name(),
				addr.to_string());

			//here call the rpc
			dsn_message_t msg = dsn_msg_create_request(RPC_RAFT_LEADER_UPDATE_MEM, timeout_milliseconds, gpid_to_hash(get_gpid()));
			raft_membership_update_request r_mem_update_request;
			r_mem_update_request.gpid = get_gpid();
			r_mem_update_request.my_ballot = get_ballot();
			r_mem_update_request.mem = _raft->get_raft_membership(); //TODO: please solve this problem
			::marshall(msg, r_mem_update_request);
			
			rpc::call(
				addr,
				msg,
				this,
				[this](error_code err, dsn_message_t request, dsn_message_t resp)
			{
				on_raft_update_membership_reply(err, request, resp);
			}
			);
		}

		void replica::on_raft_update_membership(dsn_message_t msg, const raft_membership_update_request& request)
		{
			ddebug("received a request from other peers to update my raft membership");
			raft_membership_update_response response;

			//TODO: problem, the request ballot most of time must be bigger
			if (_raft->get_ballot() > request.my_ballot)
			{	
				response.err = ERR_INVALID_BALLOT;
			}
			else
			{
				_raft->reset_raft_membership_on_follower(request.mem);
				response.err = ERR_OK;

				// do we need to update the replica_stub partition_config as well?
			}
			reply(msg, response);
		}

		void replica::on_raft_update_membership_reply(error_code err, dsn_message_t request, dsn_message_t response)
		{
			ddebug("received an raft mem update reply from peers");
			if (err != ERR_OK)
			{
				// do we need to handle failures?
			}
		}

		void replica::init_raft_leader_election_on_candidate()
		{
			if (_raft->_leader_election_task != nullptr)
			{
				CLEANUP_TASK_ALWAYS(_raft->_leader_election_task);
			}

			partition_status rr = _raft->get_raft_role();
			if (rr != PS_POTENTIAL_PRIMARY)
			{
				ddebug("no need to perform this task");
				return;
			}

			//reset the next timeout
			uint32_t timeout = _raft->get_new_leader_election_timeout_ms();

			//get a new ballot number
			ballot new_ballot = _raft->increment_and_get_raft_ballot();

			//clean up the vote set
			_raft->_vote_set.clear();
			//vote for itself
			_raft->_vote_set.insert(_stub->_primary_address);

			//get all nodes excluding itself
			std::vector<dsn::rpc_address> peers = _raft->get_peers_address(_stub->_primary_address);

			if (peers.size() == 0)
			{
				//elect itself as the leader
				upgrade_to_primary_by_raft();
				return;
			}

			for (auto it = peers.begin(); it != peers.end(); ++it)
			{
				send_raft_vote_request_message(*it, new_ballot, _options->prepare_timeout_ms_for_secondaries);
			}

			//send a request for vote to any peer
			_raft->_leader_election_task = tasking::enqueue(
				LPC_LEADER_ELECTION_TASK,
				this,
				[this] {init_raft_leader_election_on_candidate(); },
				gpid_to_hash(get_gpid()),
				std::chrono::milliseconds(timeout)
				);
		}

		void replica::send_raft_vote_request_message(::dsn::rpc_address addr, ballot n_ballot, int timeout_milliseconds)
		{
			dsn_message_t msg = dsn_msg_create_request(RPC_RAFT_VOTE_REQUEST, _options->prepare_timeout_ms_for_secondaries, gpid_to_hash(get_gpid()));

			raft_vote_request req;
			req.gpid = get_gpid();
			req.my_ballot = n_ballot;
			::marshall(msg, req);

			rpc::call(
				addr,
				msg,
				this,
				[this](error_code err, dsn_message_t request, dsn_message_t resp)
				{
					on_raft_vote_reply(err, request, resp);
				}
			);
		}

		void replica::on_raft_vote_request(dsn_message_t msg, const raft_vote_request& request)
		{
			raft_vote_response rv_resp;
			rv_resp.decision = false;
			rv_resp.err = ERR_OK;
			rv_resp.my_addr = _stub->_primary_address;
			ballot old_ballot = _raft->get_ballot();
			
			if (request.my_ballot > old_ballot)
			{
				//_raft->update_ballot(request.my_ballot);
				//update the local configuration with no ballot change to Follower
				replica_configuration new_config = _config;
				new_config.ballot = request.my_ballot;
				new_config.status = PS_SECONDARY;
				update_local_configuration(new_config, false);
				rv_resp.decision = true;
			}

			rv_resp.my_ballot = _raft->get_ballot();
			reply(msg, rv_resp);
		}

		void replica::on_raft_vote_reply(error_code err, dsn_message_t request, dsn_message_t response)
		{
			raft_vote_response rv_resp;

			if (err != ERR_OK)
			{
				derror(
					"on_recv_vote_reply err = %s",
					err.to_string()
					);
				return;
			}
			else
			{
				::unmarshall(response, rv_resp);
			}

			ballot old_ballot = _raft->get_ballot();

			if (rv_resp.my_ballot < old_ballot)
			{
				return;
			}
			else
			{
				if (rv_resp.my_ballot > old_ballot)
				{
					//change_raft_role_to_follower();
					// call update local configuration
					// if the old is secondary or potential, no need to change
					// if the old is primary, must change it to secondary
					// if the old is candidate, must change it to secondary
					replica_configuration new_config = _config;
					new_config.ballot = rv_resp.my_ballot;
					new_config.status = PS_SECONDARY;
					update_local_configuration(new_config, false);
				}
				else
				{
					partition_status r_role = _raft->get_raft_role();
					if (r_role == PS_POTENTIAL_PRIMARY)
					{
						if (rv_resp.decision)
						{
							_raft->_vote_set.insert(rv_resp.my_addr);
							if (_raft->_vote_set.size() >= _raft->get_raft_majority_num())
							{
								ddebug("%s: receives %d vote replies more than majority %d, ready to be leader",
									name(), _raft->_vote_set.size(), _raft->get_raft_majority_num());
								// upgrade to primary via calling the legacy function
								upgrade_to_primary_by_raft();
							}
						}
					}
				}
			}
		}

		void replica::upgrade_to_primary_by_raft()
		{
			configuration_update_request cu_request;
			cu_request.type = CT_UPGRADE_TO_PRIMARY;
			cu_request.config.app_type = _app_type;
			cu_request.config.gpid = get_gpid();
			cu_request.config.max_replica_count = _raft->get_raft_membership().size();
			cu_request.config.primary = _stub->_primary_address;
			cu_request.config.last_committed_decree = last_committed_decree();
			cu_request.config.secondaries = _raft->get_peers_address(_stub->_primary_address);
			cu_request.is_stateful = true;
			cu_request.node = _stub->_primary_address;
			assign_primary_called_by_raft(cu_request);
		}


		void replica::change_raft_role_to_leader()
		{
			ddebug("%s: changed raft role to leader",
				name());
			disable_raft_heartbeat_monitor_task();
			disable_raft_leader_election_task();
			install_raft_membership_on_replicas();
		}

		void replica::change_raft_role_to_follower()
		{
			ddebug("%s: changed raft role to follower",
				name());
			disable_raft_leader_election_task();
			///////////////raft////////////////////////
			//update the heartbeat receiving time
			_raft->update_last_heartbeat_arrival_time_ms();
			///////////////raft////////////////////////
			init_raft_heartbeat_monitor_on_follower();
		}

		void replica::change_raft_role_to_candidate()
		{
			ddebug("%s: changed raft role to candidate",
				name());
			disable_raft_heartbeat_monitor_task();
			init_raft_leader_election_on_candidate();
		}
	}
}