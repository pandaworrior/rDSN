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
			//destroy all locks
			dsn_rwlock_nr_destroy(_rw_lock_raft_role);
			dsn_rwlock_nr_destroy(_rw_lock_last_heartbeat_arrival_time);
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

			//initialize the locks for protecting shared variables
			_rw_lock_raft_role = dsn_rwlock_nr_create();
			_rw_lock_last_heartbeat_arrival_time = dsn_rwlock_nr_create();

			zero_mem_ballot();

			change_raft_role(RR_UNKNOWN);
		}

		void raft::zero_mem_ballot()
		{
			_membership.ballot = -1;
		}

		void raft::set_heartbeat_timeout_ms(uint32_t hb_timeout)
		{
			_heartbeat_timeout_milliseconds = hb_timeout;
		}

		uint32_t raft::get_last_heartbeat_arrival_time_ms()
		{
			uint32_t r_value = 0;
			dsn_rwlock_nr_lock_read(_rw_lock_last_heartbeat_arrival_time);
			r_value = _last_heartbeat_arrival_time_milliseconds;
			dsn_rwlock_nr_unlock_read(_rw_lock_last_heartbeat_arrival_time);
			return r_value;
		}

		void raft::update_last_heartbeat_arrival_time_ms(bool initial)
		{
			if (initial)
			{
				_last_heartbeat_arrival_time_milliseconds = 0;
				initial = false;
				return;
			}

			//acquire write lock
			dsn_rwlock_nr_lock_write(_rw_lock_last_heartbeat_arrival_time);
			_last_heartbeat_arrival_time_milliseconds = (uint32_t) dsn_now_ms();
			dsn_rwlock_nr_unlock_write(_rw_lock_last_heartbeat_arrival_time);

			ddebug(
				"heartbeat msg received at %d",
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
		}

		uint32_t raft::get_new_leader_election_timeout_ms()
		{
			set_leader_election_timeout_ms();
			return _leader_election_timeout_milliseconds;
		}

		void raft::reset_raft_membership(partition_configuration& new_mem)
		{
			_membership = new_mem;
		}

		partition_configuration raft::get_raft_membership()
		{
			return _membership;
		}

		std::set<dsn::rpc_address> raft::get_peers_address(const dsn::rpc_address& my_address)
		{
			std::set<dsn::rpc_address> nodes;
			if (_membership.primary != my_address)
			{
				nodes.insert(_membership.primary);
			}

			for (auto it = _membership.secondaries.begin(); it != _membership.secondaries.end(); ++it)
			{
				if ((*it) != my_address)
				{
					nodes.insert(*it);
				}
			}
			return nodes;
		}

		void raft::change_raft_role(raft_role rr)
		{
			//acquire write lock
			dsn_rwlock_nr_lock_write(_rw_lock_raft_role);
			_r_role = rr;
			dsn_rwlock_nr_unlock_write(_rw_lock_raft_role);
		}

		raft_role raft::get_raft_role()
		{
			raft_role return_value = RR_UNKNOWN;
			//acquire read lock
			dsn_rwlock_nr_lock_read(_rw_lock_raft_role);
			return_value = _r_role;
			dsn_rwlock_nr_unlock_read(_rw_lock_raft_role);
			return return_value;
		}

		uint32_t raft::get_raft_majority_num()
		{
			uint32_t majority = 0;
			if (_membership.secondaries.size() == 0)
			{
				derror("it must contain some secondaries");
			}
			else
			{
				majority = ((1 + _membership.secondaries.size()) / 2) + 1;
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
	}
}