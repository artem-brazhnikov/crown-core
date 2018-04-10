// Copyright (c) 2014-2018 The Crown developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include "masternode-budget.h"
#include "masternodeman.h"

using namespace std::string_literals;

std::ostream& operator<<(std::ostream& os, uint256 value)
{
    return os << value.ToString();
}

bool operator == (const CTxBudgetPayment& a, const CTxBudgetPayment& b)
{
    return a.nProposalHash == b.nProposalHash &&
           a.nAmount == b.nAmount &&
           a.payee == b.payee;
}

std::ostream& operator<<(std::ostream& os, const CTxBudgetPayment& value)
{
    return os << "{" << value.nProposalHash.ToString() << ":" << value.nAmount << "@" << value.payee.ToString() << "}";
}


namespace
{
    auto CreateKeyPair(std::vector<unsigned char> privKey)
    {
        auto keyPair = CKey{};
        keyPair.Set(std::begin(privKey), std::end(privKey), true);

        return keyPair;
    }

    void FillBlock(CBlockIndex& block, /*const*/ CBlockIndex* prevBlock, const uint256& hash)
    {
        if (prevBlock)
        {
            block.nHeight = prevBlock->nHeight + 1;
            block.pprev = prevBlock;
        }
        else
        {
            block.nHeight = 0;
        }

        block.phashBlock = &hash;
        block.BuildSkip();
    }

    void FillHash(uint256& hash, const arith_uint256& height)
    {
        hash = ArithToUint256(height);
    }

    void FillBlock(CBlockIndex& block, uint256& hash, /*const*/ CBlockIndex* prevBlock, size_t height)
    {
        FillHash(hash, height);
        FillBlock(block, height ? prevBlock : nullptr, hash);

        assert(static_cast<int>(UintToArith256(block.GetBlockHash()).GetLow64()) == block.nHeight);
        assert(block.pprev == nullptr || block.nHeight == block.pprev->nHeight + 1);

    }

    auto PayToPublicKey(const CPubKey& pubKey)
    {
        return CScript() << OP_DUP << OP_HASH160 << ToByteVector(pubKey.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;
    }

    auto CreateMasternode(CTxIn vin)
    {
        auto mn = CMasternode{};
        mn.vin = vin;
        mn.activeState = CMasternode::MASTERNODE_ENABLED;
        return mn;
    }

    auto GetPayment(const CBudgetProposal& proposal)
    {
        return CTxBudgetPayment{proposal.GetHash(), proposal.GetPayee(), proposal.GetAmount()};
    }


    struct FinalizedBudgetFixture
    {
        const std::string budgetName = "test"s;
        const int blockStart = 129600;

        const CKey keyPairA = CreateKeyPair({0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1});
        const CKey keyPairB = CreateKeyPair({0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0});
        const CKey keyPairC = CreateKeyPair({0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0});

        const CBudgetProposal proposalA = CreateProposal("A", keyPairA, 42);
        const CBudgetProposal proposalB = CreateProposal("B", keyPairB, 404);
        const CBudgetProposal proposalC = CreateProposal("C", keyPairC, 101);

        const CMasternode mn1 = CreateMasternode(CTxIn{COutPoint{ArithToUint256(1), 1 * COIN}});
        const CMasternode mn2 = CreateMasternode(CTxIn{COutPoint{ArithToUint256(2), 1 * COIN}});
        const CMasternode mn3 = CreateMasternode(CTxIn{COutPoint{ArithToUint256(3), 1 * COIN}});
        const CMasternode mn4 = CreateMasternode(CTxIn{COutPoint{ArithToUint256(4), 1 * COIN}});
        const CMasternode mn5 = CreateMasternode(CTxIn{COutPoint{ArithToUint256(5), 1 * COIN}});

        std::vector<uint256> hashes{100500};
        std::vector<CBlockIndex> blocks{100500};
        std::string error;

        FinalizedBudgetFixture()
        {
            SetMockTime(GetTime());

            fMasterNode = true;
            strBudgetMode = "auto"s;

            // Build a main chain 100500 blocks long.
            for (size_t i = 0; i < blocks.size(); ++i)
            {
                FillBlock(blocks[i], hashes[i], &blocks[i - 1], i);
            }
            chainActive.SetTip(&blocks.back());

            mnodeman.Add(mn1);
            mnodeman.Add(mn2);
            mnodeman.Add(mn3);
            mnodeman.Add(mn4);
            mnodeman.Add(mn5);
        }

        ~FinalizedBudgetFixture()
        {
            SetMockTime(0);

            mnodeman.Clear();
            budget.Clear();
            chainActive = CChain{};
        }

        auto CreateProposal(std::string name, CKey payee, CAmount amount) -> CBudgetProposal
        {
            auto p = CBudgetProposal{
                name,
                "",
                blockStart,
                blockStart + GetBudgetPaymentCycleBlocks() * 2,
                PayToPublicKey(payee.GetPubKey()),
                amount * COIN,
                uint256()
            };
            p.nTime = GetTime();
            return p;
        }

    };
}


BOOST_FIXTURE_TEST_SUITE(FinalizedBudget, FinalizedBudgetFixture)

    BOOST_AUTO_TEST_CASE(CompareHash_Equal)
    {
        // Set Up
        auto budget1 = CFinalizedBudgetBroadcast(
            budgetName, 
            blockStart, 
            std::vector<CTxBudgetPayment> { GetPayment(proposalA), GetPayment(proposalB), GetPayment(proposalC) },
            ArithToUint256(1)
        );

        auto budget2 = CFinalizedBudgetBroadcast(
            budgetName,
            blockStart,
            std::vector<CTxBudgetPayment> { GetPayment(proposalA), GetPayment(proposalB), GetPayment(proposalC) },
            ArithToUint256(2)
        );

        // Call & Check
        BOOST_CHECK_EQUAL(budget1.GetHash(), budget2.GetHash());
    }

    BOOST_AUTO_TEST_CASE(CompareHash_DifferentName)
    {
        // Set Up
        auto budget1 = CFinalizedBudgetBroadcast(
            budgetName,
            blockStart,
            std::vector<CTxBudgetPayment> { GetPayment(proposalA), GetPayment(proposalB), GetPayment(proposalC) },
            ArithToUint256(1)
        );

        auto budget2 = CFinalizedBudgetBroadcast(
            "he-who-must-not-be-named",
            blockStart,
            std::vector<CTxBudgetPayment> { GetPayment(proposalA), GetPayment(proposalB), GetPayment(proposalC) },
            ArithToUint256(2)
        );

        // Call & Check
        BOOST_CHECK(budget1.GetHash() != budget2.GetHash());
    }

    BOOST_AUTO_TEST_CASE(CompareHash_DifferentSet)
    {
        // Set Up
        auto budget1 = CFinalizedBudgetBroadcast(
            budgetName,
            blockStart,
            std::vector<CTxBudgetPayment> { GetPayment(proposalA), GetPayment(proposalC) },
            ArithToUint256(1)
        );

        auto budget2 = CFinalizedBudgetBroadcast(
            budgetName,
            blockStart,
            std::vector<CTxBudgetPayment> { GetPayment(proposalA), GetPayment(proposalB) },
            ArithToUint256(2)
        );

        // Call & Check
        BOOST_CHECK(budget1.GetHash() != budget2.GetHash());
    }

    BOOST_AUTO_TEST_CASE(CompareHash_DifferentOrder)
    {
        // Set Up
        auto budget1 = CFinalizedBudgetBroadcast(
            budgetName,
            blockStart,
            std::vector<CTxBudgetPayment> { GetPayment(proposalA), GetPayment(proposalB), GetPayment(proposalC) },
            ArithToUint256(1)
        );

        auto budget2 = CFinalizedBudgetBroadcast(
            budgetName,
            blockStart,
            std::vector<CTxBudgetPayment> { GetPayment(proposalB), GetPayment(proposalC), GetPayment(proposalA) },
            ArithToUint256(2)
        );

        // Call & Check
        BOOST_CHECK(budget1.GetHash() != budget2.GetHash());
    }
    
    BOOST_AUTO_TEST_CASE(AutoCheck_OneProposal)
    {
        // Set Up
        // Submitting proposals
        budget.AddProposal(proposalA, false); // false = don't check collateral

        // Voting for proposals
        auto vote1a = CBudgetVote{mn1.vin, proposalA.GetHash(), VOTE_YES};
        auto vote2a = CBudgetVote{mn2.vin, proposalA.GetHash(), VOTE_YES};
        auto vote3a = CBudgetVote{mn3.vin, proposalA.GetHash(), VOTE_YES};
        auto vote4a = CBudgetVote{mn4.vin, proposalA.GetHash(), VOTE_YES};
        auto vote5a = CBudgetVote{mn5.vin, proposalA.GetHash(), VOTE_YES};

        budget.UpdateProposal(vote1a, nullptr, error);
        budget.UpdateProposal(vote2a, nullptr, error);
        budget.UpdateProposal(vote3a, nullptr, error);
        budget.UpdateProposal(vote4a, nullptr, error);
        budget.UpdateProposal(vote5a, nullptr, error);

        // Finalizing budget
        SetMockTime(GetTime() + 24 * 60 * 60 + 1); // 1 hour + 1 second has passed

        auto txBudgetPayments = std::vector<CTxBudgetPayment> {
            GetPayment(proposalA)
        };
        auto budget = CFinalizedBudgetBroadcast(budgetName, blockStart, txBudgetPayments, ArithToUint256(42));

        BOOST_REQUIRE(budget.fValid);
        BOOST_REQUIRE(budget.IsValid(false));
        BOOST_REQUIRE_EQUAL(budget.IsAutoChecked(), false);

        // Call & Check
        auto result = budget.AutoCheck();

        BOOST_CHECK_EQUAL(budget.IsAutoChecked(), true);
        BOOST_CHECK(result);
    }

    BOOST_AUTO_TEST_CASE(AutoCheck_ThreeProposals_ABC)
    {
        // Set Up
        // Submitting proposals
        budget.AddProposal(proposalA, false); // false = don't check collateral
        budget.AddProposal(proposalB, false);
        budget.AddProposal(proposalC, false);

        // Voting for proposals
        auto vote1a = CBudgetVote{mn1.vin, proposalA.GetHash(), VOTE_YES};
        auto vote2a = CBudgetVote{mn2.vin, proposalA.GetHash(), VOTE_YES};
        auto vote3a = CBudgetVote{mn3.vin, proposalA.GetHash(), VOTE_YES};
        auto vote4a = CBudgetVote{mn4.vin, proposalA.GetHash(), VOTE_YES};
        auto vote5a = CBudgetVote{mn5.vin, proposalA.GetHash(), VOTE_YES};

        auto vote1b = CBudgetVote{mn1.vin, proposalB.GetHash(), VOTE_YES};
        auto vote2b = CBudgetVote{mn2.vin, proposalB.GetHash(), VOTE_YES};
        auto vote3b = CBudgetVote{mn3.vin, proposalB.GetHash(), VOTE_YES};

        auto vote1c = CBudgetVote{mn1.vin, proposalC.GetHash(), VOTE_YES};
        auto vote2c = CBudgetVote{mn2.vin, proposalC.GetHash(), VOTE_YES};

        budget.UpdateProposal(vote1a, nullptr, error);
        budget.UpdateProposal(vote2a, nullptr, error);
        budget.UpdateProposal(vote3a, nullptr, error);
        budget.UpdateProposal(vote4a, nullptr, error);
        budget.UpdateProposal(vote5a, nullptr, error);

        budget.UpdateProposal(vote1b, nullptr, error);
        budget.UpdateProposal(vote2b, nullptr, error);
        budget.UpdateProposal(vote3b, nullptr, error);

        budget.UpdateProposal(vote1c, nullptr, error);
        budget.UpdateProposal(vote2c, nullptr, error);

        // Finalizing budget
        SetMockTime(GetTime() + 24 * 60 * 60 + 1); // 1 hour + 1 second has passed

        auto txBudgetPayments = std::vector<CTxBudgetPayment> {
            GetPayment(proposalA),
            GetPayment(proposalB),
            GetPayment(proposalC)
        };
        auto budget = CFinalizedBudgetBroadcast(budgetName, blockStart, txBudgetPayments, ArithToUint256(42));

        BOOST_REQUIRE(budget.fValid);
        BOOST_REQUIRE(budget.IsValid(false));
        BOOST_REQUIRE_EQUAL(budget.IsAutoChecked(), false);

        // Call & Check
        auto result = budget.AutoCheck();

        BOOST_CHECK_EQUAL(budget.IsAutoChecked(), true);
        BOOST_CHECK(result);
    }

    BOOST_AUTO_TEST_CASE(AutoCheck_ThreeProposals_CBA)
    {
        // Set Up
        // Submitting proposals
        budget.AddProposal(proposalA, false); // false = don't check collateral
        budget.AddProposal(proposalB, false);
        budget.AddProposal(proposalC, false);

        // Voting for proposals
        auto vote1c = CBudgetVote{mn1.vin, proposalC.GetHash(), VOTE_YES};
        auto vote2c = CBudgetVote{mn2.vin, proposalC.GetHash(), VOTE_YES};
        auto vote3c = CBudgetVote{mn3.vin, proposalC.GetHash(), VOTE_YES};
        auto vote4c = CBudgetVote{mn4.vin, proposalC.GetHash(), VOTE_YES};
        auto vote5c = CBudgetVote{mn5.vin, proposalC.GetHash(), VOTE_YES};

        auto vote1b = CBudgetVote{mn1.vin, proposalB.GetHash(), VOTE_YES};
        auto vote2b = CBudgetVote{mn2.vin, proposalB.GetHash(), VOTE_YES};
        auto vote3b = CBudgetVote{mn3.vin, proposalB.GetHash(), VOTE_YES};

        auto vote1a = CBudgetVote{mn1.vin, proposalA.GetHash(), VOTE_YES};
        auto vote2a = CBudgetVote{mn2.vin, proposalA.GetHash(), VOTE_YES};

        budget.UpdateProposal(vote1c, nullptr, error);
        budget.UpdateProposal(vote2c, nullptr, error);
        budget.UpdateProposal(vote3c, nullptr, error);
        budget.UpdateProposal(vote4c, nullptr, error);
        budget.UpdateProposal(vote5c, nullptr, error);

        budget.UpdateProposal(vote1b, nullptr, error);
        budget.UpdateProposal(vote2b, nullptr, error);
        budget.UpdateProposal(vote3b, nullptr, error);

        budget.UpdateProposal(vote1a, nullptr, error);
        budget.UpdateProposal(vote2a, nullptr, error);

        // Finalizing budget
        SetMockTime(GetTime() + 24 * 60 * 60 + 1); // 1 hour + 1 second has passed

        auto txBudgetPayments = std::vector<CTxBudgetPayment> {
            GetPayment(proposalC),
            GetPayment(proposalB),
            GetPayment(proposalA)
        };
        auto budget = CFinalizedBudgetBroadcast(budgetName, blockStart, txBudgetPayments, ArithToUint256(42));

        BOOST_REQUIRE(budget.fValid);
        BOOST_REQUIRE(budget.IsValid(false));
        BOOST_REQUIRE_EQUAL(budget.IsAutoChecked(), false);

        // Call & Check
        auto result = budget.AutoCheck();

        BOOST_CHECK_EQUAL(budget.IsAutoChecked(), true);
        BOOST_CHECK(result);
    }

    BOOST_AUTO_TEST_CASE(IsTransactionValid_Block0)
    {
        // Set Up
        auto txBudgetPayments = std::vector<CTxBudgetPayment> {
            GetPayment(proposalA),
            GetPayment(proposalB)
        };
        auto budget = CFinalizedBudgetBroadcast(budgetName, blockStart, txBudgetPayments, ArithToUint256(42));

        auto expected = CMutableTransaction{};
        expected.vout.emplace_back(proposalA.GetAmount(), proposalA.GetPayee());

        // Call & Check
        BOOST_CHECK(budget.IsTransactionValid(expected, blockStart));
    }

    BOOST_AUTO_TEST_CASE(IsTransactionValid_Block0_Invalid)
    {
        // Set Up
        auto txBudgetPayments = std::vector<CTxBudgetPayment> {
            GetPayment(proposalA),
            GetPayment(proposalB)
        };
        auto budget = CFinalizedBudgetBroadcast(budgetName, blockStart, txBudgetPayments, ArithToUint256(42));

        auto wrong1 = CMutableTransaction{};
        wrong1.vout.emplace_back(proposalA.GetAmount(), proposalC.GetPayee());

        auto wrong2 = CMutableTransaction{};
        wrong2.vout.emplace_back(proposalC.GetAmount(), proposalA.GetPayee());

        // Call & Check
        BOOST_CHECK(!budget.IsTransactionValid(wrong1, blockStart));
        BOOST_CHECK(!budget.IsTransactionValid(wrong2, blockStart));
    }

    BOOST_AUTO_TEST_CASE(IsTransactionValid_Block1)
    {
        // Set Up
        auto txBudgetPayments = std::vector<CTxBudgetPayment> {
            GetPayment(proposalA),
            GetPayment(proposalB)
        };
        auto budget = CFinalizedBudgetBroadcast(budgetName, blockStart, txBudgetPayments, ArithToUint256(42));

        auto expected = CMutableTransaction{};
        expected.vout.emplace_back(proposalB.GetAmount(), proposalB.GetPayee());

        // Call & Check
        BOOST_CHECK(budget.IsTransactionValid(expected, blockStart + 1));
    }

    BOOST_AUTO_TEST_CASE(IsTransactionValid_Block2)
    {
        // Set Up
        auto txBudgetPayments = std::vector<CTxBudgetPayment> {
            GetPayment(proposalA),
            GetPayment(proposalB)
        };
        auto budget = CFinalizedBudgetBroadcast(budgetName, blockStart, txBudgetPayments, ArithToUint256(42));

        auto expected = CMutableTransaction{};
        expected.vout.emplace_back(proposalB.GetAmount(), proposalB.GetPayee());

        // Call & Check
        BOOST_CHECK(!budget.IsTransactionValid(expected, blockStart + 2));
    }

    BOOST_AUTO_TEST_CASE(GetBudgetPaymentByBlock_Block0)
    {
        // Set Up
        auto txBudgetPayments = std::vector<CTxBudgetPayment> {
            GetPayment(proposalA),
            GetPayment(proposalB)
        };
        auto budget = CFinalizedBudgetBroadcast(budgetName, blockStart, txBudgetPayments, ArithToUint256(42));
        auto actual = CTxBudgetPayment{};

        // Call & Check
        auto result = budget.GetBudgetPaymentByBlock(blockStart, actual);

        BOOST_CHECK_EQUAL(actual, GetPayment(proposalA));
        BOOST_CHECK(result);
    }

    BOOST_AUTO_TEST_CASE(GetBudgetPaymentByBlock_Block1)
    {
        // Set Up
        auto txBudgetPayments = std::vector<CTxBudgetPayment> {
            GetPayment(proposalA),
            GetPayment(proposalB)
        };
        auto budget = CFinalizedBudgetBroadcast(budgetName, blockStart, txBudgetPayments, ArithToUint256(42));
        auto actual = CTxBudgetPayment{};

        // Call & Check
        auto result = budget.GetBudgetPaymentByBlock(blockStart + 1, actual);

        BOOST_CHECK_EQUAL(actual, GetPayment(proposalB));
        BOOST_CHECK(result);
    }

    BOOST_AUTO_TEST_CASE(GetBudgetPaymentByBlock_Block2)
    {
        // Set Up
        auto txBudgetPayments = std::vector<CTxBudgetPayment> {
            GetPayment(proposalA),
            GetPayment(proposalB)
        };
        auto budget = CFinalizedBudgetBroadcast(budgetName, blockStart, txBudgetPayments, ArithToUint256(42));
        auto dummy = CTxBudgetPayment{};

        // Call & Check
        BOOST_CHECK(!budget.GetBudgetPaymentByBlock(blockStart + 2, dummy));
    }


BOOST_AUTO_TEST_SUITE_END()
