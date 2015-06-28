#ifndef BITCOINRPC_EXTRA_H
#define BITCOINRPC_EXTRA_H
#include "util.h"
#include "wallet.h"
#include "bitcoinrpc.h"
#define PAIR(t1,t2) json_spirit::Pair(t1,t2)
extern void WalletTxToJSON(const CWalletTx& wtx, const json_spirit::Object& entry);
#endif