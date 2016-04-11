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
			dassert(_raft->_heartbeat_monitor_task != nullptr, "");

			if (_raft->get_raft_role() != RR_FOLLOWER)
				return;

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

			raft_role current_role_status = _raft->get_raft_role();
			if (current_role_status == RR_FOLLOWER)
			{
				//get current time stamp and compare against the last heartbeat receiving time
				uint64_t current_timestamp_milliseconds = dsn_now_ms();
				if (_raft->not_receiving_heartbeat_in_valid_timeout(current_timestamp_milliseconds))
				{
					//change role to candidate
					change_raft_role_to_candidate();
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

		void replica::reset_raft_membership(partition_configuration& new_mem)
		{
			_raft->reset_raft_membership(new_mem);
		}

		void replica::install_raft_membership_on_other_replicas()
		{
			// pacificA piggybacks a replica config to all secondaries, which only
			// specifies the primary address, however, raft requires every replica
			// to the overall membership setting. Therefore, we send a rpc to the 
			// remote peers to let them know the overall membership

			// get membership from raft
			partition_configuration raft_mem = _raft->get_raft_membership();

			// send to all secondaries a message must reply I think
			for (auto it = raft_mem.secondaries.begin(); it != raft_mem.secondaries.end(); ++it)
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
			r_mem_update_request.mem = _raft->get_raft_membership();
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
			// get the local raft membership, compare the ballot number
			partition_configuration old_config = _raft->get_raft_membership();
			raft_membership_update_response response;

			if (old_config.ballot >= request.mem.ballot)
			{	
				response.err = ERR_INVALID_BALLOT;
			}
			else
			{
				partition_configuration new_mem(request.mem);
				reset_raft_membership(new_mem);
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

			raft_role rr = _raft->get_raft_role();
			if (rr != RR_CANDIDATE)
			{
				ddebug("no need to perform this task");
				return;
			}

			//reset the next timeout
			uint32_t timeout = _raft->get_new_leader_election_timeout_ms();

			//get a new ballot number
			ballot new_ballot = _raft->get_and_increment_raft_ballot();

			//clean up the vote set
			_raft->_vote_set.clear();
			//vote for itself
			_raft->_vote_set.insert(_stub->_primary_address);

			//get all nodes excluding itself
			std::set<dsn::rpc_address> peers = _raft->get_peers_address(_stub->_primary_address);

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
				_raft->update_ballot(request.my_ballot);
				change_raft_role_to_follower();
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
					_raft->update_ballot(rv_resp.my_ballot);
					change_raft_role_to_follower();
				}
				else
				{
					raft_role r_role = _raft->get_raft_role();
					if (r_role == RR_CANDIDATE)
					{
						if (rv_resp.decision)
						{
							_raft->_vote_set.insert(rv_resp.my_addr);
							if (_raft->_vote_set.size() >= _raft->get_raft_majority_num())
							{
								ddebug("%s: receives a majority number of vote replies, ready to be leader");
								change_raft_role_to_leader();
							}
						}
					}
				}
			}
		}

		void replica::change_raft_role_to_leader()
		{
			_raft->change_raft_role(RR_LEADER);
			disable_raft_heartbeat_monitor_task();
			disable_raft_leader_election_task();
			reset_raft_membership(_primary_states.membership);
			install_raft_membership_on_other_replicas();
		}

		void replica::change_raft_role_to_follower()
		{
			_raft->change_raft_role(RR_FOLLOWER);
			disable_raft_leader_election_task();
			init_raft_heartbeat_monitor_on_follower();
		}

		void replica::change_raft_role_to_candidate()
		{
			_raft->change_raft_role(RR_CANDIDATE);
			disable_raft_heartbeat_monitor_task();
			init_raft_leader_election_on_candidate();
		}
	}
}