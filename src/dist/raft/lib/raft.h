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
*     raft interface
*
* Revision history:
*     Mar., 2016, @pandaworrior (Cheng Li), first version
*     xxxx-xx-xx, author, fix bug about xxx
*/

# include <dsn/dist/replication/replication.types.h>
# include <dsn/dist/replication/replication_other_types.h>

namespace dsn {
	namespace replication {

		// ---------- raft membership update request message struct -------------
		struct raft_membership_update_request
		{
			global_partition_id gpid;
			::dsn::error_code err;
			partition_configuration mem;
		};

		inline void marshall(::dsn::binary_writer& writer, const raft_membership_update_request& val)
		{
			marshall(writer, val.gpid);
			marshall(writer, val.err);
			marshall(writer, val.mem);
		}

		inline void unmarshall(::dsn::binary_reader& reader, /*out*/ raft_membership_update_request& val)
		{
			unmarshall(reader, val.gpid);
			unmarshall(reader, val.err);
			unmarshall(reader, val.mem);
		}

		// ---------- raft membership update response message struct -------------
		struct raft_membership_update_response
		{
			global_partition_id gpid;
			::dsn::error_code err;
		};

		inline void marshall(::dsn::binary_writer& writer, const raft_membership_update_response& val)
		{
			marshall(writer, val.gpid);
			marshall(writer, val.err);
		}

		inline void unmarshall(::dsn::binary_reader& reader, /*out*/ raft_membership_update_response& val)
		{
			unmarshall(reader, val.gpid);
			unmarshall(reader, val.err);
		}

		// ---------- raft vote request message struct -------------
		struct raft_vote_request
		{
			global_partition_id gpid;
			ballot my_ballot;
		};

		inline void marshall(::dsn::binary_writer& writer, const raft_vote_request& val)
		{
			marshall(writer, val.gpid);
			marshall(writer, val.my_ballot);
		}

		inline void unmarshall(::dsn::binary_reader& reader, /*out*/ raft_vote_request& val)
		{
			unmarshall(reader, val.gpid);
			unmarshall(reader, val.my_ballot);
		}

		// ---------- raft vote response message struct -------------
		struct raft_vote_response
		{
			::dsn::error_code err;
			::dsn::rpc_address my_addr;
			ballot my_ballot;
			bool decision;
		};

		inline void marshall(::dsn::binary_writer& writer, const raft_vote_response& val)
		{
			marshall(writer, val.err);
			marshall(writer, val.my_addr);
			marshall(writer, val.my_ballot);
			marshall(writer, val.decision);
		}

		inline void unmarshall(::dsn::binary_reader& reader, /*out*/ raft_vote_response& val)
		{
			unmarshall(reader, val.err);
			unmarshall(reader, val.my_addr);
			unmarshall(reader, val.my_ballot);
			unmarshall(reader, val.decision);
		}

		class replica;

		class raft
		{
		public:
			raft(replica* _r, uint32_t hb_timeout, uint32_t min_le_timeout, uint32_t max_le_timeout);
			~raft(void);

			uint32_t get_heartbeat_timeout_ms() { return _heartbeat_timeout_milliseconds; };
			// update the heartbeat receiving time
			void update_last_heartbeat_arrival_time_ms(bool initial = false);

			bool not_receiving_heartbeat_in_valid_timeout(uint64_t current_ts_ms);

			// maintain raft membership
			void reset_raft_membership(partition_configuration& new_mem);

			// get raft membership
			partition_configuration get_raft_membership();

			// get and increment a new ballot number
			ballot get_and_increment_raft_ballot() { return (++(_membership.ballot)); };

			ballot get_ballot() { return _membership.ballot; };

			void update_ballot(ballot n_ballot) { _membership.ballot = n_ballot; };

			std::set<dsn::rpc_address> get_peers_address(const dsn::rpc_address& my_address);

			// get the raft role
			raft_role get_raft_role();

			// update raft role
			void change_raft_role(raft_role rr);

			uint32_t get_new_leader_election_timeout_ms();

			uint32_t get_raft_majority_num();

		private:
			//initialize raft
			void raft_init(replica* _r, uint32_t hb_timeout, uint32_t min_le_timeout, uint32_t max_le_timeout);
			//set time interval for sending a heartbeat to all followers
			void set_heartbeat_timeout_ms(uint32_t hb_timeout);
			//get the time of the most recent heartbeat message arrival
			uint32_t get_last_heartbeat_arrival_time_ms();

			// membership
			void zero_mem_ballot();
			

			//randomly choose a timeout for leader election from a range specified by min and max
			void set_min_leader_election_timeout_ms(uint32_t min_le_timeout);
			void set_max_leader_election_timeout_ms(uint32_t max_le_timeout);
			void set_leader_election_timeout_ms();

		public:

			// collecting yes vote from all other peers
			std::set<::dsn::rpc_address> _vote_set;

			/* the repeated heartbeat monitor task of LPC_GROUP_CHECK
			 * calls broadcast_group_check() to check all replicas separately
			 * created in replica::init_group_check()
			 * cancelled in cleanup() when status changed from PRIMARY to others
			 */
			dsn::task_ptr _heartbeat_monitor_task; 

			dsn::task_ptr _leader_election_task;
		private:
			//access replica information e.g. partition configuration
			replica* _replica;

			// membership mgr, including learners
			partition_configuration _membership;

			// LEADER or FOLLOWER or CANDIDATE
			raft_role _r_role;
			// lock for protecting raft_role
			dsn_handle_t _rw_lock_raft_role;

			//heartbeat
			uint32_t _heartbeat_timeout_milliseconds;
			//last heartbeat receiving time will be updated when receiving heartbeat msg or prepare and commit message from the legal leader
			uint32_t _last_heartbeat_arrival_time_milliseconds;
			// lock for protecting the last heartbeat receiving time 
			dsn_handle_t _rw_lock_last_heartbeat_arrival_time;


			//leader election
			uint32_t _min_leader_election_timeout_milliseconds;
			uint32_t _max_leader_election_timeout_milliseconds;
			uint32_t _leader_election_timeout_milliseconds;
		};
	}
}

