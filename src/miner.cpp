// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Copyright (c) 2013 The NovaCoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "txdb.h"
#include "miner.h"
#include "kernel.h"

using namespace std;

//////////////////////////////////////////////////////////////////////////////
//
// BitcoinMiner
//
extern unsigned int nMinerSleep;
double dHashesPerSec;
int64 nHPSTimerStart;
double dBurnHashesPerSec;
int64 nBurnHPSTimerStart;


int static FormatHashBlocks(void* pbuffer, unsigned int len)
{
    unsigned char* pdata = (unsigned char*)pbuffer;
    unsigned int blocks = 1 + ((len + 8) / 64);
    unsigned char* pend = pdata + 64 * blocks;
    memset(pdata + len, 0, 64 * blocks - len);
    pdata[len] = 0x80;
    unsigned int bits = len * 8;
    pend[-1] = (bits >> 0) & 0xff;
    pend[-2] = (bits >> 8) & 0xff;
    pend[-3] = (bits >> 16) & 0xff;
    pend[-4] = (bits >> 24) & 0xff;
    return blocks;
}

static const unsigned int pSHA256InitState[8] =
{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

void SHA256Transform(void* pstate, void* pinput, const void* pinit)
{
    SHA256_CTX ctx;
    unsigned char data[64];

    SHA256_Init(&ctx);

    for (int i = 0; i < 16; i++)
        ((uint32_t*)data)[i] = ByteReverse(((uint32_t*)pinput)[i]);

    for (int i = 0; i < 8; i++)
        ctx.h[i] = ((uint32_t*)pinit)[i];

    SHA256_Update(&ctx, data, sizeof(data));
    for (int i = 0; i < 8; i++)
        ((uint32_t*)pstate)[i] = ctx.h[i];
}

// Some explaining would be appreciated
class COrphan
{
public:
    CTransaction* ptx;
    set<uint256> setDependsOn;
    double dPriority;
    double dFeePerKb;

    COrphan(CTransaction* ptxIn)
    {
        ptx = ptxIn;
        dPriority = dFeePerKb = 0;
    }

    void print() const
    {
        printf("COrphan(hash=%s, dPriority=%.1f, dFeePerKb=%.1f)\n",
               ptx->GetHash().ToString().substr(0,10).c_str(), dPriority, dFeePerKb);
        BOOST_FOREACH(uint256 hash, setDependsOn)
            printf("   setDependsOn %s\n", hash.ToString().substr(0,10).c_str());
    }
};


uint64_t nLastBlockTx = 0;
uint64_t nLastBlockSize = 0;
int64_t nLastCoinStakeSearchInterval = 0;

// We want to sort transactions by priority and fee, so:
typedef boost::tuple<double, double, CTransaction*> TxPriority;
class TxPriorityCompare
{
    bool byFee;
public:
    TxPriorityCompare(bool _byFee) : byFee(_byFee) { }
    bool operator()(const TxPriority& a, const TxPriority& b)
    {
        if (byFee)
        {
            if (a.get<1>() == b.get<1>())
                return a.get<0>() < b.get<0>();
            return a.get<1>() < b.get<1>();
        }
        else
        {
            if (a.get<0>() == b.get<0>())
                return a.get<1>() < b.get<1>();
            return a.get<0>() < b.get<0>();
        }
    }
};

// CreateNewBlock: create new block (without proof-of-work/proof-of-stake)
/*CBlock* CreateNewBlock(CWallet* pwallet, bool fProofOfStake, int64_t* pFees)
{
    // Create new block
    auto_ptr<CBlock> pblock(new CBlock());
    if (!pblock.get())
        return NULL;

    CBlockIndex* pindexPrev = pindexBest;

    // Create coinbase tx
    CTransaction txNew;
    txNew.vin.resize(1);
    txNew.vin[0].prevout.SetNull();
    txNew.vout.resize(1);

    if (!fProofOfStake)
    {
        CReserveKey reservekey(pwallet);
        CPubKey pubkey;
        if (!reservekey.GetReservedKey(pubkey))
            return NULL;
        txNew.vout[0].scriptPubKey.SetDestination(pubkey.GetID());
    }
    else
    {
        // Height first in coinbase required for block.version=2
        txNew.vin[0].scriptSig = (CScript() << pindexPrev->nHeight+1) + COINBASE_FLAGS;
        assert(txNew.vin[0].scriptSig.size() <= 100);

        txNew.vout[0].SetEmpty();
    }

    // Add our coinbase tx as first transaction
    pblock->vtx.push_back(txNew);

    // Largest block you're willing to create:
    unsigned int nBlockMaxSize = GetArg("-blockmaxsize", MAX_BLOCK_SIZE_GEN/2);
    // Limit to betweeen 1K and MAX_BLOCK_SIZE-1K for sanity:
    nBlockMaxSize = std::max((unsigned int)1000, std::min((unsigned int)(MAX_BLOCK_SIZE-1000), nBlockMaxSize));

    // How much of the block should be dedicated to high-priority transactions,
    // included regardless of the fees they pay
    unsigned int nBlockPrioritySize = GetArg("-blockprioritysize", 27000);
    nBlockPrioritySize = std::min(nBlockMaxSize, nBlockPrioritySize);

    // Minimum block size you want to create; block will be filled with free transactions
    // until there are no more or the block reaches this size:
    unsigned int nBlockMinSize = GetArg("-blockminsize", 0);
    nBlockMinSize = std::min(nBlockMaxSize, nBlockMinSize);

    // Fee-per-kilobyte amount considered the same as "free"
    // Be careful setting this: if you set it to zero then
    // a transaction spammer can cheaply fill blocks using
    // 1-satoshi-fee transactions. It should be set above the real
    // cost to you of processing a transaction.
    int64_t nMinTxFee = MIN_TX_FEE;
    if (mapArgs.count("-mintxfee"))
        ParseMoney(mapArgs["-mintxfee"], nMinTxFee);

    pblock->nBits = GetNextTargetRequired(pindexPrev, fProofOfStake);

    // Collect memory pool transactions into the block
    int64_t nFees = 0;
    {
        LOCK2(cs_main, mempool.cs);
        CTxDB txdb("r");

        // Priority order to process transactions
        list<COrphan> vOrphan; // list memory doesn't move
        map<uint256, vector<COrphan*> > mapDependers;

        // This vector will be sorted into a priority queue:
        vector<TxPriority> vecPriority;
        vecPriority.reserve(mempool.mapTx.size());
        for (map<uint256, CTransaction>::iterator mi = mempool.mapTx.begin(); mi != mempool.mapTx.end(); ++mi)
        {
            CTransaction& tx = (*mi).second;
            if (tx.IsCoinBase() || tx.IsCoinStake() || !tx.IsFinal())
                continue;

            COrphan* porphan = NULL;
            double dPriority = 0;
            int64_t nTotalIn = 0;
            bool fMissingInputs = false;
            BOOST_FOREACH(const CTxIn& txin, tx.vin)
            {
                // Read prev transaction
                CTransaction txPrev;
                CTxIndex txindex;
                if (!txPrev.ReadFromDisk(txdb, txin.prevout, txindex))
                {
                    // This should never happen; all transactions in the memory
                    // pool should connect to either transactions in the chain
                    // or other transactions in the memory pool.
                    if (!mempool.mapTx.count(txin.prevout.hash))
                    {
                        printf("ERROR: mempool transaction missing input\n");
                        if (fDebug) assert("mempool transaction missing input" == 0);
                        fMissingInputs = true;
                        if (porphan)
                            vOrphan.pop_back();
                        break;
                    }

                    // Has to wait for dependencies
                    if (!porphan)
                    {
                        // Use list for automatic deletion
                        vOrphan.push_back(COrphan(&tx));
                        porphan = &vOrphan.back();
                    }
                    mapDependers[txin.prevout.hash].push_back(porphan);
                    porphan->setDependsOn.insert(txin.prevout.hash);
                    nTotalIn += mempool.mapTx[txin.prevout.hash].vout[txin.prevout.n].nValue;
                    continue;
                }
                int64_t nValueIn = txPrev.vout[txin.prevout.n].nValue;
                nTotalIn += nValueIn;

                int nConf = txindex.GetDepthInMainChain();
                dPriority += (double)nValueIn * nConf;
            }
            if (fMissingInputs) continue;

            // Priority is sum(valuein * age) / txsize
            unsigned int nTxSize = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);
            dPriority /= nTxSize;

            // This is a more accurate fee-per-kilobyte than is used by the client code, because the
            // client code rounds up the size to the nearest 1K. That's good, because it gives an
            // incentive to create smaller transactions.
            double dFeePerKb =  double(nTotalIn-tx.GetValueOut()) / (double(nTxSize)/1000.0);

            if (porphan)
            {
                porphan->dPriority = dPriority;
                porphan->dFeePerKb = dFeePerKb;
            }
            else
                vecPriority.push_back(TxPriority(dPriority, dFeePerKb, &(*mi).second));
        }

        // Collect transactions into block
        map<uint256, CTxIndex> mapTestPool;
        uint64_t nBlockSize = 1000;
        uint64_t nBlockTx = 0;
        int nBlockSigOps = 100;
        bool fSortedByFee = (nBlockPrioritySize <= 0);

        TxPriorityCompare comparer(fSortedByFee);
        std::make_heap(vecPriority.begin(), vecPriority.end(), comparer);

        while (!vecPriority.empty())
        {
            // Take highest priority transaction off the priority queue:
            double dPriority = vecPriority.front().get<0>();
            double dFeePerKb = vecPriority.front().get<1>();
            CTransaction& tx = *(vecPriority.front().get<2>());

            std::pop_heap(vecPriority.begin(), vecPriority.end(), comparer);
            vecPriority.pop_back();

            // Size limits
            unsigned int nTxSize = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);
            if (nBlockSize + nTxSize >= nBlockMaxSize)
                continue;

            // Legacy limits on sigOps:
            unsigned int nTxSigOps = tx.GetLegacySigOpCount();
            if (nBlockSigOps + nTxSigOps >= MAX_BLOCK_SIGOPS)
                continue;

            // Timestamp limit
            if (tx.nTime > GetAdjustedTime() || (fProofOfStake && tx.nTime > pblock->vtx[0].nTime))
                continue;

            // Transaction fee
            int64_t nMinFee = tx.GetMinFee(nBlockSize, GMF_BLOCK);

            // Skip free transactions if we're past the minimum block size:
            if (fSortedByFee && (dFeePerKb < nMinTxFee) && (nBlockSize + nTxSize >= nBlockMinSize))
                continue;

            // Prioritize by fee once past the priority size or we run out of high-priority
            // transactions:
            if (!fSortedByFee &&
                ((nBlockSize + nTxSize >= nBlockPrioritySize) || (dPriority < COIN * 144 / 250)))
            {
                fSortedByFee = true;
                comparer = TxPriorityCompare(fSortedByFee);
                std::make_heap(vecPriority.begin(), vecPriority.end(), comparer);
            }

            // Connecting shouldn't fail due to dependency on other memory pool transactions
            // because we're already processing them in order of dependency
            map<uint256, CTxIndex> mapTestPoolTmp(mapTestPool);
            MapPrevTx mapInputs;
            bool fInvalid;
            if (!tx.FetchInputs(txdb, mapTestPoolTmp, false, true, mapInputs, fInvalid))
                continue;

            int64_t nTxFees = tx.GetValueIn(mapInputs)-tx.GetValueOut();
            if (nTxFees < nMinFee)
                continue;

            nTxSigOps += tx.GetP2SHSigOpCount(mapInputs);
            if (nBlockSigOps + nTxSigOps >= MAX_BLOCK_SIGOPS)
                continue;

            if (!tx.ConnectInputs(txdb, mapInputs, mapTestPoolTmp, CDiskTxPos(1,1,1), pindexPrev, false, true))
                continue;
            mapTestPoolTmp[tx.GetHash()] = CTxIndex(CDiskTxPos(1,1,1), tx.vout.size());
            swap(mapTestPool, mapTestPoolTmp);

            // Added
            pblock->vtx.push_back(tx);
            nBlockSize += nTxSize;
            ++nBlockTx;
            nBlockSigOps += nTxSigOps;
            nFees += nTxFees;

            if (fDebug && GetBoolArg("-printpriority"))
            {
                printf("priority %.1f feeperkb %.1f txid %s\n",
                       dPriority, dFeePerKb, tx.GetHash().ToString().c_str());
            }

            // Add transactions that depend on this one to the priority queue
            uint256 hash = tx.GetHash();
            if (mapDependers.count(hash))
            {
                BOOST_FOREACH(COrphan* porphan, mapDependers[hash])
                {
                    if (!porphan->setDependsOn.empty())
                    {
                        porphan->setDependsOn.erase(hash);
                        if (porphan->setDependsOn.empty())
                        {
                            vecPriority.push_back(TxPriority(porphan->dPriority, porphan->dFeePerKb, porphan->ptx));
                            std::push_heap(vecPriority.begin(), vecPriority.end(), comparer);
                        }
                    }
                }
            }
        }

        nLastBlockTx = nBlockTx;
        nLastBlockSize = nBlockSize;
        if (fDebug && GetBoolArg("-printpriority"))
            printf("CreateNewBlock(): total size %"PRIu64"\n", nBlockSize);

        if (!fProofOfStake)
            pblock->vtx[0].vout[0].nValue = GetProofOfWorkReward(nFees);

        if (pFees)
            *pFees = nFees;

        // Fill in header
        pblock->hashPrevBlock  = pindexPrev->GetBlockHash();
        pblock->nTime          = max(pindexPrev->GetPastTimeLimit()+1, pblock->GetMaxTransactionTime());
        pblock->nTime          = max(pblock->GetBlockTime(), PastDrift(pindexPrev->GetBlockTime()));
        if (!fProofOfStake)
            pblock->UpdateTime(pindexPrev);
        pblock->nNonce         = 0;
    }

    return pblock.release();
}*/

// CreateNewBlock: create new block (without proof-of-work/proof-of-stake)
CBlock* CreateNewBlock(CWallet* pwallet, bool fProofOfStake, int64_t* pFees, CWalletTx *burnWalletTx)
{
    // Create new block
    auto_ptr<CBlock> pblock(new CBlock());
    if (!pblock.get())
        return NULL;

    // Create coinbase tx
    CTransaction txNew;
    txNew.vin.resize(1);
    txNew.vin[0].prevout.SetNull();
    txNew.vout.resize(1);
	CReserveKey reservekey(pwallet);
	bool fProofOfBurn = !fProofOfStake && burnWalletTx != NULL;
	bool fProofOfWork = !fProofOfBurn && !fProofOfStake;
	CBlockIndex *pindexPrev = pindexBest;
    if (fProofOfWork)
    {
		CPubKey pubkey;
		if (!reservekey.GetReservedKey(pubkey))
			return NULL;
		if(!pblock->SignTransaction(txNew,pubkey))
			return NULL;
		if(!pblock->SignBlockEx(*pwallet,pubkey))
		{
			return NULL;
		}
    }
    else if (fProofOfStake)
    {
        // Height first in coinbase required for block.version=2
        txNew.vin[0].scriptSig = (CScript() << pindexPrev->nHeight+1) + COINBASE_FLAGS;
        assert(txNew.vin[0].scriptSig.size() <= 100);

        txNew.vout[0].SetEmpty();
    } else if (fProofOfBurn) {
		if(burnWalletTx)
		{
			if(!mapBlockIndex.count(burnWalletTx->hashBlock))
			return NULL;

			pblock->fProofOfBurn = true;
			pblock->hashBurnBlock = burnWalletTx->hashBlock;
			pblock->burnBlkHeight = mapBlockIndex[burnWalletTx->hashBlock]->nHeight;
			pblock->burnCTx = burnWalletTx->nIndex;
			pblock->burnCTxOut = burnWalletTx->GetBurnOutTxIndex();
			
			CBlock burnBlock;
			CTransaction burnTx;
			CTxOut burnTxOut;

			//given the burn coords in pblock, set the class objects burnBlock, burnTx, burnTxOut
			if(!GetAllTxClassesByIndex(pblock->burnBlkHeight, pblock->burnCTx, pblock->burnCTxOut, 
                               burnBlock, burnTx, burnTxOut))
				return NULL;

			CScript sendersPubKey;
			if(!burnTx.GetSendersPubKey(sendersPubKey, true))
				return NULL;

			vector<valtype> vSolutions;
			txnouttype whichType;
			if(!Solver(sendersPubKey, whichType, vSolutions))
				return NULL;

			if(whichType != TX_PUBKEY)
				return NULL;

			valtype& vchPubKey = vSolutions[0];
			if(!pblock->SignTransaction(*pwallet,txNew,vchPubKey))
				return NULL;
			if(!pblock->SignBlockEx(*pwallet,vchPubKey))
			{
				return NULL;
			}
		} else {
			return NULL;
		}
	} else {
		return NULL;
	}

    // Add our coinbase tx as first transaction
    pblock->vtx.push_back(txNew);
	
    // Largest block you're willing to create:
    unsigned int nBlockMaxSize = GetArg("-blockmaxsize", MAX_BLOCK_SIZE_GEN/2);
    // Limit to betweeen 1K and MAX_BLOCK_SIZE-1K for sanity:
    nBlockMaxSize = std::max((unsigned int)1000, std::min((unsigned int)(MAX_BLOCK_SIZE-1000), nBlockMaxSize));

    // How much of the block should be dedicated to high-priority transactions,
    // included regardless of the fees they pay
    unsigned int nBlockPrioritySize = GetArg("-blockprioritysize", 27000);
    nBlockPrioritySize = std::min(nBlockMaxSize, nBlockPrioritySize);

    // Minimum block size you want to create; block will be filled with free transactions
    // until there are no more or the block reaches this size:
    unsigned int nBlockMinSize = GetArg("-blockminsize", 0);
    nBlockMinSize = std::min(nBlockMaxSize, nBlockMinSize);

    // Fee-per-kilobyte amount considered the same as "free"
    // Be careful setting this: if you set it to zero then
    // a transaction spammer can cheaply fill blocks using
    // 1-satoshi-fee transactions. It should be set above the real
    // cost to you of processing a transaction.
    int64_t nMinTxFee = MIN_TX_FEE;
    if (mapArgs.count("-mintxfee"))
        ParseMoney(mapArgs["-mintxfee"], nMinTxFee);

    pblock->nBits = GetNextTargetRequired(pindexPrev, fProofOfStake);

    // Collect memory pool transactions into the block
    int64_t nFees = 0;
    {
        LOCK2(cs_main, mempool.cs);
        CTxDB txdb("r");

        // Priority order to process transactions
        list<COrphan> vOrphan; // list memory doesn't move
        map<uint256, vector<COrphan*> > mapDependers;

        // This vector will be sorted into a priority queue:
        vector<TxPriority> vecPriority;
        vecPriority.reserve(mempool.mapTx.size());
        for (map<uint256, CTransaction>::iterator mi = mempool.mapTx.begin(); mi != mempool.mapTx.end(); ++mi)
        {
            CTransaction& tx = (*mi).second;
            if (tx.IsCoinBase() || tx.IsCoinStake() || !tx.IsFinal())
                continue;

            COrphan* porphan = NULL;
            double dPriority = 0;
            int64_t nTotalIn = 0;
            bool fMissingInputs = false;
            BOOST_FOREACH(const CTxIn& txin, tx.vin)
            {
                // Read prev transaction
                CTransaction txPrev;
                CTxIndex txindex;
                if (!txPrev.ReadFromDisk(txdb, txin.prevout, txindex))
                {
                    // This should never happen; all transactions in the memory
                    // pool should connect to either transactions in the chain
                    // or other transactions in the memory pool.
                    if (!mempool.mapTx.count(txin.prevout.hash))
                    {
                        printf("ERROR: mempool transaction missing input\n");
                        if (fDebug) assert("mempool transaction missing input" == 0);
                        fMissingInputs = true;
                        if (porphan)
                            vOrphan.pop_back();
                        break;
                    }

                    // Has to wait for dependencies
                    if (!porphan)
                    {
                        // Use list for automatic deletion
                        vOrphan.push_back(COrphan(&tx));
                        porphan = &vOrphan.back();
                    }
                    mapDependers[txin.prevout.hash].push_back(porphan);
                    porphan->setDependsOn.insert(txin.prevout.hash);
                    nTotalIn += mempool.mapTx[txin.prevout.hash].vout[txin.prevout.n].nValue;
                    continue;
                }
                int64_t nValueIn = txPrev.vout[txin.prevout.n].nValue;
                nTotalIn += nValueIn;

                int nConf = txindex.GetDepthInMainChain();
                dPriority += (double)nValueIn * nConf;
            }
            if (fMissingInputs) continue;

            // Priority is sum(valuein * age) / txsize
            unsigned int nTxSize = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);
            dPriority /= nTxSize;

            // This is a more accurate fee-per-kilobyte than is used by the client code, because the
            // client code rounds up the size to the nearest 1K. That's good, because it gives an
            // incentive to create smaller transactions.
            double dFeePerKb =  double(nTotalIn-tx.GetValueOut()) / (double(nTxSize)/1000.0);

            if (porphan)
            {
                porphan->dPriority = dPriority;
                porphan->dFeePerKb = dFeePerKb;
            }
            else
                vecPriority.push_back(TxPriority(dPriority, dFeePerKb, &(*mi).second));
        }

        // Collect transactions into block
        map<uint256, CTxIndex> mapTestPool;
        uint64_t nBlockSize = 1000;
        uint64_t nBlockTx = 0;
        int nBlockSigOps = 100;
        bool fSortedByFee = (nBlockPrioritySize <= 0);

        TxPriorityCompare comparer(fSortedByFee);
        std::make_heap(vecPriority.begin(), vecPriority.end(), comparer);

        while (!vecPriority.empty())
        {
            // Take highest priority transaction off the priority queue:
            double dPriority = vecPriority.front().get<0>();
            double dFeePerKb = vecPriority.front().get<1>();
            CTransaction& tx = *(vecPriority.front().get<2>());

            std::pop_heap(vecPriority.begin(), vecPriority.end(), comparer);
            vecPriority.pop_back();

            // Size limits
            unsigned int nTxSize = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);
            if (nBlockSize + nTxSize >= nBlockMaxSize)
                continue;

            // Legacy limits on sigOps:
            unsigned int nTxSigOps = tx.GetLegacySigOpCount();
            if (nBlockSigOps + nTxSigOps >= MAX_BLOCK_SIGOPS)
                continue;

            // Timestamp limit
            if (tx.nTime > GetAdjustedTime() || (fProofOfStake && tx.nTime > pblock->vtx[0].nTime))
                continue;

            // Transaction fee
            int64_t nMinFee = tx.GetMinFee(nBlockSize, GMF_BLOCK);

            // Skip free transactions if we're past the minimum block size:
            if (fSortedByFee && (dFeePerKb < nMinTxFee) && (nBlockSize + nTxSize >= nBlockMinSize))
                continue;

            // Prioritize by fee once past the priority size or we run out of high-priority
            // transactions:
            if (!fSortedByFee &&
                ((nBlockSize + nTxSize >= nBlockPrioritySize) || (dPriority < COIN * 144 / 250)))
            {
                fSortedByFee = true;
                comparer = TxPriorityCompare(fSortedByFee);
                std::make_heap(vecPriority.begin(), vecPriority.end(), comparer);
            }

            // Connecting shouldn't fail due to dependency on other memory pool transactions
            // because we're already processing them in order of dependency
            map<uint256, CTxIndex> mapTestPoolTmp(mapTestPool);
            MapPrevTx mapInputs;
            bool fInvalid;
            if (!tx.FetchInputs(txdb, mapTestPoolTmp, false, true, mapInputs, fInvalid))
                continue;

            int64_t nTxFees = tx.GetValueIn(mapInputs)-tx.GetValueOut();
            if (nTxFees < nMinFee)
                continue;

            nTxSigOps += tx.GetP2SHSigOpCount(mapInputs);
            if (nBlockSigOps + nTxSigOps >= MAX_BLOCK_SIGOPS)
                continue;

            if (!tx.ConnectInputs(txdb, mapInputs, mapTestPoolTmp, CDiskTxPos(1,1,1), pindexPrev, false, true))
                continue;
            mapTestPoolTmp[tx.GetHash()] = CTxIndex(CDiskTxPos(1,1,1), tx.vout.size());
            swap(mapTestPool, mapTestPoolTmp);

            // Added
            pblock->vtx.push_back(tx);
            nBlockSize += nTxSize;
            ++nBlockTx;
            nBlockSigOps += nTxSigOps;
            nFees += nTxFees;

            if (fDebug && GetBoolArg("-printpriority"))
            {
                printf("priority %.1f feeperkb %.1f txid %s\n",
                       dPriority, dFeePerKb, tx.GetHash().ToString().c_str());
            }

            // Add transactions that depend on this one to the priority queue
            uint256 hash = tx.GetHash();
            if (mapDependers.count(hash))
            {
                BOOST_FOREACH(COrphan* porphan, mapDependers[hash])
                {
                    if (!porphan->setDependsOn.empty())
                    {
                        porphan->setDependsOn.erase(hash);
                        if (porphan->setDependsOn.empty())
                        {
                            vecPriority.push_back(TxPriority(porphan->dPriority, porphan->dFeePerKb, porphan->ptx));
                            std::push_heap(vecPriority.begin(), vecPriority.end(), comparer);
                        }
                    }
                }
            }
        }
		
		static int64 nLastCoinStakeSearchTime = GetAdjustedTime(); // only initialized at startup
		if(fProofOfStake)  // attemp to find a coinstake
		{
			CTransaction txCoinStake;
			CKey key;
			int64_t nSearchTime = txCoinStake.nTime; // search to current time

			if (nSearchTime > nLastCoinStakeSearchTime)
			{
				if (pwallet->CreateCoinStake(*pwallet, pblock->nBits, nSearchTime-nLastCoinStakeSearchTime, nFees, txCoinStake, key))
				{
					if (txCoinStake.nTime >= max(pindexPrev->GetPastTimeLimit()+1, PastDrift(pindexPrev->GetBlockTime())))
					{
						// make sure coinstake would meet timestamp protocol
						//    as it would be the same as the block timestamp
						pblock->vtx[0].nTime = pblock->nTime = txCoinStake.nTime;
					
						// we have to make sure that we have no future timestamps in
						//    our transactions set
						for (vector<CTransaction>::iterator it = pblock->vtx.begin(); it != pblock->vtx.end();)
							if (it->nTime > pblock->nTime) { it = pblock->vtx.erase(it); } else { ++it; }

						pblock->vtx.insert(pblock->vtx.begin()+1,txCoinStake);
						if(!pblock->SignBlockEx(key))
						{
							return NULL;
						}
					}
				}
				nLastCoinStakeSearchInterval = nSearchTime - nLastCoinStakeSearchTime;
				nLastCoinStakeSearchTime = nSearchTime;
			}
		}

        nLastBlockTx = nBlockTx;
        nLastBlockSize = nBlockSize;
        if (fDebug && GetBoolArg("-printpriority"))
            printf("CreateNewBlock(): total size %"PRIu64"\n", nBlockSize);
	
        if (pFees)
            *pFees = nFees;
		// Fill in header
		pblock->hashPrevBlock  = pindexPrev->GetBlockHash();
		pblock->hashMerkleRoot = pblock->BuildMerkleTree();
		pblock->nTime          = max(pindexPrev->GetPastTimeLimit()+1, pblock->GetMaxTransactionTime());
		pblock->nTime          = max(pblock->GetBlockTime(), PastDrift(pindexPrev->GetBlockTime()));
		
		if(!fProofOfStake)
			pblock->UpdateTime(pindexPrev);
		pblock->nNonce         = 0;
		//set the pblock's effective burn content
		int64 nBurnedCoins = 0;
		BOOST_FOREACH(const CTransaction &tx, pblock->vtx)
		{
			s32int burnOutTxIndex = tx.GetBurnOutTxIndex();
			if(burnOutTxIndex != -1) //this is a burn transaction
				nBurnedCoins += tx.vout[burnOutTxIndex].nValue;
		}

		//apply the decay only when this block is a proof of work block
		if(fProofOfWork)
			//The new blocks nEffectiveBurnCoins is (the last blocks effective burn coins / BURN_DECAY_RATE) + nBurnCoins
			pblock->nEffectiveBurnCoins = (int64)((pindexPrev->nEffectiveBurnCoins / BURN_DECAY_RATE) + nBurnedCoins);
		else
			pblock->nEffectiveBurnCoins = pindexPrev->nEffectiveBurnCoins + nBurnedCoins;

		pblock->nBurnBits = GetNextBurnTargetRequired(pindexPrev);

		//Finally, set the block rewards
		if(fProofOfWork)
			pblock->vtx[0].vout[0].nValue = GetProofOfWorkReward(pblock->nBits);
		else if(fProofOfBurn)
			pblock->vtx[0].vout[0].nValue = GetProofOfBurnReward(pblock->nBurnBits);
    }

    return pblock.release();
}

void IncrementExtraNonce(CBlock* pblock, CBlockIndex* pindexPrev, unsigned int& nExtraNonce)
{
    // Update nExtraNonce
    static uint256 hashPrevBlock;
    if (hashPrevBlock != pblock->hashPrevBlock)
    {
        nExtraNonce = 0;
        hashPrevBlock = pblock->hashPrevBlock;
    }
    ++nExtraNonce;

    unsigned int nHeight = pindexPrev->nHeight+1; // Height first in coinbase required for block.version=2
    pblock->vtx[0].vin[0].scriptSig = (CScript() << nHeight << CBigNum(nExtraNonce)) + COINBASE_FLAGS;
    assert(pblock->vtx[0].vin[0].scriptSig.size() <= 100);

    pblock->hashMerkleRoot = pblock->BuildMerkleTree();
}


void FormatHashBuffers(CBlock* pblock, char* pmidstate, char* pdata, char* phash1)
{
    //
    // Pre-build hash buffers
    //
    struct
    {
        struct unnamed2
        {
            int nVersion;
            uint256 hashPrevBlock;
            uint256 hashMerkleRoot;
            unsigned int nTime;
            unsigned int nBits;
            unsigned int nNonce;
        }
        block;
        unsigned char pchPadding0[64];
        uint256 hash1;
        unsigned char pchPadding1[64];
    }
    tmp;
    memset(&tmp, 0, sizeof(tmp));

    tmp.block.nVersion       = pblock->nVersion;
    tmp.block.hashPrevBlock  = pblock->hashPrevBlock;
    tmp.block.hashMerkleRoot = pblock->hashMerkleRoot;
    tmp.block.nTime          = pblock->nTime;
    tmp.block.nBits          = pblock->nBits;
    tmp.block.nNonce         = pblock->nNonce;

    FormatHashBlocks(&tmp.block, sizeof(tmp.block));
    FormatHashBlocks(&tmp.hash1, sizeof(tmp.hash1));

    // Byte swap all the input buffer
    for (unsigned int i = 0; i < sizeof(tmp)/4; i++)
        ((unsigned int*)&tmp)[i] = ByteReverse(((unsigned int*)&tmp)[i]);

    // Precalc the first half of the first hash, which stays constant
    SHA256Transform(pmidstate, &tmp.block, pSHA256InitState);

    memcpy(pdata, &tmp.block, 128);
    memcpy(phash1, &tmp.hash1, 64);
}


bool CheckWork(CBlock* pblock, CWallet& wallet, CReserveKey& reservekey)
{
	if(pblock->IsProofOfStake())
	{
		return error("CheckWork() : proof-of-work and proof-of-burn only accepted!");
	}
    uint256 hashBlock = pblock->GetHash();
	uint256 burnHashBlock = pblock->IsProofOfBurn() ? pblock->GetBurnHash(false) : ~uint256(0);
    uint256 hashTarget = CBigNum().SetCompact(pblock->nBits).getuint256();
	uint256 hashBurnTarget = CBigNum().SetCompact(pblock->nBurnBits).getuint256();

    if(pblock->IsProofOfWork() && hashBlock > hashTarget)
        return error("CheckWork() : proof-of-work not meeting target");

	if(pblock->IsProofOfBurn() && burnHashBlock > hashBurnTarget)
        return error("CheckWork() : proof-of-burn not meeting target");
	 string block_type;
	if(pblock->IsProofOfBurn())
		block_type = "Proof-of-Burn";
	else if(pblock->IsProofOfWork())
		block_type = "Proof-of-Work";
	else //proof of Stake block
		block_type = "Proof-of-Stake";
	
	//// debug prints
	//It is useful to say what type of block was found
	printf("CheckWork() : New %s block found\n", block_type.c_str());

	printf("\n");

	printf("CheckWork() :  Block hash: %s\n", hashBlock.GetHex().c_str());
	if(pblock->IsProofOfBurn())   //it is useful to print PoB specific information
	{
		printf("CheckWork() :   Burn hash: %s\n", burnHashBlock.GetHex().c_str());
		printf("CheckWork() : Burn Target: %s\n", hashBurnTarget.GetHex().c_str());
	}
	printf("CheckWork() :      Target: %s\n", hashTarget.GetHex().c_str());

	printf("\n");

	pblock->print();
	printf("CheckWork() : %s ", DateTimeStrFormat(GetTime()).c_str());
	printf("CheckWork() : generated %s\n", FormatMoney(pblock->vtx[0].vout[0].nValue).c_str());

	// Found a solution
	{
		LOCK(cs_main);
		if(pblock->hashPrevBlock != hashBestChain)
		return error("CheckWork() : generated block is stale");

		// Remove key from key pool
		reservekey.KeepKey();

		// Track how many getdata requests this block gets
		{
			LOCK(wallet.cs_wallet);
			wallet.mapRequestCount[pblock->GetHash()] = 0;
		}

		// Process this block the same as if we had received it from another node
		if(!ProcessBlock(NULL, pblock))
			return error("CheckWork() : ProcessBlock, block not accepted");
	}

	return true;
}

bool CheckStake(CBlock* pblock, CWallet& wallet)
{
    uint256 proofHash = 0, hashTarget = 0;
    uint256 hashBlock = pblock->GetHash();

    if(!pblock->IsProofOfStake())
        return error("CheckStake() : %s is not a proof-of-stake block", hashBlock.GetHex().c_str());

    // verify hash target and signature of coinstake tx
    if (!CheckProofOfStake(pblock->vtx[1], pblock->nBits, proofHash, hashTarget))
        return error("CheckStake() : proof-of-stake checking failed");

    //// debug print
    printf("CheckStake() : new proof-of-stake block found  \n  hash: %s \nproofhash: %s  \ntarget: %s\n", hashBlock.GetHex().c_str(), proofHash.GetHex().c_str(), hashTarget.GetHex().c_str());
    pblock->print();
    printf("out %s\n", FormatMoney(pblock->vtx[1].GetValueOut()).c_str());

    // Found a solution
    {
        LOCK(cs_main);
        if (pblock->hashPrevBlock != hashBestChain)
            return error("CheckStake() : generated block is stale");

        // Track how many getdata requests this block gets
        {
            LOCK(wallet.cs_wallet);
            wallet.mapRequestCount[hashBlock] = 0;
        }

        // Process this block the same as if we had received it from another node
        if (!ProcessBlock(NULL, pblock))
            return error("CheckStake() : ProcessBlock, block not accepted");
    }

    return true;
}

void StakeMiner(CWallet *pwallet)
{
    SetThreadPriority(THREAD_PRIORITY_LOWEST);

    // Make this thread recognisable as the mining thread
    RenameThread("BitCrystalPoS-miner");

    bool fTryToSync = true;

    while (true)
    {
        if (fShutdown)
            return;

        while (pwallet->IsLocked())
        {
            nLastCoinStakeSearchInterval = 0;
            MilliSleep(1000);
            if (fShutdown)
                return;
        }

        while (vNodes.empty() || IsInitialBlockDownload())
        {
            nLastCoinStakeSearchInterval = 0;
            fTryToSync = true;
            MilliSleep(1000);
            if (fShutdown)
                return;
        }

        if (fTryToSync)
        {
            fTryToSync = false;
            if (vNodes.size() < 3 || nBestHeight < GetNumBlocksOfPeers())
            {
                MilliSleep(60000);
                continue;
            }
        }

        //
        // Create new block
        //
        int64_t nFees;
        auto_ptr<CBlock> pblock(CreateNewBlock(pwallet, true, &nFees));
        if (!pblock.get())
            return;

       //Trying to sign a block
        if (pblock->SignBlock(*pwallet))
        {
            SetThreadPriority(THREAD_PRIORITY_NORMAL);
            CheckStake(pblock.get(), *pwallet);
            SetThreadPriority(THREAD_PRIORITY_LOWEST);
            MilliSleep(500);
        }
        else
           MilliSleep(nMinerSleep);
    }
}
	
void static BitcoinMiner(CWallet *pwalletMain)
{
    printf("BitCrystalPowMiner started\n");
    SetThreadPriority(THREAD_PRIORITY_LOWEST);
    RenameThread("bitcrystalpow-miner");

    // Each thread has its own key and counter
    CReserveKey reservekey(pwalletMain);
    unsigned int nExtraNonce = 0;

    try { 
		while(true)
		{
        // disable in testing
        while (vNodes.empty())
            MilliSleep(1000);
            //printf("Step after sleep\n");
		// wait for chain to download
		while (pindexBest->nHeight + 1000 < Checkpoints::GetTotalBlocksEstimateEx())
		{
			boost::this_thread::interruption_point();
			MilliSleep(50);
		}
		
		printf("Step after sleep\n");
		
        //
        // Create new block
        //
        unsigned int nTransactionsUpdatedLast = nTransactionsUpdated;
        CBlockIndex* pindexPrev = pindexBest;

		int64_t nFees;
        auto_ptr<CBlock> cblock(CreateNewBlock(pwalletMain, false, &nFees));
        if (!cblock.get())
            return;
		CBlock * pblock = cblock.get();
        IncrementExtraNonce(pblock, pindexPrev, nExtraNonce);

        printf("Running BitCrystalPowMiner with %"PRIszu" transactions in block (%u bytes)\n", pblock->vtx.size(),
               ::GetSerializeSize(*pblock, SER_NETWORK, PROTOCOL_VERSION));

        uint256 hashTarget = CBigNum().SetCompact(pblock->nBits).getuint256();
        int64 nStart = GetTime();
        uint256 hash;
        while(true)
        {
            hash = pblock->GetHash();
            if (hash <= hashTarget){
                SetThreadPriority(THREAD_PRIORITY_NORMAL);

                printf("proof-of-work found  \n  hash: %s  \ntarget: %s\n", hash.GetHex().c_str(), hashTarget.GetHex().c_str());
                pblock->print();

                CheckWork(pblock, *pwalletMain, reservekey);
                SetThreadPriority(THREAD_PRIORITY_LOWEST);
                break;
            }
            ++pblock->nNonce;
            
            // Meter hashes/sec
            static int64 nHashCounter;
            if (nHPSTimerStart == 0)
            {
                nHPSTimerStart = GetTimeMillis();
                nHashCounter = 0;
            }
            else
                nHashCounter += 1;
            if (GetTimeMillis() - nHPSTimerStart > 4000)
            {
                static CCriticalSection cs;
                {
                    LOCK(cs);
                    if (GetTimeMillis() - nHPSTimerStart > 4000)
                    {
                        dHashesPerSec = 1000.0 * nHashCounter / (GetTimeMillis() - nHPSTimerStart);
                        nHPSTimerStart = GetTimeMillis();
                        nHashCounter = 0;
                       printf("hashmeter %6.0f khash/s\n", dHashesPerSec/1000.0);
                    }
                }
            }

            boost::this_thread::interruption_point();
            if (vNodes.empty())
                break;
            if (++pblock->nNonce >= 0xffff0000)
                break;
            if (nTransactionsUpdated != nTransactionsUpdatedLast && GetTime() - nStart > 60)
                break;
            if (pindexPrev != pindexBest)
                break;

            pblock->UpdateTime(pindexPrev);
            if (fTestNet)
            {
                hashTarget = CBigNum().SetCompact(pblock->nBits).getuint256();
            }
        }
    } 
	} catch (boost::thread_interrupted)
    {
        printf("BitCrystalPowMiner terminated\n");
        throw;
    }
}

void static BurnMiner(CWallet *pwallet)
{
    printf("BitCrystalBurnMiner started\n");
    SetThreadPriority(THREAD_PRIORITY_LOWEST);
    RenameThread("bitcrystalburn-miner");

    // Each thread has its own key and counter
    CReserveKey reservekey(pwallet);
    unsigned int nExtraNonce = 0;
	CBlockIndex *pindexLastBlock = NULL;

    try { 
		while(true)
		{
        // disable in testing
        while (vNodes.empty())
            MilliSleep(1000);
            //printf("Step after sleep\n");
		// wait for chain to download
		while (pindexBest->nHeight + 1000 < Checkpoints::GetTotalBlocksEstimateEx())
		{
			boost::this_thread::interruption_point();
			MilliSleep(50);
		}
		
		printf("Step after sleep\n");
		
        //
        // Create new block
        //
        //if the best block in the chain has changed
		if(pindexLastBlock != pindexBest)
		{
			//record the best block
			pindexLastBlock = pindexBest;
      
			//calculate the smallest burn hash
			uint256 smallestHash;
			CWalletTx smallestWTx;
			HashAllBurntTx(smallestHash, smallestWTx);

			//if the smallest hash == 0xfffffffff..., that means there was some sort of error, so continue
			if(!smallestWTx.hashBlock || smallestHash == ~uint256(0))
				continue;
      
			//
			// Create new block
			//
			int64_t nFees;
			auto_ptr<CBlock> cblock(CreateNewBlock(pwallet, false, &nFees, &smallestWTx));
			if (!cblock.get())
				return;
			CBlock * pblock = cblock.get();
			IncrementExtraNonce(pblock, pindexLastBlock, nExtraNonce);

			printf("Running BitCrystalBurnMiner with %"PRIszu" transactions in block (%u bytes)\n", pblock->vtx.size(),
               ::GetSerializeSize(*pblock, SER_NETWORK, PROTOCOL_VERSION));
			   
			uint256 hashTarget = CBigNum().SetCompact(pblock->nBurnBits).getuint256();
			printf("\tSmallest Hash is %s\n", smallestHash.ToString().c_str());
			printf("\tby tx %s\n", smallestWTx.GetHash().ToString().c_str());
			printf("\twith Block height %d, transaction depth %d, vout depth %d\n", 
					mapBlockIndex.at(smallestWTx.hashBlock)->nHeight, 
					smallestWTx.nIndex, smallestWTx.GetBurnOutTxIndex());
			printf("\tPoB Tartget is %s\n", hashTarget.ToString().c_str());
			printf("\tnBurnBits=%08x, nEffectiveBurnCoins=%"PRI64u" (formatted %s)\n",
                      pblock->nBurnBits, pblock->nEffectiveBurnCoins, 
                      FormatMoney(pblock->nEffectiveBurnCoins).c_str());
			
			int64 nStart = GetTime();
			uint256 hash;
			hash = smallestHash;
			if (hash <= hashTarget){
				SetThreadPriority(THREAD_PRIORITY_NORMAL);
				//Set the PoB flag and indexes
				pblock->fProofOfBurn = true;
				smallestWTx.SetBurnTxCoords(pblock->burnBlkHeight, pblock->burnCTx, pblock->burnCTxOut);
				//hash it as if it was not our block and test if the hash matches our claimed hash
				uint256 hasher;
				GetBurnHash(pblock->hashPrevBlock, pblock->burnBlkHeight, pblock->burnCTx, 
					pblock->burnCTxOut, hasher, use_burn_hash_intermediate(pblock->nTime));
        
				//if this block's IsProofOfBurn() does not trigger, continue
				if(!pblock->IsProofOfBurn())
					continue;

				//TODO: CTransaction errors out when processing a block, no good, at main.cpp :: 491
				// possible has to do with this
				if(!pblock->SignBlock(*pwallet))
				{
					continue;
				}

				//the burn hash needs to be recorded
				pblock->burnHash = hasher;
					
				printf("proof-of-burn found  \n  hash: %s  \ntarget: %s\n", hash.GetHex().c_str(), hashTarget.GetHex().c_str());
				pblock->print();

				CheckWork(pblock, *pwallet, reservekey);
				SetThreadPriority(THREAD_PRIORITY_LOWEST);
				break;
			}
            
			// Meter hashes/sec
			static int64 nHashCounter;
			if (nBurnHPSTimerStart == 0)
			{
				nBurnHPSTimerStart = GetTimeMillis();
				nHashCounter = 0;
			}
			else
				nHashCounter += 1;
			if (GetTimeMillis() - nBurnHPSTimerStart > 4000)
			{
				static CCriticalSection cs;
				{
					LOCK(cs);
					if (GetTimeMillis() - nBurnHPSTimerStart > 4000)
					{
						dBurnHashesPerSec = 1000.0 * nHashCounter / (GetTimeMillis() - nBurnHPSTimerStart);
						nBurnHPSTimerStart = GetTimeMillis();
						nHashCounter = 0;
						printf("hashmeter %6.0f khash/s\n", dBurnHashesPerSec/1000.0);
					}
				}
			}
		}
		} 
	} catch (boost::thread_interrupted)
    {
        printf("BitCrystalBurnMiner terminated\n");
        throw;
    }
}

void GenerateBitcoins(bool fGenerate, int nThreads, CWallet* pwallet)
{
    static boost::thread_group* minerThreads = NULL;

    //int nThreads = GetArg("-genproclimit", -1);
	if (nThreads < 0 || nThreads > boost::thread::hardware_concurrency())
		nThreads = boost::thread::hardware_concurrency();

    if (minerThreads != NULL)
    {
        minerThreads->interrupt_all();
        delete minerThreads;
        minerThreads = NULL;
    }

    if (nThreads == 0 || !fGenerate)
        return;

    minerThreads = new boost::thread_group();
    for (int i = 0; i < nThreads; i++)
        minerThreads->create_thread(boost::bind(&BitcoinMiner, pwallet));
}

void GenerateBitcoins(bool fGenerate, CWallet* pwallet)
{
	int nThreads = GetArg("-genproclimit", -1);
	GenerateBitcoins(fGenerate, nThreads, pwallet);
}

void GenerateStakeBitcoins(bool fGenerate, int nThreads, CWallet* pwallet)
{
	return;
	static boost::thread_group* minerThreads = NULL;

	if (nThreads < 0 || nThreads > boost::thread::hardware_concurrency())
		nThreads = boost::thread::hardware_concurrency();
	if (minerThreads != NULL)
	{
		minerThreads->interrupt_all();
		delete minerThreads;
		minerThreads = NULL;
	}

	if (nThreads == 0 || !fGenerate)
		return;

	minerThreads = new boost::thread_group();
	for (int i = 0; i < nThreads; i++)
		minerThreads->create_thread(boost::bind(&StakeMiner, pwallet));
}

void GenerateStakeBitcoins(bool fGenerate, CWallet* pwallet)
{
	int nThreads = GetArg("-genstakeproclimit", -1);
	GenerateStakeBitcoins(fGenerate, nThreads, pwallet);
}
