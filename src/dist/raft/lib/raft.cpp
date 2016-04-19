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
*     helper functions in raft object
*
* Revision history:
*     Mar., 2016, @pandaworrior (Cheng Li), first version
*     xxxx-xx-xx, author, fix bug about xxx
*/
# include "raft.h"
#include "replica.h"
#include "mutation.h"
#include "mutation_log.h"

namespace dsn{
	namespace replication {

		raft::raft(replica* _r, uint32_t hb_timeout, uint32_t min_le_timeout, uint32_t max_le_timeout)
		{
			raft_init(_r, hb_timeout, min_le_timeout, max_le_timeout);
		}

		raft::~raft()
		{
		}

		void raft::raft_init(replica* _r, uint32_t hb_timeout, uint32_t min_le_timeout, uint32_t max_le_timeout)
		{
			dassert(nullptr != _r, "replica ptr must be valid");
			_replica = _r;

			dassert((hb_timeout > 0 && min_le_timeout > 0 && max_le_timeout >0 &&
				(min_le_timeout < max_le_timeout)), "timeout parameters are invalid");
			//set timeout for hearbeat and election timeout
			set_heartbeat_timeout_ms(hb_timeout);
			set_min_leader_election_timeout_ms(min_le_timeout);
			set_max_leader_election_timeout_ms(max_le_timeout);
			update_last_heartbeat_arrival_time_ms(true);
			set_leader_election_timeout_ms();

			ddebug("%s: set heartbeat timeout in ms %lu",
				_replica->name(), get_heartbeat_timeout_ms());
			ddebug("%s: set leader election timeout in ms %lu",
				_replica->name(), _leader_election_timeout_milliseconds);

		}

		void raft::set_heartbeat_timeout_ms(uint32_t hb_timeout)
		{
			_heartbeat_timeout_milliseconds = hb_timeout;
		}

		uint64_t raft::get_last_heartbeat_arrival_time_ms()
		{
			return _last_heartbeat_arrival_time_milliseconds;
		}

		void raft::update_last_heartbeat_arrival_time_ms(bool initial)
		{
			if (initial)
			{
				_last_heartbeat_arrival_time_milliseconds = 0;
				ddebug(
					"initially set heartbeat arrival time to %llu",
					_last_heartbeat_arrival_time_milliseconds
					);
				return;
			}

			_last_heartbeat_arrival_time_milliseconds = dsn_now_ms();

			ddebug(
				"heartbeat msg received at %llu",
				_last_heartbeat_arrival_time_milliseconds
				);
		}

		void raft::set_min_leader_election_timeout_ms(uint32_t min_le_timeout)
		{
			_min_leader_election_timeout_milliseconds = min_le_timeout;
		}

		void raft::set_max_leader_election_timeout_ms(uint32_t max_le_timeout)
		{
			_max_leader_election_timeout_milliseconds = max_le_timeout;
		}

		void raft::set_leader_election_timeout_ms()
		{
			//randomly generate a timeout from a range
			_leader_election_timeout_milliseconds = dsn_random32(_min_leader_election_timeout_milliseconds, _max_leader_election_timeout_milliseconds);

			ddebug("%s: randomly chose a timeout %lu (ms) for leader election task",
				_replica->name(), _leader_election_timeout_milliseconds);
		}

		uint32_t raft::get_new_leader_election_timeout_ms()
		{
			set_leader_election_timeout_ms();
			return _leader_election_timeout_milliseconds;
		}

		void raft::reset_raft_membership_on_leader(partition_configuration& new_mem)
		{
			_membership.clear();

			_membership.push_back(new_mem.primary);

			for (auto it = new_mem.secondaries.begin(); it != new_mem.secondaries.end(); ++it)
			{
				_membership.push_back(*it);
			}
		}

		void raft::reset_raft_membership_on_follower(std::vector<dsn::rpc_address> nodes)
		{
			_membership = nodes;
		}

		std::vector<dsn::rpc_address> raft::get_raft_membership()
		{
			return _membership;
		}

		ballot raft::increment_and_get_raft_ballot() 
		{ 
			return _replica->increment_and_get_ballot(); 
		}

		ballot raft::get_ballot() 
		{ 
			return _replica->get_ballot(); 
		}

		std::vector<dsn::rpc_address> raft::get_peers_address(const dsn::rpc_address& my_address)
		{
			std::vector<dsn::rpc_address> nodes;

			for (auto it = _membership.begin(); it != _membership.end(); ++it)
			{
				if ((*it) != my_address)
				{
					nodes.push_back(*it);
				}
			}
			return nodes;
		}

		partition_status raft::get_raft_role()
		{
			return _replica->status();
		}

		uint32_t raft::get_raft_majority_num()
		{
			uint32_t majority = 0;
			if (_membership.size() == 0)
			{
				derror("it must contain at least one node");
			}
			else
			{
				majority = (_membership.size() / 2) + 1;
			}
			return majority;
		}

		bool raft::not_receiving_heartbeat_in_valid_timeout(uint64_t current_ts_ms)
		{
			if (current_ts_ms - get_last_heartbeat_arrival_time_ms() > _heartbeat_timeout_milliseconds)
			{
				return true;
			}
			return false;
		}

		void raft::update_ballot(ballot n_ballot)
		{
			dassert(get_ballot() < n_ballot, "invalid raft ballot input");

			_replica->update_ballot(n_ballot);
		}
	}
}