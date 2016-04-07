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

		raft::raft(uint32_t hb_timeout, uint32_t min_le_timeout, uint32_t max_le_timeout)
		{
			raft_init(hb_timeout, min_le_timeout, max_le_timeout);
		}

		raft::~raft()
		{
			//destroy all locks
			dsn_rwlock_nr_destroy(_rw_lock_raft_role);
			dsn_rwlock_nr_destroy(_rw_lock_last_heartbeat_arrival_time);
		}

		void raft::raft_init(uint32_t hb_timeout, uint32_t min_le_timeout, uint32_t max_le_timeout)
		{
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
				return;
			}

			//acquire write lock
			dsn_rwlock_nr_lock_write(_rw_lock_last_heartbeat_arrival_time);
			_last_heartbeat_arrival_time_milliseconds = (uint32_t) dsn_now_ms();
			dsn_rwlock_nr_unlock_write(_rw_lock_last_heartbeat_arrival_time);

			ddebug(
				"heartbeat msg received at %l",
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

		void raft::init_heartbeat_monitor()
		{
			if (get_raft_role() != RR_FOLLOWER)
				return;

			//only follower need to kick off this process

		}

		void raft::monitor_heartbeat()
		{
			dassert(nullptr != _heartbeat_monitor_task_handler, "");

			ddebug(
				"%s: start monitoring the heartbeat arrival",
				_replica->name()
				);

			partition_status current_status = _replica->status();
			if (current_status == PS_INACTIVE ||
				current_status == PS_INVALID ||
				current_status == PS_ERROR)
			{
				ddebug(
					"%s: is not ready yet",
					_replica->name()
					);
				return;
			}

			raft_role current_role_status = get_raft_role();
			if (current_role_status == RR_FOLLOWER)
			{
				//get current time stamp and compare against the last heartbeat receiving time
				uint32_t current_timestamp_milliseconds = (uint32_t)dsn_now_ms();
				if (current_timestamp_milliseconds - get_last_heartbeat_arrival_time_ms() > _heartbeat_timeout_milliseconds)
				{
					//change role to candidate
					change_raft_role(RR_CANDIDATE);
					//trigger leader election
					//run_leader_election_periodic();
				}
			}
		}
	}
}