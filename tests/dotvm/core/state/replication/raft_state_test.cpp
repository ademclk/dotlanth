/// @file raft_state_test.cpp
/// @brief Tests for Raft state management

#include "dotvm/core/state/replication/raft_state.hpp"

#include <gtest/gtest.h>

#include <thread>
#include <vector>

namespace dotvm::core::state::replication {
namespace {

// ============================================================================
// Test Helpers
// ============================================================================

class RaftStateTest : public ::testing::Test {
protected:
    static NodeId make_node_id(std::uint8_t seed) {
        NodeId id;
        for (std::size_t i = 0; i < id.data.size(); ++i) {
            id.data[i] = static_cast<std::uint8_t>((seed + i) % 256);
        }
        return id;
    }
};

// ============================================================================
// RaftRole Tests
// ============================================================================

TEST(RaftRoleTest, ToStringCoversAll) {
    EXPECT_EQ(to_string(RaftRole::Follower), "Follower");
    EXPECT_EQ(to_string(RaftRole::Candidate), "Candidate");
    EXPECT_EQ(to_string(RaftRole::Leader), "Leader");
}

// ============================================================================
// RaftLogEntry Tests (from message_types.hpp)
// ============================================================================

TEST_F(RaftStateTest, LogEntryNoopCreation) {
    RaftLogEntry entry;
    entry.term = Term{5};
    entry.index = LogIndex{10};
    entry.command.type = RaftCommandType::Noop;

    EXPECT_EQ(entry.term.value, 5);
    EXPECT_EQ(entry.index.value, 10);
    EXPECT_EQ(entry.command.type, RaftCommandType::Noop);
}

TEST_F(RaftStateTest, LogEntryAddNodeCreation) {
    auto node = make_node_id(42);

    RaftLogEntry entry;
    entry.term = Term{3};
    entry.index = LogIndex{7};
    entry.command.type = RaftCommandType::AddNode;
    entry.command.data.resize(node.data.size());
    for (std::size_t i = 0; i < node.data.size(); ++i) {
        entry.command.data[i] = static_cast<std::byte>(node.data[i]);
    }

    EXPECT_EQ(entry.term.value, 3);
    EXPECT_EQ(entry.index.value, 7);
    EXPECT_EQ(entry.command.type, RaftCommandType::AddNode);
}

// ============================================================================
// ClusterMembership Tests
// ============================================================================

TEST_F(RaftStateTest, ClusterMembershipBasics) {
    ClusterMembership membership;
    EXPECT_EQ(membership.total_size(), 0);
    EXPECT_EQ(membership.quorum_size(), 1);  // (0/2) + 1 = 1

    auto node1 = make_node_id(1);
    auto node2 = make_node_id(2);
    auto node3 = make_node_id(3);

    membership.voting_members.insert(node1);
    membership.voting_members.insert(node2);
    membership.voting_members.insert(node3);

    EXPECT_EQ(membership.total_size(), 3);
    EXPECT_EQ(membership.quorum_size(), 2);  // (3/2) + 1 = 2
    EXPECT_TRUE(membership.is_voting_member(node1));
    EXPECT_TRUE(membership.is_member(node1));
}

TEST_F(RaftStateTest, ClusterMembershipNonVoting) {
    ClusterMembership membership;
    auto voter = make_node_id(1);
    auto learner = make_node_id(2);

    membership.voting_members.insert(voter);
    membership.non_voting_members.insert(learner);

    EXPECT_EQ(membership.total_size(), 2);
    EXPECT_EQ(membership.quorum_size(), 1);  // Only voting members count

    EXPECT_TRUE(membership.is_voting_member(voter));
    EXPECT_FALSE(membership.is_voting_member(learner));
    EXPECT_TRUE(membership.is_member(voter));
    EXPECT_TRUE(membership.is_member(learner));
}

// ============================================================================
// RaftStateManager Role Tests
// ============================================================================

TEST_F(RaftStateTest, InitialStateIsFollower) {
    auto id = make_node_id(1);
    RaftStateManager state(id);

    EXPECT_EQ(state.role(), RaftRole::Follower);
    EXPECT_EQ(state.local_id(), id);
    EXPECT_EQ(state.current_term().value, 0);
}

TEST_F(RaftStateTest, BecomeCandidate) {
    auto id = make_node_id(1);
    RaftStateManager state(id);

    state.become_candidate();

    EXPECT_EQ(state.role(), RaftRole::Candidate);
    EXPECT_EQ(state.current_term().value, 1);  // Term incremented
    EXPECT_EQ(state.voted_for(), id);    // Voted for self
}

TEST_F(RaftStateTest, BecomeLeader) {
    auto id = make_node_id(1);
    RaftStateManager state(id);

    state.become_candidate();
    state.become_leader();

    EXPECT_EQ(state.role(), RaftRole::Leader);
    EXPECT_EQ(state.current_leader(), id);
}

TEST_F(RaftStateTest, BecomeFollower) {
    auto id = make_node_id(1);
    RaftStateManager state(id);

    state.become_candidate();
    state.become_follower(Term{5});  // Higher term

    EXPECT_EQ(state.role(), RaftRole::Follower);
    EXPECT_EQ(state.current_term().value, 5);
    EXPECT_FALSE(state.voted_for().has_value());  // Vote cleared
}

// ============================================================================
// RaftStateManager Term Tests
// ============================================================================

TEST_F(RaftStateTest, UpdateTermTransitionsToFollower) {
    auto id = make_node_id(1);
    RaftStateManager state(id);

    state.become_candidate();
    EXPECT_EQ(state.role(), RaftRole::Candidate);

    bool updated = state.update_term(Term{10});

    EXPECT_TRUE(updated);
    EXPECT_EQ(state.role(), RaftRole::Follower);
    EXPECT_EQ(state.current_term().value, 10);
    EXPECT_FALSE(state.voted_for().has_value());
}

TEST_F(RaftStateTest, UpdateTermIgnoresLowerTerm) {
    auto id = make_node_id(1);
    RaftStateManager state(id);

    state.become_candidate();  // Term = 1
    state.become_leader();

    bool updated = state.update_term(Term{0});

    EXPECT_FALSE(updated);
    EXPECT_EQ(state.role(), RaftRole::Leader);
    EXPECT_EQ(state.current_term().value, 1);
}

// ============================================================================
// RaftStateManager Vote Tests
// ============================================================================

TEST_F(RaftStateTest, RecordVoteSucceeds) {
    auto id = make_node_id(1);
    auto candidate = make_node_id(2);
    RaftStateManager state(id);

    auto result = state.record_vote(candidate);

    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(state.voted_for(), candidate);
}

TEST_F(RaftStateTest, RecordVoteFailsIfAlreadyVotedForDifferent) {
    auto id = make_node_id(1);
    auto candidate1 = make_node_id(2);
    auto candidate2 = make_node_id(3);
    RaftStateManager state(id);

    auto result1 = state.record_vote(candidate1);
    ASSERT_TRUE(result1.is_ok());

    auto result2 = state.record_vote(candidate2);
    EXPECT_TRUE(result2.is_err());
    EXPECT_EQ(result2.error(), ReplicationError::AlreadyVoted);
}

TEST_F(RaftStateTest, RecordVoteSucceedsIfSameCandidate) {
    auto id = make_node_id(1);
    auto candidate = make_node_id(2);
    RaftStateManager state(id);

    auto result1 = state.record_vote(candidate);
    ASSERT_TRUE(result1.is_ok());

    auto result2 = state.record_vote(candidate);
    EXPECT_TRUE(result2.is_ok());
}

TEST_F(RaftStateTest, ClearVote) {
    auto id = make_node_id(1);
    auto candidate = make_node_id(2);
    RaftStateManager state(id);

    auto vote_result = state.record_vote(candidate);
    (void)vote_result;
    EXPECT_TRUE(state.voted_for().has_value());

    state.clear_vote();
    EXPECT_FALSE(state.voted_for().has_value());
}

// ============================================================================
// RaftStateManager Log State Tests
// ============================================================================

TEST_F(RaftStateTest, LogStateInitiallyZero) {
    auto id = make_node_id(1);
    RaftStateManager state(id);

    EXPECT_EQ(state.last_log_index().value, 0);
    EXPECT_EQ(state.last_log_term().value, 0);
}

TEST_F(RaftStateTest, UpdateLastLog) {
    auto id = make_node_id(1);
    RaftStateManager state(id);

    state.update_last_log(LogIndex{5}, Term{3});

    EXPECT_EQ(state.last_log_index().value, 5);
    EXPECT_EQ(state.last_log_term().value, 3);
}

// ============================================================================
// RaftStateManager Commit Tests
// ============================================================================

TEST_F(RaftStateTest, CommitIndexMonotonicallyIncreasing) {
    auto id = make_node_id(1);
    RaftStateManager state(id);

    EXPECT_TRUE(state.update_commit_index(LogIndex{5}));
    EXPECT_EQ(state.commit_index().value, 5);

    EXPECT_TRUE(state.update_commit_index(LogIndex{10}));
    EXPECT_EQ(state.commit_index().value, 10);

    EXPECT_FALSE(state.update_commit_index(LogIndex{7}));  // Can't go backward
    EXPECT_EQ(state.commit_index().value, 10);
}

TEST_F(RaftStateTest, LastAppliedMonotonicallyIncreasing) {
    auto id = make_node_id(1);
    RaftStateManager state(id);

    EXPECT_TRUE(state.update_last_applied(LogIndex{3}));
    EXPECT_EQ(state.last_applied().value, 3);

    EXPECT_FALSE(state.update_last_applied(LogIndex{2}));
    EXPECT_EQ(state.last_applied().value, 3);
}

// ============================================================================
// RaftStateManager Leader State Tests
// ============================================================================

TEST_F(RaftStateTest, LeaderStateInitialization) {
    auto id = make_node_id(1);
    auto peer1 = make_node_id(2);
    auto peer2 = make_node_id(3);
    RaftStateManager state(id);

    state.update_last_log(LogIndex{10}, Term{2});
    state.initialize_leader_state({peer1, peer2});

    // next_index should be last_log_index + 1
    auto next1 = state.get_next_index(peer1);
    auto next2 = state.get_next_index(peer2);
    ASSERT_TRUE(next1.has_value());
    ASSERT_TRUE(next2.has_value());
    EXPECT_EQ(next1.value().value, 11);
    EXPECT_EQ(next2.value().value, 11);

    // match_index should be 0
    auto match1 = state.get_match_index(peer1);
    auto match2 = state.get_match_index(peer2);
    ASSERT_TRUE(match1.has_value());
    ASSERT_TRUE(match2.has_value());
    EXPECT_EQ(match1.value().value, 0);
    EXPECT_EQ(match2.value().value, 0);
}

TEST_F(RaftStateTest, SetNextAndMatchIndex) {
    auto id = make_node_id(1);
    auto peer = make_node_id(2);
    RaftStateManager state(id);

    state.initialize_leader_state({peer});

    state.set_next_index(peer, LogIndex{15});
    state.set_match_index(peer, LogIndex{14});

    EXPECT_EQ(state.get_next_index(peer).value().value, 15);
    EXPECT_EQ(state.get_match_index(peer).value().value, 14);
}

// ============================================================================
// RaftStateManager Membership Tests
// ============================================================================

TEST_F(RaftStateTest, MembershipStartsWithSelf) {
    auto id = make_node_id(1);
    RaftStateManager state(id);

    auto membership = state.membership();
    EXPECT_EQ(membership.voting_members.size(), 1);
    EXPECT_TRUE(membership.voting_members.contains(id));
}

TEST_F(RaftStateTest, AddAndRemoveMembers) {
    auto id = make_node_id(1);
    auto peer = make_node_id(2);
    RaftStateManager state(id);

    state.add_voting_member(peer);
    EXPECT_EQ(state.membership().voting_members.size(), 2);

    state.remove_member(peer);
    EXPECT_EQ(state.membership().voting_members.size(), 1);
}

TEST_F(RaftStateTest, PeersExcludesSelf) {
    auto id = make_node_id(1);
    auto peer = make_node_id(2);
    RaftStateManager state(id);

    state.add_voting_member(peer);
    auto peers = state.peers();

    EXPECT_EQ(peers.size(), 1);
    EXPECT_EQ(peers[0], peer);
}

// ============================================================================
// RaftStateManager Thread Safety Test
// ============================================================================

TEST_F(RaftStateTest, ConcurrentAccess) {
    auto id = make_node_id(1);
    RaftStateManager state(id);

    std::vector<std::thread> threads;

    // Multiple readers
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([&state]() {
            for (int j = 0; j < 100; ++j) {
                (void)state.current_term();
                (void)state.role();
                (void)state.commit_index();
            }
        });
    }

    // Multiple writers
    for (int i = 0; i < 5; ++i) {
        threads.emplace_back([&state, i]() {
            for (int j = 0; j < 100; ++j) {
                state.update_commit_index(LogIndex{static_cast<std::uint64_t>(i * 100 + j)});
                state.touch_leader_contact();
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // Should complete without deadlock or crash
    SUCCEED();
}

}  // namespace
}  // namespace dotvm::core::state::replication
