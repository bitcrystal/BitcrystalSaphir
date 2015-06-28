#include "bitcoinrpc_extra.h"
void WalletTxToJSON(const CWalletTx& wtx, const json_spirit::Object& entry)
{
    int confirms = wtx.GetDepthInMainChain();
    entry.push_back(PAIR("confirmations", confirms));
    if (wtx.IsCoinBase() || wtx.IsCoinStake())
        entry.push_back(PAIR("generated", true));
    if (confirms > 0)
    {
        entry.push_back(PAIR("blockhash", wtx.hashBlock.GetHex()));
        entry.push_back(PAIR("blockindex", wtx.nIndex));
        entry.push_back(PAIR("blocktime", (int64_t)(mapBlockIndex[wtx.hashBlock]->nTime)));
    }
    entry.push_back(PAIR("txid", wtx.GetHash().GetHex()));
    entry.push_back(PAIR("time", (int64_t)wtx.GetTxTime()));
    entry.push_back(PAIR("timereceived", (int64_t)wtx.nTimeReceived));
    BOOST_FOREACH(const PAIRTYPE(string,string)& item, wtx.mapValue)
        entry.push_back(PAIR(item.first, item.second));
}