#include "script_ex.h"

bool CScript::comparePubKeySignature(const CScript &scriptPubKey) const
{
	//Get the solutions for this CScript
	std::vector<valtype> vSolutionsThis;
	txnouttype whichTypeThis;
	if(!Solver(*this, whichTypeThis, vSolutionsThis))
		return false;

	//This CScript must be a TX_PUBKEY
	if(whichTypeThis != TX_PUBKEY)
		return false;

	//Get the solutions for the testing CScript
	std::vector<valtype> vSolutionsTesting;
	txnouttype whichTypeTesting;
	if(!Solver(scriptPubKey, whichTypeTesting, vSolutionsTesting))
		return false;

	//The testing pubkey can be either a TX_PUBKEY or TX_PUBKEYHASH
	if(whichTypeTesting == TX_PUBKEY || whichTypeTesting == TX_PUBKEYHASH)
	{
		CBitcoinAddress thisAddr, testingAddr;
		if(!ExtractAddress(*this, thisAddr))
			return false;
		if(!ExtractAddress(scriptPubKey, testingAddr))
			return false;

		// the testing is a hash, so hash this and compare
			return thisAddr == testingAddr;
	}else
		return false;

}

bool ExtractAddress(const CScript &scriptPubKey, CBitcoinAddress &addressRet)
{
  std::vector<valtype> vSolutions;
  txnouttype whichType;
  if(!Solver(scriptPubKey, whichType, vSolutions))
    return false;

  if(whichType == TX_PUBKEY)
  {
    addressRet.SetPubKey(vSolutions[0]);
    return true;
  }
  else if(whichType == TX_PUBKEYHASH)
  {
    addressRet.SetHash160(uint160(vSolutions[0]));
    return true;
  }
  else if(whichType == TX_SCRIPTHASH)
  {
    addressRet.SetScriptHash160(uint160(vSolutions[0]));
    return true;
  }
  // Multisig txns have more than one address...
  return false;
}

bool ExtractAddresses(const CScript &scriptPubKey, txnouttype &typeRet, std::vector<CBitcoinAddress> &addressRet, int &nRequiredRet)
{
  addressRet.clear();
  typeRet = TX_NONSTANDARD;
  std::vector<valtype> vSolutions;
  if(!Solver(scriptPubKey, typeRet, vSolutions))
    return false;

  if(typeRet == TX_NULL_DATA)
  {
    // This is data, not addresses
    return false;
  }

  if(typeRet == TX_MULTISIG)
  {
    nRequiredRet = vSolutions.front()[0];
    for(unsigned int i = 1; i < vSolutions.size()-1; i++)
    {
      CBitcoinAddress address;
      address.SetPubKey(vSolutions[i]);
      addressRet.push_back(address);
    }
  }
  else
  {
    nRequiredRet = 1;
    CBitcoinAddress address;
    if(typeRet == TX_PUBKEYHASH)
      address.SetHash160(uint160(vSolutions.front()));
    else if(typeRet == TX_SCRIPTHASH)
      address.SetScriptHash160(uint160(vSolutions.front()));
    else if(typeRet == TX_PUBKEY)
      address.SetPubKey(vSolutions.front());
    addressRet.push_back(address);
  }

  return true;
}