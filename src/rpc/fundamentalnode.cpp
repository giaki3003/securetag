// Copyright (c) 2014-2017 The SecureTag Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activefundamentalnode.h"
#include "base58.h"
#include "clientversion.h"
#include "init.h"
#include "netbase.h"
#include "validation.h"
#include "fundamentalnode-payments.h"
#include "fundamentalnode-sync.h"
#include "fundamentalnodeconfig.h"
#include "fundamentalnodeman.h"
#ifdef ENABLE_WALLET
#include "privatesend-client.h"
#endif // ENABLE_WALLET
#include "privatesend-server.h"
#include "rpc/server.h"
#include "util.h"
#include "utilmoneystr.h"

#include <fstream>
#include <iomanip>
#include <univalue.h>

UniValue fundamentalnodelist(const JSONRPCRequest& request);

bool EnsureWalletIsAvailable(bool avoidException);

UniValue fundamentalnode(const JSONRPCRequest& request)
{
    std::string strCommand;
    if (request.params.size() >= 1) {
        strCommand = request.params[0].get_str();
    }

#ifdef ENABLE_WALLET
    if (strCommand == "start-many")
        throw JSONRPCError(RPC_INVALID_PARAMETER, "DEPRECATED, please use start-all instead");
#endif // ENABLE_WALLET

    if (request.fHelp  ||
        (
#ifdef ENABLE_WALLET
            strCommand != "start-alias" && strCommand != "start-all" && strCommand != "start-missing" &&
         strCommand != "start-disabled" && strCommand != "outputs" &&
#endif // ENABLE_WALLET
         strCommand != "list" && strCommand != "list-conf" && strCommand != "count" &&
         strCommand != "debug" && strCommand != "current" && strCommand != "winner" && strCommand != "winners" && strCommand != "genkey" &&
         strCommand != "connect" && strCommand != "status"))
            throw std::runtime_error(
                "fundamentalnode \"command\"...\n"
                "Set of commands to execute fundamentalnode related actions\n"
                "\nArguments:\n"
                "1. \"command\"        (string or set of strings, required) The command to execute\n"
                "\nAvailable commands:\n"
                "  count        - Get information about number of fundamentalnodes (DEPRECATED options: 'total', 'ps', 'enabled', 'qualify', 'all')\n"
                "  current      - Print info on current fundamentalnode winner to be paid the next block (calculated locally)\n"
                "  genkey       - Generate new fundamentalnodeprivkey\n"
#ifdef ENABLE_WALLET
                "  outputs      - Print fundamentalnode compatible outputs\n"
                "  start-alias  - Start single remote fundamentalnode by assigned alias configured in fundamentalnode.conf\n"
                "  start-<mode> - Start remote fundamentalnodes configured in fundamentalnode.conf (<mode>: 'all', 'missing', 'disabled')\n"
#endif // ENABLE_WALLET
                "  status       - Print fundamentalnode status information\n"
                "  list         - Print list of all known fundamentalnodes (see fundamentalnodelist for more info)\n"
                "  list-conf    - Print fundamentalnode.conf in JSON format\n"
                "  winner       - Print info on next fundamentalnode winner to vote for\n"
                "  winners      - Print list of fundamentalnode winners\n"
                );

    if (strCommand == "list")
    {
        JSONRPCRequest newRequest = request;
        newRequest.params.setArray();
        // forward params but skip "list"
        for (unsigned int i = 1; i < request.params.size(); i++) {
            newRequest.params.push_back(request.params[i]);
        }
        return fundamentalnodelist(newRequest);
    }

    if(strCommand == "connect")
    {
        if (request.params.size() < 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Fundamentalnode address required");

        std::string strAddress = request.params[1].get_str();

        CService addr;
        if (!Lookup(strAddress.c_str(), addr, 0, false))
            throw JSONRPCError(RPC_INTERNAL_ERROR, strprintf("Incorrect fundamentalnode address %s", strAddress));

        // TODO: Pass CConnman instance somehow and don't use global variable.
        g_connman->OpenFundamentalnodeConnection(CAddress(addr, NODE_NETWORK));
        if (!g_connman->IsConnected(CAddress(addr, NODE_NETWORK), CConnman::AllNodes))
            throw JSONRPCError(RPC_INTERNAL_ERROR, strprintf("Couldn't connect to fundamentalnode %s", strAddress));

        return "successfully connected";
    }

    if (strCommand == "count")
    {
        if (request.params.size() > 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Too many parameters");

        int nCount;
        fundamentalnode_info_t fnInfo;
        fnodeman.GetNextFundamentalnodeInQueueForPayment(true, nCount, fnInfo);

        int total = fnodeman.size();
        int ps = fnodeman.CountEnabled(MIN_PRIVATESEND_PEER_PROTO_VERSION);
        int enabled = fnodeman.CountEnabled();

        if (request.params.size() == 1) {
            UniValue obj(UniValue::VOBJ);

            obj.push_back(Pair("total", total));
            obj.push_back(Pair("ps_compatible", ps));
            obj.push_back(Pair("enabled", enabled));
            obj.push_back(Pair("qualify", nCount));

            return obj;
        }

        std::string strMode = request.params[1].get_str();

        if (strMode == "total")
            return total;

        if (strMode == "ps")
            return ps;

        if (strMode == "enabled")
            return enabled;

        if (strMode == "qualify")
            return nCount;

        if (strMode == "all")
            return strprintf("Total: %d (PS Compatible: %d / Enabled: %d / Qualify: %d)",
                total, ps, enabled, nCount);
    }

    if (strCommand == "current" || strCommand == "winner")
    {
        int nCount;
        int nHeight;
        fundamentalnode_info_t fnInfo;
        CBlockIndex* pindex = NULL;
        {
            LOCK(cs_main);
            pindex = chainActive.Tip();
        }
        nHeight = pindex->nHeight + (strCommand == "current" ? 1 : 10);
        fnodeman.UpdateLastPaid(pindex);

        if(!fnodeman.GetNextFundamentalnodeInQueueForPayment(nHeight, true, nCount, fnInfo))
            return "unknown";

        UniValue obj(UniValue::VOBJ);

        obj.push_back(Pair("height",        nHeight));
        obj.push_back(Pair("IP:port",       fnInfo.addr.ToString()));
        obj.push_back(Pair("protocol",      fnInfo.nProtocolVersion));
        obj.push_back(Pair("outpoint",      fnInfo.outpoint.ToStringShort()));
        obj.push_back(Pair("payee",         CBitcoinAddress(fnInfo.pubKeyCollateralAddress.GetID()).ToString()));
        obj.push_back(Pair("lastseen",      fnInfo.nTimeLastPing));
        obj.push_back(Pair("activeseconds", fnInfo.nTimeLastPing - fnInfo.sigTime));
        return obj;
    }

#ifdef ENABLE_WALLET
    if (strCommand == "start-alias")
    {
        if (!EnsureWalletIsAvailable(request.fHelp))
            return NullUniValue;

        if (request.params.size() < 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Please specify an alias");

        {
            LOCK(pwalletMain->cs_wallet);
            EnsureWalletIsUnlocked();
        }

        std::string strAlias = request.params[1].get_str();

        bool fFound = false;

        UniValue statusObj(UniValue::VOBJ);
        statusObj.push_back(Pair("alias", strAlias));

        for (const auto& fne : fundamentalnodeConfig.getEntries()) {
            if(fne.getAlias() == strAlias) {
                fFound = true;
                std::string strError;
                CFundamentalnodeBroadcast fnb;

                bool fResult = CFundamentalnodeBroadcast::Create(fne.getIp(), fne.getPrivKey(), fne.getTxHash(), fne.getOutputIndex(), strError, fnb);

                int nDoS;
                if (fResult && !fnodeman.CheckFnbAndUpdateFundamentalnodeList(NULL, fnb, nDoS, *g_connman)) {
                    strError = "Failed to verify FNB";
                    fResult = false;
                }

                statusObj.push_back(Pair("result", fResult ? "successful" : "failed"));
                if(!fResult) {
                    statusObj.push_back(Pair("errorMessage", strError));
                }
                fnodeman.NotifyFundamentalnodeUpdates(*g_connman);
                break;
            }
        }

        if(!fFound) {
            statusObj.push_back(Pair("result", "failed"));
            statusObj.push_back(Pair("errorMessage", "Could not find alias in config. Verify with list-conf."));
        }

        return statusObj;

    }

    if (strCommand == "start-all" || strCommand == "start-missing" || strCommand == "start-disabled")
    {
        if (!EnsureWalletIsAvailable(request.fHelp))
            return NullUniValue;

        {
            LOCK(pwalletMain->cs_wallet);
            EnsureWalletIsUnlocked();
        }

        if((strCommand == "start-missing" || strCommand == "start-disabled") && !fundamentalnodeSync.IsFundamentalnodeListSynced()) {
            throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "You can't use this command until fundamentalnode list is synced");
        }

        int nSuccessful = 0;
        int nFailed = 0;

        UniValue resultsObj(UniValue::VOBJ);

        for (const auto& fne : fundamentalnodeConfig.getEntries()) {
            std::string strError;

            COutPoint outpoint = COutPoint(uint256S(fne.getTxHash()), (uint32_t)atoi(fne.getOutputIndex()));
            CFundamentalnode fn;
            bool fFound = fnodeman.Get(outpoint, fn);
            CFundamentalnodeBroadcast fnb;

            if(strCommand == "start-missing" && fFound) continue;
            if(strCommand == "start-disabled" && fFound && fn.IsEnabled()) continue;

            bool fResult = CFundamentalnodeBroadcast::Create(fne.getIp(), fne.getPrivKey(), fne.getTxHash(), fne.getOutputIndex(), strError, fnb);

            int nDoS;
            if (fResult && !fnodeman.CheckFnbAndUpdateFundamentalnodeList(NULL, fnb, nDoS, *g_connman)) {
                strError = "Failed to verify FNB";
                fResult = false;
            }

            UniValue statusObj(UniValue::VOBJ);
            statusObj.push_back(Pair("alias", fne.getAlias()));
            statusObj.push_back(Pair("result", fResult ? "successful" : "failed"));

            if (fResult) {
                nSuccessful++;
            } else {
                nFailed++;
                statusObj.push_back(Pair("errorMessage", strError));
            }

            resultsObj.push_back(Pair("status", statusObj));
        }
        fnodeman.NotifyFundamentalnodeUpdates(*g_connman);

        UniValue returnObj(UniValue::VOBJ);
        returnObj.push_back(Pair("overall", strprintf("Successfully started %d fundamentalnodes, failed to start %d, total %d", nSuccessful, nFailed, nSuccessful + nFailed)));
        returnObj.push_back(Pair("detail", resultsObj));

        return returnObj;
    }
#endif // ENABLE_WALLET

    if (strCommand == "genkey")
    {
        CKey secret;
        secret.MakeNewKey(false);

        return CBitcoinSecret(secret).ToString();
    }

    if (strCommand == "list-conf")
    {
        UniValue resultObj(UniValue::VOBJ);

        for (const auto& fne : fundamentalnodeConfig.getEntries()) {
            COutPoint outpoint = COutPoint(uint256S(fne.getTxHash()), (uint32_t)atoi(fne.getOutputIndex()));
            CFundamentalnode fn;
            bool fFound = fnodeman.Get(outpoint, fn);

            std::string strStatus = fFound ? fn.GetStatus() : "MISSING";

            UniValue fnObj(UniValue::VOBJ);
            fnObj.push_back(Pair("alias", fne.getAlias()));
            fnObj.push_back(Pair("address", fne.getIp()));
            fnObj.push_back(Pair("privateKey", fne.getPrivKey()));
            fnObj.push_back(Pair("txHash", fne.getTxHash()));
            fnObj.push_back(Pair("outputIndex", fne.getOutputIndex()));
            fnObj.push_back(Pair("status", strStatus));
            resultObj.push_back(Pair("fundamentalnode", fnObj));
        }

        return resultObj;
    }

#ifdef ENABLE_WALLET
    if (strCommand == "outputs") {
        if (!EnsureWalletIsAvailable(request.fHelp))
            return NullUniValue;

        // Find possible candidates
        std::vector<COutput> vPossibleCoins;
        pwalletMain->AvailableCoins(vPossibleCoins, true, NULL, false, ONLY_1000);

        UniValue obj(UniValue::VOBJ);
        for (const auto& out : vPossibleCoins) {
            obj.push_back(Pair(out.tx->GetHash().ToString(), strprintf("%d", out.i)));
        }

        return obj;
    }
#endif // ENABLE_WALLET

    if (strCommand == "status")
    {
        if (!fFundamentalnodeMode)
            throw JSONRPCError(RPC_INTERNAL_ERROR, "This is not a fundamentalnode");

        UniValue fnObj(UniValue::VOBJ);

        fnObj.push_back(Pair("outpoint", activeFundamentalnode.outpoint.ToStringShort()));
        fnObj.push_back(Pair("service", activeFundamentalnode.service.ToString()));

        CFundamentalnode fn;
        if(fnodeman.Get(activeFundamentalnode.outpoint, fn)) {
            fnObj.push_back(Pair("payee", CBitcoinAddress(fn.pubKeyCollateralAddress.GetID()).ToString()));
        }

        fnObj.push_back(Pair("status", activeFundamentalnode.GetStatus()));
        return fnObj;
    }

    if (strCommand == "winners")
    {
        int nHeight;
        {
            LOCK(cs_main);
            CBlockIndex* pindex = chainActive.Tip();
            if(!pindex) return NullUniValue;

            nHeight = pindex->nHeight;
        }

        int nLast = 10;
        std::string strFilter = "";

        if (request.params.size() >= 2) {
            nLast = atoi(request.params[1].get_str());
        }

        if (request.params.size() == 3) {
            strFilter = request.params[2].get_str();
        }

        if (request.params.size() > 3)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Correct usage is 'fundamentalnode winners ( \"count\" \"filter\" )'");

        UniValue obj(UniValue::VOBJ);

        for(int i = nHeight - nLast; i < nHeight + 20; i++) {
            std::string strPayment = GetRequiredPaymentsStringFN(i);
            if (strFilter !="" && strPayment.find(strFilter) == std::string::npos) continue;
            obj.push_back(Pair(strprintf("%d", i), strPayment));
        }

        return obj;
    }

    return NullUniValue;
}

UniValue fundamentalnodelist(const JSONRPCRequest& request)
{
    std::string strMode = "json";
    std::string strFilter = "";

    if (request.params.size() >= 1) strMode = request.params[0].get_str();
    if (request.params.size() == 2) strFilter = request.params[1].get_str();

    if (request.fHelp || (
                strMode != "activeseconds" && strMode != "addr" && strMode != "daemon" && strMode != "full" && strMode != "info" && strMode != "json" &&
                strMode != "lastseen" && strMode != "lastpaidtime" && strMode != "lastpaidblock" &&
                strMode != "protocol" && strMode != "payee" && strMode != "pubkey" &&
                strMode != "rank" && strMode != "sentinel" && strMode != "status"))
    {
        throw std::runtime_error(
                "fundamentalnodelist ( \"mode\" \"filter\" )\n"
                "Get a list of fundamentalnodes in different modes\n"
                "\nArguments:\n"
                "1. \"mode\"      (string, optional/required to use filter, defaults = json) The mode to run list in\n"
                "2. \"filter\"    (string, optional) Filter results. Partial match by outpoint by default in all modes,\n"
                "                                    additional matches in some modes are also available\n"
                "\nAvailable modes:\n"
                "  activeseconds  - Print number of seconds fundamentalnode recognized by the network as enabled\n"
                "                   (since latest issued \"fundamentalnode start/start-many/start-alias\")\n"
                "  addr           - Print ip address associated with a fundamentalnode (can be additionally filtered, partial match)\n"
                "  daemon         - Print daemon version of a fundamentalnode (can be additionally filtered, exact match)\n"
                "  full           - Print info in format 'status protocol payee lastseen activeseconds lastpaidtime lastpaidblock IP'\n"
                "                   (can be additionally filtered, partial match)\n"
                "  info           - Print info in format 'status protocol payee lastseen activeseconds sentinelversion sentinelstate IP'\n"
                "                   (can be additionally filtered, partial match)\n"
                "  json           - Print info in JSON format (can be additionally filtered, partial match)\n"
                "  lastpaidblock  - Print the last block height a node was paid on the network\n"
                "  lastpaidtime   - Print the last time a node was paid on the network\n"
                "  lastseen       - Print timestamp of when a fundamentalnode was last seen on the network\n"
                "  payee          - Print SecureTag address associated with a fundamentalnode (can be additionally filtered,\n"
                "                   partial match)\n"
                "  protocol       - Print protocol of a fundamentalnode (can be additionally filtered, exact match)\n"
                "  pubkey         - Print the fundamentalnode (not collateral) public key\n"
                "  rank           - Print rank of a fundamentalnode based on current block\n"
                "  sentinel       - Print sentinel version of a fundamentalnode (can be additionally filtered, exact match)\n"
                "  status         - Print fundamentalnode status: PRE_ENABLED / ENABLED / EXPIRED / SENTINEL_PING_EXPIRED / NEW_START_REQUIRED /\n"
                "                   UPDATE_REQUIRED / POSE_BAN / OUTPOINT_SPENT (can be additionally filtered, partial match)\n"
                );
    }

    if (strMode == "full" || strMode == "json" || strMode == "lastpaidtime" || strMode == "lastpaidblock") {
        CBlockIndex* pindex = NULL;
        {
            LOCK(cs_main);
            pindex = chainActive.Tip();
        }
        fnodeman.UpdateLastPaid(pindex);
    }

    UniValue obj(UniValue::VOBJ);
    if (strMode == "rank") {
        CFundamentalnodeMan::rank_pair_vec_t vFundamentalnodeRanks;
        fnodeman.GetFundamentalnodeRanks(vFundamentalnodeRanks);
        for (const auto& rankpair : vFundamentalnodeRanks) {
            std::string strOutpoint = rankpair.second.outpoint.ToStringShort();
            if (strFilter !="" && strOutpoint.find(strFilter) == std::string::npos) continue;
            obj.push_back(Pair(strOutpoint, rankpair.first));
        }
    } else {
        std::map<COutPoint, CFundamentalnode> mapFundamentalnodes = fnodeman.GetFullFundamentalnodeMap();
        for (const auto& fnpair : mapFundamentalnodes) {
            CFundamentalnode fn = fnpair.second;
            std::string strOutpoint = fnpair.first.ToStringShort();
            if (strMode == "activeseconds") {
                if (strFilter !="" && strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, (int64_t)(fn.lastPing.sigTime - fn.sigTime)));
            } else if (strMode == "addr") {
                std::string strAddress = fn.addr.ToString();
                if (strFilter !="" && strAddress.find(strFilter) == std::string::npos &&
                    strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, strAddress));
            } else if (strMode == "daemon") {
                std::string strDaemon = fn.lastPing.nDaemonVersion > DEFAULT_DAEMON_VERSION ? FormatVersion(fn.lastPing.nDaemonVersion) : "Unknown";
                if (strFilter !="" && strDaemon.find(strFilter) == std::string::npos &&
                    strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, strDaemon));
            } else if (strMode == "sentinel") {
                std::string strSentinel = fn.lastPing.nSentinelVersion > DEFAULT_SENTINEL_VERSION ? SafeIntVersionToString(fn.lastPing.nSentinelVersion) : "Unknown";
                if (strFilter !="" && strSentinel.find(strFilter) == std::string::npos &&
                    strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, strSentinel));
            } else if (strMode == "full") {
                std::ostringstream streamFull;
                streamFull << std::setw(18) <<
                               fn.GetStatus() << " " <<
                               fn.nProtocolVersion << " " <<
                               CBitcoinAddress(fn.pubKeyCollateralAddress.GetID()).ToString() << " " <<
                               (int64_t)fn.lastPing.sigTime << " " << std::setw(8) <<
                               (int64_t)(fn.lastPing.sigTime - fn.sigTime) << " " << std::setw(10) <<
                               fn.GetLastPaidTime() << " "  << std::setw(6) <<
                               fn.GetLastPaidBlock() << " " <<
                               fn.addr.ToString();
                std::string strFull = streamFull.str();
                if (strFilter !="" && strFull.find(strFilter) == std::string::npos &&
                    strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, strFull));
            } else if (strMode == "info") {
                std::ostringstream streamInfo;
                streamInfo << std::setw(18) <<
                               fn.GetStatus() << " " <<
                               fn.nProtocolVersion << " " <<
                               CBitcoinAddress(fn.pubKeyCollateralAddress.GetID()).ToString() << " " <<
                               (int64_t)fn.lastPing.sigTime << " " << std::setw(8) <<
                               (int64_t)(fn.lastPing.sigTime - fn.sigTime) << " " <<
                               SafeIntVersionToString(fn.lastPing.nSentinelVersion) << " "  <<
                               (fn.lastPing.fSentinelIsCurrent ? "current" : "expired") << " " <<
                               fn.addr.ToString();
                std::string strInfo = streamInfo.str();
                if (strFilter !="" && strInfo.find(strFilter) == std::string::npos &&
                    strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, strInfo));
            } else if (strMode == "json") {
                std::ostringstream streamInfo;
                streamInfo <<  fn.addr.ToString() << " " <<
                               CBitcoinAddress(fn.pubKeyCollateralAddress.GetID()).ToString() << " " <<
                               fn.GetStatus() << " " <<
                               fn.nProtocolVersion << " " <<
                               fn.lastPing.nDaemonVersion << " " <<
                               SafeIntVersionToString(fn.lastPing.nSentinelVersion) << " " <<
                               (fn.lastPing.fSentinelIsCurrent ? "current" : "expired") << " " <<
                               (int64_t)fn.lastPing.sigTime << " " <<
                               (int64_t)(fn.lastPing.sigTime - fn.sigTime) << " " <<
                               fn.GetLastPaidTime() << " " <<
                               fn.GetLastPaidBlock();
                std::string strInfo = streamInfo.str();
                if (strFilter !="" && strInfo.find(strFilter) == std::string::npos &&
                    strOutpoint.find(strFilter) == std::string::npos) continue;
                UniValue objFN(UniValue::VOBJ);
                objFN.push_back(Pair("address", fn.addr.ToString()));
                objFN.push_back(Pair("payee", CBitcoinAddress(fn.pubKeyCollateralAddress.GetID()).ToString()));
                objFN.push_back(Pair("status", fn.GetStatus()));
                objFN.push_back(Pair("protocol", fn.nProtocolVersion));
                objFN.push_back(Pair("daemonversion", fn.lastPing.nDaemonVersion > DEFAULT_DAEMON_VERSION ? FormatVersion(fn.lastPing.nDaemonVersion) : "Unknown"));
                objFN.push_back(Pair("sentinelversion", fn.lastPing.nSentinelVersion > DEFAULT_SENTINEL_VERSION ? SafeIntVersionToString(fn.lastPing.nSentinelVersion) : "Unknown"));
                objFN.push_back(Pair("sentinelstate", (fn.lastPing.fSentinelIsCurrent ? "current" : "expired")));
                objFN.push_back(Pair("lastseen", (int64_t)fn.lastPing.sigTime));
                objFN.push_back(Pair("activeseconds", (int64_t)(fn.lastPing.sigTime - fn.sigTime)));
                objFN.push_back(Pair("lastpaidtime", fn.GetLastPaidTime()));
                objFN.push_back(Pair("lastpaidblock", fn.GetLastPaidBlock()));
                obj.push_back(Pair(strOutpoint, objFN));
            } else if (strMode == "lastpaidblock") {
                if (strFilter !="" && strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, fn.GetLastPaidBlock()));
            } else if (strMode == "lastpaidtime") {
                if (strFilter !="" && strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, fn.GetLastPaidTime()));
            } else if (strMode == "lastseen") {
                if (strFilter !="" && strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, (int64_t)fn.lastPing.sigTime));
            } else if (strMode == "payee") {
                CBitcoinAddress address(fn.pubKeyCollateralAddress.GetID());
                std::string strPayee = address.ToString();
                if (strFilter !="" && strPayee.find(strFilter) == std::string::npos &&
                    strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, strPayee));
            } else if (strMode == "protocol") {
                if (strFilter !="" && strFilter != strprintf("%d", fn.nProtocolVersion) &&
                    strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, fn.nProtocolVersion));
            } else if (strMode == "pubkey") {
                if (strFilter !="" && strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, HexStr(fn.pubKeyFundamentalnode)));
            } else if (strMode == "status") {
                std::string strStatus = fn.GetStatus();
                if (strFilter !="" && strStatus.find(strFilter) == std::string::npos &&
                    strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, strStatus));
            }
        }
    }
    return obj;
}

bool DecodeHexVecFnb(std::vector<CFundamentalnodeBroadcast>& vecFnb, std::string strHexFnb) {

    if (!IsHex(strHexFnb))
        return false;

    std::vector<unsigned char> fnbData(ParseHex(strHexFnb));
    CDataStream ssData(fnbData, SER_NETWORK, PROTOCOL_VERSION);
    try {
        ssData >> vecFnb;
    }
    catch (const std::exception&) {
        return false;
    }

    return true;
}

UniValue fundamentalnodebroadcast(const JSONRPCRequest& request)
{
    std::string strCommand;
    if (request.params.size() >= 1)
        strCommand = request.params[0].get_str();

    if (request.fHelp  ||
        (
#ifdef ENABLE_WALLET
            strCommand != "create-alias" && strCommand != "create-all" &&
#endif // ENABLE_WALLET
            strCommand != "decode" && strCommand != "relay"))
        throw std::runtime_error(
                "fundamentalnodebroadcast \"command\"...\n"
                "Set of commands to create and relay fundamentalnode broadcast messages\n"
                "\nArguments:\n"
                "1. \"command\"        (string or set of strings, required) The command to execute\n"
                "\nAvailable commands:\n"
#ifdef ENABLE_WALLET
                "  create-alias  - Create single remote fundamentalnode broadcast message by assigned alias configured in fundamentalnode.conf\n"
                "  create-all    - Create remote fundamentalnode broadcast messages for all fundamentalnodes configured in fundamentalnode.conf\n"
#endif // ENABLE_WALLET
                "  decode        - Decode fundamentalnode broadcast message\n"
                "  relay         - Relay fundamentalnode broadcast message to the network\n"
                );

#ifdef ENABLE_WALLET
    if (strCommand == "create-alias")
    {
        if (!EnsureWalletIsAvailable(request.fHelp))
            return NullUniValue;

        // wait for reindex and/or import to finish
        if (fImporting || fReindex)
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Wait for reindex and/or import to finish");

        if (request.params.size() < 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Please specify an alias");

        {
            LOCK(pwalletMain->cs_wallet);
            EnsureWalletIsUnlocked();
        }

        bool fFound = false;
        std::string strAlias = request.params[1].get_str();

        UniValue statusObj(UniValue::VOBJ);
        std::vector<CFundamentalnodeBroadcast> vecFnb;

        statusObj.push_back(Pair("alias", strAlias));

        for (const auto& fne : fundamentalnodeConfig.getEntries()) {
            if(fne.getAlias() == strAlias) {
                fFound = true;
                std::string strError;
                CFundamentalnodeBroadcast fnb;

                bool fResult = CFundamentalnodeBroadcast::Create(fne.getIp(), fne.getPrivKey(), fne.getTxHash(), fne.getOutputIndex(), strError, fnb, true);

                statusObj.push_back(Pair("result", fResult ? "successful" : "failed"));
                if(fResult) {
                    vecFnb.push_back(fnb);
                    CDataStream ssVecFnb(SER_NETWORK, PROTOCOL_VERSION);
                    ssVecFnb << vecFnb;
                    statusObj.push_back(Pair("hex", HexStr(ssVecFnb)));
                } else {
                    statusObj.push_back(Pair("errorMessage", strError));
                }
                break;
            }
        }

        if(!fFound) {
            statusObj.push_back(Pair("result", "not found"));
            statusObj.push_back(Pair("errorMessage", "Could not find alias in config. Verify with list-conf."));
        }

        return statusObj;

    }

    if (strCommand == "create-all")
    {
        if (!EnsureWalletIsAvailable(request.fHelp))
            return NullUniValue;

        // wait for reindex and/or import to finish
        if (fImporting || fReindex)
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Wait for reindex and/or import to finish");

        {
            LOCK(pwalletMain->cs_wallet);
            EnsureWalletIsUnlocked();
        }

        int nSuccessful = 0;
        int nFailed = 0;

        UniValue resultsObj(UniValue::VOBJ);
        std::vector<CFundamentalnodeBroadcast> vecFnb;

        for (const auto& fne : fundamentalnodeConfig.getEntries()) {
            std::string strError;
            CFundamentalnodeBroadcast fnb;

            bool fResult = CFundamentalnodeBroadcast::Create(fne.getIp(), fne.getPrivKey(), fne.getTxHash(), fne.getOutputIndex(), strError, fnb, true);

            UniValue statusObj(UniValue::VOBJ);
            statusObj.push_back(Pair("alias", fne.getAlias()));
            statusObj.push_back(Pair("result", fResult ? "successful" : "failed"));

            if(fResult) {
                nSuccessful++;
                vecFnb.push_back(fnb);
            } else {
                nFailed++;
                statusObj.push_back(Pair("errorMessage", strError));
            }

            resultsObj.push_back(Pair("status", statusObj));
        }

        CDataStream ssVecFnb(SER_NETWORK, PROTOCOL_VERSION);
        ssVecFnb << vecFnb;
        UniValue returnObj(UniValue::VOBJ);
        returnObj.push_back(Pair("overall", strprintf("Successfully created broadcast messages for %d fundamentalnodes, failed to create %d, total %d", nSuccessful, nFailed, nSuccessful + nFailed)));
        returnObj.push_back(Pair("detail", resultsObj));
        returnObj.push_back(Pair("hex", HexStr(ssVecFnb.begin(), ssVecFnb.end())));

        return returnObj;
    }
#endif // ENABLE_WALLET

    if (strCommand == "decode")
    {
        if (request.params.size() != 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Correct usage is 'fundamentalnodebroadcast decode \"hexstring\"'");

        std::vector<CFundamentalnodeBroadcast> vecFnb;

        if (!DecodeHexVecFnb(vecFnb, request.params[1].get_str()))
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Fundamentalnode broadcast message decode failed");

        int nSuccessful = 0;
        int nFailed = 0;
        int nDos = 0;
        UniValue returnObj(UniValue::VOBJ);

        for (const auto& fnb : vecFnb) {
            UniValue resultObj(UniValue::VOBJ);

            if(fnb.CheckSignature(nDos)) {
                nSuccessful++;
                resultObj.push_back(Pair("outpoint", fnb.outpoint.ToStringShort()));
                resultObj.push_back(Pair("addr", fnb.addr.ToString()));
                resultObj.push_back(Pair("pubKeyCollateralAddress", CBitcoinAddress(fnb.pubKeyCollateralAddress.GetID()).ToString()));
                resultObj.push_back(Pair("pubKeyFundamentalnode", CBitcoinAddress(fnb.pubKeyFundamentalnode.GetID()).ToString()));
                resultObj.push_back(Pair("vchSig", EncodeBase64(&fnb.vchSig[0], fnb.vchSig.size())));
                resultObj.push_back(Pair("sigTime", fnb.sigTime));
                resultObj.push_back(Pair("protocolVersion", fnb.nProtocolVersion));
                resultObj.push_back(Pair("nLastDsq", fnb.nLastDsq));

                UniValue lastPingObj(UniValue::VOBJ);
                lastPingObj.push_back(Pair("outpoint", fnb.lastPing.fundamentalnodeOutpoint.ToStringShort()));
                lastPingObj.push_back(Pair("blockHash", fnb.lastPing.blockHash.ToString()));
                lastPingObj.push_back(Pair("sigTime", fnb.lastPing.sigTime));
                lastPingObj.push_back(Pair("vchSig", EncodeBase64(&fnb.lastPing.vchSig[0], fnb.lastPing.vchSig.size())));

                resultObj.push_back(Pair("lastPing", lastPingObj));
            } else {
                nFailed++;
                resultObj.push_back(Pair("errorMessage", "Fundamentalnode broadcast signature verification failed"));
            }

            returnObj.push_back(Pair(fnb.GetHash().ToString(), resultObj));
        }

        returnObj.push_back(Pair("overall", strprintf("Successfully decoded broadcast messages for %d fundamentalnodes, failed to decode %d, total %d", nSuccessful, nFailed, nSuccessful + nFailed)));

        return returnObj;
    }

    if (strCommand == "relay")
    {
        if (request.params.size() < 2 || request.params.size() > 3)
            throw JSONRPCError(RPC_INVALID_PARAMETER,   "fundamentalnodebroadcast relay \"hexstring\"\n"
                                                        "\nArguments:\n"
                                                        "1. \"hex\"      (string, required) Broadcast messages hex string\n");

        std::vector<CFundamentalnodeBroadcast> vecFnb;

        if (!DecodeHexVecFnb(vecFnb, request.params[1].get_str()))
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Fundamentalnode broadcast message decode failed");

        int nSuccessful = 0;
        int nFailed = 0;
        UniValue returnObj(UniValue::VOBJ);

        // verify all signatures first, bailout if any of them broken
        for (const auto& fnb : vecFnb) {
            UniValue resultObj(UniValue::VOBJ);

            resultObj.push_back(Pair("outpoint", fnb.outpoint.ToStringShort()));
            resultObj.push_back(Pair("addr", fnb.addr.ToString()));

            int nDos = 0;
            bool fResult;
            if (fnb.CheckSignature(nDos)) {
                fResult = fnodeman.CheckFnbAndUpdateFundamentalnodeList(NULL, fnb, nDos, *g_connman);
                fnodeman.NotifyFundamentalnodeUpdates(*g_connman);
            } else fResult = false;

            if(fResult) {
                nSuccessful++;
                resultObj.push_back(Pair(fnb.GetHash().ToString(), "successful"));
            } else {
                nFailed++;
                resultObj.push_back(Pair("errorMessage", "Fundamentalnode broadcast signature verification failed"));
            }

            returnObj.push_back(Pair(fnb.GetHash().ToString(), resultObj));
        }

        returnObj.push_back(Pair("overall", strprintf("Successfully relayed broadcast messages for %d fundamentalnodes, failed to relay %d, total %d", nSuccessful, nFailed, nSuccessful + nFailed)));

        return returnObj;
    }

    return NullUniValue;
}

UniValue fnsentinelping(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(
            "fnsentinelping version\n"
            "\nSentinel ping.\n"
            "\nArguments:\n"
            "1. version           (string, required) Sentinel version in the form \"x.x.x\"\n"
            "\nResult:\n"
            "state                (boolean) Ping result\n"
            "\nExamples:\n"
            + HelpExampleCli("fnsentinelping", "1.0.2")
            + HelpExampleRpc("fnsentinelping", "1.0.2")
        );
    }

    activeFundamentalnode.UpdateSentinelPing(StringVersionToInt(request.params[0].get_str()));
    return true;
}

static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         okSafe argNames
  //  --------------------- ------------------------  -----------------------  ------ ----------
    { "securetag",               "fundamentalnode",             &fundamentalnode,             true,  {} },
    { "securetag",               "fundamentalnodelist",         &fundamentalnodelist,         true,  {} },
    { "securetag",               "fundamentalnodebroadcast",    &fundamentalnodebroadcast,    true,  {} },
    { "securetag",               "fnsentinelping",           &fnsentinelping,           true,  {} },
};

void RegisterFundamentalnodeRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
