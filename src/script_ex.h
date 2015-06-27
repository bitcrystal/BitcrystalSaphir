#ifndef SCRIPT_EX_H
#define SCRIPT_EX_H
#include "base58.h"
bool ExtractAddress(const CScript &scriptPubKey, CBitcoinAddress &addressRet);
bool ExtractAddresses(const CScript &scriptPubKey, txnouttype &typeRet, std::vector<CBitcoinAddress> &addressRet, int &nRequiredRet);
#endif